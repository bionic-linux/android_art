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

#include <stdio.h>
#include <jni.h>

#ifndef NATIVE_METHOD
#define NATIVE_METHOD(className, functionName, signature) \
    { #functionName, signature, reinterpret_cast<void*>(className ## _ ## functionName) }
#endif
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

#define CLASS_NAME "benchmarks/MicroNative/java/NativeMethods"

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

  for (size_t i = 0; i != (kRepetitions / 5); ++i) {
    Recurse::recurse(env, obj, 0, 5);
  }
}

static JNINativeMethod gMethods_Locals[] = {
  NATIVE_METHOD(NativeMethods, localBaseline, "(Ljava/lang/Object;)V"),
  NATIVE_METHOD(NativeMethods, addLocal, "(Ljava/lang/Object;)V"),
  NATIVE_METHOD(NativeMethods, addRemoveLocal, "(Ljava/lang/Object;)V"),
  NATIVE_METHOD(NativeMethods, addAddRemoveAddLocals, "(Ljava/lang/Object;)V"),
  NATIVE_METHOD(NativeMethods, pushAndPopLocal, "(Ljava/lang/Object;)V"),
};

static void jniRegisterNativeMethods(JNIEnv* env,
                                     const char* className,
                                     const JNINativeMethod* methods,
                                     int numMethods) {
    jclass c = env->FindClass(className);
    if (c == nullptr) {
        char* tmp;
        const char* msg;
        if (asprintf(&tmp,
                     "Native registration unable to find class '%s'; aborting...",
                     className) == -1) {
            // Allocation failed, print default warning.
            msg = "Native registration unable to find class; aborting...";
        } else {
            msg = tmp;
        }
        env->FatalError(msg);
    }

    if (env->RegisterNatives(c, methods, numMethods) < 0) {
        char* tmp;
        const char* msg;
        if (asprintf(&tmp, "RegisterNatives failed for '%s'; aborting...", className) == -1) {
            // Allocation failed, print default warning.
            msg = "RegisterNatives failed; aborting...";
        } else {
            msg = tmp;
        }
        env->FatalError(msg);
    }
}

void register_micro_native_locals_methods(JNIEnv* env) {
  jniRegisterNativeMethods(env, CLASS_NAME, gMethods_Locals, NELEM(gMethods_Locals));
}
