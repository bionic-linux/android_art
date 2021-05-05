/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef ART_LIBARTBASE_BASE_QUICK_EXIT_H_
#define ART_LIBARTBASE_BASE_QUICK_EXIT_H_

// Header-only definition of `art::QuickExit`.
//
// Ideally, this routine should be declared in `base/os.h` and defined in
// `base/os_linux.cc`, but as `libartbase` is not linked (directly) with
// `dalvikvm`, we would not be able to easily use `art::QuickExit` in
// `dex2oat`. Use a header-only approach and define `art::QuickExit` in its own
// file for clarity.

#include <stdlib.h>

#include <base/macros.h>

namespace art {

// Terminate program without completely cleaning the resources (e.g. without
// calling destructors). Call functions registered with `at_quick_exit` (for
// instance LLVM's code coverage profile dumping routine, when running with
// code coverage instrumentation) before exiting.
NO_RETURN inline void QuickExit(int exit_code) {
#ifdef _WIN32
  // Windows toolchain does not support `quick_exit`; use `_exit` instead.
  _exit(exit_code);
#else
  quick_exit(exit_code);
#endif
}

}  // namespace art

#endif  // ART_LIBARTBASE_BASE_QUICK_EXIT_H_
