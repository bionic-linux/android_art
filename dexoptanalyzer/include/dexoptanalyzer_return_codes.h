/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef ART_DEXOPTANALYZER_INCLUDE_DEXOPTANALYZER_RETURN_CODES_H_
#define ART_DEXOPTANALYZER_INCLUDE_DEXOPTANALYZER_RETURN_CODES_H_

namespace art {
namespace dexoptanalyzer {

// See OatFileAssistant docs for the meaning of the valid return codes.
enum class ReturnCodes : int {
  kNoDexOptNeeded = 0,
  kDex2OatFromScratch = 1,
  kDex2OatForBootImageOat = 2,
  kDex2OatForFilterOat = 3,
  kDex2OatForRelocationOat = 4,
  kDex2OatForBootImageOdex = 5,
  kDex2OatForFilterOdex = 6,
  kDex2OatForRelocationOdex = 7,

  kBootImageError = 50,                 // Failure return value for --get-image-state.
                                        // kNoDexOptNeeded is returned when the image is
                                        // up-to-date.

  kErrorInvalidArguments = 101,
  kErrorCannotCreateRuntime = 102,
  kErrorUnknownDexOptNeeded = 103
};

}  // namespace dexoptanalyzer
}  // namespace art

#endif  // ART_DEXOPTANALYZER_INCLUDE_DEXOPTANALYZER_RETURN_CODES_H_
