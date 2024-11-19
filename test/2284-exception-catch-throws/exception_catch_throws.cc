/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "android-base/logging.h"
#include "android-base/stringprintf.h"

#include "jni.h"
#include "jvmti.h"
#include "scoped_local_ref.h"

// Test infrastructure
#include "jni_binder.h"
#include "jni_helper.h"
#include "jvmti_helper.h"
#include "test_env.h"
#include "ti_macros.h"

namespace art {
namespace Test2284 {

extern "C" JNIEXPORT void JNICALL Java_art_Test2284_exceptionCatchHandlerNative(JNIEnv* env,
                                                                 [[maybe_unused]] jclass klass,
                                                                 jobject, jobject, jlong, jobject) {
  jclass klass1 = env->FindClass("art/Test2284");
  jmethodID method_id = env->GetStaticMethodID(klass1, "enableMethodExit", "()V");
  env->CallStaticVoidMethod(klass1, method_id);
  jclass exception_cls = env->FindClass("java/lang/Error");
  env->ThrowNew(exception_cls, "Error");
}


}  // namespace Test2284
}  // namespace art

