/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef ART_RUNTIME_RUNTIME_CALLBACKS_H_
#define ART_RUNTIME_RUNTIME_CALLBACKS_H_

#include <vector>

#include "base/macros.h"
#include "base/mutex.h"

namespace art {

class Thread;
class ThreadLifecycleCallback;

// Note: RuntimeCallbacks uses the mutator lock the synchronize the callback lists. A thread must
//       hold the exclusive lock to add or remove a listener. A thread must hold the shared lock
//       to dispatch an event. This setup is chosen as some clients may want to suspend the
//       dispatching thread or all threads.
//
//       To make this safe, the following restrictions apply:
//       * Only the owner of a listener may ever add or remove said listener.
//       * A listener must never add or remove itself or any other listener while running.
//       * It is the responsibility of the owner to not remove the listener while it is running
//         (and suspended).
//
//       The simplest way to satisfy these restrictions is to never remove a listener, and to do
//       any state checking (is the listener enabled) in the listener itself. For an example, see
//       Dbg.

class RuntimeCallbacks {
 public:
  void AddThreadLifecycleCallback(ThreadLifecycleCallback* cb)
      REQUIRES(Locks::mutator_lock_);
  void RemoveThreadLifecycleCallback(ThreadLifecycleCallback* cb)
      REQUIRES(Locks::mutator_lock_);

  void ThreadStart(Thread* self)
      REQUIRES_SHARED(Locks::mutator_lock_);
  void ThreadDeath(Thread* self)
      REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  std::vector<ThreadLifecycleCallback*> thread_callbacks_
      GUARDED_BY(Locks::mutator_lock_);
};

}  // namespace art

#endif  // ART_RUNTIME_RUNTIME_CALLBACKS_H_
