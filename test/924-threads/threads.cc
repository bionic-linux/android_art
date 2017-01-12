/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "android-base/stringprintf.h"
#include "base/macros.h"
#include "base/logging.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"
#include "ScopedLocalRef.h"

#include "ti-agent/common_helper.h"
#include "ti-agent/common_load.h"

namespace art {
namespace Test924Threads {

// private static native Thread getCurrentThread();
// private static native Object[] getThreadInfo(Thread t);

extern "C" JNIEXPORT jthread JNICALL Java_Main_getCurrentThread(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED) {
  jthread thread = nullptr;
  jvmtiError result = jvmti_env->GetCurrentThread(&thread);
  if (JvmtiErrorToException(env, result)) {
    return nullptr;
  }
  return thread;
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_getThreadInfo(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jthread thread) {
  jvmtiThreadInfo info;
  memset(&info, 0, sizeof(jvmtiThreadInfo));

  jvmtiError result = jvmti_env->GetThreadInfo(thread, &info);
  if (JvmtiErrorToException(env, result)) {
    return nullptr;
  }

  auto callback = [&](jint component_index) -> jobject {
    switch (component_index) {
      // The name.
      case 0:
        return (info.name == nullptr) ? nullptr : env->NewStringUTF(info.name);

      // The priority. Use a string for simplicity of construction.
      case 1:
        return env->NewStringUTF(android::base::StringPrintf("%d", info.priority).c_str());

      // Whether it's a daemon. Use a string for simplicity of construction.
      case 2:
        return env->NewStringUTF(info.is_daemon == JNI_TRUE ? "true" : "false");

      // The thread group;
      case 3:
        return env->NewLocalRef(info.thread_group);

      // The context classloader.
      case 4:
        return env->NewLocalRef(info.context_class_loader);
    }
    LOG(FATAL) << "Should not reach here";
    UNREACHABLE();
  };
  jobjectArray ret = CreateObjectArray(env, 5, "java/lang/Object", callback);

  jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(info.name));
  if (info.thread_group != nullptr) {
    env->DeleteLocalRef(info.thread_group);
  }
  if (info.context_class_loader != nullptr) {
    env->DeleteLocalRef(info.context_class_loader);
  }

  return ret;
}

extern "C" JNIEXPORT jint JNICALL Java_Main_getThreadState(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jthread thread) {
  jint state;
  jvmtiError result = jvmti_env->GetThreadState(thread, &state);
  if (JvmtiErrorToException(env, result)) {
    return 0;
  }
  return state;
}

}  // namespace Test924Threads
}  // namespace art
