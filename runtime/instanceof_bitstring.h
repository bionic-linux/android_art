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

#ifndef ART_RUNTIME_INSTANCEOF_BITSTRING_H_
#define ART_RUNTIME_INSTANCEOF_BITSTRING_H_

#include "base/bit_struct.h"
#include "base/bit_utils.h"

#include <ostream>

namespace art {

/**
 * Abstraction over a single character of a BitString.
 *
 * This is only intended for reading/writing into temporaries, as the representation is
 * inefficient for memory (it uses a word for the character and another word for the bitlength).
 *
 * See also BitString below.
 */
struct BitChar {
  using StorageType = uint32_t;
  static_assert(std::is_unsigned<StorageType>::value, "BitChar::StorageType must be unsigned");

  // BitChars are always zero-initialized by default. Equivalent to BitChar(0,0).
  BitChar() { data_ = 0; bitlength_ = 0; }

  // Create a new BitChar whose data bits can be at most bitlength.
  BitChar(StorageType data, size_t bitlength)
      : data_(data), bitlength_(bitlength) {
    // All bits higher than bitlength must be set to 0.
    DCHECK_EQ(0u, data & ~MaskLeastSignificant(bitlength_))
        << "BitChar data out of range, data: " << data << ", bitlength: " << bitlength;
  }

  // What is the bitlength constraint for this character?
  // (Data could use less bits, but this is the maximum bit capacity at that BitString position).
  size_t GetBitLength() const {
    return bitlength_;
  }

  // Is there any capacity in this BitChar to store any data?
  bool IsEmpty() const {
    return bitlength_ == 0;
  }

  explicit operator StorageType() const {
    return data_;
  }

  bool operator==(StorageType storage) const {
    return data_ == storage;
  }

  bool operator!=(StorageType storage) const {
    return !(*this == storage);
  }

  // Compare equality against another BitChar. Note: bitlength is ignored.
  bool operator==(const BitChar& other) const {
    return data_ == other.data_;
  }

  // Compare non-equality against another BitChar. Note: bitlength is ignored.
  bool operator!=(const BitChar& other) const {
    return !(*this == other);
  }

  // Add a BitChar with an integer. The resulting BitChar's data must still fit within this
  // BitChar's bit length.
  BitChar operator+(StorageType storage) const {
    return BitChar(data_ + storage, bitlength_);
  }

  // Get the maximum representible value with the same bitlength.
  // (Useful to figure out the maximum value for this BitString position.)
  BitChar MaximumValue() const {
    StorageType maximimum_data = MaxInt<StorageType>(bitlength_);
    return BitChar(maximimum_data, bitlength_);
  }

 private:
  StorageType data_;
  size_t bitlength_;
};

// Print e.g. "BitChar<10>(123)" where 10=bitlength, 123=data.
inline std::ostream& operator<<(std::ostream& os, const BitChar& bc) {
  os << "BitChar<" << bc.GetBitLength() << ">(" << static_cast<BitChar::StorageType>(bc) << ")";
  return os;
}

/**
 *                           BitString
 *
 * MSB                                                      LSB
 *  +------------+------------+------------+-----+------------+
 *  |            |            |            |     |            |
 *  |   Char0    |    Char1   |   Char2    | ... |   CharN    |
 *  |            |            |            |     |            |
 *  +------------+------------+------------+-----+------------+
 *   <- len[0] -> <- len[1] -> <- len[2] ->  ...  <- len[N] ->
 *
 * Stores up to "N+1" characters in a subset of a machine word. Each character has a different
 * bitlength, as defined by len[pos]. This BitString can be nested inside of a BitStruct
 * (see e.g. InstanceOfAndStatus).
 *
 * Definitions:
 *
 *  "ABCDE...K"       := [A,B,C,D,E, ... K] + [0]*(idx(K)-N).
 *  MaxBitstringLen   := N+1
 *  StrLen(Bitstring) := MaxBitStringLen - | forall char ∈ CharI..CharN: char != 0 |
 *  Bitstring[N]      := CharN
 *  Bitstring[I..N)   := [CharI, CharI+1, ... CharN-1]
 *
 * (These are used by the InstanceOf definitions and invariants, see instanceof.h)
 */
struct BitString {
  using StorageType = BitChar::StorageType;

  // As this is meant to be used only with "InstanceOf",
  // the bitlengths and the maximum string length is tuned by maximizing the coverage of "Assigned"
  // bitstrings for instance-of and check-cast targets during Optimizing compilation.
  static constexpr size_t kBitSizeAtPosition[] = {12, 3, 8};          // len[] from header docs.
  static constexpr size_t kCapacity = arraysize(kBitSizeAtPosition);  // MaxBitstringLen above.

  // How many bits are needed to represent BitString[0..position)?
  static constexpr size_t GetBitLengthTotalAtPosition(size_t position) {
    size_t idx = 0;
    size_t sum = 0;
    while (idx < position && idx < kCapacity) {
      sum += kBitSizeAtPosition[idx];
      ++idx;
    }

    return sum;
  }

  // What is the least-significant-bit for a position?
  // (e.g. to use with BitField{Insert,Extract,Clear}.)
  static constexpr size_t GetLsbForPosition(size_t position) {
    constexpr size_t kMaximumBitLength = GetBitLengthTotalAtPosition(kCapacity);

    return kMaximumBitLength - GetBitLengthTotalAtPosition(position + 1u);
  }

  // How many bits are needed for a BitChar at the position?
  // Returns 0 if the position is out of range.
  static constexpr size_t MaybeGetBitLengthAtPosition(size_t position) {
    if (position >= kCapacity) {
      return 0;
    }
    return kBitSizeAtPosition[position];
  }

  // Read a bitchar at some index within the capacity.
  // See also "BitString[N]" in the doc header.
  BitChar operator[](size_t idx) const {
    DCHECK_LT(idx, kCapacity);

    StorageType data =
        BitFieldExtract(storage_,
                        GetLsbForPosition(idx), kBitSizeAtPosition[idx]);

    return BitChar(data, kBitSizeAtPosition[idx]);
  }

  // Overwrite a bitchar at a position with a new one.
  //
  // The `bitchar` capacity must be no more than the maximum capacity for that position.
  void SetAt(size_t idx, BitChar bitchar) {
    DCHECK_LT(idx, kCapacity);
    DCHECK_LE(bitchar.GetBitLength(), kBitSizeAtPosition[idx]);

    // Read the bitchar: Bits > bitlength in bitchar are defined to be 0.
    storage_ = BitFieldInsert(storage_,
                              static_cast<StorageType>(bitchar),
                              GetLsbForPosition(idx),
                              kBitSizeAtPosition[idx]);
  }

  // How many characters are there in this bitstring?
  // Trailing 0s are ignored, but 0s inbetween are counted.
  // See also "StrLen(BitString)" in the doc header.
  size_t Length() const {
    size_t size = 0;
    size_t i;
    for (i = kCapacity - 1u; ; --i) {
      BitChar bc = (*this)[i];
      if (bc != 0u) {
        break;
      }

      ++size;
      if (i == 0u) {
        break;
      }
    }

    return kCapacity - size;
  }

  // Cast to the underlying integral storage type.
  explicit operator StorageType() const {
    return storage_;
  }

  // Get the # of bits this would use if it was nested inside of a BitStruct.
  static constexpr size_t BitStructSizeOf() {
    size_t total = 0u;
    for (size_t size : kBitSizeAtPosition) {
      total += size;
    }
    return total;
  }

  BitString() = default;

  // Efficient O(1) comparison: Equal if both bitstring words are the same.
  bool operator==(const BitString& other) const {
    return storage_ == other.storage_;
  }

  // Efficient O(1) negative comparison: Not-equal if both bitstring words are different.
  bool operator!=(const BitString& other) const {
    return !(*this == other);
  }

  // Remove all BitChars starting at end.
  // Returns the BitString[0..end) substring as a copy.
  // See also "BitString[I..N)" in the doc header.
  BitString Truncate(size_t end) {
    DCHECK_GE(kCapacity, end);
    BitString copy = *this;

    for (size_t idx = end; idx < kCapacity; ++idx) {
      StorageType data =
          BitFieldClear(copy.storage_,
                        GetLsbForPosition(idx),
                        kBitSizeAtPosition[idx]);
      copy.storage_ = data;
    }

    return copy;
  }

 private:
  friend std::ostream& operator<<(std::ostream& os, const BitString& bit_string);

  // Data is stored with the "highest" position in the least-significant-bit.
  // As positions approach 0, the bits are stored with increasing significance.
  StorageType storage_;
};

static_assert(BitSizeOf<BitString::StorageType>() >=
                  BitString::GetBitLengthTotalAtPosition(BitString::kCapacity),
              "Storage type is too small for the # of bits requested");

// Print e.g. "BitString[1,0,3]". Trailing 0s are dropped.
inline std::ostream& operator<<(std::ostream& os, const BitString& bit_string) {
  os << "BitString[";
  for (size_t i = 0; i < bit_string.Length(); ++i) {
    BitChar bc = bit_string[i];
    os << static_cast<BitString::StorageType>(bc);
    if (i + 1 != bit_string.Length()) {
      os << ",";
    }
  }
  os << "]";
  return os;
}

}  // namespace art

#endif  // ART_RUNTIME_INSTANCEOF_BITSTRING_H_
