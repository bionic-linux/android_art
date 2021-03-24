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
  const char* class_descriptor;  // Mangled class name.
  const char* name;
  size_t name_size;
};
typedef struct ExtDexFileMethodInfo ExtDexFileMethodInfo;

enum ExtDexFileError : uint32_t {
  kExtDexFileError_ok = 0,
  kExtDexFileError_error = 1,  // Unspecified error.
  kExtDexFileError_notEnoughData = 2,
  kExtDexFileError_invalidHeader = 3,
};
typedef enum ExtDexFileError ExtDexFileError;

enum ExtDexFileMethodFlags : uint32_t {
  kExtDexFileFlags_none = 0,
  kExtDexFileFlags_nameOnly = 1 << 0,            // E.g. Main
  kExtDexFileFlags_nameWithClass = 1 << 1,       // E.g. MyClass.Main
  kExtDexFileFlags_nameWithParameters = 1 << 2,  // E.g. MyClass.Main(String[])
};
typedef enum ExtDexFileMethodFlags ExtDexFileMethodFlags;

// Callback used to return information about a dex method.
typedef void ExtDexFileMethodInfoCallback(void* user_data,
                                          ExtDexFileMethodInfo* method_info);

// Try to open a dex file in the given memory range.
// If the memory range is too small, larger suggest size is written to the argument.
ExtDexFileError ExtDexFile_create(const void* addr,
                                  /*inout*/ size_t* size,
                                  const char* location,
                                  /*out*/ ExtDexFile** ext_dex_file);

// Find a single dex method based on the given dex offset.
// Not thread-safe.
void ExtDexFile_getMethodInfoForOffset(ExtDexFile* self,
                                       uint32_t dex_offset,
                                       ExtDexFileMethodFlags flags,
                                       ExtDexFileMethodInfoCallback* method_info_cb,
                                       void* user_data);

// Return all dex methods in the dex file.
// Not thread-safe.
void ExtDexFile_getAllMethodInfos(ExtDexFile* self,
                                  ExtDexFileMethodFlags flags,
                                  ExtDexFileMethodInfoCallback* method_info_cb,
                                  void* user_data);

// Release all associated memory.
void ExtDexFile_destroy(ExtDexFile* self);

const char* ExtDexFileError_toString(ExtDexFileError self);

__END_DECLS

#endif  // ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_EXTERNAL_H_
