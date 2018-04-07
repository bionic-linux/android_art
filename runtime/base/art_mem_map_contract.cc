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

#include "base/art_mem_map_contract.h"

#include <inttypes.h>
#include <stdlib.h>
#include <sys/mman.h>  // For the PROT_* and MAP_* constants.
#ifndef ANDROID_OS
#include <sys/resource.h>
#endif

#include <map>
#include <memory>
#include <sstream>

#include "android-base/stringprintf.h"
#include "backtrace/BacktraceMap.h"
#include "cutils/ashmem.h"

#include "base/allocator.h"
#include "base/bit_utils.h"
#include "base/file_utils.h"
#include "base/globals.h"
#include "base/logging.h"  // For VLOG_IS_ON.
#include "base/memory_tool.h"
#include "base/utils.h"

namespace art {

using android::base::StringPrintf;

static std::ostream& operator<<(
    std::ostream& os,
    std::pair<BacktraceMap::iterator, BacktraceMap::iterator> iters) {
  for (BacktraceMap::iterator it = iters.first; it != iters.second; ++it) {
    const backtrace_map_t* entry = *it;
    os << StringPrintf("0x%08x-0x%08x %c%c%c %s\n",
                       static_cast<uint32_t>(entry->start),
                       static_cast<uint32_t>(entry->end),
                       (entry->flags & PROT_READ) ? 'r' : '-',
                       (entry->flags & PROT_WRITE) ? 'w' : '-',
                       (entry->flags & PROT_EXEC) ? 'x' : '-', entry->name.c_str());
  }
  return os;
}

// Return true if the address range is contained in a single memory map by either reading
// the gMaps variable or the /proc/self/map entry.
bool ArtMemMapContract::ContainedWithinExistingMap(uintptr_t begin,
                                                   uintptr_t end,
                                                   std::string* error_msg) {
  std::unique_ptr<BacktraceMap> map(BacktraceMap::Create(getpid(), true));
  if (map == nullptr) {
    if (error_msg != nullptr) {
      *error_msg = StringPrintf("Failed to build process map");
    }
    return false;
  }

  ScopedBacktraceMapIteratorLock lock(map.get());
  for (BacktraceMap::iterator it = map->begin(); it != map->end(); ++it) {
    const backtrace_map_t* entry = *it;
    if ((begin >= entry->start && begin < entry->end)     // start of new within old
        && (end > entry->start && end <= entry->end)) {   // end of new within old
      return true;
    }
  }
  if (error_msg != nullptr) {
    PrintFileToLog("/proc/self/maps", LogSeverity::ERROR);
    *error_msg = StringPrintf("Requested region 0x%08" PRIxPTR "-0x%08" PRIxPTR " does not overlap "
                              "any existing map. See process maps in the log.", begin, end);
  }
  return false;
}

// Return true if the address range does not conflict with any /proc/self/maps entry.
bool ArtMemMapContract::CheckNonOverlapping(uintptr_t begin,
                                            uintptr_t end,
                                            std::string* error_msg) {
  std::unique_ptr<BacktraceMap> map(BacktraceMap::Create(getpid(), true));
  if (map.get() == nullptr) {
    *error_msg = StringPrintf("Failed to build process map");
    return false;
  }
  ScopedBacktraceMapIteratorLock lock(map.get());
  for (BacktraceMap::iterator it = map->begin(); it != map->end(); ++it) {
    const backtrace_map_t* entry = *it;
    if ((begin >= entry->start && begin < entry->end)      // start of new within old
        || (end > entry->start && end < entry->end)        // end of new within old
        || (begin <= entry->start && end > entry->end)) {  // start/end of new includes all of old
      std::ostringstream map_info;
      map_info << std::make_pair(it, map->end());
      *error_msg = StringPrintf("Requested region 0x%08" PRIxPTR "-0x%08" PRIxPTR " overlaps with "
                                "existing map 0x%08" PRIxPTR "-0x%08" PRIxPTR " (%s)\n%s",
                                begin, end,
                                static_cast<uintptr_t>(entry->start),
                                static_cast<uintptr_t>(entry->end),
                                entry->name.c_str(),
                                map_info.str().c_str());
      return false;
    }
  }
  return true;
}

}  // namespace art
