/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "debugger_interface.h"

#include <android-base/logging.h>

#include "base/array_ref.h"
#include "base/mutex.h"
#include "base/time_utils.h"
#include "dex/dex_file.h"
#include "thread-current-inl.h"
#include "thread.h"

#include <atomic>
#include <cstddef>
#include <map>

//
// Debug interface for native tools (gdb, lldb, libunwind, simpleperf).
//
// See http://sourceware.org/gdb/onlinedocs/gdb/Declarations.html
//
// There are two ways for native tools to access the debug data safely:
//
// 1) Synchronously, by setting a breakpoint in the __*_debug_register_code
//    method, which is called after every modification of the linked list.
//    GDB does this, but it is complex to set up and it stops the process.
//
// 2) Asynchronously, by monitoring the action_seqlock_.
//   * The seqlock is a monotonically increasing counter which is incremented
//     before and after every modification of the linked list. Odd value of
//     the counter means the linked list is being modified (it is locked).
//   * The tool should read the value of the seqlock both before and after
//     copying the linked list.  If the seqlock values match and are even,
//     the copy is consistent.  Otherwise, the reader should try again.
//     * Note that using the data directly while is it being modified
//       might crash the tool.  Therefore, the only safe way is to make
//       a copy and use the copy only after the seqlock has been checked.
//     * Note that the process might even free and munmap the data while
//       it is being copied, therefore the reader should either handle
//       SEGV or use OS calls to read the memory (e.g. process_vm_readv).
//   * The seqlock can be used to determine the number of modifications of
//     the linked list, which can be used to intelligently cache the data.
//     Note the possible overflow of the seqlock.  It is intentionally
//     32-bit, since 64-bit atomics can be tricky on some architectures.
//   * The timestamps on the entry record the time when the entry was
//     created which is relevant if the unwinding is not live and is
//     postponed until much later.  All timestamps must be unique.
//   * Memory barriers are used to make it possible to reason about
//     the data even when it is being modified (e.g. the process crashed
//     while that data was locked, and thus it will be never unlocked).
//     * In particular, it should be possible to:
//       1) read the seqlock and then the linked list head pointer.
//       2) copy the entry and check that seqlock has not changed.
//       3) copy the symfile and check that seqlock has not changed.
//       4) go back to step 2 using the next pointer (if non-null).
//       This safely creates copy of all symfiles, although other data
//       might be inconsistent/unusable (e.g. prev_, action_timestamp_).
//   * For full conformance with the C++ memory model, all seqlock
//     protected accesses should be atomic. We currently do this in the
//     more critical cases. The rest will have to be fixed before
//     attempting to run TSAN on this code.
//

namespace art {

static Mutex __jit_debug_lock("JIT native debug entries", kNativeDebugInterfaceLock);
static Mutex __dex_debug_lock("DEX native debug entries", kNativeDebugInterfaceLock);

extern "C" {
  enum JITAction {
    JIT_NOACTION = 0,
    JIT_REGISTER_FN,
    JIT_UNREGISTER_FN
  };

  struct JITCodeEntry {
    // Atomic to ensure the reader can always iterate over the linked list
    // (e.g. the process could crash in the middle of writing this field).
    std::atomic<JITCodeEntry*> next_;
    // Non-atomic. The reader should not use it. It is only used for deletion.
    JITCodeEntry* prev_;
    const uint8_t* symfile_addr_;
    uint64_t symfile_size_;  // Beware of the offset (12 on x86; but 16 on ARM32).

    // Android-specific fields:
    uint64_t register_timestamp_;  // CLOCK_MONOTONIC time of entry registration.
  };

  struct JITDescriptor {
    uint32_t version_ = 1;                      // NB: GDB supports only version 1.
    uint32_t action_flag_ = JIT_NOACTION;       // One of the JITAction enum values.
    JITCodeEntry* relevant_entry_ = nullptr;    // The entry affected by the action.
    std::atomic<JITCodeEntry*> head_{nullptr};  // Head of link list of all entries.

    // Android-specific fields:
    uint8_t magic_[8] = {'A', 'n', 'd', 'r', 'o', 'i', 'd', '1'};
    uint32_t flags_ = 0;  // Reserved for future use. Must be 0.
    uint32_t sizeof_descriptor = sizeof(JITDescriptor);
    uint32_t sizeof_entry = sizeof(JITCodeEntry);
    std::atomic_uint32_t action_seqlock_{0};  // Incremented before and after any modification.
    uint64_t action_timestamp_ = 1;           // CLOCK_MONOTONIC time of last action.
  };

  // Check that std::atomic has the expected layout.
  static_assert(alignof(std::atomic_uint32_t) == alignof(uint32_t), "Weird alignment");
  static_assert(sizeof(std::atomic_uint32_t) == sizeof(uint32_t), "Weird size");
  static_assert(alignof(std::atomic<void*>) == alignof(void*), "Weird alignment");
  static_assert(sizeof(std::atomic<void*>) == sizeof(void*), "Weird size");

  // GDB may set breakpoint here. We must ensure it is not removed or deduplicated.
  void __attribute__((noinline)) __jit_debug_register_code() {
    __asm__("");
  }

  // Alternatively, native tools may overwrite this field to execute custom handler.
  void (*__jit_debug_register_code_ptr)() = __jit_debug_register_code;

  // The root data structure describing of all JITed methods.
  JITDescriptor __jit_debug_descriptor GUARDED_BY(__jit_debug_lock) {};

  // The following globals mirror the ones above, but are used to register dex files.
  void __attribute__((noinline)) __dex_debug_register_code() {
    __asm__("");
  }
  void (*__dex_debug_register_code_ptr)() = __dex_debug_register_code;
  JITDescriptor __dex_debug_descriptor GUARDED_BY(__dex_debug_lock) {};
}

// Mark the descriptor as "locked", so native tools know the data is being modified.
static void ActionSeqlock(JITDescriptor& descriptor) {
  DCHECK_EQ(descriptor.action_seqlock_.load() & 1, 0u) << "Already locked";
  descriptor.action_seqlock_.fetch_add(1, std::memory_order_relaxed);
  // Ensure that any writes within the locked section cannot be reordered before the increment.
  std::atomic_thread_fence(std::memory_order_release);
}

// Mark the descriptor as "unlocked", so native tools know the data is safe to read.
static void ActionSequnlock(JITDescriptor& descriptor) {
  DCHECK_EQ(descriptor.action_seqlock_.load() & 1, 1u) << "Already unlocked";
  // Ensure that any writes within the locked section cannot be reordered after the increment.
  std::atomic_thread_fence(std::memory_order_release);
  descriptor.action_seqlock_.fetch_add(1, std::memory_order_relaxed);
}

static JITCodeEntry* CreateJITCodeEntryInternal(
    JITDescriptor& descriptor,
    void (*register_code_ptr)(),
    ArrayRef<const uint8_t> symfile,
    bool copy_symfile) {
  // Make a copy of the buffer to shrink it and to pass ownership to JITCodeEntry.
  if (copy_symfile) {
    uint8_t* copy = new uint8_t[symfile.size()];
    CHECK(copy != nullptr);
    memcpy(copy, symfile.data(), symfile.size());
    symfile = ArrayRef<const uint8_t>(copy, symfile.size());
  }

  // Ensure the timestamp is monotonically increasing even in presence of low
  // granularity system timer.  This ensures each entry has unique timestamp.
  uint64_t timestamp = std::max(descriptor.action_timestamp_ + 1, NanoTime());

  JITCodeEntry* head = descriptor.head_.load(std::memory_order_relaxed);
  JITCodeEntry* entry = new JITCodeEntry;
  CHECK(entry != nullptr);
  entry->symfile_addr_ = symfile.data();
  entry->symfile_size_ = symfile.size();
  entry->prev_ = nullptr;
  entry->next_.store(head, std::memory_order_relaxed);
  entry->register_timestamp_ = timestamp;

  // We are going to modify the linked list, so take the seqlock.
  ActionSeqlock(descriptor);
  if (head != nullptr) {
    head->prev_ = entry;
  }
  descriptor.head_.store(entry, std::memory_order_relaxed);
  descriptor.relevant_entry_ = entry;
  descriptor.action_flag_ = JIT_REGISTER_FN;
  descriptor.action_timestamp_ = timestamp;
  ActionSequnlock(descriptor);

  (*register_code_ptr)();
  return entry;
}

static void DeleteJITCodeEntryInternal(
    JITDescriptor& descriptor,
    void (*register_code_ptr)(),
    JITCodeEntry* entry,
    bool free_symfile) {
  CHECK(entry != nullptr);
  const uint8_t* symfile = entry->symfile_addr_;

  // Ensure the timestamp is monotonically increasing even in presence of low
  // granularity system timer.  This ensures each entry has unique timestamp.
  uint64_t timestamp = std::max(descriptor.action_timestamp_ + 1, NanoTime());

  // We are going to modify the linked list, so take the seqlock.
  ActionSeqlock(descriptor);
  JITCodeEntry* next = entry->next_.load(std::memory_order_relaxed);
  if (entry->prev_ != nullptr) {
    entry->prev_->next_.store(next, std::memory_order_relaxed);
  } else {
    descriptor.head_.store(next, std::memory_order_relaxed);
  }
  if (next != nullptr) {
    next->prev_ = entry->prev_;
  }
  descriptor.relevant_entry_ = entry;
  descriptor.action_flag_ = JIT_UNREGISTER_FN;
  descriptor.action_timestamp_ = timestamp;
  ActionSequnlock(descriptor);

  (*register_code_ptr)();

  // Ensure that clear below can not be reordered above the unlock above.
  std::atomic_thread_fence(std::memory_order_release);

  // Aggressively clear the entry as an extra check of the synchronisation.
  memset(entry, 0, sizeof(*entry));

  delete entry;
  if (free_symfile) {
    delete[] symfile;
  }
}

static std::map<const DexFile*, JITCodeEntry*> __dex_debug_entries GUARDED_BY(__dex_debug_lock);

void AddNativeDebugInfoForDex(Thread* self, const DexFile* dexfile) {
  MutexLock mu(self, __dex_debug_lock);
  DCHECK(dexfile != nullptr);
  // This is just defensive check. The class linker should not register the dex file twice.
  if (__dex_debug_entries.count(dexfile) == 0) {
    const ArrayRef<const uint8_t> symfile(dexfile->Begin(), dexfile->Size());
    JITCodeEntry* entry = CreateJITCodeEntryInternal(__dex_debug_descriptor,
                                                     __dex_debug_register_code_ptr,
                                                     symfile,
                                                     /*copy_symfile=*/ false);
    __dex_debug_entries.emplace(dexfile, entry);
  }
}

void RemoveNativeDebugInfoForDex(Thread* self, const DexFile* dexfile) {
  MutexLock mu(self, __dex_debug_lock);
  auto it = __dex_debug_entries.find(dexfile);
  // We register dex files in the class linker and free them in DexFile_closeDexFile, but
  // there might be cases where we load the dex file without using it in the class linker.
  if (it != __dex_debug_entries.end()) {
    DeleteJITCodeEntryInternal(__dex_debug_descriptor,
                               __dex_debug_register_code_ptr,
                               /*entry=*/ it->second,
                               /*free_symfile=*/ false);
    __dex_debug_entries.erase(it);
  }
}

// Mapping from handle to entry. Used to manage life-time of the entries.
static std::map<const void*, JITCodeEntry*> __jit_debug_entries GUARDED_BY(__jit_debug_lock);

void AddNativeDebugInfoForJit(Thread* self,
                              const void* code_ptr,
                              const std::vector<uint8_t>& symfile) {
  MutexLock mu(self, __jit_debug_lock);
  DCHECK_NE(symfile.size(), 0u);

  JITCodeEntry* entry = CreateJITCodeEntryInternal(
      __jit_debug_descriptor,
      __jit_debug_register_code_ptr,
      ArrayRef<const uint8_t>(symfile),
      /*copy_symfile=*/ true);

  // We don't provide code_ptr for type debug info, which means we cannot free it later.
  // (this only happens when --generate-debug-info flag is enabled for the purpose
  // of being debugged with gdb; it does not happen for debuggable apps by default).
  if (code_ptr != nullptr) {
    bool ok = __jit_debug_entries.emplace(code_ptr, entry).second;
    DCHECK(ok) << "Native debug entry already exists for " << std::hex << code_ptr;
  }
}

void RemoveNativeDebugInfoForJit(Thread* self, const void* code_ptr) {
  MutexLock mu(self, __jit_debug_lock);
  auto it = __jit_debug_entries.find(code_ptr);
  // We generate JIT native debug info only if the right runtime flags are enabled,
  // but we try to remove it unconditionally whenever code is freed from JIT cache.
  if (it != __jit_debug_entries.end()) {
    DeleteJITCodeEntryInternal(__jit_debug_descriptor,
                               __jit_debug_register_code_ptr,
                               it->second,
                               /*free_symfile=*/ true);
    __jit_debug_entries.erase(it);
  }
}

size_t GetJitMiniDebugInfoMemUsage() {
  MutexLock mu(Thread::Current(), __jit_debug_lock);
  size_t size = 0;
  for (auto entry : __jit_debug_entries) {
    size += sizeof(JITCodeEntry) + entry.second->symfile_size_ + /*map entry*/ 4 * sizeof(void*);
  }
  return size;
}

}  // namespace art
