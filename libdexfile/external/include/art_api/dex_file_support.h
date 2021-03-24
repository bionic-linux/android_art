/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_SUPPORT_H_
#define ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_SUPPORT_H_

// C++ wrapper for the dex file external API.

#include <memory>
#include <string>

#include <android-base/macros.h>

#include "art_api/dex_file_external.h"

namespace art_api {
namespace dex {

#define FOR_ALL_DLFUNCS(MACRO) \
  MACRO(ADexFile_Error_toString) \
  MACRO(ADexFile_Method_getClassDescriptor) \
  MACRO(ADexFile_Method_getCodeOffset) \
  MACRO(ADexFile_Method_getName) \
  MACRO(ADexFile_Method_getQualifiedName) \
  MACRO(ADexFile_create) \
  MACRO(ADexFile_destroy) \
  MACRO(ADexFile_findMethodAtOffset) \
  MACRO(ADexFile_forEachMethod) \

#define DEFINE_DLFUNC_PTR(DLFUNC) extern decltype(DLFUNC)* g_##DLFUNC;
FOR_ALL_DLFUNCS(DEFINE_DLFUNC_PTR)
#undef DEFINE_DLFUNC_PTR

// Returns true if libdexfile.so is already loaded. Otherwise tries to
// load it and returns true if successful. Otherwise returns false and sets
// *error_msg. If false is returned then calling any function below may abort
// the process. Thread safe.
bool TryLoadLibdexfileExternal(std::string* error_msg);

// API for reading ordinary dex files and CompactDex files.
// It is minimal 1:1 C++ wrapper around the C ABI.
// See documentation in dex_file_external.h
struct DexFile {
 public:
  struct Method {
    size_t GetCodeOffset(size_t* out_size = nullptr) const {
      return g_ADexFile_Method_getCodeOffset(self, out_size);
    }

    const char* GetName(size_t* out_size = nullptr) const {
      return g_ADexFile_Method_getName(self, out_size);
    }

    const char* GetQualifiedName(bool with_params = false, size_t* out_size = nullptr) const {
      return g_ADexFile_Method_getQualifiedName(self, with_params, out_size);
    }

    const char* GetClassDescriptor(size_t* out_size = nullptr) const {
      return g_ADexFile_Method_getClassDescriptor(self, out_size);
    }

    const ADexFile_Method* self;
  };

  struct Error {
    const char* ToString() const {
      return g_ADexFile_Error_toString(code);
    }

    operator bool() const {
      return code == ADEXFILE_ERROR_OK;
    }

    ADexFile_Error code;
  };

  static Error Create(const void* address,
                      size_t size,
                      size_t* new_size,
                      const char* location,
                      /*out*/ std::unique_ptr<DexFile>* out_dex_file) {
    std::string err;
    if (!TryLoadLibdexfileExternal(&err)) {
      return Error{ADEXFILE_ERROR_MISSING_LIBRARY};
    }
    ADexFile* adex = nullptr;
    ADexFile_Error error = g_ADexFile_create(address, size, new_size, location, &adex);
    if (adex != nullptr) {
      *out_dex_file = std::unique_ptr<DexFile>(new DexFile{adex});
    }
    return Error{error};
  }

  template<typename T /* lambda which takes (const DexFile::Method&) as argument */>
  inline size_t FindMethodAtOffset(uint32_t dex_offset, T callback) {
    auto cb = [](void* ctx, const ADexFile_Method* m) { (*reinterpret_cast<T*>(ctx))(Method{m}); };
    return g_ADexFile_findMethodAtOffset(self, dex_offset, cb, &callback);
  }

  template<typename T /* lambda which takes (const DexFile::Method&) as argument */>
  inline size_t ForEachMethod(T callback) {
    auto cb = [](void* ctx, const ADexFile_Method* m) { (*reinterpret_cast<T*>(ctx))(Method{m}); };
    return g_ADexFile_forEachMethod(self, cb, &callback);
  }

  ~DexFile() {
    g_ADexFile_destroy(self);
  };

  ADexFile* self = nullptr;

  DISALLOW_COPY_AND_ASSIGN(DexFile);
};

}  // namespace dex
}  // namespace art_api

#endif  // ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_SUPPORT_H_
