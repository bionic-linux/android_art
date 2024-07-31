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

#ifndef ART_PROFMAN_INLINE_CACHE_FORMAT_UTIL_H_
#define ART_PROFMAN_INLINE_CACHE_FORMAT_UTIL_H_

#include "profile/profile_compilation_info.h"

namespace art {

static constexpr char kProfileParsingInlineChacheSep = '+';
static constexpr char kProfileParsingInlineChacheTargetSep = ']';
static const char kMissingTypesMarker[] =
    "missing_types"; // NOLINT [runtime/string] [4]
static const char kMegamorphicTypesMarker[] =
    "megamorphic_types";  // NOLINT [runtime/string] [4]
static constexpr char kProfileParsingTypeSep = ',';

using InlineCacheInfo = FlattenProfileData::ItemMetadata::InlineCacheInfo;

// Creates the inline-cache portion of a text-profile line. If there is no
// inline-caches this will be an empty string. Otherwise it will be '@'
// followed by an IC description matching the format described by ProcessLine
// below.
std::string GetInlineCacheLine(
    const SafeMap<std::string, InlineCacheInfo>& inline_cache);

}  // namespace art

#endif  // ART_PROFMAN_INLINE_CACHE_FORMAT_UTIL_H_
