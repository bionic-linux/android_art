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

#ifndef ART_RUNTIME_TI_ENV_H_
#define ART_RUNTIME_TI_ENV_H_

#include "globals.h"
#include "thread.h"
#include "java_vm_ext.h"
#include "jni_env_ext.h"

namespace openjdkjvmti {
class JvmtiFunctions;
}

namespace art {
namespace ti {

// The environment for the tool interface
class Env {
 public:
  JavaVMExt* GetVM() const {
    return vm_;
  }

  explicit Env(JavaVMExt* vm) : vm_(vm) { }

  ~Env() { }

 private:
  // The VM this TiEnv is on.
  JavaVMExt* const vm_;

  friend class JvmtiFunctions;
};

}  // namespace ti
}  // namespace art

#endif  // ART_RUNTIME_TI_ENV_H_
