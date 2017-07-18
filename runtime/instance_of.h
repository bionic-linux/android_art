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

#ifndef ART_RUNTIME_INSTANCE_OF_H_
#define ART_RUNTIME_INSTANCE_OF_H_

namespace art {

// The struct combines the Status byte and the 56-bit bitstring into one structure.
struct InstanceOfAndStatus {
  uint64_t data;

  // The five possible states of the bitstring of each class.
  // Uninitialized: We have not done anything on the bitstring.
  // Overflowed uninitialized: Unintialized but also overflowed (too depp case).
  // This state is set to differenciate the case with normal overflowed case, since if
  // depth exceeds kMaxBitstringDepth, it is checked before CheckInitialized.
  // Initialized: The class has inherited its bitstring from its super, should be exactly the
  // same value except the incremental value of its own depth.
  // The difference between Initialized and Assigned is that the latter one has caused the
  // incremental level of its super class to increase.
  // Assigned: The class has been assigend a bitstring.
  // Overflowed: The class is overflowed, either too wide, too deep, or being a descendant
  // of an overflowed class.
  // Possible transitions:
  // 0->2, 1->4, 2->3,4.
  enum class State {
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
    data = GetUpdatedFirst56Bits(data, now);
  }

  void SetStatus(uint64_t now) {
    data = GetUpdatedLast8Bits(data, now);
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
  State GetState(size_t dep) const {
    // Check Assigend first, since the overflow bit
    // can be set to 1 if the children overflowed.
    if (IsAssigned(dep)) {
      return State::kBitstringAssigned;
    }
    // Note that each bitstring which is intialized will have the non-zero incremental
    // value reserved for its children, so the initialized bitstring of the depth 1
    // won't be all zero either.
    if (IsUninited()) {
      return State::kBitstringUninitialized;
    }
    if (IsOverflowed(dep)) {
      return State::kBitstringOverflowed;
    }
    return State::kBitstringInitialized;
  }

  uint64_t GetIncrementalValue(size_t dep) {
    return GetBitsByDepth(data, dep);
  }

  void SetIncrementalValue(uint64_t inc, size_t dep) {
    SetBitstring(GetUpdatedBitsByDepth(data, inc, dep));
  }

  inline uint64_t GetBitstringPrefix(size_t dep) {
    return GetRangedBits(data, 0, BitstringLength[dep]);
  }

  void InitializeBitstring(uint64_t super, size_t dep) {
    if (dep > 0 && dep <= kMaxBitstringDepth) {
      super = GetUpdatedBitsByDepth(super, 0, dep);
    }
    if (dep < kMaxBitstringDepth) {
      super = GetUpdatedBitsByDepth(super, 1, dep + 1);
    }
    SetBitstring(super);
  }

  // The real fast path of IsSubClass.
  // Returns a pair (valid_check, is_sub_class) where:
  // - valid_check: whether to accept this check or to do a slow path IsSubClass
  // - is_sub_class: if valid_check is true, then this will return true if "this"
  //                 is a subclass of target.
  std::pair<bool, bool> IsSubClass(InstanceOfAndStatus target, int dep) {
    if (!target.IsAssigned(dep) || IsUninited()) {
      return std::make_pair(false, false);
    }

    uint64_t prefix = target.GetBitstringPrefix(dep);
    uint64_t thisprefix = GetBitstringPrefix(dep);
    return std::make_pair(true, thisprefix == prefix);
  }
};

}  // namespace art

#endif  // ART_RUNTIME_INSTANCE_OF_H_
