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

#ifndef ART_LIBPALETTE_INCLUDE_PALETTE_PALETTE_TYPES_H_
#define ART_LIBPALETTE_INCLUDE_PALETTE_PALETTE_TYPES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Return values for palette functions.
enum PaletteStatus {
  kOkay,
  kCheckErrno,
  kInvalidArgument,
  kNotSupported,
};

// Metrics logging category values.
enum PaletteEventCategory {
  kHiddenApiAccess,
};

// Metrics logging tag values.
enum PaletteEventTag {
  kHiddenApiAccessMethod,
  kHiddenApiAccessDenied,
  kHiddenApiSignature,
};

// Values for PaletteMetricsRecordTaggedDataStruct.value.i32 for Hidden API events.
enum PaletteEventCategoryHiddenApiAccess {
  kNone,
  kMethodViaReflection,
  kMethodViaJNI,
  kMethodViaLinking,
};

// Kind descriminator for PaletteMetricsRecordTaggedDataStruct.value.
enum PaletteEventTaggedDataKind {
  kString,
  kInt32,
  kInt64,
  kFloat,
};

typedef struct PaletteMetricsRecordTaggedDataStruct {
  enum PaletteEventTag tag;
  enum PaletteEventTaggedDataKind kind;
  struct {
  union {
    const char* c_str;
    int32_t i32;
    int64_t i64;
    float f;
  };
  } value;
} PaletteMetricsRecordTaggedData;

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // ART_LIBPALETTE_INCLUDE_PALETTE_PALETTE_TYPES_H_
