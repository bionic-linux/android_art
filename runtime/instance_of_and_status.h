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

#ifndef ART_RUNTIME_INSTANCE_OF_AND_STATUS_H_
#define ART_RUNTIME_INSTANCE_OF_AND_STATUS_H_

namespace art {

// The struct combines the Status byte and the 56-bit bitstring into one structure.
struct InstanceOfAndStatus {
  uint64_t data;

  // The five possible states of the bitstring of each class.
  // Uninitialized: We have not done anything on the bitstring.
  // Initialized: The class has inherited its bitstring from its super, should be exactly the
  // same value except the incremental value of its own depth.
  // The difference between Initialized and Assigned is that the latter one has caused the
  // incremental level of its super class to increase.
  // Assigned: The class has been assigend a bitstring.
  // Overflowed: The class is overflowed, either too wide, too deep, or being a descendant
  // of an overflowed class.
  // Possible transitions:
  // 0->1 3, 1->2 3.
  enum class BitstringState {
    kBitstringUninitialized = 0,
    kBitstringInitialized = 1,
    kBitstringAssigned = 2,
    kBitstringOverflowed = 3,
  };

  InstanceOfAndStatus() : data(0) {}

  explicit InstanceOfAndStatus(uint64_t value) : data(value) {}

  uint64_t GetData() const {
    return data;
  }

  void SetData(uint64_t now) {
    data = now;
  }

  uint64_t GetBitstring() const {
    return GetFirst56Bits(data);
  }

  int8_t GetStatus() const {
    return static_cast<int8_t>(GetLast8Bits(data));
  }

  void SetBitstring(uint64_t now) {
    data = UpdateFirst56Bits(data, now);
  }

  void SetStatus(uint64_t now) {
    data = UpdateLast8Bits(data, now);
  }

  // Check if the bitstring is assigned.
  bool IsAssigned(size_t dep) const {
    if (dep > kMaxBitstringDepth) {
      return false;
    }
    if (dep == 0) {
      return GetBitstring() > 0;
    }
    return GetBitsByDepth(data, dep) > 0;
  }

  // Check if the bitstring is overflowed.
  bool IsOverflowed(size_t dep) const {
    if (dep > kMaxBitstringDepth) {
      return true;
    }
    if (IsAssigned(dep)) {
      return false;
    }
    return (data >> 8) & 1;
  }

  bool IsUninited() const {
    return GetBitstring() == 0;
  }

  void MarkOverflowed() {
    data |= (1 << 8);
  }

  // Check if we add a child to the class, will it be overflowed?
  bool CheckChildrenOverflowed(size_t dep) {
    if (dep >= kMaxBitstringDepth) {
      return true;
    }
    return (data >> 8) & 1;
  }

  // Get the state from the current bitstring.
  BitstringState GetBitstringState(size_t dep) const {
    // Check Assigend first, since the overflow bit
    // can be set to 1 if the children overflowed.
    if (IsAssigned(dep)) {
      return BitstringState::kBitstringAssigned;
    }
    // Note that each bitstring which is intialized will have the non-zero incremental
    // value reserved for its children, so the initialized bitstring of the depth 1
    // won't be all zero either.
    if (IsUninited()) {
      return BitstringState::kBitstringUninitialized;
    }
    if (IsOverflowed(dep)) {
      return BitstringState::kBitstringOverflowed;
    }
    return BitstringState::kBitstringInitialized;
  }

  uint64_t GetIncrementalValue(size_t dep) {
    return GetBitsByDepth(data, dep);
  }

  void SetIncrementalValue(uint64_t inc, size_t dep) {
    SetBitstring(UpdateBitsByDepth(data, inc, dep));
  }

  inline uint64_t GetBitstringPrefix(size_t dep) {
    return GetRangedBits(data, 0, BitstringLength[dep]);
  }

  void InitializeBitstring(uint64_t super, size_t dep) {
    if (dep > 0 && dep <= kMaxBitstringDepth) {
      // Clear the inherited incremental value of its depth.
      super = UpdateBitsByDepth(super, 0, dep);
    }
    if (dep < kMaxBitstringDepth) {
      // Preset the next level's incremental value to 1.
      super = UpdateBitsByDepth(super, 1, dep + 1);
    }
    SetBitstring(super);
  }

  bool IsValidFastIsSubClass(InstanceOfAndStatus target, int dep) {
    return target.IsAssigned(dep) && !IsUninited();
  }

  // The real fast path of IsSubClass.
  bool IsSubClass(InstanceOfAndStatus target, int dep) {
    DCHECK(IsValidFastIsSubClass(target, dep));
    uint64_t prefix = target.GetBitstringPrefix(dep);
    uint64_t thisprefix = GetBitstringPrefix(dep);
    return thisprefix == prefix;
  }
};

}  // namespace art

#endif  // ART_RUNTIME_INSTANCE_OF_AND_STATUS_H_
