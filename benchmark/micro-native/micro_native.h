/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_BENCHMARK_MICRO_NATIVE_MICRO_NATIVE_H_
#define ART_BENCHMARK_MICRO_NATIVE_MICRO_NATIVE_H_

#include <jni.h>

namespace art {

#ifndef NATIVE_METHOD
#define NATIVE_METHOD(className, functionName, signature) \
    { #functionName, signature, reinterpret_cast<void*>(className ## _ ## functionName) }
#endif
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

static constexpr const char* kClassName = "benchmarks/MicroNative/java/NativeMethods";

void jniRegisterNativeMethodsHelper(JNIEnv* env,
                                    const char* className,
                                    const JNINativeMethod* methods,
                                    int numMethods);

void register_micro_native_methods(JNIEnv* env);
void register_micro_native_locals_methods(JNIEnv* env);

}  // namespace art

#endif  // ART_BENCHMARK_MICRO_NATIVE_MICRO_NATIVE_H_
