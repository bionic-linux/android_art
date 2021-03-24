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

struct ExtDexFileMethodInfo {
  size_t sizeof_struct;  // Size of this structure (to allow future extensions).
  uint32_t addr;  // Start of dex byte-code relative to the start of the dex file.
  uint32_t size;  // Size of the dex byte-code in bytes.
  const char* _Nonnull class_descriptor;  // Mangled class name.
  const char* _Nonnull name;
  size_t name_size;
};
typedef struct ExtDexFileMethodInfo ExtDexFileMethodInfo;

enum ExtDexFileError : uint32_t {
  ExtDexFileError_Ok = 0,
  ExtDexFileError_Error = 1,  // Unspecified error.
  ExtDexFileError_NotEnoughData = 2,
  ExtDexFileError_InvalidHeader = 3,
};
typedef enum ExtDexFileError ExtDexFileError;

enum ExtDexFileFlags : uint32_t {
  ExtDexFileFlags_None = 0,
  ExtDexFileFlags_NameOnly = 1 << 0,            // E.g. Main
  ExtDexFileFlags_NameWithClass = 1 << 1,       // E.g. MyClass.Main
  ExtDexFileFlags_NameWithParameters = 1 << 2,  // E.g. MyClass.Main(String[])
};
typedef enum ExtDexFileFlags ExtDexFileFlags;

// Callback used to return information about a dex method.
typedef void ExtDexFile_MethodInfoCallback(void* _Nullable user_data,
                                           ExtDexFileMethodInfo* _Nonnull method_info);

// Try to open a dex file in the given memory range.
// If the memory range is too small, larger suggested size is written to the argument.
ExtDexFileError ExtDexFile_Create(const void* _Nonnull addr,
                                  /*inout*/ size_t* _Nonnull size,
                                  const char* _Nonnull location,
                                  /*out*/ ExtDexFile* _Nullable * _Nonnull ext_dex_file);

// Find a single dex method based on the given dex offset.
// If the method is not found, the callback is not called.
// Not thread-safe.  Returns number of found methods (0 or 1).
size_t ExtDexFile_FindMethodAtOffset(ExtDexFile* _Nonnull self,
                                     uint32_t dex_offset,
                                     ExtDexFileFlags flags,
                                     ExtDexFile_MethodInfoCallback* _Nullable method_info_cb,
                                     void* _Nullable user_data);

// Return all dex methods in the dex file.
// Not thread-safe.  Returns number of methods.
size_t ExtDexFile_ForEachMethod(ExtDexFile* _Nonnull self,
                                ExtDexFileFlags flags,
                                ExtDexFile_MethodInfoCallback* _Nullable method_info_cb,
                                void* _Nullable user_data);

// Release all associated memory.
void ExtDexFile_Destroy(ExtDexFile* _Nonnull self);

const char* _Nullable ExtDexFileError_ToString(ExtDexFileError self);

__END_DECLS

#endif  // ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_EXTERNAL_H_
