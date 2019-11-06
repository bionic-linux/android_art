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

#include "perfetto_hprof.h"

#include <android-base/logging.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>

#include "gc/heap-visit-objects-inl.h"
#include "gc/heap.h"
#include "gc/scoped_gc_critical_section.h"
#include "mirror/object-refvisitor-inl.h"
#include "nativehelper/scoped_local_ref.h"
#include "perfetto/profiling/normalize.h"
#include "perfetto/trace/interned_data/interned_data.pbzero.h"
#include "perfetto/trace/profiling/heap_graph.pbzero.h"
#include "perfetto/trace/profiling/profile_common.pbzero.h"
#include "perfetto/config/profiling/java_hprof_config.pbzero.h"
#include "perfetto/tracing.h"
#include "runtime-inl.h"
#include "runtime_callbacks.h"
#include "scoped_thread_state_change-inl.h"
#include "thread_list.h"
#include "well_known_classes.h"

// There are three threads involved in this:
// * listener thread: this is idle in the background when this plugin gets loaded, and waits
//   for data on on g_signal_pipe_fds.
// * signal thread: an arbitrary thread that handles the signal and writes data to
//   g_signal_pipe_fds.
// * perfetto producer thread: once the signal is received, the app forks. In the newly forked
//   child, the Perfetto Client API spawns a thread to communicate with traced.

namespace perfetto_hprof {

constexpr int kJavaHeapprofdSignal = __SIGRTMIN + 6;
constexpr time_t kWatchdogTimeoutSec = 120;
constexpr size_t kObjectsPerPacket = 100;
constexpr char kByte[1] = {'x'};
static art::Mutex& GetStateMutex() {
  static art::Mutex state_mutex("perfetto_hprof_state_mutex", art::LockLevel::kGenericBottomLock);
  return state_mutex;
}

static art::ConditionVariable& GetStateCV() {
  static art::ConditionVariable state_cv("perfetto_hprof_state_cv", GetStateMutex());
  return state_cv;
}

static State g_state = State::kUninitialized;

// Pipe to signal from the signal handler into a worker thread that handles the
// dump requests.
int g_signal_pipe_fds[2];
static struct sigaction g_orig_act = {};

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
    PLOG(FATAL) << "failed to create watchdog timer";
  }

  struct itimerspec its {};
  its.it_value.tv_sec = kWatchdogTimeoutSec;

  if (timer_settime(timerid, 0, &its, nullptr) == -1) {
    // This only gets called in the child, so we can fatal without impacting
    // the app.
    PLOG(FATAL) << "failed to arm watchdog timer";
  }
}

constexpr size_t kMaxCmdlineSize = 512;

class JavaHprofDataSource : public perfetto::DataSource<JavaHprofDataSource> {
 public:
  constexpr static perfetto::BufferExhaustedPolicy kBufferExhaustedPolicy =
    perfetto::BufferExhaustedPolicy::kStall;
  void OnSetup(const SetupArgs& args) override {
    // This is on the heap as it triggers -Wframe-larger-than.
    std::unique_ptr<perfetto::protos::pbzero::JavaHprofConfig::Decoder> cfg(
        new perfetto::protos::pbzero::JavaHprofConfig::Decoder(
          args.config->java_hprof_config_raw()));

    uint64_t self_pid = static_cast<uint64_t>(getpid());
    for (auto pid_it = cfg->pid(); pid_it; ++pid_it) {
      if (*pid_it == self_pid) {
        enabled_ = true;
        return;
      }
    }

    if (cfg->has_process_cmdline()) {
      int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
      if (fd == -1) {
        PLOG(ERROR) << "failed to open /proc/self/cmdline";
        return;
      }
      char cmdline[kMaxCmdlineSize];
      ssize_t rd = read(fd, cmdline, sizeof(cmdline) - 1);
      if (rd == -1) {
        PLOG(ERROR) << "failed to read /proc/self/cmdline";
      }
      close(fd);
      if (rd == -1) {
        return;
      }
      cmdline[rd] = '\0';
      char* cmdline_ptr = cmdline;
      ssize_t sz = perfetto::profiling::NormalizeCmdLine(&cmdline_ptr, static_cast<size_t>(rd + 1));
      if (sz == -1) {
        PLOG(ERROR) << "failed to normalize cmdline";
      }
      for (auto it = cfg->process_cmdline(); it; ++it) {
        std::string other = (*it).ToStdString();
        // Append \0 to make this a C string.
        other.resize(other.size() + 1);
        char* other_ptr = &(other[0]);
        ssize_t other_sz = perfetto::profiling::NormalizeCmdLine(&other_ptr, other.size());
        if (other_sz == -1) {
          PLOG(ERROR) << "failed to normalize other cmdline";
          continue;
        }
        if (sz == other_sz && strncmp(cmdline_ptr, other_ptr, static_cast<size_t>(sz)) == 0) {
          enabled_ = true;
          return;
        }
      }
    }
  }

  bool enabled() { return enabled_; }

  void OnStart(const StartArgs&) override {
    if (!enabled()) {
      return;
    }
    art::MutexLock lk(art_thread(), GetStateMutex());
    if (g_state == State::kWaitForStart) {
      g_state = State::kStart;
      GetStateCV().Broadcast(art_thread());
    }
  }

  void OnStop(const StopArgs&) override {}

  static art::Thread* art_thread() {
    // TODO(fmayer): Attach the Perfetto producer thread to ART and give it a name. This is
    // not trivial, we cannot just attach the first time this method is called, because
    // AttachCurrentThread deadlocks with the ConditionVariable::Wait in WaitForDataSource.
    //
    // We should attach the thread as soon as the Client API spawns it, but that needs more
    // complicated plumbing.
    return nullptr;
  }

 private:
  bool enabled_ = false;
  static art::Thread* self_;
};

art::Thread* JavaHprofDataSource::self_ = nullptr;


void WaitForDataSource(art::Thread* self) {
  perfetto::TracingInitArgs args;
  args.backends = perfetto::BackendType::kSystemBackend;
  perfetto::Tracing::Initialize(args);

  perfetto::DataSourceDescriptor dsd;
  dsd.set_name("android.java_hprof");
  JavaHprofDataSource::Register(dsd);

  LOG(INFO) << "waiting for data source";

  art::MutexLock lk(self, GetStateMutex());
  while (g_state != State::kStart) {
    GetStateCV().Wait(self);
  }
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
    if (trace_packet_) {
      trace_packet_->Finalize();
    }
    heap_graph_ = nullptr;
  }

  ~Writer() { Finalize(); }

 private:
  const pid_t parent_pid_;
  JavaHprofDataSource::TraceContext* const ctx_;

  perfetto::DataSource<JavaHprofDataSource>::TraceContext::TracePacketHandle
      trace_packet_;
  perfetto::protos::pbzero::HeapGraph* heap_graph_ = nullptr;

  uint64_t index_ = 0;
  size_t objects_written_ = 0;
};

class ReferredObjectsFinder {
 public:
  explicit ReferredObjectsFinder(
      std::vector<std::pair<std::string, art::mirror::Object*>>* referred_objects)
      : referred_objects_(referred_objects) {}

  // For art::mirror::Object::VisitReferences.
  void operator()(art::ObjPtr<art::mirror::Object> obj, art::MemberOffset offset,
                  bool is_static) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    art::mirror::Object* ref = obj->GetFieldObject<art::mirror::Object>(offset);
    art::ArtField* field;
    if (is_static) {
      field = art::ArtField::FindStaticFieldWithOffset(obj->AsClass(), offset.Uint32Value());
    } else {
      field = art::ArtField::FindInstanceFieldWithOffset(obj->GetClass(), offset.Uint32Value());
    }
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
  // We can use a raw Object* pointer here, because there are no concurrent GC threads after the
  // fork.
  std::vector<std::pair<std::string, art::mirror::Object*>>* referred_objects_;
};

class RootFinder : public art::SingleRootVisitor {
 public:
  explicit RootFinder(
    std::map<art::RootType, std::vector<art::mirror::Object*>>* root_objects)
      : root_objects_(root_objects) {}

  void VisitRoot(art::mirror::Object* root, const art::RootInfo& info) override {
    (*root_objects_)[info.GetType()].emplace_back(root);
  }

 private:
  // We can use a raw Object* pointer here, because there are no concurrent GC threads after the
  // fork.
  std::map<art::RootType, std::vector<art::mirror::Object*>>* root_objects_;
};

perfetto::protos::pbzero::HeapGraphRoot::Type ToProtoType(art::RootType art_type) {
  switch (art_type) {
    case art::kRootUnknown:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_UNKNOWN;
    case art::kRootJNIGlobal:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_JNI_GLOBAL;
    case art::kRootJNILocal:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_JNI_LOCAL;
    case art::kRootJavaFrame:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_JAVA_FRAME;
    case art::kRootNativeStack:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_NATIVE_STACK;
    case art::kRootStickyClass:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_STICKY_CLASS;
    case art::kRootThreadBlock:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_THREAD_BLOCK;
    case art::kRootMonitorUsed:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_MONITOR_USED;
    case art::kRootThreadObject:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_THREAD_OBJECT;
    case art::kRootInternedString:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_INTERNED_STRING;
    case art::kRootFinalizing:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_FINALIZING;
    case art::kRootDebugger:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_DEBUGGER;
    case art::kRootReferenceCleanup:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_REFERENCE_CLEANUP;
    case art::kRootVMInternal:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_VM_INTERNAL;
    case art::kRootJNIMonitor:
      return perfetto::protos::pbzero::HeapGraphRoot::ROOT_JNI_MONITOR;
  }
}

void DumpPerfetto(art::Thread* self) {
  pid_t parent_pid = getpid();
  LOG(INFO) << "preparing to dump heap for " << parent_pid;

  // Need to take a heap dump while GC isn't running. See the comment in
  // Heap::VisitObjects(). Also we need the critical section to avoid visiting
  // the same object twice. See b/34967844.
  //
  // We need to do this before the fork, because otherwise it can deadlock
  // waiting for the GC, as all other threads get terminated by the clone, but
  // their locks are not released.
  art::gc::ScopedGCCriticalSection gcs(self, art::gc::kGcCauseHprof,
                                       art::gc::kCollectorTypeHprof);

  art::ScopedSuspendAll ssa(__FUNCTION__, /* long_suspend=*/ true);

  pid_t pid = fork();
  if (pid != 0) {
    return;
  }

  // Make sure that this is the first thing we do after forking, so if anything
  // below hangs, the fork will go away from the watchdog.
  ArmWatchdogOrDie();

  WaitForDataSource(self);

  JavaHprofDataSource::Trace(
      [parent_pid](JavaHprofDataSource::TraceContext ctx)
          NO_THREAD_SAFETY_ANALYSIS {
            {
              auto ds = ctx.GetDataSourceLocked();
              if (!ds || !ds->enabled()) {
                LOG(INFO) << "skipping irrelevant data source.";
                return;
              }
            }
            LOG(INFO) << "dumping heap for " << parent_pid;
            Writer writer(parent_pid, &ctx);
            // Make sure that intern ID 0 (default proto value for a uint64_t) always maps to ""
            // (default proto value for a string).
            std::map<std::string, uint64_t> interned_fields{{"", 0}};
            std::map<std::string, uint64_t> interned_types{{"", 0}};

            std::map<art::RootType, std::vector<art::mirror::Object*>> root_objects;
            RootFinder rcf(&root_objects);
            art::Runtime::Current()->VisitRoots(&rcf);
            for (const auto& p : root_objects) {
              const art::RootType root_type = p.first;
              const std::vector<art::mirror::Object*>& children = p.second;
              perfetto::protos::pbzero::HeapGraphRoot* root_proto =
                writer.GetHeapGraph()->add_roots();
              root_proto->set_root_type(ToProtoType(root_type));
              for (art::mirror::Object* obj : children)
                root_proto->add_object_ids(reinterpret_cast<uintptr_t>(obj));
            }

            art::Runtime::Current()->GetHeap()->VisitObjectsPaused(
                [&writer, &interned_types, &interned_fields](
                    art::mirror::Object* obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
                  perfetto::protos::pbzero::HeapGraphObject* object_proto =
                    writer.GetHeapGraph()->add_objects();
                  object_proto->set_id(reinterpret_cast<uintptr_t>(obj));
                  object_proto->set_type_id(
                      FindOrAppend(&interned_types, obj->PrettyTypeOf()));
                  object_proto->set_self_size(obj->SizeOf());

                  std::vector<std::pair<std::string, art::mirror::Object*>>
                      referred_objects;
                  ReferredObjectsFinder objf(&referred_objects);
                  obj->VisitReferences(objf, art::VoidFunctor());
                  for (const auto& p : referred_objects) {
                    object_proto->add_reference_field_id(
                        FindOrAppend(&interned_fields, p.first));
                    object_proto->add_reference_object_id(
                        reinterpret_cast<uintptr_t>(p.second));
                  }
                });

            for (const auto& p : interned_fields) {
              const std::string& str = p.first;
              uint64_t id = p.second;

              perfetto::protos::pbzero::InternedString* field_proto =
                writer.GetHeapGraph()->add_field_names();
              field_proto->set_iid(id);
              field_proto->set_str(
                  reinterpret_cast<const uint8_t*>(str.c_str()), str.size());
            }
            for (const auto& p : interned_types) {
              const std::string& str = p.first;
              uint64_t id = p.second;

              perfetto::protos::pbzero::InternedString* type_proto =
                writer.GetHeapGraph()->add_type_names();
              type_proto->set_iid(id);
              type_proto->set_str(reinterpret_cast<const uint8_t*>(str.c_str()),
                                  str.size());
            }

            writer.Finalize();

            ctx.Flush([] {
              {
                art::MutexLock lk(JavaHprofDataSource::art_thread(), GetStateMutex());
                g_state = State::kEnd;
                GetStateCV().Broadcast(JavaHprofDataSource::art_thread());
              }
            });
          });

  art::MutexLock lk(self, GetStateMutex());
  while (g_state != State::kEnd) {
    GetStateCV().Wait(self);
  }
  LOG(INFO) << "finished dumping heap for " << parent_pid;
  // Prevent the atexit handlers to run. We do not want to call cleanup
  // functions the parent process has registered.
  _exit(0);
}

// The plugin initialization function.
extern "C" bool ArtPlugin_Initialize() {
  if (art::Runtime::Current() == nullptr) {
    return false;
  }
  art::Thread* self = art::Thread::Current();
  {
    art::MutexLock lk(self, GetStateMutex());
    if (g_state != State::kUninitialized) {
      LOG(ERROR) << "perfetto_hprof already initialized. state: " << g_state;
      return false;
    }
    g_state = State::kWaitForListener;
  }

  if (pipe(g_signal_pipe_fds) == -1) {
    PLOG(ERROR) << "Failed to pipe";
    return false;
  }

  struct sigaction act = {};
  act.sa_sigaction = [](int, siginfo_t*, void*) {
    if (write(g_signal_pipe_fds[1], kByte, sizeof(kByte)) == -1) {
      PLOG(ERROR) << "Failed to trigger heap dump";
    }
  };

  // TODO(fmayer): We can probably use the SignalCatcher thread here to not
  // have an idle thread.
  if (sigaction(kJavaHeapprofdSignal, &act, &g_orig_act) != 0) {
    close(g_signal_pipe_fds[0]);
    close(g_signal_pipe_fds[1]);
    PLOG(ERROR) << "Failed to sigaction";
    return false;
  }

  std::thread th([] {
    art::Runtime* runtime = art::Runtime::Current();
    if (!runtime) {
      LOG(FATAL_WITHOUT_ABORT) << "no runtime in hprof_listener";
      return;
    }
    if (!runtime->AttachCurrentThread("hprof_listener", /*as_daemon=*/ true,
                                      runtime->GetSystemThreadGroup(), /*create_peer=*/ false)) {
      LOG(ERROR) << "failed to attach thread.";
      return;
    }
    art::Thread* self = art::Thread::Current();
    if (!self) {
      LOG(FATAL_WITHOUT_ABORT) << "no thread in hprof_listener";
      return;
    }
    {
      art::MutexLock lk(self, GetStateMutex());
      if (g_state == State::kWaitForListener) {
        g_state = State::kWaitForStart;
        GetStateCV().Broadcast(self);
      }
    }
    char buf[1];
    for (;;) {
      int res;
      do {
        res = read(g_signal_pipe_fds[0], buf, sizeof(buf));
      } while (res == -1 && errno == EINTR);

      if (res <= 0) {
        if (res == -1) {
          PLOG(ERROR) << "failed to read";
        }
        close(g_signal_pipe_fds[0]);
        return;
      }

      perfetto_hprof::DumpPerfetto(self);
    }
  });
  th.detach();

  art::MutexLock lk(art::Thread::Current(), GetStateMutex());
  while (g_state == State::kWaitForListener) {
    GetStateCV().Wait(art::Thread::Current());
  }
  return true;
}

extern "C" bool ArtPlugin_Deinitialize() {
  if (sigaction(kJavaHeapprofdSignal, &g_orig_act, nullptr) != 0) {
    PLOG(ERROR) << "failed to reset signal handler";
    // We cannot close the pipe if the signal handler wasn't unregistered,
    // to avoid receiving SIGPIPE.
    return false;
  }
  close(g_signal_pipe_fds[1]);

  art::Thread* self = art::Thread::Current();
  art::MutexLock lk(self, GetStateMutex());
  if (g_state != State::kWaitForListener) {
    g_state = State::kUninitialized;
    GetStateCV().Broadcast(self);
  }
  return true;
}

}  // namespace perfetto_hprof

namespace perfetto {

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(perfetto_hprof::JavaHprofDataSource);

}
