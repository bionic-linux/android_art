/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <sys/types.h>

#include <memory>
#include <string>
#include <string_view>

#include <android-base/file.h>
#include <dex/dex_file.h>
#include <gtest/gtest.h>

#include "art_api/dex_file_support.h"

namespace art_api {
namespace dex {

static constexpr uint32_t kDexData[] = {
    0x0a786564, 0x00383330, 0xc98b3ab8, 0xf3749d94, 0xaecca4d8, 0xffc7b09a, 0xdca9ca7f, 0x5be5deab,
    0x00000220, 0x00000070, 0x12345678, 0x00000000, 0x00000000, 0x0000018c, 0x00000008, 0x00000070,
    0x00000004, 0x00000090, 0x00000002, 0x000000a0, 0x00000000, 0x00000000, 0x00000003, 0x000000b8,
    0x00000001, 0x000000d0, 0x00000130, 0x000000f0, 0x00000122, 0x0000012a, 0x00000132, 0x00000146,
    0x00000151, 0x00000154, 0x00000158, 0x0000016d, 0x00000001, 0x00000002, 0x00000004, 0x00000006,
    0x00000004, 0x00000002, 0x00000000, 0x00000005, 0x00000002, 0x0000011c, 0x00000000, 0x00000000,
    0x00010000, 0x00000007, 0x00000001, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000000,
    0x00000003, 0x00000000, 0x0000017e, 0x00000000, 0x00010001, 0x00000001, 0x00000173, 0x00000004,
    0x00021070, 0x000e0000, 0x00010001, 0x00000000, 0x00000178, 0x00000001, 0x0000000e, 0x00000001,
    0x3c060003, 0x74696e69, 0x4c06003e, 0x6e69614d, 0x4c12003b, 0x6176616a, 0x6e616c2f, 0x624f2f67,
    0x7463656a, 0x4d09003b, 0x2e6e6961, 0x6176616a, 0x00560100, 0x004c5602, 0x6a4c5b13, 0x2f617661,
    0x676e616c, 0x7274532f, 0x3b676e69, 0x616d0400, 0x01006e69, 0x000e0700, 0x07000103, 0x0000000e,
    0x81000002, 0x01f00480, 0x02880901, 0x0000000c, 0x00000000, 0x00000001, 0x00000000, 0x00000001,
    0x00000008, 0x00000070, 0x00000002, 0x00000004, 0x00000090, 0x00000003, 0x00000002, 0x000000a0,
    0x00000005, 0x00000003, 0x000000b8, 0x00000006, 0x00000001, 0x000000d0, 0x00002001, 0x00000002,
    0x000000f0, 0x00001001, 0x00000001, 0x0000011c, 0x00002002, 0x00000008, 0x00000122, 0x00002003,
    0x00000002, 0x00000173, 0x00002000, 0x00000001, 0x0000017e, 0x00001000, 0x00000001, 0x0000018c,
};

TEST(DexFileTest, create) {
  size_t size = sizeof(kDexData);
  std::unique_ptr<DexFile> dex_file;
  EXPECT_TRUE(DexFile::Create(kDexData, size, &size, "", &dex_file));
  EXPECT_EQ(size, sizeof(kDexData));
  EXPECT_NE(dex_file, nullptr);
}

TEST(DexFileTest, create_header_too_small) {
  size_t size = sizeof(art::DexFile::Header) - 1;
  std::unique_ptr<DexFile> dex_file;
  EXPECT_FALSE(DexFile::Create(kDexData, size, &size, "", &dex_file));
  EXPECT_EQ(size, sizeof(art::DexFile::Header));
  EXPECT_EQ(dex_file, nullptr);
}

TEST(DexFileTest, create_file_too_small) {
  size_t size = sizeof(art::DexFile::Header);
  std::unique_ptr<DexFile> dex_file;
  EXPECT_FALSE(DexFile::Create(kDexData, size, &size, "", &dex_file));
  EXPECT_EQ(size, sizeof(kDexData));
  EXPECT_EQ(dex_file, nullptr);
}

static std::unique_ptr<DexFile> GetTestDexData() {
  size_t size = sizeof(kDexData);
  std::unique_ptr<DexFile> dex_file;
  EXPECT_TRUE(DexFile::Create(kDexData, size, &size, "", &dex_file));
  EXPECT_EQ(size, sizeof(kDexData));
  EXPECT_NE(dex_file, nullptr);
  return dex_file;
}

TEST(DexFileTest, findMethodAtOffset) {
  std::unique_ptr<DexFile> dex_file = GetTestDexData();
  ASSERT_NE(dex_file, nullptr);

  bool found_init = false;
  auto check_init = [&](const DexFile::Method& method) {
    size_t size;
    size_t offset = method.GetCodeOffset(&size);
    EXPECT_EQ(offset, 0x100u);
    EXPECT_EQ(size, 8u);
    EXPECT_STREQ(method.GetName(), "<init>");
    EXPECT_STREQ(method.GetQualifiedName(), "Main.<init>");
    EXPECT_STREQ(method.GetQualifiedName(true), "void Main.<init>()");
    EXPECT_STREQ(method.GetClassDescriptor(), "LMain;");
    found_init = true;
  };
  EXPECT_EQ(dex_file->FindMethodAtOffset(0x102, check_init), 1u);
  EXPECT_TRUE(found_init);

  bool found_main = false;
  auto check_main = [&](const DexFile::Method& method) {
    size_t size;
    size_t offset = method.GetCodeOffset(&size);
    EXPECT_EQ(offset, 0x118u);
    EXPECT_EQ(size, 2u);
    EXPECT_STREQ(method.GetName(), "main");
    EXPECT_STREQ(method.GetQualifiedName(), "Main.main");
    EXPECT_STREQ(method.GetQualifiedName(true), "void Main.main(java.lang.String[])");
    EXPECT_STREQ(method.GetClassDescriptor(), "LMain;");
    found_main = true;
  };
  EXPECT_EQ(dex_file->FindMethodAtOffset(0x118, check_main), 1u);
  EXPECT_TRUE(found_main);
}

TEST(DexFileTest, get_method_info_for_offset_boundaries) {
  std::unique_ptr<DexFile> dex_file = GetTestDexData();
  ASSERT_NE(dex_file, nullptr);

  EXPECT_EQ(dex_file->FindMethodAtOffset(0x99, [](auto){}), 0);
  EXPECT_EQ(dex_file->FindMethodAtOffset(0x100, [](auto){}), 1);
  EXPECT_EQ(dex_file->FindMethodAtOffset(0x107, [](auto){}), 1);
  EXPECT_EQ(dex_file->FindMethodAtOffset(0x108, [](auto){}), 0);
  EXPECT_EQ(dex_file->FindMethodAtOffset(0x100000, [](auto){}), 0);
}

TEST(DexFileTest, get_all_method_infos_without_signature) {
  std::unique_ptr<DexFile> dex_file = GetTestDexData();
  ASSERT_NE(dex_file, nullptr);

  std::vector<std::string> names;
  auto add = [&](const DexFile::Method& method) { names.push_back(method.GetQualifiedName()); };
  EXPECT_EQ(dex_file->ForEachMethod(add), 2u);
  EXPECT_EQ(names, std::vector<std::string>({"Main.<init>", "Main.main"}));
}

}  // namespace dex
}  // namespace art_api
