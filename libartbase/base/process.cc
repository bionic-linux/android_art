/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "process.h"

#include "android-base/process.h"
#include "logging.h"

#if defined(__linux__)
#include "procinfo/process.h"
#endif

namespace art {

std::vector<pid_t> GetPidByName(const std::string& process_name) {
#if defined(__linux__)
  std::vector<pid_t> results;
  for (pid_t pid : android::base::AllPids{}) {
    android::procinfo::ProcessInfo process_info;
    std::string error;
    if (!android::procinfo::GetProcessInfo(pid, &process_info, &error)) {
      continue;
    }
    if (process_info.name == process_name) {
      results.push_back(pid);
    }
  }
  return results;
#else
  (void)process_name;  // Unused.
  UNIMPLEMENTED(WARNING);
  return std::vector<pid_t>{};
#endif
}

}  // namespace art
