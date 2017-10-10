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

#include "cdex_file.h"

namespace art {

const uint8_t CDexFile::kDexMagic[] = { 'c', 'd', 'e', 'x' };
const uint8_t CDexFile::kDexMagicVersion[] = {'0', '0', '1', '\0'};

bool CDexFile::IsMagicValid(const uint8_t* magic) {
  return (memcmp(magic, kDexMagic, sizeof(kDexMagic)) == 0);
}

bool CDexFile::IsVersionValid(const uint8_t* magic) {
  const uint8_t* version = &magic[sizeof(kDexMagic)];
  return memcmp(version, kDexMagicVersion, kDexVersionLen) == 0;
}

bool CDexFile::IsMagicValid() const {
  return IsMagicValid(header_->magic_);
}

bool CDexFile::IsVersionValid() const {
  return IsVersionValid(header_->magic_);
}

}  // namespace art
