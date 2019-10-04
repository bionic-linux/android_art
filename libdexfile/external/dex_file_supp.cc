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

#include "art_api/dex_file_support.h"

#include <dlfcn.h>
#include <mutex>

#ifndef STATIC_LIB
// Not used in the static lib, so avoid a dependency on this header in
// libdexfile_support_static.
#include <log/log.h>
#endif

namespace art_api {
namespace dex {

#ifdef STATIC_LIB
#define DEFINE_DLFUNC_PTR(CLASS, DLFUNC) decltype(DLFUNC)* CLASS::g_##DLFUNC = DLFUNC
#else
#define DEFINE_DLFUNC_PTR(CLASS, DLFUNC) decltype(DLFUNC)* CLASS::g_##DLFUNC = nullptr
#endif

DEFINE_DLFUNC_PTR(DexString, ExtDexFileMakeString);
DEFINE_DLFUNC_PTR(DexString, ExtDexFileGetString);
DEFINE_DLFUNC_PTR(DexString, ExtDexFileFreeString);
DEFINE_DLFUNC_PTR(DexFile, ExtDexFileOpenFromMemory);
DEFINE_DLFUNC_PTR(DexFile, ExtDexFileOpenFromFd);
DEFINE_DLFUNC_PTR(DexFile, ExtDexFileGetMethodInfoForOffset);
DEFINE_DLFUNC_PTR(DexFile, ExtDexFileGetAllMethodInfos);
DEFINE_DLFUNC_PTR(DexFile, ExtDexFileFree);

#undef DEFINE_DLFUNC_PTR

bool TryLoadLibdexfileExternal(std::string* err_msg) {
#if defined(STATIC_LIB)
  // Nothing to do here since all function pointers are initialised statically.
  return true;
#elif defined(NO_DEXFILE_SUPPORT)
  *err_msg = "Dex file support not available.";
  return false;
#else
  // Use a plain old mutex since we want to try again if loading fails (to set
  // err_msg, if nothing else).
  static std::mutex load_mutex;
  static bool is_loaded = false;
  std::lock_guard<std::mutex> lock(load_mutex);

  if (!is_loaded) {
    // Check which version is already loaded to avoid loading both debug and
    // release builds. We might also be backtracing from separate process, in
    // which case neither is loaded.
    const char* so_name = "libdexfiled_external.so";
    void* handle = dlopen(so_name, RTLD_NOLOAD | RTLD_NOW | RTLD_NODELETE);
    if (handle == nullptr) {
      so_name = "libdexfile_external.so";
      handle = dlopen(so_name, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
    }
    if (handle == nullptr) {
      *err_msg = std::string("Failed to load ") + so_name + ": " + dlerror();
      return false;
    }

#define SET_DLFUNC_PTR(CLASS, DLFUNC) \
    do { \
      CLASS::g_##DLFUNC = reinterpret_cast<decltype(DLFUNC)*>(dlsym(handle, #DLFUNC)); \
      if (CLASS::g_##DLFUNC == nullptr) { \
        *err_msg = std::string("Failed to find ") + #DLFUNC + \
                   " in " + so_name + ": " + dlerror(); \
        return false; \
      } \
    } while (0)

    SET_DLFUNC_PTR(DexString, ExtDexFileMakeString);
    SET_DLFUNC_PTR(DexString, ExtDexFileGetString);
    SET_DLFUNC_PTR(DexString, ExtDexFileFreeString);
    SET_DLFUNC_PTR(DexFile, ExtDexFileOpenFromMemory);
    SET_DLFUNC_PTR(DexFile, ExtDexFileOpenFromFd);
    SET_DLFUNC_PTR(DexFile, ExtDexFileGetMethodInfoForOffset);
    SET_DLFUNC_PTR(DexFile, ExtDexFileGetAllMethodInfos);
    SET_DLFUNC_PTR(DexFile, ExtDexFileFree);

#undef SET_DLFUNC_PTR
    is_loaded = true;
  }

  return is_loaded;
#endif  // !defined(NO_DEXFILE_SUPPORT) && !defined(STATIC_LIB)
}

void LoadLibdexfileExternal() {
  if (std::string err_msg; !TryLoadLibdexfileExternal(&err_msg)) {
    LOG_FATAL(err_msg);
  }
}

DexFile::~DexFile() { g_ExtDexFileFree(ext_dex_file_); }

MethodInfo DexFile::AbsorbMethodInfo(const ExtDexFileMethodInfo& ext_method_info) {
  return {ext_method_info.offset, ext_method_info.len, DexString(ext_method_info.name)};
}

void DexFile::AddMethodInfoCallback(const ExtDexFileMethodInfo* ext_method_info, void* ctx) {
  auto vect = static_cast<MethodInfoVector*>(ctx);
  vect->emplace_back(AbsorbMethodInfo(*ext_method_info));
}

}  // namespace dex
}  // namespace art_api
