/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "odrefresh/odrefresh.h"

#include <memory>

#include "base/common_art_test.h"
#include "base/file_utils.h"
#include "gtest/gtest.h"
#include "odr_fs_utils.h"

namespace art {
namespace odrefresh {

constexpr int kReplace = 1;

class OdRefreshTest : public CommonArtTest {
 protected:
  void SetUp() override {
    CommonArtTest::SetUp();

    temp_dir_ = std::make_unique<ScratchDir>();

    std::string android_root_path = temp_dir_->GetPath() + "/system";
    ASSERT_TRUE(EnsureDirectoryExists(android_root_path));
    setenv("ANDROID_ROOT", android_root_path.c_str(), kReplace);

    std::string art_apex_data_path = temp_dir_->GetPath() + kOdrefreshArtifactDirectory;
    ASSERT_TRUE(EnsureDirectoryExists(art_apex_data_path));
    setenv("ART_APEX_DATA", art_apex_data_path.c_str(), kReplace);

    ASSERT_TRUE(EnsureDirectoryExists(art_apex_data_path + "/dalvik-cache"));
  }

  void TearDown() override {
    temp_dir_.reset();

    CommonArtTest::TearDown();
  }

  std::unique_ptr<ScratchDir> temp_dir_;
  ScopedUnsetEnvironmentVariable android_root_env_{"ANDROID_ROOT"};
  ScopedUnsetEnvironmentVariable art_apex_data_env_{"ART_APEX_DATA"};
};

TEST_F(OdRefreshTest, OdrefreshArtifactDirectory) {
  // odrefresh.h defines kOdrefreshArtifactDirectory for external callers of odrefresh. This is
  // where compilation artifacts end up.
  ScopedUnsetEnvironmentVariable no_env("ART_APEX_DATA");
  EXPECT_EQ(kOdrefreshArtifactDirectory, GetArtApexData() + "/dalvik-cache");
}

}  // namespace odrefresh
}  // namespace art
