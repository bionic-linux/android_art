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

#include <iostream>
#include <pthread.h>
#include <stdio.h>
#include <vector>

#include "art_method-inl.h"
#include "base/logging.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"
#include "ti-agent/common_helper.h"
#include "ti-agent/common_load.h"
#include "utils.h"

namespace art {
namespace Test914HelloObsolescence {

static bool RuntimeIsJvm = false;

bool IsJVM() {
  return RuntimeIsJvm;
}

// base64 encoded class/dex file for
//
// class Transform {
//   public void sayHi(Runnable r) {
//     System.out.println("Hello - Transformed");
//     r.run();
//     System.out.println("Goodbye - Transformed");
//   }
// }
const char* class_file_base64 =
  "yv66vgAAADQAJAoACAARCQASABMIABQKABUAFgsAFwAYCAAZBwAaBwAbAQAGPGluaXQ+AQADKClW"
  "AQAEQ29kZQEAD0xpbmVOdW1iZXJUYWJsZQEABXNheUhpAQAXKExqYXZhL2xhbmcvUnVubmFibGU7"
  "KVYBAApTb3VyY2VGaWxlAQAOVHJhbnNmb3JtLmphdmEMAAkACgcAHAwAHQAeAQATSGVsbG8gLSBU"
  "cmFuc2Zvcm1lZAcAHwwAIAAhBwAiDAAjAAoBABVHb29kYnllIC0gVHJhbnNmb3JtZWQBAAlUcmFu"
  "c2Zvcm0BABBqYXZhL2xhbmcvT2JqZWN0AQAQamF2YS9sYW5nL1N5c3RlbQEAA291dAEAFUxqYXZh"
  "L2lvL1ByaW50U3RyZWFtOwEAE2phdmEvaW8vUHJpbnRTdHJlYW0BAAdwcmludGxuAQAVKExqYXZh"
  "L2xhbmcvU3RyaW5nOylWAQASamF2YS9sYW5nL1J1bm5hYmxlAQADcnVuACAABwAIAAAAAAACAAAA"
  "CQAKAAEACwAAAB0AAQABAAAABSq3AAGxAAAAAQAMAAAABgABAAAAAQABAA0ADgABAAsAAAA7AAIA"
  "AgAAABeyAAISA7YABCu5AAUBALIAAhIGtgAEsQAAAAEADAAAABIABAAAAAMACAAEAA4ABQAWAAYA"
  "AQAPAAAAAgAQ";

const char* dex_file_base64 =
  "ZGV4CjAzNQAYeAMMXgYWxoeSHAS9EWKCCtVRSAGpqZVQAwAAcAAAAHhWNBIAAAAAAAAAALACAAAR"
  "AAAAcAAAAAcAAAC0AAAAAwAAANAAAAABAAAA9AAAAAUAAAD8AAAAAQAAACQBAAAMAgAARAEAAKIB"
  "AACqAQAAwQEAANYBAADjAQAA+gEAAA4CAAAkAgAAOAIAAEwCAABcAgAAXwIAAGMCAAB3AgAAfAIA"
  "AIUCAACKAgAAAwAAAAQAAAAFAAAABgAAAAcAAAAIAAAACgAAAAoAAAAGAAAAAAAAAAsAAAAGAAAA"
  "lAEAAAsAAAAGAAAAnAEAAAUAAQANAAAAAAAAAAAAAAAAAAEAEAAAAAEAAgAOAAAAAgAAAAAAAAAD"
  "AAAADwAAAAAAAAAAAAAAAgAAAAAAAAAJAAAAAAAAAJ8CAAAAAAAAAQABAAEAAACRAgAABAAAAHAQ"
  "AwAAAA4ABAACAAIAAACWAgAAFAAAAGIAAAAbAQIAAABuIAIAEAByEAQAAwBiAAAAGwEBAAAAbiAC"
  "ABAADgABAAAAAwAAAAEAAAAEAAY8aW5pdD4AFUdvb2RieWUgLSBUcmFuc2Zvcm1lZAATSGVsbG8g"
  "LSBUcmFuc2Zvcm1lZAALTFRyYW5zZm9ybTsAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwASTGphdmEv"
  "bGFuZy9PYmplY3Q7ABRMamF2YS9sYW5nL1J1bm5hYmxlOwASTGphdmEvbGFuZy9TdHJpbmc7ABJM"
  "amF2YS9sYW5nL1N5c3RlbTsADlRyYW5zZm9ybS5qYXZhAAFWAAJWTAASZW1pdHRlcjogamFjay00"
  "LjEzAANvdXQAB3ByaW50bG4AA3J1bgAFc2F5SGkAAQAHDgADAQAHDoc8hwAAAAEBAICABMQCAQHc"
  "AgAAAA0AAAAAAAAAAQAAAAAAAAABAAAAEQAAAHAAAAACAAAABwAAALQAAAADAAAAAwAAANAAAAAE"
  "AAAAAQAAAPQAAAAFAAAABQAAAPwAAAAGAAAAAQAAACQBAAABIAAAAgAAAEQBAAABEAAAAgAAAJQB"
  "AAACIAAAEQAAAKIBAAADIAAAAgAAAJECAAAAIAAAAQAAAJ8CAAAAEAAAAQAAALACAAA=";

static void JNICALL transformationHook(jvmtiEnv *jvmtienv,
                                       JNIEnv* jni_env                 ATTRIBUTE_UNUSED,
                                       jclass class_being_redefined    ATTRIBUTE_UNUSED,
                                       jobject loader                  ATTRIBUTE_UNUSED,
                                       const char* name,
                                       jobject protection_domain       ATTRIBUTE_UNUSED,
                                       jint class_data_len             ATTRIBUTE_UNUSED,
                                       const unsigned char* class_data ATTRIBUTE_UNUSED,
                                       jint* new_class_data_len,
                                       unsigned char** new_class_data) {
  if (strcmp("Transform", name)) {
    return;
  }
  printf("modifying class '%s'\n", name);
  bool is_jvm = IsJVM();
  size_t decode_len = 0;
  unsigned char* new_data;
  std::unique_ptr<uint8_t[]> file_data(
      DecodeBase64((is_jvm) ? class_file_base64 : dex_file_base64, &decode_len));
  jvmtiError ret = JVMTI_ERROR_NONE;
  if ((ret = jvmtienv->Allocate(static_cast<jlong>(decode_len), &new_data)) != JVMTI_ERROR_NONE) {
    printf("Unable to allocate buffer!\n");
    return;
  }
  memcpy(new_data, file_data.get(), decode_len);
  *new_class_data_len = static_cast<jint>(decode_len);
  *new_class_data = new_data;
  return;
}

using RetransformWithHookFunction = jvmtiError (*)(jvmtiEnv*, jclass, jvmtiEventClassFileLoadHook);
static void DoClassTransformation(jvmtiEnv* jvmtienv, JNIEnv* jnienv, jclass target) {
  if (IsJVM()) {
    UNUSED(jnienv);
    jvmtienv->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
    jvmtiError ret = jvmtienv->RetransformClasses(1, &target);
    if (ret != JVMTI_ERROR_NONE) {
      char* err;
      jvmtienv->GetErrorName(ret, &err);
      printf("Error transforming: %s\n", err);
    }
  } else {
    RetransformWithHookFunction f =
        reinterpret_cast<RetransformWithHookFunction>(jvmtienv->functions->reserved1);
    if (f(jvmtienv, target, transformationHook) != JVMTI_ERROR_NONE) {
      printf("Failed to tranform class!");
      return;
    }
  }
}

extern "C" JNIEXPORT void JNICALL Java_Main_doClassTransformation914(JNIEnv* env,
                                                                  jclass,
                                                                  jclass target) {
  JavaVM* vm;
  if (env->GetJavaVM(&vm)) {
    printf("Unable to get javaVM!\n");
    return;
  }
  DoClassTransformation(jvmti_env, env, target);
}

// Don't do anything
jint OnLoad(JavaVM* vm,
            char* options,
            void* reserved ATTRIBUTE_UNUSED) {
  RuntimeIsJvm = (strcmp("jvm", options) == 0);
  if (vm->GetEnv(reinterpret_cast<void**>(&jvmti_env), JVMTI_VERSION_1_0)) {
    printf("Unable to get jvmti env!\n");
    return 1;
  }
  SetAllCapabilities(jvmti_env);
  if (IsJVM()) {
    jvmtiEventCallbacks cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.ClassFileLoadHook = transformationHook;
    jvmti_env->SetEventCallbacks(&cbs, sizeof(jvmtiEventCallbacks));
  }
  return 0;
}

}  // namespace Test914HelloObsolescence
}  // namespace art

