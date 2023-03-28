/*
 * Copyright (C) 2023 The Android Open Source Project
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
#include "optimizing/unimplemented_intrinsics_arm_vixl.h"

namespace art HIDDEN {
namespace arm {

const std::unordered_set<Intrinsics> unimplemented_intrinsics = {
#define ADD_TO_SET(Name) Intrinsics::k##Name,
    UNIMPLEMENTED_INTRINSIC_LIST_ARM(ADD_TO_SET)
#undef ADD_TO_SET
};

}  // namespace arm
}  // namespace art
