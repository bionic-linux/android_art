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

struct ADexFile;
typedef struct ADexFile ADexFile;

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
  ADEXFILE_FLAGS_NAMEWITHPARAMETERS = 1 << 2,  // E.g. void MyClass.Main(String[])
};
typedef enum ADexFile_Flags ADexFile_Flags;

// Callback used to return information about a dex method.
typedef void ADexFile_MethodInfoCallback(void* _Nullable user_data,
                                         ADexFile_MethodInfo* _Nonnull method_info);

// Interprets a chunk of memory as a dex file.
//
// @param address Pointer to the start of dex file data.
//                The caller must retain the memory.
// @param size Size of the memory range. If the size is too small, the method returns
//             ADEXFILE_ERROR_NOTENOUGHDATA and sets size to a new size to try again with.
// @param location A string that describes the dex file. Preferably its path.
//                 It is mostly used just for log messages and may be "".
// @param dex_file The created dex file object, or nullptr on error.
//                 It must be later freed with ADexFile_Destroy.
//
// @return ADEXFILE_ERROR_OK if successful.
// @return ADEXFILE_ERROR_NOTENOUGHDATA if the provided dex file is too short (truncated).
// @return ADEXFILE_ERROR_INVALIDHEADER if the memory does not seem to represent DEX file.
// @return ADEXFILE_ERROR_ERROR if any other non-specific error occurs.
//
// Thread-safe (creates new object).
ADexFile_Error ADexFile_Create(const void* _Nonnull address,
                               /*inout*/ size_t* _Nonnull size,
                               const char* _Nonnull location,
                               /*out*/ ADexFile* _Nullable * _Nonnull dex_file);

// Find method at given offset and call callback with information about the method.
//
// @param dex_offset Offset relative to the start of the dex file header.
// @param flags Specifies which information should be obtained.
// @param callback The callback to call. Returned pointers are valid only in the callback.
// @param user_data Extra user-specified argument for the callback.
//
// @return Number of methods found (0 or 1).
//
// Not thread-safe for calls on the same ADexFile instance.
size_t ADexFile_FindMethodAtOffset(ADexFile* _Nonnull self,
                                   uint32_t dex_offset,
                                   ADexFile_Flags flags,
                                   ADexFile_MethodInfoCallback* _Nullable callback,
                                   void* _Nullable user_data);

// Call callback for all methods in the DEX file.
//
// @param flags Specifies which information should be obtained.
// @param callback The callback to call. Returned pointers are valid only in the callback.
// @param user_data Extra user-specified argument for the callback.
//
// @return Number of methods found.
//
// Not thread-safe for calls on the same ADexFile instance.
size_t ADexFile_ForEachMethod(ADexFile* _Nonnull self,
                              ADexFile_Flags flags,
                              ADexFile_MethodInfoCallback* _Nullable method_info_cb,
                              void* _Nullable user_data);

// Free the given object.
//
// Thread-safe, can be called only once for given instance.
void ADexFile_Destroy(ADexFile* _Nullable self);

// @return Compile-time literal or nullptr on error.
const char* _Nullable ADexFile_Error_ToString(ADexFile_Error self);

__END_DECLS

#endif  // ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_EXTERNAL_H_
