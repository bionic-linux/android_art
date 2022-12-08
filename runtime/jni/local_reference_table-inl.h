/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef ART_RUNTIME_JNI_LOCAL_REFERENCE_TABLE_INL_H_
#define ART_RUNTIME_JNI_LOCAL_REFERENCE_TABLE_INL_H_

#include "local_reference_table.h"

#include "android-base/stringprintf.h"

#include "base/casts.h"
#include "gc_root-inl.h"
#include "obj_ptr-inl.h"
#include "mirror/object_reference.h"
#include "verify_object.h"

namespace art {
namespace jni {

inline void LrtEntry::SetReference(ObjPtr<mirror::Object> ref) {
  root_ = GcRoot<mirror::Object>(
      mirror::CompressedReference<mirror::Object>::FromMirrorPtr(ref.Ptr()));
  DCHECK(!IsFree());
  DCHECK(!IsDeleted());
}

inline ObjPtr<mirror::Object> LrtEntry::GetReference() {
  DCHECK(!IsFree());
  DCHECK(!IsDeleted());
  DCHECK(!IsNull());
  // Local references do not need read barriers. They are marked during the thread root flip.
  return root_.Read<kWithoutReadBarrier>();
}

inline void LrtEntry::SetFree(uint32_t next_free) {
  uint32_t value = NextFreeField::Update(next_free, 1u << kFlagFree);
  root_ = GcRoot<mirror::Object>(mirror::CompressedReference<mirror::Object>::FromVRegValue(value));
}

inline void LrtEntry::SetDeleted(uint32_t next_free) {
  uint32_t value = NextFreeField::Update(next_free, (1u << kFlagDeleted) | (1u << kFlagFree));
  root_ = GcRoot<mirror::Object>(mirror::CompressedReference<mirror::Object>::FromVRegValue(value));
}

inline uint32_t LocalReferenceTable::GetReferenceEntryIndex(IndirectRef iref) const {
  DCHECK_EQ(GetIndirectRefKind(iref), kLocal);
  LrtEntry* entry = ToLrtEntry(iref);

  if (LIKELY(small_table_ != nullptr)) {
    if (!std::less<const LrtEntry*>()(entry, small_table_) &&
        std::less<const LrtEntry*>()(entry, small_table_ + kSmallLrtEntries)) {
      return dchecked_integral_cast<uint32_t>(entry - small_table_);
    }
  } else {
    for (size_t i = 0, size = tables_.size(); i != size; ++i) {
      LrtEntry* table = tables_[i];
      size_t table_size = GetTableSize(i);
      if (!std::less<const LrtEntry*>()(entry, table) &&
          std::less<const LrtEntry*>()(entry, table + table_size)) {
        return dchecked_integral_cast<size_t>(i != 0u ? table_size : 0u) +
               dchecked_integral_cast<size_t>(entry - table);
      }
    }
  }
  return std::numeric_limits<uint32_t>::max();
}

inline bool LocalReferenceTable::IsValidReference(IndirectRef iref,
                                                  /*out*/std::string* error_msg) const {
  uint32_t entry_index = GetReferenceEntryIndex(iref);
  if (UNLIKELY(entry_index == std::numeric_limits<uint32_t>::max())) {
    *error_msg = android::base::StringPrintf("reference outside the table: %p", iref);
    return false;
  }
  if (UNLIKELY(entry_index >= segment_state_.top_index)) {
    *error_msg = android::base::StringPrintf("popped reference at index %u in a table of size %u",
                                             entry_index,
                                             segment_state_.top_index);
    return false;
  }
  LrtEntry* entry = ToLrtEntry(iref);
  if (UNLIKELY(entry->IsFree())) {
    *error_msg = android::base::StringPrintf("%s reference at index %u",
                                             entry->IsDeleted() ? "deleted" : "popped",
                                             entry_index);
    return false;
  }
  if (UNLIKELY(entry->IsNull())) {
    *error_msg = android::base::StringPrintf("null reference at index %u", entry_index);
    return false;
  }
  return true;
}

inline void LocalReferenceTable::DCheckValidReference(IndirectRef iref) const {
  if (kIsDebugBuild) {
    std::string error_msg;
    CHECK(IsValidReference(iref, &error_msg)) << error_msg;
  }
}

inline ObjPtr<mirror::Object> LocalReferenceTable::Get(IndirectRef iref) const {
  DCheckValidReference(iref);
  return ToLrtEntry(iref)->GetReference();
}

inline void LocalReferenceTable::Update(IndirectRef iref, ObjPtr<mirror::Object> obj) {
  DCheckValidReference(iref);
  ToLrtEntry(iref)->SetReference(obj);
}

}  // namespace jni
}  // namespace art

#endif  // ART_RUNTIME_JNI_LOCAL_REFERENCE_TABLE_INL_H_
