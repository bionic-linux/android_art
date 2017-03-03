/* Copyright (C) 2017 The Android Open Source Project
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This file implements interfaces from the file jvmti.h. This implementation
 * is licensed under the same terms as the file jvmti.h.  The
 * copyright and license information for the file jvmti.h follows.
 *
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#ifndef ART_RUNTIME_OPENJDKJVMTI_JVMTI_WEAK_TABLE_H_
#define ART_RUNTIME_OPENJDKJVMTI_JVMTI_WEAK_TABLE_H_

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

  template <typename Storage, class Allocator = std::allocator<T>>
  struct ReleasableContainer;

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

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_JVMTI_WEAK_TABLE_H_
