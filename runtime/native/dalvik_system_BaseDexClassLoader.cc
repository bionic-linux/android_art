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

#include "dalvik_system_BaseDexClassLoader.h"

#include "class_linker.h"
#include "dex/descriptors_names.h"
#include "dex/utf.h"
#include "handle.h"
#include "handle_scope-inl.h"
#include "mirror/class_loader.h"
#include "obj_ptr-inl.h"
#include "native_util.h"
#include "nativehelper/jni_macros.h"
#include "nativehelper/scoped_utf_chars.h"
#include "scoped_fast_native_object_access-inl.h"

namespace art {

static jclass BaseDexClassLoader_findClassNative(JNIEnv* env,
                                                 jobject javaLoader,
                                                 jstring javaName) {
  ScopedFastNativeObjectAccess soa(env);
  ScopedUtfChars name(env, javaName);
  if (name.c_str() == nullptr) {
    return nullptr;
  }

  if (!IsValidBinaryClassName(name.c_str())) {
    return nullptr;
  }

  std::string descriptor(DotToDescriptor(name.c_str()));
  const size_t hash = ComputeModifiedUtf8Hash(descriptor.c_str());
  StackHandleScope<2> hs(soa.Self());
  Handle<mirror::ClassLoader> class_loader(
      hs.NewHandle(soa.Decode<mirror::ClassLoader>(javaLoader)));
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  ObjPtr<mirror::Class> result_ptr;
  bool known_hierarchy = class_linker->FindClassInBaseDexClassLoader(
      soa, soa.Self(), descriptor.c_str(), hash, class_loader, &result_ptr);
  if (result_ptr != nullptr) {
    return soa.AddLocalReference<jclass>(result_ptr);
  }

  if (known_hierarchy) {
    // If the hierarchy was known, but the class not found, throw
    // the exception if the fast path is enabled.
    class_linker->MaybeThrowFastClassNotFoundException(soa.Self(), name.c_str());
  }
  return nullptr;
}

static JNINativeMethod gMethods[] = {
  FAST_NATIVE_METHOD(BaseDexClassLoader, findClassNative, "(Ljava/lang/String;)Ljava/lang/Class;"),
};

void register_dalvik_system_BaseDexClassLoader(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("dalvik/system/BaseDexClassLoader");
}

}  // namespace art
