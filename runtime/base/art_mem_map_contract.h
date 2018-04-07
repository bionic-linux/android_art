/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef ART_RUNTIME_BASE_ART_MEM_MAP_CONTRACT_H_
#define ART_RUNTIME_BASE_ART_MEM_MAP_CONTRACT_H_

#include <stddef.h>
#include <sys/types.h>

#include <string>

#include "base/macros.h"
#include "base/mem_map.h"

namespace art {

// Interface used by MemMap to check consistency.
class ArtMemMapContract : public MemMapContract {
 public:
  ArtMemMapContract() { }
  ~ArtMemMapContract() OVERRIDE { }

  bool ContainedWithinExistingMap(uintptr_t begin,
                                  uintptr_t end,
                                  std::string* error_msg) OVERRIDE;
  bool CheckNonOverlapping(uintptr_t expected,
                           uintptr_t limit,
                           std::string* error_detail) OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(ArtMemMapContract);
};

}  // namespace art

#endif  // ART_RUNTIME_BASE_ART_MEM_MAP_CONTRACT_H_
