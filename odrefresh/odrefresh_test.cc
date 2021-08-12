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

#include "android-base/stringprintf.h"
#include "arch/instruction_set.h"
#include "base/common_art_test.h"
#include "base/file_utils.h"
#include "gtest/gtest.h"
#include "odr_common.h"
#include "odr_config.h"
#include "odr_fs_utils.h"
#include "odr_metrics.h"

namespace art {
namespace odrefresh {

constexpr int kReplace = 1;

class OdRefreshTest : public CommonArtTest {
 public:
  OdRefreshTest() : config_("odrefresh") {}

 protected:
  void SetUp() override {
    CommonArtTest::SetUp();

    temp_dir_ = std::make_unique<ScratchDir>();
    const std::string& temp_dir_path = temp_dir_->GetPath();

    std::string android_root_path = temp_dir_path + "/system";
    ASSERT_TRUE(EnsureDirectoryExists(android_root_path));
    android_root_env_ = std::make_unique<ScopedUnsetEnvironmentVariable>("ANDROID_ROOT");
    setenv("ANDROID_ROOT", android_root_path.c_str(), kReplace);

    std::string art_apex_data_path = temp_dir_path + kOdrefreshArtifactDirectory;
    ASSERT_TRUE(EnsureDirectoryExists(art_apex_data_path));
    art_apex_data_env_ = std::make_unique<ScopedUnsetEnvironmentVariable>("ART_APEX_DATA");
    setenv("ART_APEX_DATA", art_apex_data_path.c_str(), kReplace);

    std::string dalvik_cache_dir = art_apex_data_path + "/dalvik-cache";
    ASSERT_TRUE(EnsureDirectoryExists(dalvik_cache_dir));

    std::string framework_dir = android_root_path + "/framework";
    std::string framework_jar = framework_dir + "/framework.jar";
    std::string location_provider_jar = framework_dir + "/com.android.location.provider.jar";
    std::string services_jar = framework_dir + "/services.jar";

    // Create dummy files.
    ASSERT_TRUE(EnsureDirectoryExists(framework_dir));
    OS::CreateEmptyFile(framework_jar.c_str())->Release();
    OS::CreateEmptyFile(location_provider_jar.c_str())->Release();
    OS::CreateEmptyFile(services_jar.c_str())->Release();

    config_.SetApexInfoListFile(temp_dir_path + "/apex-info-list.xml");
    config_.SetArtBinDir(temp_dir_path + "/bin");
    config_.SetBootClasspath(framework_jar);
    config_.SetDex2oatBootclasspath(framework_jar);
    config_.SetSystemServerClasspath(Concatenate({location_provider_jar, ":", services_jar}));
    config_.SetIsa(InstructionSet::kX86_64);
    config_.SetZygoteKind(ZygoteKind::kZygote64_32);

    std::string staging_dir = dalvik_cache_dir + "/staging";
    ASSERT_TRUE(EnsureDirectoryExists(staging_dir));
    config_.SetStagingDir(staging_dir);

    metrics_ = std::make_unique<OdrMetrics>(dalvik_cache_dir);
    odrefresh_ = std::make_unique<OnDeviceRefresh>(config_);
  }

  void TearDown() override {
    temp_dir_.reset();
    android_root_env_.reset();
    art_apex_data_env_.reset();

    CommonArtTest::TearDown();
  }

  std::unique_ptr<ScratchDir> temp_dir_;
  std::unique_ptr<ScopedUnsetEnvironmentVariable> android_root_env_;
  std::unique_ptr<ScopedUnsetEnvironmentVariable> art_apex_data_env_;
  OdrConfig config_;
  std::unique_ptr<OdrMetrics> metrics_;
  std::unique_ptr<OnDeviceRefresh> odrefresh_;
};

TEST_F(OdRefreshTest, OdrefreshArtifactDirectory) {
  // odrefresh.h defines kOdrefreshArtifactDirectory for external callers of odrefresh. This is
  // where compilation artifacts end up.
  ScopedUnsetEnvironmentVariable no_env("ART_APEX_DATA");
  EXPECT_EQ(kOdrefreshArtifactDirectory, GetArtApexData() + "/dalvik-cache");
}

TEST_F(OdRefreshTest, CompileSetsCompilerFilter) {
  EXPECT_EQ(odrefresh_->Compile(
                *metrics_, /*compile_boot_extensions=*/{}, /*compile_system_server=*/true),
            ExitCode::kOkay);
}

}  // namespace odrefresh
}  // namespace art
