/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "art_method-inl.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "jit/profiling_info.h"
#include "oat_quick_method_header.h"
#include "scoped_thread_state_change.h"
#include "ScopedUtfChars.h"
#include "stack_map.h"
#include "gc/scoped_gc_critical_section.h"

namespace art {
  Thread *mutatorThread;
  static void ThreadSuspendSleep(useconds_t delay_us) {
    if (delay_us == 0) {
      sched_yield();
    } else {
      usleep(delay_us);
    }
  }

  extern "C" JNIEXPORT void JNICALL Java_Main_gcRunCheckpoint(JNIEnv*,
      jclass) {
    printf("gcRunCheckpoint start.\n");
    gc::ScopedGCCriticalSection gcs(Thread::Current(),
        gc::kGcCauseTrim,
        gc::kCollectorTypeHeapTrim);
    ScopedObjectAccess soa(Thread::Current());
    WriterMutexLock mu(soa.Self(), *Locks::heap_bitmap_lock_);
    printf("gcRunCheckpoint heap_bitmap_lock_ held.\n");
    do {
      ThreadSuspendSleep(0);
    } while (!mutatorThread->IsSuspended());
    printf("gcRunCheckpoint finish.\n");
  }

  extern "C" JNIEXPORT void JNICALL Java_Main_mutatorHeld(JNIEnv*,
      jclass) {
    printf("mutatorHeld start.\n");
    gc::ScopedGCCriticalSection gcs(Thread::Current(),
        gc::kGcCauseTrim,
        gc::kCollectorTypeHeapTrim);
    mutatorThread = Thread::Current();
    ScopedObjectAccess soa(Thread::Current());
    printf("mutatorHeld mutator held.\n");
    // simulate deadlock case
    usleep(5 * 1000 * 1000);
    WriterMutexLock mu(soa.Self(), *Locks::heap_bitmap_lock_);
    printf("mutatorHeld finish.\n");
  }
}  // namespace art
