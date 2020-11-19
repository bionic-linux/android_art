/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef ART_RUNTIME_COMPAT_FRAMEWORK_H_
#define ART_RUNTIME_COMPAT_FRAMEWORK_H_

#include <set>

#include "base/string_view_cpp20.h"

namespace art {

class CompatFramework {
 public:
  enum class ChangeState {
    kUnknown,
    kEnabled,
    kDisabled,
    kLogged
  };

  void SetDisabledCompatChanges(const std::set<uint64_t>& disabled_changes) {
    disabled_compat_changes_ = disabled_changes;
  }

  std::set<uint64_t> GetDisabledCompatChanges() const {
    return disabled_compat_changes_;
  }

  bool IsChangeEnabled(uint64_t change_id);

  void LogChange(uint64_t change_id);

 private:
  static std::string_view ChangeStateToString(ChangeState s);
  void ReportChange(uint64_t change_id, ChangeState state);

  // A set of disabled compat changes for the running app, all other changes are enabled.
  std::set<uint64_t> disabled_compat_changes_;

  // A set of repoted compat changes for the running app.
  std::set<uint64_t> reported_compat_changes_;
};

}  // namespace art

#endif  // ART_RUNTIME_COMPAT_FRAMEWORK_H_
