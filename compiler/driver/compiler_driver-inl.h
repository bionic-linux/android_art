/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef ART_COMPILER_DRIVER_COMPILER_DRIVER_INL_H_
#define ART_COMPILER_DRIVER_COMPILER_DRIVER_INL_H_

#include "compiler_driver.h"

#include "art_field-inl.h"
#include "art_method-inl.h"
#include "base/enums.h"
#include "class_linker-inl.h"
#include "dex_compilation_unit.h"
#include "mirror/class_loader.h"
#include "mirror/dex_cache-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "handle_scope-inl.h"

namespace art {

inline mirror::ClassLoader* CompilerDriver::GetClassLoader(const ScopedObjectAccess& soa,
                                                           const DexCompilationUnit* mUnit) {
  return soa.Decode<mirror::ClassLoader>(mUnit->GetClassLoader()).Ptr();
}

inline mirror::Class* CompilerDriver::ResolveClass(
    const ScopedObjectAccess& soa, Handle<mirror::DexCache> dex_cache,
    Handle<mirror::ClassLoader> class_loader, dex::TypeIndex cls_index,
    const DexCompilationUnit* mUnit) {
  DCHECK_EQ(dex_cache->GetDexFile(), mUnit->GetDexFile());
  DCHECK_EQ(class_loader.Get(), GetClassLoader(soa, mUnit));
  mirror::Class* cls = mUnit->GetClassLinker()->ResolveType(
      *mUnit->GetDexFile(), cls_index, dex_cache, class_loader);
  DCHECK_EQ(cls == nullptr, soa.Self()->IsExceptionPending());
  if (UNLIKELY(cls == nullptr)) {
    // Clean up any exception left by type resolution.
    soa.Self()->ClearException();
  }
  return cls;
}

inline mirror::Class* CompilerDriver::ResolveCompilingMethodsClass(
    const ScopedObjectAccess& soa, Handle<mirror::DexCache> dex_cache,
    Handle<mirror::ClassLoader> class_loader, const DexCompilationUnit* mUnit) {
  DCHECK_EQ(dex_cache->GetDexFile(), mUnit->GetDexFile());
  DCHECK_EQ(class_loader.Get(), GetClassLoader(soa, mUnit));
  const DexFile::MethodId& referrer_method_id =
      mUnit->GetDexFile()->GetMethodId(mUnit->GetDexMethodIndex());
  return ResolveClass(soa, dex_cache, class_loader, referrer_method_id.class_idx_, mUnit);
}

inline ArtField* CompilerDriver::ResolveFieldWithDexFile(
    const ScopedObjectAccess& soa, Handle<mirror::DexCache> dex_cache,
    Handle<mirror::ClassLoader> class_loader, const DexFile* dex_file,
    uint32_t field_idx, bool is_static) {
  DCHECK_EQ(dex_cache->GetDexFile(), dex_file);
  ArtField* resolved_field = Runtime::Current()->GetClassLinker()->ResolveField(
      *dex_file, field_idx, dex_cache, class_loader, is_static);
  DCHECK_EQ(resolved_field == nullptr, soa.Self()->IsExceptionPending());
  if (UNLIKELY(resolved_field == nullptr)) {
    // Clean up any exception left by type resolution.
    soa.Self()->ClearException();
    return nullptr;
  }
  if (UNLIKELY(resolved_field->IsStatic() != is_static)) {
    // ClassLinker can return a field of the wrong kind directly from the DexCache.
    // Silently return null on such incompatible class change.
    return nullptr;
  }
  return resolved_field;
}

inline ArtField* CompilerDriver::ResolveField(
    const ScopedObjectAccess& soa, Handle<mirror::DexCache> dex_cache,
    Handle<mirror::ClassLoader> class_loader, const DexCompilationUnit* mUnit,
    uint32_t field_idx, bool is_static) {
  DCHECK_EQ(class_loader.Get(), GetClassLoader(soa, mUnit));
  return ResolveFieldWithDexFile(soa, dex_cache, class_loader, mUnit->GetDexFile(), field_idx,
                                 is_static);
}

inline std::pair<bool, bool> CompilerDriver::IsFastInstanceField(
    mirror::DexCache* dex_cache, mirror::Class* referrer_class,
    ArtField* resolved_field, uint16_t field_idx) {
  DCHECK(!resolved_field->IsStatic());
  ObjPtr<mirror::Class> fields_class = resolved_field->GetDeclaringClass();
  bool fast_get = referrer_class != nullptr &&
      referrer_class->CanAccessResolvedField(fields_class,
                                             resolved_field,
                                             dex_cache,
                                             field_idx);
  bool fast_put = fast_get && (!resolved_field->IsFinal() || fields_class == referrer_class);
  return std::make_pair(fast_get, fast_put);
}

template <typename ArtMember>
inline bool CompilerDriver::CanAccessResolvedMember(mirror::Class* referrer_class ATTRIBUTE_UNUSED,
                                                    mirror::Class* access_to ATTRIBUTE_UNUSED,
                                                    ArtMember* member ATTRIBUTE_UNUSED,
                                                    mirror::DexCache* dex_cache ATTRIBUTE_UNUSED,
                                                    uint32_t field_idx ATTRIBUTE_UNUSED) {
  // Not defined for ArtMember values other than ArtField or ArtMethod.
  UNREACHABLE();
}

template <>
inline bool CompilerDriver::CanAccessResolvedMember<ArtField>(mirror::Class* referrer_class,
                                                              mirror::Class* access_to,
                                                              ArtField* field,
                                                              mirror::DexCache* dex_cache,
                                                              uint32_t field_idx) {
  return referrer_class->CanAccessResolvedField(access_to, field, dex_cache, field_idx);
}

template <>
inline bool CompilerDriver::CanAccessResolvedMember<ArtMethod>(
    mirror::Class* referrer_class,
    mirror::Class* access_to,
    ArtMethod* method,
    mirror::DexCache* dex_cache,
    uint32_t field_idx) {
  return referrer_class->CanAccessResolvedMethod(access_to, method, dex_cache, field_idx);
}

template <typename ArtMember>
inline std::pair<bool, bool> CompilerDriver::IsClassOfStaticMemberAvailableToReferrer(
    mirror::DexCache* dex_cache,
    mirror::Class* referrer_class,
    ArtMember* resolved_member,
    uint16_t member_idx,
    dex::TypeIndex* storage_index) {
  DCHECK(resolved_member->IsStatic());
  if (LIKELY(referrer_class != nullptr)) {
    ObjPtr<mirror::Class> members_class = resolved_member->GetDeclaringClass();
    if (members_class == referrer_class) {
      *storage_index = members_class->GetDexTypeIndex();
      return std::make_pair(true, true);
    }
    if (CanAccessResolvedMember<ArtMember>(
        referrer_class, members_class.Ptr(), resolved_member, dex_cache, member_idx)) {
      // We have the resolved member, we must make it into a index for the referrer
      // in its static storage (which may fail if it doesn't have a slot for it)
      // TODO: for images we can elide the static storage base null check
      // if we know there's a non-null entry in the image
      const DexFile* dex_file = dex_cache->GetDexFile();
      dex::TypeIndex storage_idx(DexFile::kDexNoIndex16);
      if (LIKELY(members_class->GetDexCache() == dex_cache)) {
        // common case where the dex cache of both the referrer and the member are the same,
        // no need to search the dex file
        storage_idx = members_class->GetDexTypeIndex();
      } else {
        // Search dex file for localized ssb index, may fail if member's class is a parent
        // of the class mentioned in the dex file and there is no dex cache entry.
        storage_idx = resolved_member->GetDeclaringClass()->FindTypeIndexInOtherDexFile(*dex_file);
      }
      if (storage_idx.IsValid()) {
        *storage_index = storage_idx;
        return std::make_pair(true, !resolved_member->IsFinal());
      }
    }
  }
  // Conservative defaults.
  *storage_index = dex::TypeIndex(DexFile::kDexNoIndex16);
  return std::make_pair(false, false);
}

inline std::pair<bool, bool> CompilerDriver::IsFastStaticField(
    mirror::DexCache* dex_cache, mirror::Class* referrer_class,
    ArtField* resolved_field, uint16_t field_idx, dex::TypeIndex* storage_index) {
  return IsClassOfStaticMemberAvailableToReferrer(
      dex_cache, referrer_class, resolved_field, field_idx, storage_index);
}

inline bool CompilerDriver::IsClassOfStaticMethodAvailableToReferrer(
    mirror::DexCache* dex_cache, mirror::Class* referrer_class,
    ArtMethod* resolved_method, uint16_t method_idx, dex::TypeIndex* storage_index) {
  std::pair<bool, bool> result = IsClassOfStaticMemberAvailableToReferrer(
      dex_cache, referrer_class, resolved_method, method_idx, storage_index);
  // Only the first member of `result` is meaningful, as there is no
  // "write access" to a method.
  return result.first;
}

inline ArtMethod* CompilerDriver::ResolveMethod(
    ScopedObjectAccess& soa, Handle<mirror::DexCache> dex_cache,
    Handle<mirror::ClassLoader> class_loader, const DexCompilationUnit* mUnit,
    uint32_t method_idx, InvokeType invoke_type, bool check_incompatible_class_change) {
  DCHECK_EQ(class_loader.Get(), GetClassLoader(soa, mUnit));
  ArtMethod* resolved_method =
      check_incompatible_class_change
          ? mUnit->GetClassLinker()->ResolveMethod<ClassLinker::kForceICCECheck>(
              *dex_cache->GetDexFile(), method_idx, dex_cache, class_loader, nullptr, invoke_type)
          : mUnit->GetClassLinker()->ResolveMethod<ClassLinker::kNoICCECheckForCache>(
              *dex_cache->GetDexFile(), method_idx, dex_cache, class_loader, nullptr, invoke_type);
  if (UNLIKELY(resolved_method == nullptr)) {
    DCHECK(soa.Self()->IsExceptionPending());
    // Clean up any exception left by type resolution.
    soa.Self()->ClearException();
  }
  return resolved_method;
}

}  // namespace art

#endif  // ART_COMPILER_DRIVER_COMPILER_DRIVER_INL_H_
