/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef ART_RUNTIME_HIDDEN_API_H_
#define ART_RUNTIME_HIDDEN_API_H_

#include "dex/hidden_api_access_flags.h"
#include "reflection.h"
#include "runtime.h"

namespace art {
namespace hiddenapi {

enum Action {
  kAllow,
  kAllowButWarn,
  kAllowButWarnAndToast,
  kDeny
};

enum AccessMethod {
  kReflection,
  kJNI
};

inline std::ostream& operator<<(std::ostream& os, AccessMethod value) {
  switch (value) {
    case kReflection:
      os << "reflection";
      break;
    case kJNI:
      os << "JNI";
      break;
  }
  return os;
}

inline Action GetMemberAction(uint32_t access_flags) {
  switch (HiddenApiAccessFlags::DecodeFromRuntime(access_flags)) {
    case HiddenApiAccessFlags::kWhitelist:
      return kAllow;
    case HiddenApiAccessFlags::kLightGreylist:
      return kAllowButWarn;
    case HiddenApiAccessFlags::kDarkGreylist:
      return kAllowButWarnAndToast;
    case HiddenApiAccessFlags::kBlacklist:
      return kDeny;
  }
}

// Issue a warning about field access.
inline void WarnAboutMemberAccess(ArtField* field, AccessMethod access_method)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  std::string tmp;
  LOG(WARNING) << "Accessing hidden field "
               << field->GetDeclaringClass()->GetDescriptor(&tmp) << "->"
               << field->GetName() << ":" << field->GetTypeDescriptor()
               << " (" << HiddenApiAccessFlags::DecodeFromRuntime(field->GetAccessFlags())
               << ", " << access_method << ")";
}

// Issue a warning about method access.
inline void WarnAboutMemberAccess(ArtMethod* method, AccessMethod access_method)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  std::string tmp;
  LOG(WARNING) << "Accessing hidden method "
               << method->GetDeclaringClass()->GetDescriptor(&tmp) << "->"
               << method->GetName() << method->GetSignature().ToString()
               << " (" << HiddenApiAccessFlags::DecodeFromRuntime(method->GetAccessFlags())
               << ", " << access_method << ")";
}

// Returns true if access to `member` should be denied to the caller of the
// reflective query. The decision is based on whether the caller is in boot
// class path or not. Because different users of this function determine this
// in a different way, `fn_caller_in_boot(self)` is called and should return
// true if the caller is in boot class path.
// This function might print warnings into the log if the member is greylisted.
template<typename T>
inline bool ShouldBlockAccessToMember(T* member,
                                      Thread* self,
                                      std::function<bool(Thread*)> fn_caller_in_boot,
                                      AccessMethod access_method)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(member != nullptr);
  Runtime* runtime = Runtime::Current();

  if (!runtime->AreHiddenApiChecksEnabled()) {
    // Exit early. Nothing to enforce.
    return false;
  }

  Action action = GetMemberAction(member->GetAccessFlags());
  if (action == kAllow) {
    // Nothing to do.
    return false;
  }

  // Member is hidden. Walk the stack to find the caller.
  // This can be *very* expensive. Save it for last.
  if (fn_caller_in_boot(self)) {
    // Caller in boot class path. Exit.
    return false;
  }

  // Member is hidden and we are not in the boot class path.

  // Print a log message with information about this class member access.
  // We do this regardless of whether we block the access or not.
  WarnAboutMemberAccess(member, access_method);

  // Block access if on blacklist.
  if (action == kDeny) {
    return true;
  }

  // Allow access to this member but print a warning.
  DCHECK(action == kAllowButWarn || action == kAllowButWarnAndToast);

  // Depending on a runtime flag, we might move the member into whitelist and
  // skip the warning the next time the member is accessed.
  if (runtime->ShouldDedupeHiddenApiWarnings()) {
    member->SetAccessFlags(HiddenApiAccessFlags::EncodeForRuntime(
        member->GetAccessFlags(), HiddenApiAccessFlags::kWhitelist));
  }

  // If this action requires a UI warning, set the appropriate flag.
  if (action == kAllowButWarnAndToast || runtime->ShouldAlwaysSetHiddenApiWarningFlag()) {
    Runtime::Current()->SetPendingHiddenApiWarning(true);
  }

  return false;
}

// Returns true if access to member with `access_flags` should be denied to `caller`.
// This function should be called on statically linked uses of hidden API.
inline bool ShouldBlockAccessToMember(uint32_t access_flags, mirror::Class* caller)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  if (!Runtime::Current()->AreHiddenApiChecksEnabled()) {
    // Exit early. Nothing to enforce.
    return false;
  }

  // Only continue if we want to deny access. Warnings are *not* printed.
  if (GetMemberAction(access_flags) != kDeny) {
    return false;
  }

  // Member is hidden. Check if the caller is in boot class path.
  if (caller == nullptr) {
    // The caller is unknown. We assume that this is *not* boot class path.
    return true;
  }

  return !caller->IsBootStrapClassLoaded();
}

}  // namespace hiddenapi
}  // namespace art

#endif  // ART_RUNTIME_HIDDEN_API_H_
