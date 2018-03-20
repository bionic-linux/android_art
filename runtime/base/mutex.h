/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_RUNTIME_BASE_MUTEX_H_
#define ART_RUNTIME_BASE_MUTEX_H_

#include <stdint.h>

#include <iostream>

#include <android-base/logging.h>

#include "base/atomic.h"
#include "base/globals.h"
#include "base/lock_level.h"
#include "base/macros.h"

#if defined(__APPLE__)
#define ART_USE_FUTEXES 0
#else
#define ART_USE_FUTEXES 1
#endif

// Currently Darwin doesn't support locks with timeouts.
#if !defined(__APPLE__)
#define HAVE_TIMED_RWLOCK 1
#else
#define HAVE_TIMED_RWLOCK 0
#endif

namespace art {

class SHARED_LOCKABLE ReaderWriterMutex;
class ScopedContentionRecorder;
class Thread;
class BaseMutex;
class Mutex;

const bool kDebugLocking = kIsDebugBuild;

// Record Log contention information, dumpable via SIGQUIT.
#ifdef ART_USE_FUTEXES
// To enable lock contention logging, set this to true.
const bool kLogLockContentions = false;
#else
// Keep this false as lock contention logging is supported only with
// futex.
const bool kLogLockContentions = false;
#endif
const size_t kContentionLogSize = 4;
const size_t kContentionLogDataSize = kLogLockContentions ? 1 : 0;
const size_t kAllMutexDataSize = kLogLockContentions ? 1 : 0;

// Class used to check locking invariants (lock order, abort status, etc.).
// The expressed intent of this class is to isolate the implementation details
// of Thread from mutex clients.
class MutexContract {
 public:
  // Checking shutdown while locking for thread safety.
  static bool IsSafeToCallAbortSafe();

  // Checking shutdown without locking for thread safety.
  static bool IsSafeToCallAbortRacy();

  // Get the thread id.
  static pid_t GetTid(const Thread* self);

  // Get a pointer to the current Thread.  This can return nullptr during early startup.
  static Thread* CurrentThread();

  // Returns true if 'self' is either null or the "current" thread.
  static bool IsNullOrCurrentThread(const Thread* self);

  // Returns true if 'mutex' is 'self's current mutex at 'level'.
  static bool IsCurrentMutexAtLevel(const Thread* self,
                                    LockLevel level,
                                    const BaseMutex* mutex);

  // Sets self's current mutex at 'level' to 'mutex'.
  static void SetCurrentMutexAtLevel(Thread* self,
                                     LockLevel level,
                                     BaseMutex* mutex);

  // Checks that it is safe for 'self' to wait on 'mutex' at 'level'.
  static void CheckSafeToWait(const Thread* self,
                              LockLevel level,
                              const BaseMutex* mutex);

  // Checks that there are no pending operations for 'self'.
  static void CheckEmptyCheckpoint(Thread* self);

  // Checks that 'self's current mutex at 'level' are consistent with 'mutex'.
  static void CheckAndLogInvalidThreadNames(const Thread* self,
                                            LockLevel level,
                                            const BaseMutex* mutex);

  // Marks that 'self's current mutex at 'level' is 'mutex' and locked and checks consistency.
  static void SetCurrentMutexChecked(Thread* self,
                                     LockLevel level,
                                     BaseMutex* mutex);

  // Marks that 'self's current mutex at 'level' is 'mutex' and unlocked and checks consistency.
  static void SetCurrentMutexToNullChecked(Thread* self,
                                           LockLevel level,
                                           const BaseMutex* mutex);

  // Report that attempting to lock 'mutex' failed because the state was 'cur_state' and terminate.
  static void FatalUnlockFailed(int32_t cur_state,
                                BaseMutex* mutex,
                                const char* file,
                                uint32_t line);

  // If the current art::Runtime is already shut down, park 'self'.  This is necessary to guard
  // accesses to mutexes, etc., that may be deleted when Runtime shuts down.
  static void SleepForeverIfRuntimeDeleted(const Thread* self);

  // Check whether art::Runtime is in the process of aborting.  If it is, then some checking is
  // disabled in order to log failures, etc.
  static bool RuntimeIsAborting();
};

// Base class for all Mutex implementations
class BaseMutex {
 public:
  const char* GetName() const {
    return name_;
  }

  virtual bool IsMutex() const { return false; }
  virtual bool IsReaderWriterMutex() const { return false; }
  virtual bool IsMutatorMutex() const { return false; }

  virtual void Dump(std::ostream& os) const = 0;

  static void DumpAll(std::ostream& os);

  bool ShouldRespondToEmptyCheckpointRequest() const {
    return should_respond_to_empty_checkpoint_request_;
  }

  void SetShouldRespondToEmptyCheckpointRequest(bool value) {
    should_respond_to_empty_checkpoint_request_ = value;
  }

  virtual void WakeupToRespondToEmptyCheckpoint() = 0;

  // Wait for an amount of time that roughly increases in the argument usec_increment.
  // Spin for small arguments and yield/sleep for longer ones.
  static void BackOff(uint32_t usec_increment);

  static Thread* CurrentThread();

 protected:
  friend class ConditionVariable;

  BaseMutex(const char* name, LockLevel level);
  virtual ~BaseMutex();
  void RegisterAsLocked(Thread* self);
  void RegisterAsUnlocked(Thread* self);
  void CheckSafeToWait(Thread* self);

  friend class ScopedContentionRecorder;

  void RecordContention(uint64_t blocked_tid, uint64_t owner_tid, uint64_t nano_time_blocked);
  void DumpContention(std::ostream& os) const;

  const LockLevel level_;  // Support for lock hierarchy.
  const char* const name_;
  bool should_respond_to_empty_checkpoint_request_;

  // A log entry that records contention but makes no guarantee that either tid will be held live.
  struct ContentionLogEntry {
    ContentionLogEntry() : blocked_tid(0), owner_tid(0) {}
    uint64_t blocked_tid;
    uint64_t owner_tid;
    AtomicInteger count;
  };
  struct ContentionLogData {
    ContentionLogEntry contention_log[kContentionLogSize];
    // The next entry in the contention log to be updated. Value ranges from 0 to
    // kContentionLogSize - 1.
    AtomicInteger cur_content_log_entry;
    // Number of times the Mutex has been contended.
    AtomicInteger contention_count;
    // Sum of time waited by all contenders in ns.
    Atomic<uint64_t> wait_time;
    void AddToWaitTime(uint64_t value);
    ContentionLogData() : wait_time(0) {}
  };
  ContentionLogData contention_log_data_[kContentionLogDataSize];

 public:
  bool HasEverContended() const {
    if (kLogLockContentions) {
      return contention_log_data_->contention_count.LoadSequentiallyConsistent() > 0;
    }
    return false;
  }
};

// A Mutex is used to achieve mutual exclusion between threads. A Mutex can be used to gain
// exclusive access to what it guards. A Mutex can be in one of two states:
// - Free - not owned by any thread,
// - Exclusive - owned by a single thread.
//
// The effect of locking and unlocking operations on the state is:
// State     | ExclusiveLock | ExclusiveUnlock
// -------------------------------------------
// Free      | Exclusive     | error
// Exclusive | Block*        | Free
// * Mutex is not reentrant and so an attempt to ExclusiveLock on the same thread will result in
//   an error. Being non-reentrant simplifies Waiting on ConditionVariables.
std::ostream& operator<<(std::ostream& os, const Mutex& mu);
class LOCKABLE Mutex : public BaseMutex {
 public:
  explicit Mutex(const char* name, LockLevel level = kDefaultMutexLevel, bool recursive = false);
  ~Mutex();

  virtual bool IsMutex() const { return true; }

  // Block until mutex is free then acquire exclusive access.
  void ExclusiveLock(Thread* self) ACQUIRE();
  void Lock(Thread* self) ACQUIRE() {  ExclusiveLock(self); }

  // Returns true if acquires exclusive access, false otherwise.
  bool ExclusiveTryLock(Thread* self) TRY_ACQUIRE(true);
  bool TryLock(Thread* self) TRY_ACQUIRE(true) { return ExclusiveTryLock(self); }

  // Release exclusive access.
  void ExclusiveUnlock(Thread* self) RELEASE();
  void Unlock(Thread* self) RELEASE() {  ExclusiveUnlock(self); }

  // Is the current thread the exclusive holder of the Mutex.
  ALWAYS_INLINE bool IsExclusiveHeld(const Thread* self) const;

  // Assert that the Mutex is exclusively held by the current thread.
  ALWAYS_INLINE void AssertExclusiveHeld(const Thread* self) const ASSERT_CAPABILITY(this);
  ALWAYS_INLINE void AssertHeld(const Thread* self) const ASSERT_CAPABILITY(this);

  // Assert that the Mutex is not held by the current thread.
  void AssertNotHeldExclusive(const Thread* self) ASSERT_CAPABILITY(!*this) {
    if (kDebugLocking && !MutexContract::RuntimeIsAborting()) {
      CHECK(!IsExclusiveHeld(self)) << *this;
    }
  }
  void AssertNotHeld(const Thread* self) ASSERT_CAPABILITY(!*this) {
    AssertNotHeldExclusive(self);
  }

  // Id associated with exclusive owner. No memory ordering semantics if called from a thread other
  // than the owner.
  pid_t GetExclusiveOwnerTid() const;

  // Returns how many times this Mutex has been locked, it is better to use AssertHeld/NotHeld.
  unsigned int GetDepth() const {
    return recursion_count_;
  }

  virtual void Dump(std::ostream& os) const;

  // For negative capabilities in clang annotations.
  const Mutex& operator!() const { return *this; }

  void WakeupToRespondToEmptyCheckpoint() OVERRIDE;

 private:
#if ART_USE_FUTEXES
  // 0 is unheld, 1 is held.
  AtomicInteger state_;
  // Exclusive owner.
  Atomic<pid_t> exclusive_owner_;
  // Number of waiting contenders.
  AtomicInteger num_contenders_;
#else
  pthread_mutex_t mutex_;
  Atomic<pid_t> exclusive_owner_;  // Guarded by mutex_. Asynchronous reads are OK.
#endif
  const bool recursive_;  // Can the lock be recursively held?
  unsigned int recursion_count_;
  friend class ConditionVariable;
  DISALLOW_COPY_AND_ASSIGN(Mutex);
};

// A ReaderWriterMutex is used to achieve mutual exclusion between threads, similar to a Mutex.
// Unlike a Mutex a ReaderWriterMutex can be used to gain exclusive (writer) or shared (reader)
// access to what it guards. A flaw in relation to a Mutex is that it cannot be used with a
// condition variable. A ReaderWriterMutex can be in one of three states:
// - Free - not owned by any thread,
// - Exclusive - owned by a single thread,
// - Shared(n) - shared amongst n threads.
//
// The effect of locking and unlocking operations on the state is:
//
// State     | ExclusiveLock | ExclusiveUnlock | SharedLock       | SharedUnlock
// ----------------------------------------------------------------------------
// Free      | Exclusive     | error           | SharedLock(1)    | error
// Exclusive | Block         | Free            | Block            | error
// Shared(n) | Block         | error           | SharedLock(n+1)* | Shared(n-1) or Free
// * for large values of n the SharedLock may block.
std::ostream& operator<<(std::ostream& os, const ReaderWriterMutex& mu);
class SHARED_LOCKABLE ReaderWriterMutex : public BaseMutex {
 public:
  explicit ReaderWriterMutex(const char* name, LockLevel level = kDefaultMutexLevel);
  ~ReaderWriterMutex();

  virtual bool IsReaderWriterMutex() const { return true; }

  // Block until ReaderWriterMutex is free then acquire exclusive access.
  void ExclusiveLock(Thread* self) ACQUIRE();
  void WriterLock(Thread* self) ACQUIRE() {  ExclusiveLock(self); }

  // Release exclusive access.
  void ExclusiveUnlock(Thread* self) RELEASE();
  void WriterUnlock(Thread* self) RELEASE() {  ExclusiveUnlock(self); }

  // Block until ReaderWriterMutex is free and acquire exclusive access. Returns true on success
  // or false if timeout is reached.
#if HAVE_TIMED_RWLOCK
  bool ExclusiveLockWithTimeout(Thread* self, int64_t ms, int32_t ns)
      EXCLUSIVE_TRYLOCK_FUNCTION(true);
#endif

  // Block until ReaderWriterMutex is shared or free then acquire a share on the access.
  void SharedLock(Thread* self) ACQUIRE_SHARED() ALWAYS_INLINE;
  void ReaderLock(Thread* self) ACQUIRE_SHARED() { SharedLock(self); }

  // Try to acquire share of ReaderWriterMutex.
  bool SharedTryLock(Thread* self) SHARED_TRYLOCK_FUNCTION(true);

  // Release a share of the access.
  void SharedUnlock(Thread* self) RELEASE_SHARED() ALWAYS_INLINE;
  void ReaderUnlock(Thread* self) RELEASE_SHARED() { SharedUnlock(self); }

  // Is the current thread the exclusive holder of the ReaderWriterMutex.
  ALWAYS_INLINE bool IsExclusiveHeld(const Thread* self) const;

  // Assert the current thread has exclusive access to the ReaderWriterMutex.
  ALWAYS_INLINE void AssertExclusiveHeld(const Thread* self) const ASSERT_CAPABILITY(this);
  ALWAYS_INLINE void AssertWriterHeld(const Thread* self) const ASSERT_CAPABILITY(this);

  // Assert the current thread doesn't have exclusive access to the ReaderWriterMutex.
  void AssertNotExclusiveHeld(const Thread* self) ASSERT_CAPABILITY(!this) {
    if (kDebugLocking && !MutexContract::RuntimeIsAborting()) {
      CHECK(!IsExclusiveHeld(self)) << *this;
    }
  }
  void AssertNotWriterHeld(const Thread* self) ASSERT_CAPABILITY(!this) {
    AssertNotExclusiveHeld(self);
  }

  // Is the current thread a shared holder of the ReaderWriterMutex.
  bool IsSharedHeld(const Thread* self) const;

  // Assert the current thread has shared access to the ReaderWriterMutex.
  ALWAYS_INLINE void AssertSharedHeld(const Thread* self) ASSERT_SHARED_CAPABILITY(this) {
    if (kDebugLocking && !MutexContract::RuntimeIsAborting()) {
      // TODO: we can only assert this well when self != null.
      CHECK(IsSharedHeld(self) || self == nullptr) << *this;
    }
  }
  ALWAYS_INLINE void AssertReaderHeld(const Thread* self) ASSERT_SHARED_CAPABILITY(this) {
    AssertSharedHeld(self);
  }

  // Assert the current thread doesn't hold this ReaderWriterMutex either in shared or exclusive
  // mode.
  ALWAYS_INLINE void AssertNotHeld(const Thread* self) ASSERT_SHARED_CAPABILITY(!this) {
    if (kDebugLocking && !MutexContract::RuntimeIsAborting()) {
      CHECK(!IsSharedHeld(self)) << *this;
    }
  }

  // Id associated with exclusive owner. No memory ordering semantics if called from a thread other
  // than the owner. Returns 0 if the lock is not held. Returns either 0 or -1 if it is held by
  // one or more readers.
  pid_t GetExclusiveOwnerTid() const;

  virtual void Dump(std::ostream& os) const;

  // For negative capabilities in clang annotations.
  const ReaderWriterMutex& operator!() const { return *this; }

  void WakeupToRespondToEmptyCheckpoint() OVERRIDE;

 private:
#if ART_USE_FUTEXES
  // Out-of-inline path for handling contention for a SharedLock.
  void HandleSharedLockContention(Thread* self, int32_t cur_state);

  // -1 implies held exclusive, +ve shared held by state_ many owners.
  AtomicInteger state_;
  // Exclusive owner. Modification guarded by this mutex.
  Atomic<pid_t> exclusive_owner_;
  // Number of contenders waiting for a reader share.
  AtomicInteger num_pending_readers_;
  // Number of contenders waiting to be the writer.
  AtomicInteger num_pending_writers_;
#else
  pthread_rwlock_t rwlock_;
  Atomic<pid_t> exclusive_owner_;  // Writes guarded by rwlock_. Asynchronous reads are OK.
#endif
  DISALLOW_COPY_AND_ASSIGN(ReaderWriterMutex);
};

// ConditionVariables allow threads to queue and sleep. Threads may then be resumed individually
// (Signal) or all at once (Broadcast).
class ConditionVariable {
 public:
  ConditionVariable(const char* name, Mutex& mutex);
  ~ConditionVariable();

  void Broadcast(Thread* self);
  void Signal(Thread* self);
  // TODO: No thread safety analysis on Wait and TimedWait as they call mutex operations via their
  //       pointer copy, thereby defeating annotalysis.
  void Wait(Thread* self) NO_THREAD_SAFETY_ANALYSIS;
  bool TimedWait(Thread* self, int64_t ms, int32_t ns) NO_THREAD_SAFETY_ANALYSIS;
  // Variant of Wait that should be used with caution. Doesn't validate that no mutexes are held
  // when waiting.
  // TODO: remove this.
  void WaitHoldingLocks(Thread* self) NO_THREAD_SAFETY_ANALYSIS;

 private:
  const char* const name_;
  // The Mutex being used by waiters. It is an error to mix condition variables between different
  // Mutexes.
  Mutex& guard_;
#if ART_USE_FUTEXES
  // A counter that is modified by signals and broadcasts. This ensures that when a waiter gives up
  // their Mutex and another thread takes it and signals, the waiting thread observes that sequence_
  // changed and doesn't enter the wait. Modified while holding guard_, but is read by futex wait
  // without guard_ held.
  AtomicInteger sequence_;
  // Number of threads that have come into to wait, not the length of the waiters on the futex as
  // waiters may have been requeued onto guard_. Guarded by guard_.
  volatile int32_t num_waiters_;
#else
  pthread_cond_t cond_;
#endif
  DISALLOW_COPY_AND_ASSIGN(ConditionVariable);
};

// Scoped locker/unlocker for a regular Mutex that acquires mu upon construction and releases it
// upon destruction.
class SCOPED_CAPABILITY MutexLock {
 public:
  MutexLock(Thread* self, Mutex& mu) ACQUIRE(mu) : self_(self), mu_(mu) {
    mu_.ExclusiveLock(self_);
  }

  ~MutexLock() RELEASE() {
    mu_.ExclusiveUnlock(self_);
  }

 private:
  Thread* const self_;
  Mutex& mu_;
  DISALLOW_COPY_AND_ASSIGN(MutexLock);
};
// Catch bug where variable name is omitted. "MutexLock (lock);" instead of "MutexLock mu(lock)".
#define MutexLock(x) static_assert(0, "MutexLock declaration missing variable name")

// Scoped locker/unlocker for a ReaderWriterMutex that acquires read access to mu upon
// construction and releases it upon destruction.
class SCOPED_CAPABILITY ReaderMutexLock {
 public:
  ALWAYS_INLINE ReaderMutexLock(Thread* self, ReaderWriterMutex& mu) ACQUIRE(mu);

  ALWAYS_INLINE ~ReaderMutexLock() RELEASE();

 private:
  Thread* const self_;
  ReaderWriterMutex& mu_;
  DISALLOW_COPY_AND_ASSIGN(ReaderMutexLock);
};

// Scoped locker/unlocker for a ReaderWriterMutex that acquires write access to mu upon
// construction and releases it upon destruction.
class SCOPED_CAPABILITY WriterMutexLock {
 public:
  WriterMutexLock(Thread* self, ReaderWriterMutex& mu) EXCLUSIVE_LOCK_FUNCTION(mu) :
      self_(self), mu_(mu) {
    mu_.ExclusiveLock(self_);
  }

  ~WriterMutexLock() UNLOCK_FUNCTION() {
    mu_.ExclusiveUnlock(self_);
  }

 private:
  Thread* const self_;
  ReaderWriterMutex& mu_;
  DISALLOW_COPY_AND_ASSIGN(WriterMutexLock);
};
// Catch bug where variable name is omitted. "WriterMutexLock (lock);" instead of
// "WriterMutexLock mu(lock)".
#define WriterMutexLock(x) static_assert(0, "WriterMutexLock declaration missing variable name")

// MutatorMutex is a special kind of ReaderWriterMutex created specifically for the
// Locks::mutator_lock_ mutex. The behaviour is identical to the ReaderWriterMutex except that
// thread state changes also play a part in lock ownership. The mutator_lock_ will not be truly
// held by any mutator threads. However, a thread in the kRunnable state is considered to have
// shared ownership of the mutator lock and therefore transitions in and out of the kRunnable
// state have associated implications on lock ownership. Extra methods to handle the state
// transitions have been added to the interface but are only accessible to the methods dealing
// with state transitions. The thread state and flags attributes are used to ensure thread state
// transitions are consistent with the permitted behaviour of the mutex.
//
// *) The most important consequence of this behaviour is that all threads must be in one of the
// suspended states before exclusive ownership of the mutator mutex is sought.
//
class SHARED_LOCKABLE MutatorMutex : public ReaderWriterMutex {
 public:
  explicit MutatorMutex(const char* name, LockLevel level = kDefaultMutexLevel)
    : ReaderWriterMutex(name, level) {}
  ~MutatorMutex() {}

  virtual bool IsMutatorMutex() const { return true; }

  // For negative capabilities in clang annotations.
  const MutatorMutex& operator!() const { return *this; }

 private:
  friend class Thread;
  void TransitionFromRunnableToSuspended(Thread* self) UNLOCK_FUNCTION() ALWAYS_INLINE;
  void TransitionFromSuspendedToRunnable(Thread* self) SHARED_LOCK_FUNCTION() ALWAYS_INLINE;

  DISALLOW_COPY_AND_ASSIGN(MutatorMutex);
};
std::ostream& operator<<(std::ostream& os, const MutatorMutex& mu);

inline void MutatorMutex::TransitionFromRunnableToSuspended(Thread* self) {
  AssertSharedHeld(self);
  RegisterAsUnlocked(self);
}

inline void MutatorMutex::TransitionFromSuspendedToRunnable(Thread* self) {
  RegisterAsLocked(self);
  AssertSharedHeld(self);
}

// For StartNoThreadSuspension and EndNoThreadSuspension.
class CAPABILITY("role") Role {
 public:
  void Acquire() ACQUIRE() {}
  void Release() RELEASE() {}
  const Role& operator!() const { return *this; }
};

class Uninterruptible : public Role {
};

class Roles {
 public:
  // Uninterruptible means that the thread may not become suspended.
  static Uninterruptible uninterruptible_;
};

}  // namespace art

#include "base/locks.h"

#endif  // ART_RUNTIME_BASE_MUTEX_H_
