/*
 * Copyright (C) 2014 The Android Open Source Project
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

#ifndef ART_RUNTIME_NATIVE_DALVIK_SYSTEM_DEXFILE_H_
#define ART_RUNTIME_NATIVE_DALVIK_SYSTEM_DEXFILE_H_

#include <jni.h>
#include <unistd.h>
#include <vector>

namespace art {

class DexFile;
class OatFile;

// DexFileCookie is a handle dalvik.system.DexFile uses to access its native
// oat and dex files. A new DexFileCookie is allocated when a
// dalvik.system.DexFile object is constructed, and deleted via
// NativeAllocationRegistry after the dalvik.system.DexFile is garbage
// collected.
struct DexFileCookie {
  // The oat file associated with the dex location. May be null if an oat file
  // is not available. The oat_file will be unregistered and freed when
  // the DexFileCookie is freed.
  const OatFile* oat_file;

  // The dex files associated with the dex location.
  // These dex files are backed by oat_file if it is available.
  std::vector<std::unique_ptr<const DexFile>> dex_files;
};

DexFileCookie* DexFileCookieFromAddr(jlong addr);
jlong DexFileCookieToAddr(DexFileCookie* cookie);

void register_dalvik_system_DexFile(JNIEnv* env);

}  // namespace art

#endif  // ART_RUNTIME_NATIVE_DALVIK_SYSTEM_DEXFILE_H_
