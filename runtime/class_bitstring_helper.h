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

#ifndef ART_RUNTIME_CLASS_BITSTRING_HELPER_H_
#define ART_RUNTIME_CLASS_BITSTRING_HELPER_H_

namespace art {
// The maximum class depth that may be assigned a bitstring.
// java.lang.Object is at depth 0.
constexpr int32_t kMaxBitstringDepth = 6;

// The fixed bitstring length for each depth.
// BitstringLength[2] means the length for depth 1 plus length for depth 2.
// Bug: https://buganizer.corp.google.com/issues/64692057
constexpr uint32_t BitstringLength[kMaxBitstringDepth + 1] = {0, 12, 22, 32, 41, 49, 55};

inline uint64_t GetFirst56Bits(uint64_t mask) {
  return (mask >> 8) << 8;
}

inline uint64_t GetLast8Bits(uint64_t mask) {
  return mask & 0xff;
}

inline uint64_t UpdateFirst56Bits(uint64_t old, uint64_t cur) {
  return GetLast8Bits(old) | GetFirst56Bits(cur);
}

inline uint64_t UpdateLast8Bits(uint64_t old, uint64_t cur) {
  return GetFirst56Bits(old) | GetLast8Bits(cur);
}

// Get bitstring from l-th bit (inclusive) to r-th bit (exclusive)
// of cur. The index starts from left.
inline uint64_t GetRangedBits(uint64_t cur, int l, int r) {
  return (cur >> (64 - r)) & (((uint64_t)1 << (r - l)) - 1);
}

// Change the bitstring from l-th bit (inclusive) to r-th bit (exclusive)
// of old to cur.
inline uint64_t GetUpdatedRangedBits(uint64_t old, int l, int r, uint64_t cur) {
  uint64_t mask = ((((uint64_t)1 << (r - l)) - 1) << (64 - r));
  return (old ^ (old & mask)) | ((cur << (64 - r)) & mask);
}

// Get the bitstring for the specific depth offset.
inline uint64_t GetBitsByDepth(uint64_t cur, int dep) {
  return GetRangedBits(cur, BitstringLength[dep - 1], BitstringLength[dep]);
}

// Update the bitstring for the specific depth offset.
inline uint64_t UpdateBitsByDepth(uint64_t old, uint64_t cur, int dep) {
  return GetUpdatedRangedBits(old, BitstringLength[dep - 1], BitstringLength[dep], cur);
}

}  // namespace art

#endif  // ART_RUNTIME_CLASS_BITSTRING_HELPER_H_
