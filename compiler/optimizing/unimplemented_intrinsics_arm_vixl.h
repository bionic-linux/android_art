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

#ifndef ART_COMPILER_OPTIMIZING_UNIMPLEMENTED_INTRINSICS_ARM_VIXL_H_
#define ART_COMPILER_OPTIMIZING_UNIMPLEMENTED_INTRINSICS_ARM_VIXL_H_

#include <unordered_set>

#include "base/macros.h"
#include "intrinsics_enum.h"

namespace art HIDDEN {
namespace arm {

#define UNIMPLEMENTED_INTRINSIC_LIST_ARM(V)                                \
  V(MathRoundDouble) /* Could be done by changing rounding mode, maybe? */ \
  V(UnsafeCASLong)   /* High register pressure */                          \
  V(SystemArrayCopyChar)                                                   \
  V(LongDivideUnsigned)                                                    \
  V(CRC32Update)                                                           \
  V(CRC32UpdateBytes)                                                      \
  V(CRC32UpdateByteBuffer)                                                 \
  V(FP16ToFloat)                                                           \
  V(FP16ToHalf)                                                            \
  V(FP16Floor)                                                             \
  V(FP16Ceil)                                                              \
  V(FP16Rint)                                                              \
  V(FP16Greater)                                                           \
  V(FP16GreaterEquals)                                                     \
  V(FP16Less)                                                              \
  V(FP16LessEquals)                                                        \
  V(FP16Compare)                                                           \
  V(FP16Min)                                                               \
  V(FP16Max)                                                               \
  V(MathMultiplyHigh)                                                      \
  V(StringStringIndexOf)                                                   \
  V(StringStringIndexOfAfter)                                              \
  V(StringBufferAppend)                                                    \
  V(StringBufferLength)                                                    \
  V(StringBufferToString)                                                  \
  V(StringBuilderAppendObject)                                             \
  V(StringBuilderAppendString)                                             \
  V(StringBuilderAppendCharSequence)                                       \
  V(StringBuilderAppendCharArray)                                          \
  V(StringBuilderAppendBoolean)                                            \
  V(StringBuilderAppendChar)                                               \
  V(StringBuilderAppendInt)                                                \
  V(StringBuilderAppendLong)                                               \
  V(StringBuilderAppendFloat)                                              \
  V(StringBuilderAppendDouble)                                             \
  V(StringBuilderLength)                                                   \
  V(StringBuilderToString)                                                 \
  V(SystemArrayCopyByte)                                                   \
  V(SystemArrayCopyInt)                                                    \
  /* 1.8 */                                                                \
  V(MathFmaDouble)                                                         \
  V(MathFmaFloat)                                                          \
  V(UnsafeGetAndAddInt)                                                    \
  V(UnsafeGetAndAddLong)                                                   \
  V(UnsafeGetAndSetInt)                                                    \
  V(UnsafeGetAndSetLong)                                                   \
  V(UnsafeGetAndSetObject)                                                 \
  V(MethodHandleInvokeExact)                                               \
  V(MethodHandleInvoke)                                                    \
  /* OpenJDK 11 */                                                         \
  V(JdkUnsafeCASLong) /* High register pressure */                         \
  V(JdkUnsafeGetAndAddInt)                                                 \
  V(JdkUnsafeGetAndAddLong)                                                \
  V(JdkUnsafeGetAndSetInt)                                                 \
  V(JdkUnsafeGetAndSetLong)                                                \
  V(JdkUnsafeGetAndSetObject)                                              \
  V(JdkUnsafeCompareAndSetLong)

extern const std::unordered_set<Intrinsics> unimplemented_intrinsics;

}  // namespace arm
}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_UNIMPLEMENTED_INTRINSICS_ARM_VIXL_H_
