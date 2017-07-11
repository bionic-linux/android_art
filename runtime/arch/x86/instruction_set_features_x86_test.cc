/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "instruction_set_features_x86.h"

#include <gtest/gtest.h>

namespace art {

TEST(X86InstructionSetFeaturesTest, X86FeaturesFromDefaultVariant) {
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> x86_features(
      InstructionSetFeatures::FromVariant(kX86, "default", &error_msg));
  ASSERT_TRUE(x86_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_features->GetInstructionSet(), kX86);
  EXPECT_TRUE(x86_features->Equals(x86_features.get()));
  EXPECT_STREQ("-avx,-avx2", x86_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_features->AsBitmap(), 0U);
}

TEST(X86InstructionSetFeaturesTest, X86FeaturesFromHaswellVariant) {
  // Build features for a 32-bit x86 haswell processor.
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> x86_features(
      InstructionSetFeatures::FromVariant(kX86, "haswell", &error_msg));
  ASSERT_TRUE(x86_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_features->GetInstructionSet(), kX86);
  EXPECT_TRUE(x86_features->Equals(x86_features.get()));
  EXPECT_STREQ("avx,avx2", x86_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_features->AsBitmap(), 3U);

  // Build features for a 32-bit x86 default processor.
  std::unique_ptr<const InstructionSetFeatures> x86_default_features(
      InstructionSetFeatures::FromVariant(kX86, "default", &error_msg));
  ASSERT_TRUE(x86_default_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_default_features->GetInstructionSet(), kX86);
  EXPECT_TRUE(x86_default_features->Equals(x86_default_features.get()));
  EXPECT_STREQ("-avx,-avx2", x86_default_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_default_features->AsBitmap(), 0U);

  // Build features for a 64-bit x86-64 haswell processor.
  std::unique_ptr<const InstructionSetFeatures> x86_64_features(
      InstructionSetFeatures::FromVariant(kX86_64, "haswell", &error_msg));
  ASSERT_TRUE(x86_64_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_64_features->GetInstructionSet(), kX86_64);
  EXPECT_TRUE(x86_64_features->Equals(x86_64_features.get()));
  EXPECT_STREQ("avx,avx2", x86_64_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_64_features->AsBitmap(), 3U);

  EXPECT_FALSE(x86_64_features->Equals(x86_features.get()));
  EXPECT_FALSE(x86_64_features->Equals(x86_default_features.get()));
  EXPECT_FALSE(x86_features->Equals(x86_default_features.get()));
}

}  // namespace art
