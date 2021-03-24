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

#include "art_api/dex_file_external.h"

#include <inttypes.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/macros.h>
#include <android-base/mapped_file.h>
#include <android-base/stringprintf.h>

#include <dex/class_accessor-inl.h>
#include <dex/code_item_accessors-inl.h>
#include <dex/dex_file-inl.h>
#include <dex/dex_file_loader.h>

extern "C" {
// Wraps DexFile to add the caching needed by the external interface. This is
// what gets passed over as ExtDexFile*.
struct ExtDexFile {
  struct MethodCacheEntry {
    uint32_t offset;  // Offset relative to the start of the dex file header.
    uint32_t len;
    uint32_t index;  // Method index.
  };

 public:
  std::unique_ptr<const art::DexFile> dex_file_;
  explicit ExtDexFile(std::unique_ptr<const art::DexFile>&& dex_file)
      : dex_file_(std::move(dex_file)) {}

  bool GetMethodDefIndex(uint32_t dex_offset, uint32_t* index, uint32_t* addr, uint32_t* size) {
    uint32_t class_def_index;
    if (GetClassDefIndex(dex_offset, &class_def_index)) {
      art::ClassAccessor accessor(*dex_file_, class_def_index);

      for (const art::ClassAccessor::Method& method : accessor.GetMethods()) {
        art::CodeItemInstructionAccessor code = method.GetInstructions();
        if (!code.HasCodeItem()) {
          continue;
        }

        uint32_t offset = reinterpret_cast<const uint8_t*>(code.Insns()) - dex_file_->Begin();
        uint32_t len = code.InsnsSizeInBytes();
        if (offset <= dex_offset && dex_offset < offset + len) {
          *index = method.GetIndex();
          *addr = offset;
          *size = len;
          return true;
        }
      }
    }
    return false;
  }

  // Collection information about the given method. Not thread safe.
  // The returned name is very short lived (refers to the instance field). Use with care!
  ExtDexFileMethodInfo NewMethodInfo(uint64_t method_index,
                                     ExtDexFileMethodFlags flags,
                                     uint32_t addr,
                                     uint32_t size) {
    const art::dex::MethodId& method_id = dex_file_->GetMethodId(method_index);
    temporary_name_.clear();
    std::string_view name;
    if ((flags & kExtDexFileFlags_nameWithParameters) != 0) {
      dex_file_->AppendPrettyMethod(method_index, true, &temporary_name_);
      name = temporary_name_;
    } else if ((flags & kExtDexFileFlags_nameWithClass) != 0) {
      dex_file_->AppendPrettyMethod(method_index, false, &temporary_name_);
      name = temporary_name_;
    } else if ((flags & kExtDexFileFlags_nameOnly) != 0) {
      name = dex_file_->GetMethodName(method_id);
    }
    return {
      .sizeof_struct = sizeof(ExtDexFileMethodInfo),
      .addr = addr,
      .size = size,
      .class_descriptor = dex_file_->GetMethodDeclaringClassDescriptor(method_id),
      .name = name.data(),
      .name_size = name.size(),
    };
  }

 private:
  void CreateClassCache() {
    // Create binary search table with (end_dex_offset, class_def_index) entries.
    // That is, we don't assume that dex code of given class is consecutive.
    std::deque<std::pair<uint32_t, uint32_t>> cache;
    for (art::ClassAccessor accessor : dex_file_->GetClasses()) {
      for (const art::ClassAccessor::Method& method : accessor.GetMethods()) {
        art::CodeItemInstructionAccessor code = method.GetInstructions();
        if (code.HasCodeItem()) {
          int32_t offset = reinterpret_cast<const uint8_t*>(code.Insns()) - dex_file_->Begin();
          DCHECK_NE(offset, 0);
          cache.emplace_back(offset + code.InsnsSizeInBytes(), accessor.GetClassDefIndex());
        }
      }
    }
    std::sort(cache.begin(), cache.end());

    // If two consecutive methods belong to same class, we can merge them.
    // This tends to reduce the number of entries (used memory) by 10x.
    size_t num_entries = cache.size();
    if (cache.size() > 1) {
      for (auto it = std::next(cache.begin()); it != cache.end(); it++) {
        if (std::prev(it)->second == it->second) {
          std::prev(it)->first = 0;  // Clear entry with lower end_dex_offset (mark to remove).
          num_entries--;
        }
      }
    }

    // The cache is immutable now. Store it as continuous vector to save space.
    class_cache_.reserve(num_entries);
    auto pred = [](auto it) { return it.first != 0; };  // Entries to copy (not cleared above).
    std::copy_if(cache.begin(), cache.end(), std::back_inserter(class_cache_), pred);
  }

  inline bool GetClassDefIndex(uint32_t dex_offset, uint32_t* class_def_index) {
    if (class_cache_.empty()) {
      CreateClassCache();
    }

    // Binary search in the class cache. First element of the pair is the key.
    auto comp = [](uint32_t value, const auto& it) { return value < it.first; };
    auto it = std::upper_bound(class_cache_.begin(), class_cache_.end(), dex_offset, comp);
    if (it != class_cache_.end()) {
      *class_def_index = it->second;
      return true;
    }
    return false;
  }

  // Binary search table with (end_dex_offset, class_def_index) entries.
  std::vector<std::pair<uint32_t, uint32_t>> class_cache_;

  // Used as short lived temporary when needed. Avoids alloc/free.
  std::string temporary_name_;
};

ExtDexFileError ExtDexFile_create(const void* addr,
                                  /*inout*/ size_t* size,
                                  const char* location,
                                  /*out*/ ExtDexFile** ext_dex_file) {
  if (*size < sizeof(art::DexFile::Header)) {
    *size = sizeof(art::DexFile::Header);
    return kExtDexFileError_notEnoughData;
  }

  const art::DexFile::Header* header = reinterpret_cast<const art::DexFile::Header*>(addr);
  uint32_t file_size = header->file_size_;
  if (art::CompactDexFile::IsMagicValid(header->magic_)) {
    // Compact dex files store the data section separately so that it can be shared.
    // Therefore we need to extend the read memory range to include it.
    // TODO: This might be wasteful as we might read data in between as well.
    //       In practice, this should be fine, as such sharing only happens on disk.
    uint32_t computed_file_size;
    if (__builtin_add_overflow(header->data_off_, header->data_size_, &computed_file_size)) {
      return kExtDexFileError_invalidHeader;
    }
    if (computed_file_size > file_size) {
      file_size = computed_file_size;
    }
  } else if (!art::StandardDexFile::IsMagicValid(header->magic_)) {
    return kExtDexFileError_invalidHeader;
  }

  if (*size < file_size) {
    *size = file_size;
    return kExtDexFileError_notEnoughData;
  }

  std::string loc_str(location);
  art::DexFileLoader loader;
  std::string error_msg;
  std::unique_ptr<const art::DexFile> dex_file = loader.Open(static_cast<const uint8_t*>(addr),
                                                             *size,
                                                             loc_str,
                                                             header->checksum_,
                                                             /*oat_dex_file=*/nullptr,
                                                             /*verify=*/false,
                                                             /*verify_checksum=*/false,
                                                             &error_msg);
  if (dex_file == nullptr) {
    LOG(ERROR) << "Can not opend dex file " << loc_str << ": " << error_msg;
    return kExtDexFileError_error;
  }

  *ext_dex_file = new ExtDexFile(std::move(dex_file));
  return kExtDexFileError_ok;
}

void ExtDexFile_getMethodInfoForOffset(ExtDexFile* self,
                                       uint32_t dex_offset,
                                       ExtDexFileMethodFlags flags,
                                       ExtDexFileMethodInfoCallback* method_info_cb,
                                       void* user_data) {
  const art::DexFile* dex_file = self->dex_file_.get();
  if (!dex_file->IsInDataSection(dex_file->Begin() + dex_offset)) {
    return;  // The DEX offset is not within the bytecode of this dex file.
  }

  if (dex_file->IsCompactDexFile()) {
    // The data section of compact dex files might be shared.
    // Check the subrange unique to this compact dex.
    const art::CompactDexFile::Header& cdex_header =
        dex_file->AsCompactDexFile()->GetHeader();
    uint32_t begin = cdex_header.data_off_ + cdex_header.OwnedDataBegin();
    uint32_t end = cdex_header.data_off_ + cdex_header.OwnedDataEnd();
    if (dex_offset < begin || dex_offset >= end) {
      return;  // The DEX offset is not within the bytecode of this dex file.
    }
  }

  uint32_t method_index, addr, size;
  if (!self->GetMethodDefIndex(dex_offset, &method_index, &addr, &size)) {
    return;
  }

  ExtDexFileMethodInfo info = self->NewMethodInfo(method_index, flags, addr, size);
  method_info_cb(user_data, &info);
}

void ExtDexFile_getAllMethodInfos(ExtDexFile* self,
                                  ExtDexFileMethodFlags flags,
                                  ExtDexFileMethodInfoCallback* method_info_cb,
                                  void* user_data) {
  for (art::ClassAccessor accessor : self->dex_file_->GetClasses()) {
    for (const art::ClassAccessor::Method& method : accessor.GetMethods()) {
      art::CodeItemInstructionAccessor code = method.GetInstructions();
      uint32_t addr = code.HasCodeItem()
          ? reinterpret_cast<const uint8_t*>(code.Insns()) - self->dex_file_->Begin()
          : 0u;
      ExtDexFileMethodInfo info = self->NewMethodInfo(
          method.GetIndex(), flags, addr, code.InsnsSizeInBytes());
      method_info_cb(user_data, &info);
    }
  }
}

void ExtDexFile_destroy(ExtDexFile* self) { delete (self); }

const char* ExtDexFileError_toString(ExtDexFileError self) {
  switch (self) {
    case kExtDexFileError_ok: return "Ok";
    case kExtDexFileError_error: return  "Error";
    case kExtDexFileError_notEnoughData: return  "NotEnoughData";
    case kExtDexFileError_invalidHeader: return  "InvalidHeader";
  }
  return nullptr;
}

}  // extern "C"
