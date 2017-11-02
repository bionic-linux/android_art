/*
 * Copyright 2017 The Android Open Source Project
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

#include <atomic>
#include <fstream>
#include <random>
#include <string>

#include <assert.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>


#include "/data/ws/aosp/art/runtime/thread.h"
#include "/data/ws/aosp/art/runtime/thread-current-inl.h"
#include "/data/ws/aosp/art/runtime/thread-inl.h"
#include "/data/ws/aosp/art/runtime/base/mutex.h"
#include "/data/ws/aosp/art/runtime/base/mutex-inl.h"

namespace {

static const uint32_t kNop = 0xd503201fu;
static const uint32_t kReturn = 0xd65f03c0u;
static const uint32_t kUndefined = 0xffffffffu;

static const size_t kReportIterations = 100000;

static const size_t kJitFunctionCount = 1;
static const size_t kMaxInstructions = 32;

static const size_t kPageSize = 4096;
static const size_t kJitCacheSize =
    (kMaxInstructions * sizeof(kNop) * kJitFunctionCount + kPageSize - 1) & ~(kPageSize - 1);

#if 0

// RWLock class based on art::ReaderWriterMutex.
class RWLock {
 public:
  RWLock() : m("default") {}

  inline void ReaderAcquire() ACQUIRE_SHARED() {
    m.ReaderLock(nullptr);
  }

  inline void ReaderRelease() RELEASE_SHARED() {
    m.ReaderUnlock(nullptr);
  }

  inline void WriterAcquire() ACQUIRE() {
    m.WriterLock(nullptr);
  }

  inline void WriterRelease() RELEASE() {
    m.WriterUnlock(nullptr);
  }

 private:
  art::ReaderWriterMutex m;
};

// RWLock classed based on Orion's implementation.
class RWLock {
 public:
  RWLock() : lockword(0) {}

  inline void ReaderAcquire() {
    uint32_t old_value;
    uint32_t new_value;
    do {
      old_value = lockword.load(std::memory_order_relaxed) & kReaderMask;
      new_value = old_value + 1;
    } while (!lockword.compare_exchange_weak(old_value, new_value, std::memory_order_relaxed));
     __asm __volatile("dmb ish");
  }

  inline void ReaderRelease() {
    __asm __volatile("dmb ish");
    lockword.fetch_sub(1, std::memory_order_relaxed);
  }

  inline void WriterAcquire() {
    while (lockword.fetch_or(kWriterMask, std::memory_order_relaxed) != kWriterMask) {
    }
    __asm __volatile("dmb ish");
  }

  inline void WriterRelease() {
    __asm __volatile("dmb ish");
    lockword.fetch_xor(kWriterMask, std::memory_order_relaxed);
  }

 private:
  const uint32_t kReaderMask = 0x7fffffffu;
  const uint32_t kWriterMask = 0x80000000u;

  std::atomic<uint32_t> lockword;
};
#endif

// Simple lock class based on std::atomic_flag.
class RWLock {
 public:
  RWLock() : lock(false) {}

  inline void ReaderAcquire() {
    while (lock.test_and_set(std::memory_order_acquire)) {  // acquire lock
    // spin
    }
  }

  inline void ReaderRelease() {
    lock.clear(std::memory_order_release);
  }

  inline void WriterAcquire() {
    while (lock.test_and_set(std::memory_order_acquire)) {  // acquire lock
    // spin
    }
  }

  inline void WriterRelease() {
    lock.clear(std::memory_order_release);
  }

 private:
  std::atomic_flag lock;;
};


// Memory allocated for the JIT code cache cache.
static uint8_t *g_cache;

// Function pointer for JIT generated functions.
typedef void(*volatile JitFunction)(void);

struct JitFunctionInfo {
  volatile JitFunction function;
  size_t instruction_count;
  RWLock lock;

  inline void InvokeFunction() {
    lock.ReaderAcquire();
    function();
    lock.ReaderRelease();
  }
};

static JitFunctionInfo g_jit_function_info[kJitFunctionCount];

// Number of iterations run (JIT code re-generations).
static std::atomic<size_t> g_iteration(0);

static std::vector<pid_t> g_thread_ids;
static std::atomic<size_t> g_thread_idx(0);
static thread_local pid_t g_current_thread_id;
static size_t g_current_function[8];

static void InitializeThreadIds(size_t thread_count) {
  g_thread_ids.resize(thread_count);
}

static void SaveThreadId() {
  size_t index = g_thread_idx.fetch_add(1);
  g_current_thread_id = index;
  g_thread_ids[index] = gettid();
}

static std::random_device g_rd;

// void ShuffleAffinity() {
//  // Shuffle array of thread ids and then bind threads to different cores.
//  std::mt19937 generator(g_rd());
//  std::shuffle(g_thread_ids.begin(), g_thread_ids.end(), generator);
//  for (size_t i = 0; i < g_thread_ids.size(); ++i) {
//    cpu_set_t cpu_set;
//    CPU_ZERO(&cpu_set);
//    CPU_SET(i, &cpu_set);
//    int result = sched_setaffinity(g_thread_ids[i], sizeof(cpu_set), &cpu_set);
//    if (result != 0) {
//      fprintf(stderr, "Thread %d Cpu %zu\n", g_thread_ids[i], i);
//      perror("sched_setaffinity");
//    }
//  }
//}

// uint32_t* WriteJitFunctionCbz(uint32_t* start_address, size_t function_size, bool rare_case) {
//  uint32_t* instruction_address = start_address;
//  size_t instruction_count = function_size;
//
//  if (!rare_case) {
//    *instruction_address++ = 0x2a1f03e0;    // mov w0, wzr
//    *instruction_address++ = 0x34000200;    // cbz w0
//    instruction_count -= 2;
//
//    for (int i = 0; i < 15; i++) {
//      *instruction_address++ = kUndefined;  // Should never be hit.
//      instruction_count--;
//    }
//
//  } else {
//    *instruction_address++ = 0x2a1f03e0;    // mov w0, wzr
//    *instruction_address++ = 0x34000220;    // cbz w0
//    instruction_count -= 2;
//
//    for (int i = 0; i < 16; i++) {
//      *instruction_address++ = kUndefined;  // Should never be hit.
//      instruction_count--;
//    }
//  }
//
//  while (instruction_count > 0) {
//    *instruction_address++ = kNop;
//    instruction_count -= 1;
//  }
//
//  return instruction_address;
//}

// uint32_t* WriteJitFunctionAdrBrOrNop(uint32_t* start_address, size_t function_size, bool rare_case) {
//  uint32_t* instruction_address = start_address;
//  size_t instruction_count = function_size;
//
//  if (rare_case) {
//    *instruction_address++ = 0x10000060;    // adr x0
//    *instruction_address++ = kNop;
//    instruction_count -= 2;
//
//    for (int i = 0; i < 15; i++) {
//      *instruction_address++ = kNop;        // Should never be hit.
//      instruction_count--;
//    }
//
//    *instruction_address++ = kReturn;
//    instruction_count--;
//
//    while (instruction_count > 0) {
//      *instruction_address++ = kUndefined;
//      instruction_count -= 1;
//    }
//  } else {
//    *instruction_address++ = 0x10000240;    // adr x0
//    *instruction_address++ = 0xd61f0000;    // br x0
//    instruction_count -= 2;
//
//    for (int i = 0; i < 16; i++) {
//      *instruction_address++ = kUndefined;  // Should never be hit.
//      instruction_count--;
//    }
//    // The BR might see junk here from the rare case.
//
//    while (instruction_count > 0) {
//      *instruction_address++ = kNop;
//      instruction_count -= 1;
//    }
//  }
//
//  return instruction_address;
//}


uint32_t* WriteJitFunctionAdrSubBr(uint32_t* start_address, size_t function_size, bool rare_case) {
  uint32_t* instruction_address = start_address;
  size_t instruction_count = function_size;

  if (!rare_case) {
    *instruction_address++ = 0x10000260;    // adr x0
    *instruction_address++ = 0xd1001000;    // sub x0, x0, 4
    *instruction_address++ = 0xd61f0000;    // br x0  ; branch over 15 kUndefined
    instruction_count -= 3;

    for (int i = 0; i < 15; i++) {
      *instruction_address++ = kUndefined;  // Should never be hit.
      instruction_count--;
    }

    // The BR here might see junk from the "rare_case".
  } else {
    *instruction_address++ = 0x10000260;    // adr x0
    *instruction_address++ = 0xd1000000;    // sub x0, x0, 0
    *instruction_address++ = 0xd61f0000;    // br x0  ; branch over 16 kUndefined
    instruction_count -= 3;

    for (int i = 0; i < 16; i++) {
      *instruction_address++ = kUndefined;  // Should never be hit.
      instruction_count--;
    }
  }

  while (instruction_count > 0) {
    *instruction_address++ = kNop;
    instruction_count -= 1;
  }

  return instruction_address;
}

JitFunction WriteJitFunction(uint32_t* instruction_address, size_t instruction_count, bool flag) {
  uint32_t* start_address = instruction_address;

  if (reinterpret_cast<uint8_t*>(instruction_address) < g_cache ||
      reinterpret_cast<uint8_t*>(instruction_address + instruction_count) > (g_cache + kJitCacheSize)) {
    fprintf(stderr, "Bad function info %p..%p\n",
            instruction_address,
            instruction_address + instruction_count);
    fprintf(stderr, "Cache %p..%p\n", g_cache, g_cache + kJitCacheSize);
    exit(EXIT_FAILURE);
  }

//  instruction_address = WriteJitFunctionCbz(instruction_address, instruction_count - 2, flag);
//  instruction_address = WriteJitFunctionAdrBrOrNop(instruction_address, instruction_count - 2, flag);
  instruction_address = WriteJitFunctionAdrSubBr(instruction_address, instruction_count - 2, flag);

  *instruction_address++ = kReturn;
  *instruction_address++ = kUndefined;

  DCHECK(static_cast<size_t>(instruction_address - start_address) == instruction_count);

  return reinterpret_cast<JitFunction>(start_address);
}

void UpdateJitFunction(bool flag) {
  JitFunctionInfo* const current = &g_jit_function_info[0];
  uint32_t* start_address = reinterpret_cast<uint32_t*>(g_cache);

  current->lock.WriterAcquire();

  // Write the function
  WriteJitFunction(start_address, kMaxInstructions, flag);

  // Update the function information.
  current->function = reinterpret_cast<JitFunction>(start_address);
  current->instruction_count = kMaxInstructions;

  // Flush the caches and invalidate the instruction pipeline.
  __builtin___clear_cache(reinterpret_cast<char*>(g_cache),
                          reinterpret_cast<char*>(g_cache + kJitCacheSize));
  __asm __volatile("isb");
  current->lock.WriterRelease();
}

void SetupTest() {
  // Creating RWX. ART toggles between RX and RW during updates, but this is not material here.
  g_cache = reinterpret_cast<uint8_t*>(mmap(nullptr,
                                            kJitCacheSize,
                                            PROT_READ | PROT_WRITE | PROT_EXEC,
                                            MAP_PRIVATE | MAP_ANONYMOUS,
                                            -1,
                                            0));
  uint32_t* current_address = reinterpret_cast<uint32_t*>(g_cache);

  JitFunctionInfo* jfi = &g_jit_function_info[0];
  jfi->lock.WriterAcquire();
  jfi->function = WriteJitFunction(current_address, kMaxInstructions, true);
  jfi->instruction_count = kMaxInstructions;
  __builtin___clear_cache(reinterpret_cast<char*>(g_cache),
                          reinterpret_cast<char*>(g_cache + kJitCacheSize));
  __asm __volatile("isb");
  jfi->lock.WriterRelease();
}

void* WorkerMain(void*) {
  SaveThreadId();

  fprintf(stderr,
          "Starting thread %d (tid = %08x)\n",
          static_cast<int>(g_current_thread_id), gettid());

  while (true) {
    size_t index[4];
    index[0] = 0;
    // Try to invoke functions with few instructions in between in
    // case this is factor.
    {
      g_current_function[g_current_thread_id] = index[0];
      JitFunctionInfo* jfi = &g_jit_function_info[index[0]];
      jfi->InvokeFunction();
    }
  }
}

// constexpr static int kFlipFrequency = 10; // Each tenth case.

void DriverMain() {
  SaveThreadId();

  std::mt19937 generator(g_rd());
  std::uniform_int_distribution<size_t> rng(0, 1);

  size_t iteration = 0;
  while (true) {
    // Pass the 'true' flag every kFlipFrequency occasion.
    bool flag = static_cast<bool>(rng(generator));
    UpdateJitFunction(flag);
    g_iteration.store(++iteration);
    if ((iteration % kReportIterations) == 0) {
      fputc('.', stdout);
      fflush(stdout);
      // ShuffleAffinity();
    }
  }
}

void thread_fail(int result, const char* msg) {
  errno = result;
  perror(msg);
  exit(EXIT_FAILURE);
}

uint32_t GetCpuCount() {
  std::ifstream is("/sys/devices/system/cpu/present");
  std::string present((std::istreambuf_iterator<char>(is)), (std::istreambuf_iterator<char>()));
  // Assuming form is 0-N so skip the first 2 characters.
  return std::stoul(present.substr(2)) + 1u;
}

static struct sigaction g_default_sigill_action;

void UndefinedInstructionHandler(int signo, siginfo_t* info, void* opaque_ucontext) {
  struct ucontext* ucontext = reinterpret_cast<struct ucontext*>(opaque_ucontext);
  mcontext_t* context = &ucontext->uc_mcontext;
  fprintf(stderr, "SIGNAL %d pc %p fault %p\n", signo,
          reinterpret_cast<void*>(context->pc), reinterpret_cast<void*>(context->fault_address));

  fprintf(stderr, "JIT function info\n");
  for (size_t i = 0; i < kJitFunctionCount; ++i) {
    JitFunctionInfo* jfi = &g_jit_function_info[i];
    fprintf(stderr, "  Function %zu %p..%p\n",
            i, jfi->function, reinterpret_cast<uint32_t*>(jfi->function) + jfi->instruction_count);
  }

  // This isn't very smart, should check bounds.
  if (context->fault_address != 0ull) {
    fprintf(stderr, "Around fault address\n");
    uint32_t* start_address = std::max(reinterpret_cast<uint32_t*>(context->fault_address) - 8,
                                       reinterpret_cast<uint32_t*>(g_cache));
    uint32_t* end_address = std::min(reinterpret_cast<uint32_t*>(context->fault_address) + 8,
                                     reinterpret_cast<uint32_t*>(g_cache + kJitCacheSize));
    while (start_address < end_address) {
      fprintf(stderr, "  %p: %08x\n", start_address, *start_address);
      ++start_address;
    }
  }

  if (context->pc != 0ull) {
    fprintf(stderr, "Memory around pc\n");
    uint32_t* addr = reinterpret_cast<uint32_t*>(context->pc) - 8;
    for (int i = 0; i < 16; i += 4) {
      fprintf(stderr, "  %p: %08x %08x %08x %08x\n",
              addr + i, addr[i], addr[i + 1], addr[i + 2], addr[i + 3]);
    }
  }

  fprintf(stderr, "Worker thread calling info (current tid = %08x)\n", gettid());
  for (size_t i = 1; i < sizeof(g_current_function) / sizeof(g_current_function[0]); ++i) {
    fprintf(stderr, "  %zu: was calling %zu\n", i, g_current_function[i]);
  }

  // Invoke the default handler.
  g_default_sigill_action.sa_sigaction(signo, info, opaque_ucontext);
}

void InstallUndefinedInstructionHandler() {
  struct sigaction action;
  memset(&action, 0, sizeof(action));

  action.sa_sigaction = UndefinedInstructionHandler;
  action.sa_flags = SA_SIGINFO;

  if (sigaction(SIGILL, &action, &g_default_sigill_action) < 0) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }
}

}  // namespace

constexpr static size_t kWorkerThreadsCount = 1;

int main() {
  uint32_t cpu_count = GetCpuCount();
  InitializeThreadIds(cpu_count);
  SetupTest();
  InstallUndefinedInstructionHandler();
  for (uint32_t i = 0; i < kWorkerThreadsCount; ++i) {
    int result;
    pthread_t thread;
    pthread_attr_t attr;
    result = pthread_attr_init(&attr);
    if (result != 0) {
      thread_fail(result, "pthread_attr_init");
    }
    result = pthread_create(&thread, &attr, WorkerMain, nullptr);
    if (result != 0) {
      thread_fail(result, "pthread_create");
    }
  }
  DriverMain();
  return 0;
}
