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

#include "dex_file_tracking_registrar.h"

#include <deque>
#include <tuple>

// For dex tracking through poisoning. Note: Requires forcing sanitization. This is the reason for
// the ifdefs and early include.
#ifdef ART_DEX_FILE_ACCESS_TRACKING
#ifndef ART_ENABLE_ADDRESS_SANITIZER
#define ART_ENABLE_ADDRESS_SANITIZER
#endif
#endif
#include "base/memory_tool.h"

#include "base/logging.h"
#include "dex_file-inl.h"

namespace art {
namespace dex {

// If true, poison dex files to track accesses.
static constexpr bool kDexFileAccessTracking =
#ifdef ART_DEX_FILE_ACCESS_TRACKING
    true;
#else
    false;
#endif

inline void SetRegistrationRange(const void* begin, size_t size, bool should_poison) {
  if (should_poison) {
    MEMORY_TOOL_MAKE_NOACCESS(begin, size);
  } else {
    // Note: MEMORY_TOOL_MAKE_UNDEFINED has the same functionality with Address
    // Sanitizer. The difference has not been tested with Valgrind
    MEMORY_TOOL_MAKE_DEFINED(begin, size);
  }
}

void DexFileTrackingRegistrar::SetCurrentRanges() {
  // This also empties range_values_ to prevent redundant behavior upon
  // subsequent calls.
  while (!range_values_.empty()) {
    const std::tuple<const void*, size_t, bool>& current_range = range_values_.front();
    SetRegistrationRange(std::get<0>(current_range),
                         std::get<1>(current_range),
                         std::get<2>(current_range));
    range_values_.pop_front();
  }
}



void DexFileTrackingRegistrar::SetDexFileRegistration(bool should_poison) {
  if (kDexFileAccessTracking || dex_file_ != nullptr) {
    const void* dex_file_begin = reinterpret_cast<const void*>(dex_file_->Begin());
    size_t dex_file_size = dex_file_->Size();
    range_values_.push_back(std::make_tuple(dex_file_begin, dex_file_size, should_poison));
  }
}

void DexFileTrackingRegistrar::SetAllCodeItemRegistration(bool should_poison) {
  if (kDexFileAccessTracking || dex_file_ != nullptr) {
    for (size_t classdef_ctr = 0; classdef_ctr < dex_file_->NumClassDefs(); ++classdef_ctr) {
      const DexFile::ClassDef& cd = dex_file_->GetClassDef(classdef_ctr);
      const uint8_t* class_data = dex_file_->GetClassData(cd);
      if (class_data != nullptr) {
        ClassDataItemIterator cdit(*dex_file_, class_data);
        // Skipping Fields
        while (cdit.HasNextStaticField() || cdit.HasNextInstanceField()) {
          cdit.Next();
        }
        while (cdit.HasNextDirectMethod()) {
          const DexFile::CodeItem* code_item = cdit.GetMethodCodeItem();
          if (code_item != nullptr) {
            const void* code_item_begin = reinterpret_cast<const void*>(code_item);
            size_t code_item_size = DexFile::GetCodeItemSize(*code_item);
            range_values_.push_back(std::make_tuple(code_item_begin, code_item_size, should_poison));
          }
          cdit.Next();
        }
      }
    }
  }
}

void DexFileTrackingRegistrar::SetAllInsnsRegistration(bool should_poison) {
  if (kDexFileAccessTracking || dex_file_ != nullptr) {
    for (size_t classdef_ctr = 0; classdef_ctr < dex_file_->NumClassDefs(); ++classdef_ctr) {
      const DexFile::ClassDef& cd = dex_file_->GetClassDef(classdef_ctr);
      const uint8_t* class_data = dex_file_->GetClassData(cd);
      if (class_data != nullptr) {
        ClassDataItemIterator cdit(*dex_file_, class_data);
        // Skipping Fields
        while (cdit.HasNextStaticField() || cdit.HasNextInstanceField()) {
          cdit.Next();
        }
        while (cdit.HasNextDirectMethod()) {
          const DexFile::CodeItem* code_item = cdit.GetMethodCodeItem();
          if (code_item != nullptr) {
            const void* insns_begin = reinterpret_cast<const void*>(&code_item->insns_);
            size_t insns_size = code_item->insns_size_in_code_units_ * 2;
            range_values_.push_back(std::make_tuple(insns_begin, insns_size, should_poison));
          }
          cdit.Next();
        }
      }
    }
  }
}

void DexFileTrackingRegistrar::SetCodeItemRegistration(const char* class_name, bool should_poison) {
  if (kDexFileAccessTracking || dex_file_ != nullptr) {
    for (size_t classdef_ctr = 0; classdef_ctr < dex_file_->NumClassDefs(); ++classdef_ctr) {
      const DexFile::ClassDef& cd = dex_file_->GetClassDef(classdef_ctr);
      const uint8_t* class_data = dex_file_->GetClassData(cd);
      if (class_data != nullptr) {
        ClassDataItemIterator cdit(*dex_file_, class_data);
        // Skipping Fields
        while (cdit.HasNextStaticField() || cdit.HasNextInstanceField()) {
          cdit.Next();
        }
        while (cdit.HasNextDirectMethod()) {
          const DexFile::MethodId& methodid_item = dex_file_->GetMethodId(cdit.GetMemberIndex());
          const char * methodid_name = dex_file_->GetMethodName(methodid_item);
          const DexFile::CodeItem* code_item = cdit.GetMethodCodeItem();
          if (code_item != nullptr && strcmp(methodid_name, class_name) == 0) {
            const void* code_item_begin = reinterpret_cast<const void*>(code_item);
            size_t code_item_size = DexFile::GetCodeItemSize(*code_item);
            range_values_.push_back(
                std::make_tuple(code_item_begin, code_item_size, should_poison));
          }
          cdit.Next();
        }
      }
    }
  }
}

}  // namespace dex
}  // namespace art
