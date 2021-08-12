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

#ifndef ART_RUNTIME_MOCKABLE_EXEC_UTILS_H_
#define ART_RUNTIME_MOCKABLE_EXEC_UTILS_H_

#include "exec_utils.h"

namespace art {

// A wrapper class to make exec_utils mockable.
class MockableExecUtils {
 public:
  virtual ~MockableExecUtils() = default;

  virtual bool Exec(std::vector<std::string>& arg_vector, /*out*/ std::string* error_msg) const {
    return art::Exec(arg_vector, error_msg);
  }

  virtual int ExecAndReturnCode(std::vector<std::string>& arg_vector,
                                /*out*/ std::string* error_msg) const {
    return art::ExecAndReturnCode(arg_vector, error_msg);
  }

  virtual int ExecAndReturnCode(std::vector<std::string>& arg_vector,
                                time_t timeout_secs,
                                /*out*/ bool* timed_out,
                                /*out*/ std::string* error_msg) const {
    return art::ExecAndReturnCode(arg_vector, timeout_secs, timed_out, error_msg);
  }
};

}  // namespace art

#endif  // ART_RUNTIME_MOCKABLE_EXEC_UTILS_H_
