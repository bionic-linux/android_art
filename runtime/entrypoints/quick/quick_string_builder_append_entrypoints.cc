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

#include "instrumentation.h"
#include "jvalue-inl.h"
#include "mirror/string.h"
#include "obj_ptr-inl.h"
#include "quick_entrypoints.h"
#include "runtime.h"
#include "string_builder_append.h"

namespace art {

extern "C" mirror::String* artStringBuilderAppend(uint32_t format,
                                                  const uint32_t* args,
                                                  Thread* self) {
  auto res = StringBuilderAppend::AppendF(format, args, self).Ptr();
  JValue value;
  value.SetL(res);
  if (Runtime::Current()->GetInstrumentation()->PushDeoptContextIfNeeded(
          self, DeoptimizationMethodType::kDefault, /* is_ref= */ true, value)) {
    return nullptr;
  }
  return res;
}

}  // namespace art
