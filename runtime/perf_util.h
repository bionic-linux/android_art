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

#ifndef ART_RUNTIME_GC_COLLECTOR_UCLAMP_H_
#define ART_RUNTIME_GC_COLLECTOR_UCLAMP_H_

#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>
#include <log/log.h>
#include "base/systrace.h"
#include "android-base/logging.h"

namespace art {
namespace gc {
namespace collector {

class PerfUtil {

public:
 //TODO: All value should read from product config
 static void setUclampMax(int tid) {
    setUclamp(0, 638, tid);//frequency point is middle-core 2.4GHZ
    ScopedTrace trace(android::base::StringPrintf("set_uclamp %d ", 638));
 }

 static void restoreUclampMax(int tid) {
    setUclamp(0, 1024, tid);//frequency point 1024 is restore default
 }

private:
 struct sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    uint32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
    uint32_t sched_util_min;
    uint32_t sched_util_max;
 };

 #if defined(__x86_64__)
 #define NR_sched_setattr 314
 #elif defined(__i386__)
 #define NR_sched_setattr 351
 #elif defined(__arm__)
 #define NR_sched_setattr 380
 #elif defined(__aarch64__)
 #define NR_sched_setattr 274
 #else
 #error "We don't have an NR_sched_setattr for this architecture."
 #endif

 #define SCHED_FLAG_KEEP_POLICY 0x08
 #define SCHED_FLAG_KEEP_PARAMS 0x10
 #define SCHED_FLAG_UTIL_CLAMP_MIN 0x20
 #define SCHED_FLAG_UTIL_CLAMP_MAX 0x40
 #define SCHED_FLAG_KEEP_ALL (SCHED_FLAG_KEEP_POLICY | SCHED_FLAG_KEEP_PARAMS)
 #define SCHED_FLAG_UTIL_CLAMP (SCHED_FLAG_UTIL_CLAMP_MIN | SCHED_FLAG_UTIL_CLAMP_MAX)

 static int sched_setattr(int pid, struct sched_attr* attr, unsigned int flags) {
     return syscall(NR_sched_setattr, pid, attr, flags);
 }

 static void setUclamp(int32_t min, int32_t max, int tid) {
 #ifdef __linux__
     sched_attr attr = {};
     attr.size = sizeof(attr);
     attr.sched_flags = (SCHED_FLAG_KEEP_ALL | SCHED_FLAG_UTIL_CLAMP);
     attr.sched_util_min = min;
     attr.sched_util_max = max;

     int ret = sched_setattr(tid, &attr, 0);
     LOG(INFO) << "set_cc_gc uclamp: max " <<  max << "; thread id = " << tid;
     if (ret) {
         int err = ret;
         LOG(ERROR) << "sched_setattr failed for thread " << tid << " err=" << err;
     }
 #else
 #endif
 }

};

}  // namespace collector
}  // namespace gc
}  // namespace art

#endif
