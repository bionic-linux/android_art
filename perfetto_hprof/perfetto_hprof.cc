/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "perfetto_hprof"

#include <thread>

#include "nativehelper/scoped_local_ref.h"
#include "runtime-inl.h"
#include "runtime_callbacks.h"
#include "scoped_thread_state_change-inl.h"
#include "well_known_classes.h"

#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "gc/heap-visit-objects-inl.h"
#include "gc/heap.h"
#include "gc/scoped_gc_critical_section.h"
#include "mirror/object-refvisitor-inl.h"
#include "thread_list.h"

#include <android-base/logging.h>

#include "perfetto/trace/interned_data/interned_data.pbzero.h"
#include "perfetto/trace/profiling/heap_graph.pbzero.h"
#include "perfetto/trace/profiling/profile_common.pbzero.h"
#include "perfetto/tracing.h"

namespace perfetto_hprof {

constexpr int kJavaHeapprofdSignal = __SIGRTMIN + 6;
constexpr time_t kWatchdogTimeoutSec = 120;
constexpr size_t kObjectsPerPacket = 100;
constexpr char kByte[1] = {'x'};

enum class State {
  kWaitForStart,
  kStart,
  kEnd,
};
static art::Mutex state_mutex;
static art::ConditionVariable state_cv;
static State state = State::kWaitForStart;

// Pipe to signal from the signal handler into a worker thread that handles the
// dump requests.
int signal_pipe_fds[2];
static struct sigaction orig_act = {};

uint64_t FindOrAppend(std::map<std::string, uint64_t>* m,
                      const std::string& s) {
  auto it = m->find(s);
  if (it == m->end()) {
    std::tie(it, std::ignore) = m->emplace(s, m->size());
  }
  return it->second;
}

void ArmWatchdogOrDie() {
  timer_t timerid{};
  struct sigevent sev {};
  sev.sigev_notify = SIGEV_SIGNAL;
  sev.sigev_signo = SIGKILL;

  if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) == -1) {
    // This only gets called in the child, so we can fatal without impacting
    // the app.
    PLOG(FATAL) << "failed to create watchdog timer.";
  }

  struct itimerspec its {};
  its.it_value.tv_sec = kWatchdogTimeoutSec;

  if (timer_settime(timerid, 0, &its, nullptr) == -1) {
    // This only gets called in the child, so we can fatal without impacting
    // the app.
    PLOG(FATAL) << "failed to arm watchdog timer.";
  }
}

class JavaHprofDataSource : public perfetto::DataSource<JavaHprofDataSource> {
  // TODO(fmayer): Change Client API and reject configs that do not target
  // this process.
  void OnSetup(const SetupArgs&) override {}
  void OnStart(const StartArgs&) override {
    bool changed_state = false;
    {
      art::MutexLock lk(state_mutex);
      if (state == State::kWaitForStart) {
        state = State::kStart;
        changed_state = true;
      }
    }
    if (changed_state) {
      state_cv.notify_all();
    }
  }
  void OnStop(const StopArgs&) override {}
};


void WaitForDataSource() {
  perfetto::TracingInitArgs args;
  args.backends = perfetto::BackendType::kSystemBackend;
  perfetto::Tracing::Initialize(args);

  perfetto::DataSourceDescriptor dsd;
  dsd.set_name("android.java_hprof");
  JavaHprofDataSource::Register(dsd);

  std::unique_lock<std::mutex> lk(state_mutex);
  state_cv.wait(lk, [] { return state == State::kStart; });
}

class Writer {
 public:
  Writer(pid_t parent_pid, JavaHprofDataSource::TraceContext* ctx)
      : parent_pid_(parent_pid), ctx_(ctx) {}

  perfetto::protos::pbzero::HeapGraph* GetHeapGraph() {
    if (!heap_graph_ || ++objects_written_ % kObjectsPerPacket == 0) {
      if (heap_graph_) {
        heap_graph_->set_continued(true);
      }
      Finalize();

      trace_packet_ = ctx_->NewTracePacket();
      heap_graph_ = trace_packet_->set_heap_graph();
      heap_graph_->set_pid(parent_pid_);
      heap_graph_->set_index(index_++);
    }
    return heap_graph_;
  }

  void Finalize() {
    if (trace_packet_) trace_packet_->Finalize();
    heap_graph_ = nullptr;
  }

  ~Writer() { Finalize(); }

 private:
  const pid_t parent_pid_;
  JavaHprofDataSource::TraceContext* ctx_;

  perfetto::DataSource<JavaHprofDataSource>::TraceContext::TracePacketHandle
      trace_packet_;
  perfetto::protos::pbzero::HeapGraph* heap_graph_;

  uint64_t index_ = 0;
  size_t objects_written_ = 0;
};

class ReferredObjectsFinder {
 public:
  explicit ReferredObjectsFinder(
      std::vector<std::pair<std::string, art::mirror::Object*>>* referred_objects)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      : referred_objects_(referred_objects) {}

  // For art::mirror::Object::VisitReferences.
  void operator()(art::ObjPtr<art::mirror::Object> obj, art::MemberOffset offset,
                  bool is_static ATTRIBUTE_UNUSED) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    art::mirror::Object* ref = obj->GetFieldObject<art::mirror::Object>(offset);
    art::ArtField* field = obj->FindFieldByOffset(offset);
    std::string field_name = "";
    if (field != nullptr) {
      field_name = field->PrettyField(/*with_type=*/false);
    }
    referred_objects_->emplace_back(std::move(field_name), ref);
  }

  void VisitRootIfNonNull(art::mirror::CompressedReference<art::mirror::Object>* root
                              ATTRIBUTE_UNUSED) const {}
  void VisitRoot(art::mirror::CompressedReference<art::mirror::Object>* root
                     ATTRIBUTE_UNUSED) const {}

 private:
  std::vector<std::pair<std::string, art::mirror::Object*>>* referred_objects_;
};

void DumpPerfetto(art::Thread* self) {
  // Need to take a heap dump while GC isn't running. See the comment in
  // Heap::VisitObjects(). Also we need the critical section to avoid visiting
  // the same object twice. See b/34967844
  //
  // We need to do this before the clone, because otherwise it can deadlock
  // waiting for the GC, as all other threads get terminated by the clone, but
  // their locks are not released.
  art::gc::ScopedGCCriticalSection gcs(self, art::gc::kGcCauseHprof,
                                  art::gc::kCollectorTypeHprof);

  art::ScopedSuspendAll ssa(__FUNCTION__, true);
  pid_t parent_pid = getpid();

  pid_t pid = clone(nullptr, nullptr, 0, nullptr);
  if (pid != 0) {
    return;
  }

  // Make sure that this is the first thing we do after forking, so if anything
  // below hangs, the fork will go away from the watchdog.
  ArmWatchdogOrDie();

  WaitForDataSource();

  JavaHprofDataSource::Trace(
      [parent_pid](JavaHprofDataSource::TraceContext ctx)
          NO_THREAD_SAFETY_ANALYSIS {
            LOG(INFO) << "dumping heap for " << parent_pid;
            Writer writer(parent_pid, &ctx);
            std::map<std::string, uint64_t> interned_fields{{"", 0}};
            std::map<std::string, uint64_t> interned_types{{"", 0}};

            art::Runtime::Current()->GetHeap()->VisitObjectsPaused(
                [&writer, &interned_types, &interned_fields](
                    art::mirror::Object* obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
                  auto* object_proto = writer.GetHeapGraph()->add_objects();
                  object_proto->set_id(reinterpret_cast<uintptr_t>(obj));
                  object_proto->set_type_id(
                      FindOrAppend(&interned_types, obj->PrettyTypeOf()));
                  object_proto->set_self_size(obj->SizeOf());

                  std::vector<std::pair<std::string, art::mirror::Object*>>
                      referred_objects;
                  ReferredObjectsFinder objf(&referred_objects);
                  obj->VisitReferences(objf, art::VoidFunctor());
                  for (const std::pair<std::string, art::mirror::Object*>& p :
                       referred_objects) {
                    object_proto->add_reference_field_id(
                        FindOrAppend(&interned_fields, p.first));
                    object_proto->add_reference_object_id(
                        reinterpret_cast<uintptr_t>(p.second));
                  }
                });

            for (const auto& p : interned_fields) {
              const std::string& str = p.first;
              uint64_t id = p.second;

              auto* field_proto = writer.GetHeapGraph()->add_field_names();
              field_proto->set_iid(id);
              field_proto->set_str(
                  reinterpret_cast<const uint8_t*>(str.c_str()), str.size());
            }
            for (const auto& p : interned_types) {
              const std::string& str = p.first;
              uint64_t id = p.second;

              auto* type_proto = writer.GetHeapGraph()->add_type_names();
              type_proto->set_iid(id);
              type_proto->set_str(reinterpret_cast<const uint8_t*>(str.c_str()),
                                  str.size());
            }

            writer.Finalize();

            ctx.Flush([] {
              {
                art::MutexLock lk(state_mutex);
                state = State::kEnd;
              }
              state_cv.notify_all();
            });
          });

  std::unique_lock<std::mutex> lk(state_mutex);
  state_cv.wait(lk, [] { return state == State::kEnd; });
  LOG(INFO) << "finished dumping heap for " << parent_pid;
  // Prevent the atexit handlers to run. We do not want to call cleanup
  // functions the parent process has registered.
  _exit(0);
}

// The plugin initialization function.
extern "C" bool ArtPlugin_Initialize()
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  if (art::Runtime::Current() == nullptr) {
    return false;
  }

  if (pipe(signal_pipe_fds) == -1) {
    PLOG(ERROR) << "Failed to pipe";
    return false;
  }

  struct sigaction act = {};
  act.sa_sigaction = [](int, siginfo_t*, void*) {
    if (write(signal_pipe_fds[1], kByte, sizeof(kByte)) == -1) {
      PLOG(ERROR) << "Failed to trigger heap dump.";
    }
  };

  // TODO(fmayer): We can probably use the SignalCatcher thread here to not
  // have an idle thread.
  if (sigaction(kJavaHeapprofdSignal, &act, &orig_act) != 0) {
    close(signal_pipe_fds[0]);
    close(signal_pipe_fds[1]);
    PLOG(ERROR) << "Failed to sigaction";
    return false;
  }

  std::thread th([] {
    art::Runtime* runtime = art::Runtime::Current();
    if (!runtime->AttachCurrentThread("hprof_listener", true,
                                      runtime->GetSystemThreadGroup(), false)) {
      LOG(ERROR) << "failed to attach thread.";
      return;
    }
    art::Thread* self = art::Thread::Current();
    char buf[1];
    for (;;) {
      int res;
      do {
        res = read(signal_pipe_fds[0], buf, sizeof(buf));
      } while (res == -1 && errno == EINTR);

      if (res <= 0) {
        if (res == -1) {
          PLOG(ERROR) << "failed to read.";
        }
        close(signal_pipe_fds[0]);
        return;
      }

      perfetto_hprof::DumpPerfetto(self);
    }
  });
  th.detach();
  return true;
}

extern "C" bool ArtPlugin_Deinitialize() {
  if (sigaction(kJavaHeapprofdSignal, &orig_act, nullptr) != 0) {
    PLOG(ERROR) << "failed to reset signal handler.";
    // We cannot close the pipe if the signal handler wasn't unregistered,
    // to avoid receiving SIGPIPE.
    return false;
  }
  close(signal_pipe_fds[1]);
  return true;
}

}  // namespace perfetto_hprof

namespace perfetto {

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(perfetto_hprof::JavaHprofDataSource);

}
