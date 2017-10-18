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

#include "gtest/gtest.h"
#include "android-base/logging.h"

namespace art {

constexpr size_t BitString::kBitSizeAtPosition[BitString::kCapacity];
constexpr size_t BitString::kCapacity;

};  // namespace art

using namespace art;

// These helper functions are only used by the test,
// so they are not in the main BitString class.
std::string Stringify(BitString bit_string) {
  std::stringstream ss;
  ss << bit_string;
  return ss.str();
}

BitChar MakeBitChar(size_t idx, size_t val) {
  return BitChar(val, BitString::MaybeGetBitLengthAtPosition(idx));
}

BitChar MakeBitChar(size_t val) {
  return BitChar(val, MinimumBitsToStore(val));
}

BitString MakeBitString(std::initializer_list<size_t> values = {}) {
  CHECK_GE(BitString::kCapacity, values.size());

  BitString bs{};  // NOLINT

  size_t i = 0;
  for (size_t val : values) {
    bs.SetAt(i, MakeBitChar(i, val));
    ++i;
  }

  return bs;
}

template <typename T>
size_t AsUint(const T& value) {
  size_t uint_value = 0;
  memcpy(&uint_value, &value, sizeof(value));
  return uint_value;
}

// Make max bistring, e.g. BitString[4095,7,255] for {12,3,8}
template <size_t kCount=BitString::kCapacity>
BitString MakeBitStringMax() {
  BitString bs{};

  for (size_t i = 0; i < kCount; ++i) {
    bs.SetAt(i,
             MakeBitChar(i, MaxInt<BitChar::StorageType>(BitString::kBitSizeAtPosition[i])));
  }

  return bs;
}

BitString SetBitCharAt(BitString bit_string, size_t i, size_t val) {
  BitString bs = bit_string;
  bs.SetAt(i, MakeBitChar(i, val));
  return bs;
}

struct InstanceOfTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    android::base::InitLogging(/*argv*/nullptr);
  }

  virtual void TearDown() {
  }

  static InstanceOf MakeInstanceOf(BitString path_to_root = {},
                                   BitChar next = {},
                                   bool overflow = false,
                                   size_t depth = 1u) {
    // Depth=1 is good default because it will go through all state transitions,
    // and its children will also go through all state transitions.
    return InstanceOf(path_to_root, next, overflow, depth);
  }

  static InstanceOf MakeInstanceOfInfused(BitString bs = {},
                                          bool overflow = false,
                                          size_t depth = 1u) {
    // Depth=1 is good default because it will go through all state transitions,
    // and its children will also go through all state transitions.
    InstanceOfData iod;
    iod.bitstring_ = bs;
    iod.overflow_ = overflow;
    return InstanceOf::Infuse(iod, depth);
  }

  static InstanceOf MakeInstanceOfUnchecked(BitString bs = {},
                                            bool overflow = false,
                                            size_t depth = 1u) {
    // Depth=1 is good default because it will go through all state transitions,
    // and its children will also go through all state transitions.
    return InstanceOf::MakeUnchecked(bs, overflow, depth);
  }

  static bool HasNext(InstanceOf io) {
    return io.HasNext();
  }

  static BitString GetPathToRoot(InstanceOf io) {
    return io.GetPathToRoot();
  }
};

const char* GetExpectedMessageForDeathTest(const char* msg) {
#ifdef ART_TARGET_ANDROID
  // On Android, dcheck failure messages go to logcat,
  // which gtest death tests does not check, and thus the tests would fail with
  // "unexpected message ''"
  UNUSED(msg);
  return "";  // Still ensures there was a bad return code, but match anything.
#else
  return msg;
#endif
}

TEST_F(InstanceOfTest, IllegalValues) {
  // This test relies on BitString being at least 3 large.
  // It will need to be updated otherwise.
  ASSERT_LE(3u, BitString::kCapacity);

  // Illegal values during construction would cause a Dcheck failure and crash.
  ASSERT_DEATH(MakeInstanceOf(MakeBitString({1u}),
                              /*next*/MakeBitChar(0),
                              /*overflow*/false,
                              /*depth*/0u),
               GetExpectedMessageForDeathTest("Path was too long for the depth"));
  ASSERT_DEATH(MakeInstanceOfInfused(MakeBitString({1u, 1u}),
                                     /*overflow*/false,
                                     /*depth*/0u),
               GetExpectedMessageForDeathTest("Bitstring too long for depth"));
  ASSERT_DEATH(MakeInstanceOf(MakeBitString({1u}),
                              /*next*/MakeBitChar(0),
                              /*overflow*/false,
                              /*depth*/1u),
               GetExpectedMessageForDeathTest("Expected \\(Assigned\\|Initialized\\) "
                                              "state to have >0 Next value"));
  ASSERT_DEATH(MakeInstanceOfInfused(MakeBitString({0u, 2u, 1u}),
                                     /*overflow*/false,
                                     /*depth*/2u),
               GetExpectedMessageForDeathTest("Path to root had non-0s following 0s"));
  ASSERT_DEATH(MakeInstanceOf(MakeBitString({0u, 2u}),
                              /*next*/MakeBitChar(1u),
                              /*overflow*/false,
                              /*depth*/2u),
               GetExpectedMessageForDeathTest("Path to root had non-0s following 0s"));
  ASSERT_DEATH(MakeInstanceOf(MakeBitString({0u, 1u, 1u}),
                              /*next*/MakeBitChar(0),
                              /*overflow*/false,
                              /*depth*/3u),
               GetExpectedMessageForDeathTest("Path to root had non-0s following 0s"));

  // These are really slow (~1sec per death test on host),
  // keep them down to a minimum.
}

TEST_F(InstanceOfTest, States) {
  EXPECT_EQ(InstanceOf::kUninitialized, MakeInstanceOf().GetState());
  EXPECT_EQ(InstanceOf::kInitialized,
            MakeInstanceOf(/*path*/{}, /*next*/MakeBitChar(1)).GetState());
  EXPECT_EQ(InstanceOf::kOverflowed,
            MakeInstanceOf(/*path*/{},
                           /*next*/MakeBitChar(1),
                           /*overflow*/true,
                           /*depth*/1u).GetState());
  EXPECT_EQ(InstanceOf::kAssigned,
            MakeInstanceOf(/*path*/MakeBitString({1u}),
                           /*next*/MakeBitChar(1),
                           /*overflow*/false,
                           /*depth*/1u).GetState());

  // Test edge conditions: depth == BitString::kCapacity (No Next value).
  EXPECT_EQ(InstanceOf::kAssigned,
            MakeInstanceOf(/*path*/MakeBitStringMax(),
                           /*next*/MakeBitChar(0),
                           /*overflow*/false,
                           /*depth*/BitString::kCapacity).GetState());
  EXPECT_EQ(InstanceOf::kInitialized,
            MakeInstanceOf(/*path*/MakeBitStringMax<BitString::kCapacity - 1u>(),
                           /*next*/MakeBitChar(0),
                           /*overflow*/false,
                           /*depth*/BitString::kCapacity).GetState());
  // Test edge conditions: depth > BitString::kCapacity (Must overflow).
  EXPECT_EQ(InstanceOf::kOverflowed,
            MakeInstanceOf(/*path*/MakeBitStringMax(),
                           /*next*/MakeBitChar(0),
                           /*overflow*/true,
                           /*depth*/BitString::kCapacity + 1u).GetState());
}

TEST_F(InstanceOfTest, NextValue) {
  // Validate "Next" is correctly aliased as the Bitstring[Depth] character.
  EXPECT_EQ(MakeBitChar(1u), MakeInstanceOfUnchecked(MakeBitString({1u, 2u, 3u}),
                                                     /*overflow*/false,
                                                     /*depth*/0u).GetNext());
  EXPECT_EQ(MakeBitChar(2u), MakeInstanceOfUnchecked(MakeBitString({1u, 2u, 3u}),
                                                     /*overflow*/false,
                                                     /*depth*/1u).GetNext());
  EXPECT_EQ(MakeBitChar(3u), MakeInstanceOfUnchecked(MakeBitString({1u, 2u, 3u}),
                                                     /*overflow*/false,
                                                     /*depth*/2u).GetNext());
  EXPECT_EQ(MakeBitChar(1u), MakeInstanceOfUnchecked(MakeBitString({0u, 2u, 1u}),
                                                     /*overflow*/false,
                                                     /*depth*/2u).GetNext());
  // Test edge conditions: depth == BitString::kCapacity (No Next value).
  EXPECT_FALSE(HasNext(MakeInstanceOfUnchecked(MakeBitStringMax<BitString::kCapacity>(),
                                               /*overflow*/false,
                                               /*depth*/BitString::kCapacity)));
  // Anything with depth >= BitString::kCapacity has no next value.
  EXPECT_FALSE(HasNext(MakeInstanceOfUnchecked(MakeBitStringMax<BitString::kCapacity>(),
                                               /*overflow*/false,
                                               /*depth*/BitString::kCapacity + 1u)));
  EXPECT_FALSE(HasNext(MakeInstanceOfUnchecked(MakeBitStringMax(),
                                               /*overflow*/false,
                                               /*depth*/std::numeric_limits<size_t>::max())));

}

template <size_t kPos = BitString::kCapacity>
size_t LenForPos() { return BitString::GetBitLengthTotalAtPosition(kPos); }

TEST_F(InstanceOfTest, EncodedPathToRoot) {
  using StorageType = BitString::StorageType;

  InstanceOf io =
      MakeInstanceOf(/*path_to_root*/MakeBitStringMax(),
                     /*next*/BitChar{},
                     /*overflow*/false,
                     /*depth*/BitString::kCapacity);
  // 0b11111...000 where MSB == 1, and leading 1s = the maximum bitstring representation.
  EXPECT_EQ(MaxInt<StorageType>(LenForPos()) << (BitSizeOf<StorageType>() - LenForPos()),
            io.GetEncodedPathToRoot());

  EXPECT_EQ(MaxInt<StorageType>(LenForPos()) << (BitSizeOf<StorageType>() - LenForPos()),
            io.GetEncodedPathToRootMask());

  // 0b11111...000 where MSB == 1, and leading 1s = the maximum bitstring representation.

  // The rest of this test is written assuming kCapacity == 3 for convenience.
  // Please update the test if this changes.
  ASSERT_EQ(3u, BitString::kCapacity);
  ASSERT_EQ(12u, BitString::kBitSizeAtPosition[0]);
  ASSERT_EQ(3u, BitString::kBitSizeAtPosition[1]);
  ASSERT_EQ(8u, BitString::kBitSizeAtPosition[2]);

  InstanceOf io2 =
      MakeInstanceOfUnchecked(MakeBitStringMax<2u>(),
                             /*overflow*/false,
                             /*depth*/BitString::kCapacity);

#define MAKE_ENCODED_PATH(pos0, pos1, pos2) (((pos0) << 3u << 8u << 9u) | ((pos1) << 8u << 9u) | ((pos2) << 9u))

  EXPECT_EQ(MAKE_ENCODED_PATH(MaxInt<BitString::StorageType>(12), 0b111, 0b0), io2.GetEncodedPathToRoot());
  EXPECT_EQ(MAKE_ENCODED_PATH(MaxInt<BitString::StorageType>(12), 0b111, 0b11111111), io2.GetEncodedPathToRootMask());

  InstanceOf io3 =
      MakeInstanceOfUnchecked(MakeBitStringMax<2u>(),
                             /*overflow*/false,
                             /*depth*/BitString::kCapacity - 1u);

  EXPECT_EQ(MAKE_ENCODED_PATH(MaxInt<BitString::StorageType>(12), 0b111, 0b0), io3.GetEncodedPathToRoot());
  EXPECT_EQ(MAKE_ENCODED_PATH(MaxInt<BitString::StorageType>(12), 0b111, 0b0), io3.GetEncodedPathToRootMask());

  InstanceOf io4 =
      MakeInstanceOfUnchecked(MakeBitString({0b1010101u}),
                             /*overflow*/false,
                             /*depth*/BitString::kCapacity - 2u);

  EXPECT_EQ(MAKE_ENCODED_PATH(0b1010101u, 0b000, 0b0), io4.GetEncodedPathToRoot());
  EXPECT_EQ(MAKE_ENCODED_PATH(MaxInt<BitString::StorageType>(12), 0b000, 0b0), io4.GetEncodedPathToRootMask());
}

TEST_F(InstanceOfTest, NewForRoot) {
  InstanceOf io = InstanceOf::NewForRoot();
  EXPECT_EQ(InstanceOf::kAssigned, io.GetState());  // Root is always assigned.
  EXPECT_EQ(0u, GetPathToRoot(io).Length());  // Root's path length is 0.
  EXPECT_TRUE(HasNext(io));  // Root always has a "Next".
  EXPECT_EQ(MakeBitChar(1u), io.GetNext());  // Next>=1 to disambiguate from Uninitialized.
}

TEST_F(InstanceOfTest, CopyCleared) {
  InstanceOf root = InstanceOf::NewForRoot();
  EXPECT_EQ(MakeBitChar(1u), root.GetNext());

  InstanceOf childC = root.NewForChild(/*assign*/true);
  EXPECT_EQ(InstanceOf::kAssigned, childC.GetState());
  EXPECT_EQ(MakeBitChar(2u), root.GetNext());  // Next incremented for Assign.
  EXPECT_EQ(MakeBitString({1u}), GetPathToRoot(childC));

  InstanceOf cleared_copy = childC.CopyCleared();
  EXPECT_EQ(InstanceOf::kUninitialized, cleared_copy.GetState());
  EXPECT_EQ(MakeBitString({}), GetPathToRoot(cleared_copy));

  // CopyCleared is just a thin wrapper around value-init and providing the depth.
  InstanceOf cleared_copy_value = InstanceOf::Infuse(InstanceOfData{}, /*depth*/1u);
  EXPECT_EQ(InstanceOf::kUninitialized, cleared_copy_value.GetState());
  EXPECT_EQ(MakeBitString({}), GetPathToRoot(cleared_copy_value));
}

TEST_F(InstanceOfTest, NewForChild2) {
  InstanceOf root = InstanceOf::NewForRoot();
  EXPECT_EQ(MakeBitChar(1u), root.GetNext());

  InstanceOf childC = root.NewForChild(/*assign*/true);
  EXPECT_EQ(InstanceOf::kAssigned, childC.GetState());
  EXPECT_EQ(MakeBitChar(2u), root.GetNext());  // Next incremented for Assign.
  EXPECT_EQ(MakeBitString({1u}), GetPathToRoot(childC));
}

TEST_F(InstanceOfTest, NewForChild) {
  InstanceOf root = InstanceOf::NewForRoot();
  EXPECT_EQ(MakeBitChar(1u), root.GetNext());

  InstanceOf childA = root.NewForChild(/*assign*/false);
  EXPECT_EQ(InstanceOf::kInitialized, childA.GetState());
  EXPECT_EQ(MakeBitChar(1u), root.GetNext());  // Next unchanged for Initialize.
  EXPECT_EQ(MakeBitString({}), GetPathToRoot(childA));

  InstanceOf childB = root.NewForChild(/*assign*/false);
  EXPECT_EQ(InstanceOf::kInitialized, childB.GetState());
  EXPECT_EQ(MakeBitChar(1u), root.GetNext());  // Next unchanged for Initialize.
  EXPECT_EQ(MakeBitString({}), GetPathToRoot(childB));

  InstanceOf childC = root.NewForChild(/*assign*/true);
  EXPECT_EQ(InstanceOf::kAssigned, childC.GetState());
  EXPECT_EQ(MakeBitChar(2u), root.GetNext());  // Next incremented for Assign.
  EXPECT_EQ(MakeBitString({1u}), GetPathToRoot(childC));

  {
    size_t cur_depth = 1u;
    InstanceOf latest_child = childC;
    while (cur_depth != BitString::kCapacity) {
      latest_child = latest_child.NewForChild(/*assign*/true);
      ASSERT_EQ(InstanceOf::kAssigned, latest_child.GetState());
      ASSERT_EQ(cur_depth + 1u, GetPathToRoot(latest_child).Length());
      cur_depth++;
    }

    // Future assignments will result in a too-deep overflow.
    InstanceOf child_of_deep = latest_child.NewForChild(/*assign*/true);
    EXPECT_EQ(InstanceOf::kOverflowed, child_of_deep.GetState());
    EXPECT_EQ(GetPathToRoot(latest_child), GetPathToRoot(child_of_deep));

    // Assignment of too-deep overflow also causes overflow.
    InstanceOf child_of_deep_2 = child_of_deep.NewForChild(/*assign*/true);
    EXPECT_EQ(InstanceOf::kOverflowed, child_of_deep_2.GetState());
    EXPECT_EQ(GetPathToRoot(child_of_deep), GetPathToRoot(child_of_deep_2));
  }

  {
    size_t cur_next = 2u;
    while (true) {
      if (cur_next == MaxInt<BitString::StorageType>(BitString::kBitSizeAtPosition[0u])) {
        break;
      }

      InstanceOf child = root.NewForChild(/*assign*/true);
      ASSERT_EQ(InstanceOf::kAssigned, child.GetState());
      ASSERT_EQ(MakeBitChar(cur_next+1u), root.GetNext());
      ASSERT_EQ(MakeBitString({cur_next}), GetPathToRoot(child));

      cur_next++;
    }
    // Now the root will be in a state that further assigns will be too-wide overflow.

    // Initialization still succeeds.
    InstanceOf child = root.NewForChild(/*assign*/false);
    EXPECT_EQ(InstanceOf::kInitialized, child.GetState());
    EXPECT_EQ(MakeBitChar(cur_next), root.GetNext());
    EXPECT_EQ(MakeBitString({}), GetPathToRoot(child));

    // Assignment goes to too-wide Overflow.
    InstanceOf child_of = root.NewForChild(/*assign*/true);
    EXPECT_EQ(InstanceOf::kOverflowed, child_of.GetState());
    EXPECT_EQ(MakeBitChar(cur_next), root.GetNext());
    EXPECT_EQ(MakeBitString({}), GetPathToRoot(child_of));

    // Assignment of overflowed child still succeeds.
    // The path to root is the same.
    InstanceOf child_of2 = child_of.NewForChild(/*assign*/true);
    EXPECT_EQ(InstanceOf::kOverflowed, child_of2.GetState());
    EXPECT_EQ(GetPathToRoot(child_of), GetPathToRoot(child_of2));
  }
}
