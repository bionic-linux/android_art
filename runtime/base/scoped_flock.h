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

#ifndef ART_RUNTIME_BASE_SCOPED_FLOCK_H_
#define ART_RUNTIME_BASE_SCOPED_FLOCK_H_

#include <memory>
#include <string>

#include "android-base/unique_fd.h"

#include "base/macros.h"
#include "os.h"

namespace art {

// A scoped file-lock implemented using flock. The file is locked by calling the Init function and
// is released during destruction. Note that failing to unlock the file only causes a warning to be
// printed. Users should take care that this does not cause potential deadlocks.
//
// Only printing a warning on unlock failure is okay since this is only used with either:
// 1) a non-blocking Init call, or
// 2) as a part of a seperate binary (eg dex2oat) which has it's own timeout logic to prevent
//    deadlocks.
// This means we can be sure that the warning won't cause a deadlock.
class ScopedFlock {
 public:
  ScopedFlock();

  // Attempts to acquire an exclusive file lock (see flock(2)) on the file
  // at filename, and blocks until it can do so.
  //
  // Returns true if the lock could be acquired, or false if an error occurred.
  // It is an error if its inode changed (usually due to a new file being
  // created at the same path) between attempts to lock it. In blocking mode,
  // locking will be retried if the file changed. In non-blocking mode, false
  // is returned and no attempt is made to re-acquire the lock.
  //
  // The file is opened with the provided flags.
  // Attempt to acquire an exclusive file lock (see flock(2)) on 'file'.
  // Returns true if the lock could be acquired or false if an error
  // occured.
  bool Init(File* file, bool block, std::string* error_msg);

  ~ScopedFlock();

 private:
  android::base::unique_fd locked_fd_;
  DISALLOW_COPY_AND_ASSIGN(ScopedFlock);
};

}  // namespace art

#endif  // ART_RUNTIME_BASE_SCOPED_FLOCK_H_
