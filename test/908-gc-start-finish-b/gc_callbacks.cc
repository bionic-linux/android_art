/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include "gc_callbacks.h"

#include <stdio.h>
#include <string.h>
#include <vector>

#include "android-base/macros.h"

#include "jni.h"
#include "jvmti.h"

// Test infrastructure
#include "jvmti_helper.h"
#include "test_env.h"

namespace art {
namespace Test908GcStartFinishB {

static int32_t tag_incre = 1;
static std::vector<jobject> jweaks;

static size_t starts = 0;
static size_t finishes = 0;

JavaVM* jvm;

static void printTags(jvmtiEnv* ti_env) {
  JNIEnv* jni;
  jint result = jvm->GetEnv((void**)&jni, JNI_VERSION_1_6);
  if (result == JNI_EDETACHED) {
    jvm->AttachCurrentThread(&jni, nullptr);
  }

  int i = 0;
  for (const auto& value : jweaks) {
    if (!jni->IsSameObject(value, NULL)) {
      jlong tag = 0;
      ti_env->GetTag(value, &tag);
      if (tag == 0) {
        printf("%d - %d, ", i, (int32_t)tag);
      }
    }
    i++;
  }
}

static void JNICALL GarbageCollectionFinish(jvmtiEnv* ti_env) {
  printf("GCFinish:\n");
  printTags(ti_env);
  printf("\n");
  finishes++;
}

static void JNICALL GarbageCollectionStart(jvmtiEnv* ti_env) {
  printf("GCStart:\n");
  printTags(ti_env);
  printf("\n");
  starts++;
}

static void JNICALL ObjectAllocCallback(jvmtiEnv* ti_env,
                                        JNIEnv* jni,
                                        jthread thread ATTRIBUTE_UNUSED,
                                        jobject object,
                                        jclass klass ATTRIBUTE_UNUSED,
                                        jlong size ATTRIBUTE_UNUSED) {
  ti_env->SetTag(object, tag_incre++);
  jweaks.push_back(jni->NewWeakGlobalRef(object));
}

extern "C" JNIEXPORT void JNICALL Java_art_Test908B_setupGcCallback(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED) {
  jvmtiEventCallbacks callbacks;
  memset(&callbacks, 0, sizeof(jvmtiEventCallbacks));
  callbacks.GarbageCollectionFinish = GarbageCollectionFinish;
  callbacks.GarbageCollectionStart = GarbageCollectionStart;
  callbacks.VMObjectAlloc = &ObjectAllocCallback;

  jvmtiError ret = jvmti_env->SetEventCallbacks(&callbacks, sizeof(callbacks));
  JvmtiErrorToException(env, jvmti_env, ret);
}

extern "C" JNIEXPORT void JNICALL Java_art_Test908B_enableGcTracking(JNIEnv* env,
                                                                    jclass klass ATTRIBUTE_UNUSED,
                                                                    jboolean enable) {
  jvmtiError ret = jvmti_env->SetEventNotificationMode(
      enable ? JVMTI_ENABLE : JVMTI_DISABLE,
      JVMTI_EVENT_GARBAGE_COLLECTION_START,
      nullptr);
  if (JvmtiErrorToException(env, jvmti_env, ret)) {
    return;
  }
  ret = jvmti_env->SetEventNotificationMode(
      enable ? JVMTI_ENABLE : JVMTI_DISABLE,
      JVMTI_EVENT_GARBAGE_COLLECTION_FINISH,
      nullptr);
  if (JvmtiErrorToException(env, jvmti_env, ret)) {
    return;
  }

  ret = jvmti_env->SetEventNotificationMode(
      enable ? JVMTI_ENABLE : JVMTI_DISABLE,
      JVMTI_EVENT_VM_OBJECT_ALLOC,
      nullptr);
  if (JvmtiErrorToException(env, jvmti_env, ret)) {
    return;
  }
}

jint OnLoad(JavaVM* vm, char* options ATTRIBUTE_UNUSED, void* reserved ATTRIBUTE_UNUSED) {
  jvm = vm;

  if (vm->GetEnv(reinterpret_cast<void**>(&jvmti_env), JVMTI_VERSION_1_0) != 0) {
    printf("Unable to get jvmti env!\n");
    return 1;
  }
  SetStandardCapabilities(jvmti_env);

  return JNI_OK;
}

extern "C" JNIEXPORT jint JNICALL Java_art_Test908B_getGcStarts(JNIEnv* env ATTRIBUTE_UNUSED,
                                                               jclass klass ATTRIBUTE_UNUSED) {
  jint result = static_cast<jint>(starts);
  starts = 0;
  return result;
}

extern "C" JNIEXPORT jint JNICALL Java_art_Test908B_getGcFinishes(JNIEnv* env ATTRIBUTE_UNUSED,
                                                                 jclass klass ATTRIBUTE_UNUSED) {
  jint result = static_cast<jint>(finishes);
  finishes = 0;
  return result;
}

}  // namespace Test908GcStartFinishB
}  // namespace art
