/* Copyright (C) 2016 The Android Open Source Project
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

#include "object_tagging.h"

#include <limits>

#include "art_jvmti.h"
#include "events-inl.h"
#include "jvmti_weak_table-inl.h"

namespace openjdkjvmti {

// Instantiate for jlong = JVMTI tags.
template class JvmtiWeakTable<jlong>;

bool ObjectTagTable::SetLocked(art::Thread* self,
                               art::mirror::Object* obj,
                               jlong new_tag,
                               /* out */ jlong* old_tag) {
  if (new_tag == 0) {
    jlong tmp;
    return RemoveLocked(self, obj, &tmp);
  }
  bool ret = JvmtiWeakTable<jlong>::SetLocked(self, obj, new_tag, old_tag);
  if (ret && new_tag != *old_tag) {
    // Update reverse table.
    auto range_start_end = reverse_tagged_objects_.equal_range(*old_tag);
    for (auto it = range_start_end.first; it != range_start_end.second; ++it) {
      // Read without a read barrier. Worst case the next sweep will take care of this. Retrieval
      // is using a full read barrier, so that's fine.
      if (it->second.template Read<art::kWithoutReadBarrier>() == obj) {
        reverse_tagged_objects_.erase(it);
        break;
      }
    }
  }
  if (!ret || new_tag != *old_tag) {
    reverse_tagged_objects_.emplace(new_tag, art::GcRoot<art::mirror::Object>(obj));
  }

  return ret;
}

bool ObjectTagTable::ObjectTagTable::RemoveLocked(art::Thread* self,
                                                  art::mirror::Object* obj,
                                                  /* out */ jlong* tag) {
  bool ret = JvmtiWeakTable<jlong>::RemoveLocked(self, obj, tag);
  if (ret) {
    // Update reverse table.
    auto range_start_end = reverse_tagged_objects_.equal_range(*tag);
    for (auto it = range_start_end.first; it != range_start_end.second; ++it) {
      // Read without a read barrier. Worst case the next sweep will take care of this. Retrieval
      // is using a full read barrier, so that's fine.
      if (it->second.template Read<art::kWithoutReadBarrier>() == obj) {
        reverse_tagged_objects_.erase(it);
        break;
      }
    }
  }
  return ret;
}

void ObjectTagTable::Sweep(art::IsMarkedVisitor* visitor) {
  JvmtiWeakTable<jlong>::Sweep(visitor);
  SweepReverseTable(visitor);
}

void ObjectTagTable::SweepReverseTable(art::IsMarkedVisitor* visitor) {
  art::Thread* self = art::Thread::Current();
  art::MutexLock mu(self, allow_disallow_lock_);

  // We optimistically hope that elements will still be well-distributed when re-inserting them.
  // So play with the map mechanics, and postpone rehashing. This avoids the need of a side
  // vector and two passes.
  float original_max_load_factor = reverse_tagged_objects_.max_load_factor();
  reverse_tagged_objects_.max_load_factor(std::numeric_limits<float>::max());
  // For checking that a max load-factor actually does what we expect.
  size_t original_bucket_count = reverse_tagged_objects_.bucket_count();

  for (auto it = reverse_tagged_objects_.begin(); it != reverse_tagged_objects_.end();) {
    DCHECK(!it->second.IsNull());
    art::mirror::Object* original_obj = it->second.template Read<art::kWithoutReadBarrier>();
    art::mirror::Object* target_obj = visitor->IsMarked(original_obj);
    if (original_obj != target_obj) {
      jlong tag = it->first;
      it = reverse_tagged_objects_.erase(it);
      if (target_obj != nullptr) {
        reverse_tagged_objects_.emplace(tag, art::GcRoot<art::mirror::Object>(target_obj));
        DCHECK_EQ(original_bucket_count, reverse_tagged_objects_.bucket_count());
      }
      continue;  // Iterator was implicitly updated by erase.
    }
    it++;
  }

  reverse_tagged_objects_.max_load_factor(original_max_load_factor);
  // TODO: consider rehash here.
}

bool ObjectTagTable::DoesHandleNullOnSweep() {
  return event_handler_->IsEventEnabledAnywhere(ArtJvmtiEvent::kObjectFree);
}
void ObjectTagTable::HandleNullSweep(jlong tag) {
  event_handler_->DispatchEvent<ArtJvmtiEvent::kObjectFree>(jvmti_env_, nullptr, tag);
}

art::mirror::Object* ObjectTagTable::GetObjectForTag(jlong tag) {
  art::Thread* self = art::Thread::Current();
  art::MutexLock mu(self, allow_disallow_lock_);

  auto it = reverse_tagged_objects_.find(tag);
  if (it != reverse_tagged_objects_.end()) {
    return it->second.template Read<art::kWithReadBarrier>();
  }

  return nullptr;
}

}  // namespace openjdkjvmti
