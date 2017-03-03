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

#ifndef ART_RUNTIME_OPENJDKJVMTI_OBJECT_TAGGING_H_
#define ART_RUNTIME_OPENJDKJVMTI_OBJECT_TAGGING_H_

#include <unordered_map>

#include "base/mutex.h"
#include "gc/system_weak.h"
#include "gc_root-inl.h"
#include "globals.h"
#include "jvmti.h"
#include "mirror/object.h"
#include "thread-inl.h"

namespace openjdkjvmti {

class EventHandler;

template <typename T>
class JvmtiWeakTable : public art::gc::SystemWeakHolder {
 public:
  JvmtiWeakTable()
      : art::gc::SystemWeakHolder(kTaggingLockLevel),
        update_since_last_sweep_(false) {
  }

  void Add(art::mirror::Object* obj, T tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);

  bool Remove(art::mirror::Object* obj, T* tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);
  bool RemoveLocked(art::mirror::Object* obj, T* tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_);

  virtual bool Set(art::mirror::Object* obj, T tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);
  virtual bool SetLocked(art::mirror::Object* obj, T tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_);

  bool GetTag(art::mirror::Object* obj, T* result)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_) {
    art::Thread* self = art::Thread::Current();
    art::MutexLock mu(self, allow_disallow_lock_);
    Wait(self);

    return GetTagLocked(self, obj, result);
  }
  bool GetTagLocked(art::mirror::Object* obj, T* result)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_) {
    art::Thread* self = art::Thread::Current();
    allow_disallow_lock_.AssertHeld(self);
    Wait(self);

    return GetTagLocked(self, obj, result);
  }

  void Sweep(art::IsMarkedVisitor* visitor)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);

  jvmtiError GetTaggedObjects(jvmtiEnv* jvmti_env,
                              jint tag_count,
                              const T* tags,
                              jint* count_ptr,
                              jobject** object_result_ptr,
                              T** tag_result_ptr)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);

  void Lock() ACQUIRE(allow_disallow_lock_);
  void Unlock() RELEASE(allow_disallow_lock_);
  void AssertLocked() ASSERT_CAPABILITY(allow_disallow_lock_);

 protected:
  virtual bool DoesHandleNullOnSweep() {
    return false;
  }
  virtual void HandleNullSweep(T tag ATTRIBUTE_UNUSED) {}

 private:
  bool SetLocked(art::Thread* self, art::mirror::Object* obj, T tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_);

  bool RemoveLocked(art::Thread* self, art::mirror::Object* obj, T* tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_);

  bool GetTagLocked(art::Thread* self, art::mirror::Object* obj, T* result)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_) {
    auto it = tagged_objects_.find(art::GcRoot<art::mirror::Object>(obj));
    if (it != tagged_objects_.end()) {
      *result = it->second;
      return true;
    }

    if (art::kUseReadBarrier &&
        self != nullptr &&
        self->GetIsGcMarking() &&
        !update_since_last_sweep_) {
      return GetTagSlowPath(self, obj, result);
    }

    return false;
  }

  // Slow-path for GetTag. We didn't find the object, but we might be storing from-pointers and
  // are asked to retrieve with a to-pointer.
  bool GetTagSlowPath(art::Thread* self, art::mirror::Object* obj, T* result)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_);

  // Update the table by doing read barriers on each element, ensuring that to-space pointers
  // are stored.
  void UpdateTableWithReadBarrier()
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_);

  template <bool kHandleNull>
  void SweepImpl(art::IsMarkedVisitor* visitor)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);

  enum TableUpdateNullTarget {
    kIgnoreNull,
    kRemoveNull,
    kCallHandleNull
  };

  template <typename Updater, TableUpdateNullTarget kTargetNull>
  void UpdateTableWith(Updater& updater)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_);

  struct HashGcRoot {
    size_t operator()(const art::GcRoot<art::mirror::Object>& r) const
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      return reinterpret_cast<uintptr_t>(r.Read<art::kWithoutReadBarrier>());
    }
  };

  struct EqGcRoot {
    bool operator()(const art::GcRoot<art::mirror::Object>& r1,
                    const art::GcRoot<art::mirror::Object>& r2) const
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      return r1.Read<art::kWithoutReadBarrier>() == r2.Read<art::kWithoutReadBarrier>();
    }
  };

  // The tag table is used when visiting roots. So it needs to have a low lock level.
  static constexpr art::LockLevel kTaggingLockLevel =
      static_cast<art::LockLevel>(art::LockLevel::kAbortLock + 1);

  std::unordered_map<art::GcRoot<art::mirror::Object>,
                     T,
                     HashGcRoot,
                     EqGcRoot> tagged_objects_
      GUARDED_BY(allow_disallow_lock_)
      GUARDED_BY(art::Locks::mutator_lock_);
  // To avoid repeatedly scanning the whole table, remember if we did that since the last sweep.
  bool update_since_last_sweep_;
};

class ObjectTagTable : public JvmtiWeakTable<jlong> {
 public:
  explicit ObjectTagTable(EventHandler* event_handler) : event_handler_(event_handler) {
  }

  bool Set(art::mirror::Object* obj, jlong tag) OVERRIDE
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);
  bool SetLocked(art::mirror::Object* obj, jlong tag) OVERRIDE
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_);

  jlong GetTagOrZero(art::mirror::Object* obj)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_) {
    jlong tmp = 0;
    GetTag(obj, &tmp);
    return tmp;
  }
  jlong GetTagOrZeroLocked(art::mirror::Object* obj)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_) {
    jlong tmp = 0;
    GetTagLocked(obj, &tmp);
    return tmp;
  }

 protected:
  bool DoesHandleNullOnSweep() OVERRIDE;
  void HandleNullSweep(jlong tag) OVERRIDE;

 private:
  EventHandler* event_handler_;
};

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_OBJECT_TAGGING_H_
