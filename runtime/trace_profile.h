/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef ART_RUNTIME_TRACE_PROFILE_H_
#define ART_RUNTIME_TRACE_PROFILE_H_

#include <unordered_set>

#include "base/locks.h"
#include "base/macros.h"

namespace art HIDDEN {

class ArtMethod;

// TODO(mythria): A randomly chosen value. Tune it later based on the number of
// entries required in the buffer.
static constexpr size_t kAlwaysOnTraceBufSize = 2048;

class TraceProfiler {
 public:
  static void Start();
  static void Stop();
  static void Dump(const char* trace_filename);
  static bool IsTraceProfileInProgress() REQUIRES(Locks::trace_lock_);

 private:
  static uint8_t* DumpBuffer(uint32_t thread_id,
                             uintptr_t* thread_buffer,
                             uint8_t* buffer,
                             std::unordered_set<ArtMethod*>& methods);

  static bool profile_in_progress_ GUARDED_BY(Locks::trace_lock_);
  DISALLOW_COPY_AND_ASSIGN(TraceProfiler);
};

}  // namespace art

#endif  // ART_RUNTIME_TRACE_PROFILE_H_
