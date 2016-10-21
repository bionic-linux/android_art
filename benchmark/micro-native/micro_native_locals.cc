/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "micro_native.h"

#include <stdio.h>

namespace art {

static void NativeMethods_localBaseline(JNIEnv*, jclass, jobject) {
}
static constexpr size_t kRepetitions = 100;
static void NativeMethods_addLocal(JNIEnv* env, jclass, jobject obj) {
  for (size_t i = 0; i != kRepetitions; ++i) {
    env->NewLocalRef(obj);
  }
  // Leak the local references, they will be freed automatically.
}
static void NativeMethods_addRemoveLocal(JNIEnv* env, jclass, jobject obj) {
  for (size_t i = 0; i != kRepetitions; ++i) {
    jobject local = env->NewLocalRef(obj);
    env->DeleteLocalRef(local);
  }
}
static void NativeMethods_addRemoveLocalSeq(JNIEnv* env, jclass, jobject obj) {
  jobject buf[kRepetitions];

  for (size_t i = 0; i != kRepetitions; ++i) {
    buf[i] = env->NewLocalRef(obj);
  }
  for (size_t i = 0; i != kRepetitions; ++i) {
    env->DeleteLocalRef(buf[i]);
  }
}
static void NativeMethods_addAddRemoveAddLocals(JNIEnv* env, jclass, jobject obj) {
  for (size_t i = 0; i != kRepetitions; ++i) {
    jobject to_delete = env->NewLocalRef(obj);
    env->NewLocalRef(obj);
    env->DeleteLocalRef(to_delete);
    env->NewLocalRef(obj);
  }
  // Leak the local references, they will be freed automatically.
}
static void NativeMethods_pushAndPopLocal(JNIEnv* env, jclass, jobject obj) {
  struct Recurse {
    static void recurse(JNIEnv* jenv, jobject jobj, size_t depth, size_t max_depth) {
      jenv->PushLocalFrame(10);
      if (depth != max_depth) {
        // Add 3 local references, delete the middle.
        jenv->NewLocalRef(jobj);
        jobject to_delete = jenv->NewLocalRef(jobj);
        jenv->NewLocalRef(jobj);
        jenv->DeleteLocalRef(to_delete);

        // Recurse.
        recurse(jenv, jobj, depth + 1, max_depth);

        // Add two references, delete one.
        jobject to_delete2 = jenv->NewLocalRef(jobj);
        jenv->NewLocalRef(jobj);
        jenv->DeleteLocalRef(to_delete2);
      }
      jenv->PopLocalFrame(nullptr);
    }
  };

  constexpr size_t kMaxDepth = 5;
  for (size_t i = 0; i != (kRepetitions / kMaxDepth); ++i) {
    Recurse::recurse(env, obj, 0, kMaxDepth);
  }
}

static JNINativeMethod gMethods_Locals[] = {
  NATIVE_METHOD(NativeMethods, localBaseline, "(Ljava/lang/Object;)V"),
  NATIVE_METHOD(NativeMethods, addLocal, "(Ljava/lang/Object;)V"),
  NATIVE_METHOD(NativeMethods, addRemoveLocal, "(Ljava/lang/Object;)V"),
  NATIVE_METHOD(NativeMethods, addRemoveLocalSeq, "(Ljava/lang/Object;)V"),
  NATIVE_METHOD(NativeMethods, addAddRemoveAddLocals, "(Ljava/lang/Object;)V"),
  NATIVE_METHOD(NativeMethods, pushAndPopLocal, "(Ljava/lang/Object;)V"),
};

void register_micro_native_locals_methods(JNIEnv* env) {
  jniRegisterNativeMethodsHelper(env, kClassName, gMethods_Locals, NELEM(gMethods_Locals));
}

}  // namespace art
