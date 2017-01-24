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

#ifndef ART_RUNTIME_OPENJDKJVMTI_EVENTS_INL_H_
#define ART_RUNTIME_OPENJDKJVMTI_EVENTS_INL_H_

#include <array>

#include "events.h"

#include "art_jvmti.h"

namespace openjdkjvmti {

static inline ArtJvmtiEvent GetArtJvmtiEvent(ArtJvmTiEnv* env, jvmtiEvent e) {
  if (UNLIKELY(e == JVMTI_EVENT_CLASS_FILE_LOAD_HOOK)) {
    if (env->capabilities.can_retransform_classes) {
      return ArtJvmtiEvent::kClassFileLoadHookRetransformable;
    } else {
      return ArtJvmtiEvent::kClassFileLoadHookNonRetransformable;
    }
  } else {
    return static_cast<ArtJvmtiEvent>(e);
  }
}

namespace impl {

template <ArtJvmtiEvent kEvent, typename FnType>
ALWAYS_INLINE inline FnType* GetCallback(ArtJvmTiEnv* env);

static jvmtiEventCallbacks dummy;

#define GET_CALLBACK(name, enum_name) \
using name ## Type = decltype(dummy.name); \
template <> \
ALWAYS_INLINE inline name ## Type GetCallback<ArtJvmtiEvent::enum_name>(ArtJvmTiEnv* env) { \
  if (env->event_callbacks == nullptr) { \
    return nullptr; \
  } \
  return reinterpret_cast<name ## Type>(env->event_callbacks->name); \
}

GET_CALLBACK(VMInit,                  kVmInit)
GET_CALLBACK(VMDeath,                 kVmDeath)
GET_CALLBACK(ThreadStart,             kThreadStart)
GET_CALLBACK(ThreadEnd,               kThreadEnd)
GET_CALLBACK(ClassFileLoadHook,       kClassFileLoadHookRetransformable)
GET_CALLBACK(ClassFileLoadHook,       kClassFileLoadHookNonRetransformable)
GET_CALLBACK(ClassLoad,               kClassLoad)
GET_CALLBACK(ClassPrepare,            kClassPrepare)
GET_CALLBACK(VMStart,                 kVmStart)
GET_CALLBACK(Exception,               kException)
GET_CALLBACK(ExceptionCatch,          kExceptionCatch)
GET_CALLBACK(SingleStep,              kSingleStep)
GET_CALLBACK(FramePop,                kFramePop)
GET_CALLBACK(Breakpoint,              kBreakpoint)
GET_CALLBACK(FieldAccess,             kFieldAccess)
GET_CALLBACK(FieldModification,       kFieldModification)
GET_CALLBACK(MethodEntry,             kMethodEntry)
GET_CALLBACK(MethodExit,              kMethodExit)
GET_CALLBACK(NativeMethodBind,        kNativeMethodBind)
GET_CALLBACK(CompiledMethodLoad,      kCompiledMethodLoad)
GET_CALLBACK(CompiledMethodUnload,    kCompiledMethodUnload)
GET_CALLBACK(DynamicCodeGenerated,    kDynamicCodeGenerated)
GET_CALLBACK(DataDumpRequest,         kDataDumpRequest)
GET_CALLBACK(MonitorWait,             kMonitorWait)
GET_CALLBACK(MonitorWaited,           kMonitorWaited)
GET_CALLBACK(MonitorContendedEnter,   kMonitorContendedEnter)
GET_CALLBACK(MonitorContendedEntered, kMonitorContendedEntered)
GET_CALLBACK(ResourceExhausted,       kResourceExhausted)
GET_CALLBACK(GarbageCollectionStart,  kGarbageCollectionStart)
GET_CALLBACK(GarbageCollectionFinish, kGarbageCollectionFinish)
GET_CALLBACK(ObjectFree,              kObjectFree)
GET_CALLBACK(VMObjectAlloc,           kVmObjectAlloc)

}  // namespace impl

// C++ does not allow partial template function specialization. The dispatch for our separated
// ClassFileLoadHook event types is the same, so use this helper for code deduplication.
// TODO Locking of some type!
template <ArtJvmtiEvent kEvent>
inline void EventHandler::DispatchClassFileLoadHookEvent(art::Thread* thread,
                                                         JNIEnv* jnienv,
                                                         jclass class_being_redefined,
                                                         jobject loader,
                                                         const char* name,
                                                         jobject protection_domain,
                                                         jint class_data_len,
                                                         const unsigned char* class_data,
                                                         jint* new_class_data_len,
                                                         unsigned char** new_class_data) const {
  static_assert(kEvent == ArtJvmtiEvent::kClassFileLoadHookRetransformable ||
                kEvent == ArtJvmtiEvent::kClassFileLoadHookNonRetransformable, "Unsupported event");
  using FnType = void(jvmtiEnv*            /* jvmti_env */,
                      JNIEnv*              /* jnienv */,
                      jclass               /* class_being_redefined */,
                      jobject              /* loader */,
                      const char*          /* name */,
                      jobject              /* protection_domain */,
                      jint                 /* class_data_len */,
                      const unsigned char* /* class_data */,
                      jint*                /* new_class_data_len */,
                      unsigned char**      /* new_class_data */);
  jint current_len = class_data_len;
  unsigned char* current_class_data = const_cast<unsigned char*>(class_data);
  ArtJvmTiEnv* last_env = nullptr;
  for (ArtJvmTiEnv* env : envs) {
    if (ShouldDispatch<kEvent>(env, thread)) {
      jint new_len;
      unsigned char* new_data;
      FnType* callback = impl::GetCallback<kEvent, FnType>(env);
      callback(env,
               jnienv,
               class_being_redefined,
               loader,
               name,
               protection_domain,
               current_len,
               current_class_data,
               &new_len,
               &new_data);
      if (new_data != nullptr && new_data != current_class_data) {
        // Destroy the data the last transformer made. We skip this if the previous state was the
        // initial one since we don't know here which jvmtiEnv allocated it.
        // NB Currently this doesn't matter since all allocations just go to malloc but in the
        // future we might have jvmtiEnv's keep track of their allocations for leak-checking.
        if (last_env != nullptr) {
          last_env->Deallocate(current_class_data);
        }
        last_env = env;
        current_class_data = new_data;
        current_len = new_len;
      }
    }
  }
  if (last_env != nullptr) {
    *new_class_data_len = current_len;
    *new_class_data = current_class_data;
  }
}

template <ArtJvmtiEvent kEvent, typename ...Args>
inline void EventHandler::DispatchEvent(art::Thread* thread,
                                        Args... args) const {
  using FnType = void(jvmtiEnv*, Args...);
  for (ArtJvmTiEnv* env : envs) {
    if (ShouldDispatch<kEvent>(env, thread)) {
      FnType* callback = impl::GetCallback<kEvent, FnType>(env);
      if (callback != nullptr) {
        (*callback)(env, args...);
      }
    }
  }
}

// C++ does not allow partial template function specialization. The dispatch for our separated
// ClassFileLoadHook event types is the same, and in the DispatchClassFileLoadHookEvent helper.
// The following two DispatchEvent specializations dispatch to it.
template <>
inline void EventHandler::DispatchEvent<ArtJvmtiEvent::kClassFileLoadHookRetransformable>(
    art::Thread* thread,
    JNIEnv* jnienv,
    jclass class_being_redefined,
    jobject loader,
    const char* name,
    jobject protection_domain,
    jint class_data_len,
    const unsigned char* class_data,
    jint* new_class_data_len,
    unsigned char** new_class_data) const {
  return DispatchClassFileLoadHookEvent<ArtJvmtiEvent::kClassFileLoadHookRetransformable>(
      thread,
      jnienv,
      class_being_redefined,
      loader,
      name,
      protection_domain,
      class_data_len,
      class_data,
      new_class_data_len,
      new_class_data);
}
template <>
inline void EventHandler::DispatchEvent<ArtJvmtiEvent::kClassFileLoadHookNonRetransformable>(
    art::Thread* thread,
    JNIEnv* jnienv,
    jclass class_being_redefined,
    jobject loader,
    const char* name,
    jobject protection_domain,
    jint class_data_len,
    const unsigned char* class_data,
    jint* new_class_data_len,
    unsigned char** new_class_data) const {
  return DispatchClassFileLoadHookEvent<ArtJvmtiEvent::kClassFileLoadHookNonRetransformable>(
      thread,
      jnienv,
      class_being_redefined,
      loader,
      name,
      protection_domain,
      class_data_len,
      class_data,
      new_class_data_len,
      new_class_data);
}

template <ArtJvmtiEvent kEvent>
inline bool EventHandler::ShouldDispatch(ArtJvmTiEnv* env,
                                         art::Thread* thread) {
  bool dispatch = env->event_masks.global_event_mask.Test(kEvent);

  if (!dispatch && thread != nullptr && env->event_masks.unioned_thread_event_mask.Test(kEvent)) {
    EventMask* mask = env->event_masks.GetEventMaskOrNull(thread);
    dispatch = mask != nullptr && mask->Test(kEvent);
  }
  return dispatch;
}

inline void EventHandler::RecalculateGlobalEventMask(ArtJvmtiEvent event) {
  bool union_value = false;
  for (const ArtJvmTiEnv* stored_env : envs) {
    union_value |= stored_env->event_masks.global_event_mask.Test(event);
    union_value |= stored_env->event_masks.unioned_thread_event_mask.Test(event);
    if (union_value) {
      break;
    }
  }
  global_mask.Set(event, union_value);
}

inline bool EventHandler::NeedsEventUpdate(ArtJvmTiEnv* env,
                                           const jvmtiCapabilities& caps,
                                           bool added) {
  ArtJvmtiEvent event = added ? ArtJvmtiEvent::kClassFileLoadHookNonRetransformable
                              : ArtJvmtiEvent::kClassFileLoadHookRetransformable;
  return caps.can_retransform_classes == 1 &&
      IsEventEnabledAnywhere(event) &&
      env->event_masks.IsEnabledAnywhere(event);
}

inline void EventHandler::HandleChangedCapabilities(ArtJvmTiEnv* env,
                                                    const jvmtiCapabilities& caps,
                                                    bool added) {
  if (UNLIKELY(NeedsEventUpdate(env, caps, added))) {
    env->event_masks.HandleChangedCapabilities(caps, added);
    if (caps.can_retransform_classes == 1) {
      RecalculateGlobalEventMask(ArtJvmtiEvent::kClassFileLoadHookRetransformable);
      RecalculateGlobalEventMask(ArtJvmtiEvent::kClassFileLoadHookNonRetransformable);
    }
  }
}

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_EVENTS_INL_H_
