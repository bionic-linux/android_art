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

#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// This is the stable C ABI that backs art_api::dex below. Structs and functions
// may only be added here. C++ users should use dex_file_support.h instead.

struct ExtDexFile;

// Try to open a dex file in the given memory range.
// If the memory range is too small, larger suggest size is written to the argument.
int ExtDexFileOpenFromMemory(const void* addr,
                             /*inout*/ size_t* size,
                             const char* location,
                             /*out*/ struct ExtDexFile** ext_dex_file);

// Callback used to return information about a dex method.
typedef void ExtDexFileMethodInfoCallback(void* user_data,
                                          char* name,
                                          size_t len,      // strlen of name.
                                          uint32_t addr,   // dex offset.
                                          uint32_t size);  // dex code size.

// Find a single dex method based on the given dex offset.
int ExtDexFileGetMethodInfoForOffset(struct ExtDexFile* ext_dex_file,
                                     uint32_t dex_offset,
                                     int with_signature,
                                     ExtDexFileMethodInfoCallback* method_info_cb,
                                     void* user_data);

// Return all dex methods in the dex file.
void ExtDexFileGetAllMethodInfos(struct ExtDexFile* ext_dex_file,
                                 int with_signature,
                                 ExtDexFileMethodInfoCallback* method_info_cb,
                                 void* user_data);

// Release all associated memory.
void ExtDexFileClose(struct ExtDexFile* ext_dex_file);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_EXTERNAL_H_
