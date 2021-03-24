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

#ifndef ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_EXTERNAL_H_
#define ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_EXTERNAL_H_

// Dex file external API
#include <stdint.h>
#include <sys/cdefs.h>
#include <sys/types.h>

__BEGIN_DECLS

// This is the stable C ABI that backs art_api::dex below. Structs and functions
// may only be added here. C++ users should use dex_file_support.h instead.

struct ExtDexFile;
typedef struct ExtDexFile ExtDexFile;

struct ADexFile_MethodInfo {
  size_t sizeof_struct;  // Size of this structure (to allow future extensions).
  uint32_t addr;  // Start of dex byte-code relative to the start of the dex file.
  uint32_t size;  // Size of the dex byte-code in bytes.
  const char* _Nonnull class_descriptor;  // Mangled class name.
  const char* _Nonnull name;
  size_t name_size;
};
typedef struct ADexFile_MethodInfo ADexFile_MethodInfo;

enum ADexFile_Error : uint32_t {
  ADEXFILE_ERROR_OK = 0,
  ADEXFILE_ERROR_ERROR = 1,  // Unspecified error.
  ADEXFILE_ERROR_NOTENOUGHDATA = 2,
  ADEXFILE_ERROR_INVALIDHEADER = 3,
};
typedef enum ADexFile_Error ADexFile_Error;

enum ADexFile_Flags : uint32_t {
  ADEXFILE_FLAGS_NONE = 0,
  ADEXFILE_FLAGS_NAMEONLY = 1 << 0,            // E.g. Main
  ADEXFILE_FLAGS_NAMEWITHCLASS = 1 << 1,       // E.g. MyClass.Main
  ADEXFILE_FLAGS_NAMEWITHPARAMETERS = 1 << 2,  // E.g. MyClass.Main(String[])
};
typedef enum ADexFile_Flags ADexFile_Flags;

// Callback used to return information about a dex method.
typedef void ADexFile_MethodInfoCallback(void* _Nullable user_data,
                                         ADexFile_MethodInfo* _Nonnull method_info);

// Try to open a dex file in the given memory range.
// If the memory range is too small, larger suggested size is written to the argument.
ADexFile_Error ADexFile_Create(const void* _Nonnull addr,
                               /*inout*/ size_t* _Nonnull size,
                               const char* _Nonnull location,
                               /*out*/ ExtDexFile* _Nullable * _Nonnull ext_dex_file);

// Find a single dex method based on the given dex offset.
// If the method is not found, the callback is not called.
// Not thread-safe.  Returns number of found methods (0 or 1).
size_t ADexFile_FindMethodAtOffset(ExtDexFile* _Nonnull self,
                                   uint32_t dex_offset,
                                   ADexFile_Flags flags,
                                   ADexFile_MethodInfoCallback* _Nullable method_info_cb,
                                   void* _Nullable user_data);

// Return all dex methods in the dex file.
// Not thread-safe.  Returns number of methods.
size_t ADexFile_ForEachMethod(ExtDexFile* _Nonnull self,
                              ADexFile_Flags flags,
                              ADexFile_MethodInfoCallback* _Nullable method_info_cb,
                              void* _Nullable user_data);

// Release all associated memory.
void ADexFile_Destroy(ExtDexFile* _Nonnull self);

const char* _Nullable ADexFile_Error_ToString(ADexFile_Error self);

__END_DECLS

#endif  // ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_EXTERNAL_H_
