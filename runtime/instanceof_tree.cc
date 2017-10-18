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

#include "instanceof_tree.h"

#include "mirror/class-inl.h"
#include "object_lock.h"
namespace art {

  // FIXME: This file had a bunch of hacks to it, in order to debug a race issue.
  // It needs to be absolutely cleaned up before merging anything.

  static bool CasFieldWeakSequentiallyConsistent32(const InstanceOfTree::ClassT& klass,
                                     MemberOffset offset,
                                     int32_t old_value,
                                     int32_t new_value)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (Runtime::Current() != nullptr && Runtime::Current()->IsActiveTransaction()) {
      return klass->template CasFieldWeakSequentiallyConsistent32</*kTransactionActive*/true>(offset,
                                                                                              old_value,
                                                                                              new_value);
    } else {
      return klass->template CasFieldWeakSequentiallyConsistent32</*kTransactionActive*/false>(offset,
                                                                                               old_value,
                                                                                               new_value);
    }
  }

  void InstanceOfTree::WriteStatus(const InstanceOfTree::ClassT& klass, ClassStatus status)
      REQUIRES_SHARED(Locks::mutator_lock_) {

    InstanceOfAndStatusNew new_value;
    ClassStatus old_status;
    do {
      // TODO: Atomic compare-and-swap does not update the 'expected' parameter,
      // so we have to read it as a separate step instead.
      InstanceOfAndStatusNew old_value = ReadField(klass);
      old_status = old_value.status_;

      new_value = old_value;
      new_value.status_ = status;

      if (old_status > 0 && status > 0) {
        DCHECK_GE(status, old_status) << "ClassStatus went back in time for " << klass->PrettyClass();
      }

      if (CasFieldWeakSequentiallyConsistent32(klass,
                                klass->StatusOffset(),
                                old_value.int32_alias_,
                                new_value.int32_alias_)) {
        break;
      }
    } while (true);

    // racy dcheck below.
    // could've raced with WriteField.
    if (kIsDebugBuild) {
      InstanceOfAndStatusNew recently_written_field = ReadField(klass);
      DCHECK_EQ(0, memcmp(&recently_written_field, &new_value, sizeof(new_value)))
          << "expected: " << new_value.int32_alias_
          << " actual: " << recently_written_field.int32_alias_;
    }
  }

  // Note: Can't use ObjPtr here since image_writer copies are not in low-4gb.
  InstanceOfAndStatusNew InstanceOfTree::ReadField(const InstanceOfTree::ClassT& klass)
      REQUIRES_SHARED(Locks::mutator_lock_) {

    InstanceOfAndStatusNew current_ios;

    int32_t int32_data = klass->GetField32Volatile(klass->StatusOffset());
    current_ios.int32_alias_ = int32_data;

    if (kIsDebugBuild) {
      InstanceOfAndStatusNew tmp;
      memcpy(&tmp, &int32_data, sizeof(tmp));
      DCHECK_EQ(0, memcmp(&tmp, &current_ios, sizeof(tmp))) << int32_data;
    }
    return current_ios;
  }

  template <typename T>
  uint32_t AsUint32Copy(T value) {
    static_assert(sizeof(T) == sizeof(uint32_t), "");

    uint32_t data;
    memcpy(&data, &value, sizeof(data));

    return data;
  }


  void InstanceOfTree::WriteField(const InstanceOfTree::ClassT& klass, const InstanceOfAndStatusNew& new_ios)
      REQUIRES(Locks::bitstring_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_) {

    // Use a "CAS" to write the InstanceOfData in the class.
    // Although we have exclusive access to the bitstrings, because
    // status and InstanceOf share the same word, another thread could
    // potentially overwrite that word still.

    InstanceOfAndStatusNew new_value;
    ClassStatus old_status;
    InstanceOfAndStatusNew full_old;
    while (true) {
      // TODO: Atomic compare-and-swap does not update the 'expected' parameter,
      // so we have to read it as a separate step instead.
      InstanceOfAndStatusNew old_value = ReadField(klass);

      full_old = old_value;
      old_status = old_value.status_;

      new_value = old_value;
      new_value.instance_of_ = new_ios.instance_of_;

      if (kIsDebugBuild) {
        int32_t int32_data = 0;
        memcpy(&int32_data, &new_value, sizeof(int32_t));
        DCHECK_EQ(int32_data, new_value.int32_alias_) << int32_data;

        DCHECK_EQ(old_status, new_value.status_)
          << "full new: " << AsUint32Copy(new_value)
          << ", full old: " << AsUint32Copy(full_old);
      }

      if (CasFieldWeakSequentiallyConsistent32(klass,
                                 klass->StatusOffset(),
                                 old_value.int32_alias_,
                                 new_value.int32_alias_)) {
        break;
      }
    }

    // racy dcheck below.
    // could've raced with WriteStatus.
    if (kIsDebugBuild) {
      InstanceOfAndStatusNew recently_written_field = ReadField(klass);
      DCHECK_EQ(0, memcmp(&recently_written_field, &new_value, sizeof(new_value)))
          << "expected: " << new_value.int32_alias_
          << " actual: " << recently_written_field.int32_alias_;

      // Ensure that 'Status' did not change out from under us.
      DCHECK_EQ(old_status, recently_written_field.status_)
          << "full new: " << AsUint32Copy(recently_written_field)
          << ", full old: " << AsUint32Copy(full_old);
    }
  }

};  // namespace art
