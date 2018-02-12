/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "jni.h"

#include <iostream>

#include "art_method-inl.h"
#include "jit/jit.h"
#include "mirror/method.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"

namespace art {
namespace {

extern "C" JNIEXPORT void JNICALL Java_IntHolder_waitForCompilation(JNIEnv*, jclass) {
  jit::Jit* jit = Runtime::Current()->GetJit();
  if (jit != nullptr) {
    jit->WaitForCompilationToFinish(Thread::Current());
  }
}

extern "C" JNIEXPORT jboolean JNICALL Java_Main_isCopiedMethod(JNIEnv*, jclass, jobject obj) {
  CHECK(obj != nullptr);
  ScopedObjectAccess soa(Thread::Current());
  ObjPtr<mirror::Method> method = soa.Decode<mirror::Method>(obj);
  return method->GetArtMethod()->IsCopied() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jobject JNICALL Java_Main_returnCopiedMethod(JNIEnv*, jclass, jclass cls) {
  CHECK(cls != nullptr);
  ScopedObjectAccess soa(Thread::Current());
  ObjPtr<mirror::Class> klass = soa.Decode<mirror::Class>(cls);
  ArraySlice<ArtMethod> copied_methods = klass->GetCopiedMethods(kRuntimePointerSize);
  CHECK_EQ(copied_methods.size(), 1u);
  return soa.AddLocalReference<jobject>(
      mirror::Method::CreateFromArtMethod<kRuntimePointerSize,
                                          /*kTransactionActive*/ false>(soa.Self(),
                                                                        &copied_methods[0]));
}


}  // namespace
}  // namespace art
