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
#include <type_traits>
#include <array>

namespace art {

// TODO: move to detail
// Base class for a statically-sized BitChar.
// Note that the "bit length" is not stored.
template <size_t kBitSize, bool kDynamicBitSize = false>
struct BitCharBase {
  constexpr size_t GetBitLength() const {
    return kBitSize;
  }
};

// TODO: move to detail header.
// Base class for a dynamically-sized bitchar.
// The bitlength is stored as a size_t in each instance.
template <size_t kBitSize>
struct BitCharBase<kBitSize, /*kDynamicBitSize*/true> {
  constexpr size_t GetBitLength() const {
    return bitlength_;
  }

 protected:
  constexpr BitCharBase(size_t bitlength) : bitlength_(bitlength) {}

  // Note: We could be more efficient here by using only MinimumBitsToStore(bitlength).
  size_t bitlength_{};  // NOLINT
};

/**
 * Abstraction over a single character of a BitString.
 *
 * This is only intended for reading/writing into temporaries, as the representation is
 * inefficient for memory (it uses a word for the character and another word for the bitlength).
 *
 * See also BitString below.
 *
 * Parameters:
 *    <T> - Storage type for a char (e.g. 'uint8_t').
 *    <kBitSize> - Exact bitwidth of a char if static, if dynamic then maximim bitwidth.
 *    <kDynamicBitSizeV> - Whether the bitwidth can vary at runtime.
 */
template <typename T, size_t kBitSize, bool kDynamicBitSizeV = false>
struct BitChar : BitCharBase<kBitSize, kDynamicBitSizeV> {
  using BaseT = BitCharBase<kBitSize, kDynamicBitSizeV>;
  static constexpr bool kDynamicBitSize = kDynamicBitSizeV;
  using StorageType = T;

  static_assert(std::is_unsigned<StorageType>::value, "BitChar::StorageType must be unsigned");

  // BitChars are always zero-initialized by default. Equivalent to BitChar(0,0).
  constexpr BitChar() { data_ = 0; }

  // Create a new BitChar whose data bits can be at most bitlength.
  // Only available when kDynamicBitSize == true.
  template <bool kHasDynamicBitSize = kDynamicBitSizeV,
            typename = std::enable_if_t<kHasDynamicBitSize>>
  constexpr BitChar(StorageType data, size_t bitlength)
      : BaseT(bitlength), data_(data) {
    DCHECK_GE(kBitSize, bitlength) << "BitChar bitlength too large for kBitSize";
    // All bits higher than bitlength must be set to 0.
    DCHECK_EQ(0u, data & ~MaskLeastSignificant(bitlength))
        << "BitChar data out of range, data: " << data << ", bitlength: " << bitlength;
  }

  // Create a new BitChar whose data bits can be at most kBitSize.
  // Only available when kDynamicBitSize == false.
  template <bool kHasStaticBitSize = !kDynamicBitSizeV,
            typename = std::enable_if_t<kHasStaticBitSize>>
  constexpr BitChar(StorageType data)
      : data_(data) {
    // All bits higher than bitlength must be set to 0.
    DCHECK_EQ(0u, data & ~MaskLeastSignificant(kBitSize))
        << "BitChar data out of range, data: " << data << ", bitlength: " << kBitSize;
  }

  // Construct a BitChar with the bitlength=kBitSizeV.
  // This is always safe because kBitSizeV is known at compile-time.
  template <size_t kBitSizeV>
  constexpr BitChar Create(StorageType data) {
    static_assert(kBitSize >= kBitSizeV, "kBitSizeV too big");
    static_assert(kDynamicBitSizeV == false && kBitSize == kBitSizeV, "kBitSizeV too small");
    return CreateImpl<kBitSizeV>(data);
  }

  // What is the bitlength constraint for this character?
  // (Data could use less bits, but this is the maximum bit capacity at that BitString position).
  constexpr size_t GetBitLength() const {
    return BaseT::GetBitLength();
  }

  // Is there any capacity in this BitChar to store any data?
  constexpr bool IsEmpty() const {
    return GetBitLength() == 0;
  }

  constexpr explicit operator StorageType() const {
    return data_;
  }

  constexpr StorageType AsStorageType() const {
    return static_cast<StorageType>(*this);
  }

  constexpr bool operator==(StorageType storage) const {
    return data_ == storage;
  }

  constexpr bool operator!=(StorageType storage) const {
    return !(*this == storage);
  }

  // Compare equality against another BitChar. Note: bitlength is ignored.
  constexpr bool operator==(const BitChar& other) const {
    return data_ == other.data_;
  }

  // Compare non-equality against another BitChar. Note: bitlength is ignored.
  constexpr bool operator!=(const BitChar& other) const {
    return !(*this == other);
  }

  // Add a BitChar with an integer. The resulting BitChar's data must still fit within this
  // BitChar's bit length.
  constexpr BitChar operator+(StorageType storage) const {
    return BitChar(data_ + storage, GetBitLength());
  }

  // Get the maximum representible value with the same bitlength.
  // (Useful to figure out the maximum value for this BitString position.)
  constexpr BitChar MaximumValue() const {
    StorageType maximum_data = MaxInt<StorageType>(GetBitLength());
    return CopyWithNewValue(maximum_data);
  }

  std::ostream& Print(std::ostream& os) const {
    StorageType val = static_cast<StorageType>(*this);
    // 'uint8_t' needs to be cast to int before printing to ostream,
    // otherwise we will get the 'char' overload.
    if (sizeof(StorageType) == sizeof(uint8_t)) {
      os << static_cast<uint32_t>(val);
    } else {
      os << val;
    }
    return os;
  }

 private:
  // Split CopyWithNewValue into 2 impls because we don't have "if constexpr" from C++17.
  template <bool kHasDynamicBitSize = kDynamicBitSizeV>
  constexpr BitChar CopyWithNewValue(StorageType value, std::enable_if_t<kHasDynamicBitSize>* = 0, void* = 0) {
    return BitChar(value);
  }

  template <bool kHasStaticBitSize = !kDynamicBitSizeV>
  constexpr BitChar CopyWithNewValue(StorageType value, std::enable_if_t<kHasStaticBitSize>* = 0) {
    return BitChar(value, GetBitLength());
  }

  template <size_t kBitSizeV, bool kHasDynamicBitSize = kDynamicBitSizeV>
  constexpr BitChar CreateImpl(StorageType value, std::enable_if_t<kHasDynamicBitSize>* = 0, void* = 0) {
    return BitChar(value);
  }

  template <size_t kBitSizeV, bool kHasStaticBitSize = !kDynamicBitSizeV>
  constexpr BitChar CreateImpl(StorageType value, std::enable_if_t<kHasStaticBitSize>* = 0) {
    return BitChar(value, kBitSizeV);
  }

  // Note: We could be more efficient here by using only kBitSize bits, e.g. with a BitStruct.
  StorageType data_;
};

// Print e.g. "BitChar<10>(123)" where 10=bitlength, 123=data.
template <typename T, size_t kBitSize, bool kDynamicBitSize>
inline std::ostream& operator<<(std::ostream& os, const BitChar<T, kBitSize, kDynamicBitSize>& bc) {
  os << "BitChar<" << bc.GetBitLength() << ">(";
  bc.Print(os);
  os << ")";
  return os;
}

// Traits helper for a fixed-capacity bitstring with fixed-length chars.
//
// e.g.
//     uint2_t chars within uint32 bitstring (16 chars inferred)
//       BitStringTraitsFixedBitLength<uint32_t, 2>
// or
//     uint4_t chars within uint64_t bitstring (12 total chars explicitly)
//       BitStringTraitsFixedBitLength<uint64_t, 4, 12>
//
// Any bits over the kCapacity are unused (i.e. the bits there are undefined).
//
// See also bit_char.h
template <typename T, size_t kBitSize, size_t kCapacityV = BitSizeOf<T>() / kBitSize>
struct BitStringTraitsFixedBitLength {
  using Char = BitChar<T, kBitSize, /*kDynamicBitSize*/false>;
  // Maximum number of chars in the bitstring?
  static constexpr size_t kCapacity = kCapacityV;

  // How many bits is the character at a specific position? Always kBitSize.
  static constexpr size_t GetBitSizeAtPosition(size_t pos ATTRIBUTE_UNUSED) {
    return kBitSize;
  }

  // Factory function to create a char at BitString position 'pos' and 'value'.
  static constexpr Char MakeChar(size_t pos, size_t value) {
    DCHECK_GE(kCapacity, pos) << "pos out of range";
    return Char(value);
  }
};

// Traits helper for a fixed-capacity bitstring where each position
// has a char of different length.
//
// e.g.
//     uint3_t, uint5_t, uint7_t within uint16_t bitstring
//     -> 3 chars total, char[0] == uint3, char[1] == uint5, char[2] == uint7
//     BitStringTraitsFlexBitLength<uint16_t, 3, 5, 7>
//
// Any bits over the kCapacity are unused (i.e. the bits there are undefined).
//
// See also bit_char.h
template <typename T, size_t ... kBitSize>
struct BitStringTraitsFlexBitLength {
  using Char = BitChar<T, /*kBitSize*/std::numeric_limits<T>::max(), /*kDynamicBitSize*/true>;
  // Maximum number of chars in the bitstring?
  static constexpr size_t kCapacity = sizeof...(kBitSize);

  // How many bits is the character at a specific position?
  // Equivalent to the position within the variadic argument list (kBitSize).
  static constexpr size_t GetBitSizeAtPosition(size_t pos) {
    return kBitSizeAtPosition[pos];
  }

  // Factory function to create a char at BitString position 'pos' and 'value'.
  static constexpr Char MakeChar(size_t pos, size_t value) {
    DCHECK_GE(kCapacity, pos) << "pos out of range";
    DCHECK_NE(0u, kBitSizeAtPosition[pos]);
    return Char(value, kBitSizeAtPosition[pos]);
  }

 private:
  static constexpr size_t kBitSizeAtPosition[kCapacity] = {kBitSize...};  // NOLINT
};

// As this is meant to be used only with "SubtypeCheck",
// the bitlengths and the maximum string length is tuned by maximizing the coverage of "Assigned"
// bitstrings for instance-of and check-cast targets during Optimizing compilation.
struct SubtypeCheckBitStringTraits : BitStringTraitsFlexBitLength<uint32_t,
                                                                  12, 3, 8>  // len[] from header docs.
{};

// TODO: move to detail header
template<typename T, std::size_t N, typename Func, std::size_t... I>
static constexpr auto CreateArrayImpl(Func func, std::index_sequence<I...>) {
  // Use std::index_sequence because std::array::operator[] is not constexpr until C++17.
  return std::array<T, N>{ {func(I)...} };  // NOLINT
}

// Create array T[N] = { Func(0), Func(1), ... Func(N-1) };
template<typename T, std::size_t N, typename Func>
static constexpr std::array<T, N> CreateArray(Func func) {
  return CreateArrayImpl<T, N, Func>(func, std::make_index_sequence<N>{});
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
 * (see e.g. SubtypeCheckBitsAndStatus).
 *
 * Definitions:
 *
 *  "ABCDE...K"       := [A,B,C,D,E, ... K] + [0]*(idx(K)-N).
 *  MaxBitstringLen   := N+1
 *  StrLen(Bitstring) := MaxBitStringLen - | forall char ∈ CharI..CharN: char != 0 |
 *  Bitstring[N]      := CharN
 *  Bitstring[I..N)   := [CharI, CharI+1, ... CharN-1]
 *
 * (These are used by the SubtypeCheckInfo definitions and invariants, see subtype_check_info.h)
 */
template <typename BitStringTraits>  // see bit_string_traits.h
struct BitString {
  using Char = typename BitStringTraits::Char;
  using StorageType = typename Char::StorageType;

  // The maximum number of chars in this string (e.g. MaxBitstringLen above).
  static constexpr size_t kCapacity = BitStringTraits::kCapacity;
  // How many bits wide each character is at that index (e.g. len[i] above).
  static constexpr std::array<size_t, kCapacity> kBitSizeAtPosition =
      CreateArray<size_t, kCapacity>(&BitStringTraits::GetBitSizeAtPosition);

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

  // How many bits are needed for a BitStringChar at the position?
  // Returns 0 if the position is out of range.
  static constexpr size_t MaybeGetBitLengthAtPosition(size_t position) {
    if (position >= kCapacity) {
      return 0;
    }
    return kBitSizeAtPosition[position];
  }

  // Read a bitchar at some index within the capacity.
  // See also "BitString[N]" in the doc header.
  Char operator[](size_t idx) const {
    DCHECK_LT(idx, kCapacity);

    StorageType data =
        BitFieldExtract(storage_,
                        GetLsbForPosition(idx), kBitSizeAtPosition[idx]);

    return BitStringTraits::MakeChar(idx, data);
  }
  // TODO: return a proxy that supports setting operator= here.

  // Overwrite a bitchar at a position with a new one.
  //
  // The `bitchar` capacity must be no more than the maximum capacity for that position.
  void SetAt(size_t idx, Char bitchar) {
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
      Char bc = (*this)[i];
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
  // Print e.g. "BitString[1,0,3]". Trailing 0s are dropped.
  template <typename U>
  friend std::ostream& operator<<(std::ostream& os, const BitString<U>& bit_string) {
    const size_t length = bit_string.Length();
    os << "BitString[";
    for (size_t i = 0; i < bit_string.Length(); ++i) {
      bit_string[i].Print(os);
      if (i + 1 != length) {
        os << ",";
      }
    }
    os << "]";
    return os;
  }

  // Data is stored with the "highest" position in the least-significant-bit.
  // As positions approach 0, the bits are stored with increasing significance.
  StorageType storage_;

  static_assert(BitSizeOf<StorageType>() >=
                  GetBitLengthTotalAtPosition(BitString::kCapacity),
              "Storage type is too small for the # of bits requested");
};

template <typename T, size_t kBitSize, size_t kCapacityV = BitSizeOf<T>() / kBitSize>
using FixedBitString = BitString<BitStringTraitsFixedBitLength<T, kBitSize, kCapacityV>>;

}  // namespace art

#endif  // ART_RUNTIME_INSTANCEOF_BITSTRING_H_
