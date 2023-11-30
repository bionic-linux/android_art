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

#ifndef ART_RUNTIME_RUNTIME_GLOBALS_H_
#define ART_RUNTIME_RUNTIME_GLOBALS_H_

#include <android-base/logging.h>

#include "base/bit_utils.h"
#include "base/globals.h"

namespace art {

// Size of Dex virtual registers.
static constexpr size_t kVRegSize = 4;

#ifdef ART_PAGE_SIZE_AGNOSTIC
struct PageSizeLog2 {
  PageSizeLog2()
    : value_(WhichPowerOf2(GetPageSizeSlow())), is_initialized_(true) {}

  ALWAYS_INLINE operator size_t() const {
    DCHECK(is_initialized_);
    return value_;
  }

 private:
  const size_t value_;
  const bool is_initialized_;
};

extern const PageSizeLog2 gPageSizeLog2 ALWAYS_HIDDEN;

// Wrapper over gPageSizeLog2 returning the page size value.
// There is no data in the struct, so it can be just a static const local in each module using it.
struct PageSize {
  ALWAYS_INLINE operator size_t() const {
    return (1u << gPageSizeLog2);
  }
};

// The gPageSize variable is defined as a global static value, meaning that it is loaded
// dynamically for all references which are separated by one or more function call, as the compiler
// does not recognize the value as constant (unchanged after initialization), despite the const
// attribute.
// We therefore optimize usage of the variable as follows:
// * For very hot functions, if the running Thread object is already locally accessed, get the
//   const (log2) value cached in 32-bit TLS.
// * Otherwise if there are multiple class function invocations of the same object instantiation
//   which require a reference to the static value, instead reference a cached copy of the (log2)
//   value as a const member variable.
// * Otherwise if a local member variable is not possible or appropriate, but we do have an existing
//   reference to an object that has the cached const (log2) value, we use that object's value.
// * Otherwise, if repeat references are required by the same function and separated by one or more
//   function calls, we store the value as a local const variable which is then reused throughout
//   the function.
// Note: We cache the log2 of the page size as member variables instead of the page size directly,
// as the compiler loses knowledge that gPageSize is a power-of-two when it is cached. Therefore, to
// maintain power-of-two optimizations we store the log2 and left-shift when needed to access the
// page size.
//
static const PageSize gPageSize;
#else
static constexpr size_t gPageSize = kMinPageSize;
static constexpr size_t gPageSizeLog2 = WhichPowerOf2(gPageSize);
#endif

// Returns whether the given memory offset can be used for generating
// an implicit null check.
static inline bool CanDoImplicitNullCheckOn(uintptr_t offset) {
  return offset < gPageSize;
}

// Required object alignment
static constexpr size_t kObjectAlignmentShift = 3;
static constexpr size_t kObjectAlignment = 1u << kObjectAlignmentShift;
static constexpr size_t kLargeObjectAlignment = kMaxPageSize;
static_assert(kLargeObjectAlignment <= 16 * KB, "Consider redesign if more than 16K is required.");

// Garbage collector constants.
static constexpr bool kMovingCollector = true;
static constexpr bool kMarkCompactSupport = false && kMovingCollector;
// True if we allow moving classes.
static constexpr bool kMovingClasses = !kMarkCompactSupport;
// When using the Concurrent Copying (CC) collector, if
// `ART_USE_GENERATIONAL_CC` is true, enable generational collection by default,
// i.e. use sticky-bit CC for minor collections and (full) CC for major
// collections.
// This default value can be overridden with the runtime option
// `-Xgc:[no]generational_cc`.
//
// TODO(b/67628039): Consider either:
// - renaming this to a better descriptive name (e.g.
//   `ART_USE_GENERATIONAL_CC_BY_DEFAULT`); or
// - removing `ART_USE_GENERATIONAL_CC` and having a fixed default value.
// Any of these changes will require adjusting users of this preprocessor
// directive and the corresponding build system environment variable (e.g. in
// ART's continuous testing).
#ifdef ART_USE_GENERATIONAL_CC
static constexpr bool kEnableGenerationalCCByDefault = true;
#else
static constexpr bool kEnableGenerationalCCByDefault = false;
#endif

// If true, enable the tlab allocator by default.
#ifdef ART_USE_TLAB
static constexpr bool kUseTlab = true;
#else
static constexpr bool kUseTlab = false;
#endif

// Kinds of tracing clocks.
enum class TraceClockSource {
  kThreadCpu,
  kWall,
  kDual,  // Both wall and thread CPU clocks.
};

#if defined(__linux__)
static constexpr TraceClockSource kDefaultTraceClockSource = TraceClockSource::kDual;
#else
static constexpr TraceClockSource kDefaultTraceClockSource = TraceClockSource::kWall;
#endif

static constexpr bool kDefaultMustRelocate = true;

// Size of a heap reference.
static constexpr size_t kHeapReferenceSize = sizeof(uint32_t);

}  // namespace art

#endif  // ART_RUNTIME_RUNTIME_GLOBALS_H_
