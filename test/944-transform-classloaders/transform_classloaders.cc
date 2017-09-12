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

#include "art_field-inl.h"
#include "native/dalvik_system_DexFile.h"
#include "well_known_classes.h"

namespace art {

extern "C" JNIEXPORT jboolean JNICALL Java_art_Test944_dexFileContainsPtr(JNIEnv*, jclass, jlong cookie_addr, jlong ptr) {
  DexFile* target = reinterpret_cast<DexFile*>(static_cast<uintptr_t>(ptr));
  DexFileCookie* cookie = DexFileCookieFromAddr(cookie_addr);
  for (auto& dex_file : cookie->dex_files) {
    if (dex_file.get() == target) {
      return JNI_TRUE;
    }
  }
  return JNI_FALSE;
}

}  // namespace art
