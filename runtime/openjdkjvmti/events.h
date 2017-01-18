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

#ifndef ART_RUNTIME_OPENJDKJVMTI_EVENTS_H_
#define ART_RUNTIME_OPENJDKJVMTI_EVENTS_H_

#include <bitset>
#include <vector>

#include "base/logging.h"
#include "jvmti.h"
#include "thread.h"

namespace openjdkjvmti {

struct ArtJvmTiEnv;
class JvmtiAllocationListener;
class JvmtiGcPauseListener;

// an enum for ArtEvents. This differs from the JVMTI events only in that we distinguish between
// retransformation capable and incapable loading
enum class ArtJvmtiEvent {
    kMinEventTypeVal = 50,
    kVmInit = 50,
    kVmDeath = 51,
    kThreadStart = 52,
    kThreadEnd = 53,
    kClassFileLoadHookNonRetransformable = 54,
    kClassLoad = 55,
    kClassPrepare = 56,
    kVmStart = 57,
    kException = 58,
    kExceptionCatch = 59,
    kSingleStep = 60,
    kFramePop = 61,
    kBreakpoint = 62,
    kFieldAccess = 63,
    kFieldModification = 64,
    kMethodEntry = 65,
    kMethodExit = 66,
    kNativeMethodBind = 67,
    kCompiledMethodLoad = 68,
    kCompiledMethodUnload = 69,
    kDynamicCodeGenerated = 70,
    kDataDumpRequest = 71,
    kMonitorWait = 73,
    kMonitorWaited = 74,
    kMonitorContendedEnter = 75,
    kMonitorContendedEntered = 76,
    kResourceExhausted = 80,
    kGarbageCollectionStart = 81,
    kGarbageCollectionFinish = 82,
    kObjectFree = 83,
    kVmObjectAlloc = 84,
    kClassFileLoadHookRetransformable = 85,
    kMaxEventTypeVal = 85
};

// Convert a jvmtiEvent into a ArtJvmtiEvent
static inline ArtJvmtiEvent GetArtJvmtiEvent(jvmtiEvent e) {
  CHECK_NE(e, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK);
  return static_cast<ArtJvmtiEvent>(e);
}

// Convert a jvmtiEvent into a ArtJvmtiEvent
static inline ArtJvmtiEvent GetArtJvmtiEvent(ArtJvmTiEnv* env, jvmtiEvent e);

static inline jvmtiEvent GetJvmtiEvent(ArtJvmtiEvent e) {
  if (UNLIKELY(e == ArtJvmtiEvent::kClassFileLoadHookRetransformable)) {
    return JVMTI_EVENT_CLASS_FILE_LOAD_HOOK;
  } else {
    return static_cast<jvmtiEvent>(e);
  }
}

struct EventMask {
  static constexpr size_t kEventsSize =
      static_cast<size_t>(ArtJvmtiEvent::kMaxEventTypeVal) -
      static_cast<size_t>(ArtJvmtiEvent::kMinEventTypeVal) + 1;
  std::bitset<kEventsSize> bit_set;

  static bool EventIsInRange(ArtJvmtiEvent event) {
    return event >= ArtJvmtiEvent::kMinEventTypeVal && event <= ArtJvmtiEvent::kMaxEventTypeVal;
  }

  void Set(ArtJvmtiEvent event, bool value = true) {
    DCHECK(EventIsInRange(event));
    bit_set.set(static_cast<size_t>(event) - static_cast<size_t>(ArtJvmtiEvent::kMinEventTypeVal),
                value);
  }

  bool Test(ArtJvmtiEvent event) const {
    DCHECK(EventIsInRange(event));
    return bit_set.test(
        static_cast<size_t>(event) - static_cast<size_t>(ArtJvmtiEvent::kMinEventTypeVal));
  }
};

struct EventMasks {
  // The globally enabled events.
  EventMask global_event_mask;

  // The per-thread enabled events.

  // It is not enough to store a Thread pointer, as these may be reused. Use the pointer and the
  // thread id.
  // Note: We could just use the tid like tracing does.
  using UniqueThread = std::pair<art::Thread*, uint32_t>;
  // TODO: Native thread objects are immovable, so we can use them as keys in an (unordered) map,
  //       if necessary.
  std::vector<std::pair<UniqueThread, EventMask>> thread_event_masks;

  // A union of the per-thread events, for fast-pathing.
  EventMask unioned_thread_event_mask;

  EventMask& GetEventMask(art::Thread* thread);
  EventMask* GetEventMaskOrNull(art::Thread* thread);
  void EnableEvent(art::Thread* thread, ArtJvmtiEvent event);
  void DisableEvent(art::Thread* thread, ArtJvmtiEvent event);
};

// Helper class for event handling.
class EventHandler {
 public:
  EventHandler();
  ~EventHandler();

  // Register an env. It is assumed that this happens on env creation, that is, no events are
  // enabled, yet.
  void RegisterArtJvmTiEnv(ArtJvmTiEnv* env);

  bool IsEventEnabledAnywhere(ArtJvmtiEvent event) const {
    if (!EventMask::EventIsInRange(event)) {
      return false;
    }
    return global_mask.Test(event);
  }

  jvmtiError SetEvent(ArtJvmTiEnv* env,
                      art::Thread* thread,
                      ArtJvmtiEvent event,
                      jvmtiEventMode mode);

  template <typename ...Args> ALWAYS_INLINE inline
  void DispatchEvent(art::Thread* thread, ArtJvmtiEvent event, Args... args) const;

 private:
  template <typename ...Args>
  ALWAYS_INLINE inline void GenericDispatchEvent(art::Thread* thread,
                                                 ArtJvmtiEvent event,
                                                 Args... args) const;
  template <typename ...Args>
  ALWAYS_INLINE inline void DispatchClassFileLoadHookEvent(art::Thread* thread,
                                                           ArtJvmtiEvent event,
                                                           Args... args) const;

  ALWAYS_INLINE
  static inline bool ShouldDispatch(ArtJvmtiEvent event, ArtJvmTiEnv* env, art::Thread* thread);

  void HandleEventType(ArtJvmtiEvent event, bool enable);

  // List of all JvmTiEnv objects that have been created, in their creation order.
  std::vector<ArtJvmTiEnv*> envs;

  // A union of all enabled events, anywhere.
  EventMask global_mask;

  std::unique_ptr<JvmtiAllocationListener> alloc_listener_;
  std::unique_ptr<JvmtiGcPauseListener> gc_pause_listener_;
};

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_EVENTS_H_
