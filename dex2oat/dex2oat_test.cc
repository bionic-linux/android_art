/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "android-base/stringprintf.h"

#include "common_runtime_test.h"

#include "base/logging.h"
#include "base/macros.h"
#include "dex_file-inl.h"
#include "dex2oat_environment_test.h"
#include "jit/profile_compilation_info.h"
#include "oat.h"
#include "oat_file.h"
#include "utils.h"

namespace art {

class Dex2oatTest : public Dex2oatEnvironmentTest {
 public:
  virtual void TearDown() OVERRIDE {
    Dex2oatEnvironmentTest::TearDown();

    output_ = "";
    error_msg_ = "";
    success_ = false;
  }

 protected:
  void GenerateOdexForTest(const std::string& dex_location,
                           const std::string& odex_location,
                           CompilerFilter::Filter filter,
                           const std::vector<std::string>& extra_args = {},
                           bool expect_success = true) {
    std::vector<std::string> args;
    args.push_back("--dex-file=" + dex_location);
    args.push_back("--oat-file=" + odex_location);
    args.push_back("--compiler-filter=" + CompilerFilter::NameOfFilter(filter));
    args.push_back("--runtime-arg");
    args.push_back("-Xnorelocate");

    args.insert(args.end(), extra_args.begin(), extra_args.end());

    std::string error_msg;
    bool success = Dex2Oat(args, &error_msg);

    if (expect_success) {
      ASSERT_TRUE(success) << error_msg << std::endl << output_;

      // Verify the odex file was generated as expected.
      std::unique_ptr<OatFile> odex_file(OatFile::Open(odex_location.c_str(),
                                                       odex_location.c_str(),
                                                       nullptr,
                                                       nullptr,
                                                       false,
                                                       /*low_4gb*/false,
                                                       dex_location.c_str(),
                                                       &error_msg));
      ASSERT_TRUE(odex_file.get() != nullptr) << error_msg;

      CheckFilter(filter, odex_file->GetCompilerFilter());
    } else {
      ASSERT_FALSE(success) << output_;

      error_msg_ = error_msg;

      // Verify there's no loadable odex file.
      std::unique_ptr<OatFile> odex_file(OatFile::Open(odex_location.c_str(),
                                                       odex_location.c_str(),
                                                       nullptr,
                                                       nullptr,
                                                       false,
                                                       /*low_4gb*/false,
                                                       dex_location.c_str(),
                                                       &error_msg));
      ASSERT_TRUE(odex_file.get() == nullptr);
    }
  }

  // Check the input compiler filter against the generated oat file's filter. Mayb be overridden
  // in subclasses when equality is not expected.
  virtual void CheckFilter(CompilerFilter::Filter expected, CompilerFilter::Filter actual) {
    EXPECT_EQ(expected, actual);
  }

  bool Dex2Oat(const std::vector<std::string>& dex2oat_args, std::string* error_msg) {
    Runtime* runtime = Runtime::Current();

    const std::vector<gc::space::ImageSpace*>& image_spaces =
        runtime->GetHeap()->GetBootImageSpaces();
    if (image_spaces.empty()) {
      *error_msg = "No image location found for Dex2Oat.";
      return false;
    }
    std::string image_location = image_spaces[0]->GetImageLocation();

    std::vector<std::string> argv;
    argv.push_back(runtime->GetCompilerExecutable());
    argv.push_back("--runtime-arg");
    argv.push_back("-classpath");
    argv.push_back("--runtime-arg");
    std::string class_path = runtime->GetClassPathString();
    if (class_path == "") {
      class_path = OatFile::kSpecialSharedLibrary;
    }
    argv.push_back(class_path);
    if (runtime->IsJavaDebuggable()) {
      argv.push_back("--debuggable");
    }
    runtime->AddCurrentRuntimeFeaturesAsDex2OatArguments(&argv);

    if (!runtime->IsVerificationEnabled()) {
      argv.push_back("--compiler-filter=verify-none");
    }

    if (runtime->MustRelocateIfPossible()) {
      argv.push_back("--runtime-arg");
      argv.push_back("-Xrelocate");
    } else {
      argv.push_back("--runtime-arg");
      argv.push_back("-Xnorelocate");
    }

    if (!kIsTargetBuild) {
      argv.push_back("--host");
    }

    argv.push_back("--boot-image=" + image_location);

    std::vector<std::string> compiler_options = runtime->GetCompilerOptions();
    argv.insert(argv.end(), compiler_options.begin(), compiler_options.end());

    argv.insert(argv.end(), dex2oat_args.begin(), dex2oat_args.end());

    // We must set --android-root.
    const char* android_root = getenv("ANDROID_ROOT");
    CHECK(android_root != nullptr);
    argv.push_back("--android-root=" + std::string(android_root));

    int link[2];

    if (pipe(link) == -1) {
      return false;
    }

    pid_t pid = fork();
    if (pid == -1) {
      return false;
    }

    if (pid == 0) {
      // We need dex2oat to actually log things.
      setenv("ANDROID_LOG_TAGS", "*:d", 1);
      dup2(link[1], STDERR_FILENO);
      close(link[0]);
      close(link[1]);
      std::vector<const char*> c_args;
      for (const std::string& str : argv) {
        c_args.push_back(str.c_str());
      }
      c_args.push_back(nullptr);
      execv(c_args[0], const_cast<char* const*>(c_args.data()));
      exit(1);
    } else {
      close(link[1]);
      char buffer[128];
      memset(buffer, 0, 128);
      ssize_t bytes_read = 0;

      while (TEMP_FAILURE_RETRY(bytes_read = read(link[0], buffer, 128)) > 0) {
        output_ += std::string(buffer, bytes_read);
      }
      close(link[0]);
      int status = 0;
      if (waitpid(pid, &status, 0) != -1) {
        success_ = (status == 0);
      }
    }
    return success_;
  }

  std::string output_ = "";
  std::string error_msg_ = "";
  bool success_ = false;
};

class Dex2oatSwapTest : public Dex2oatTest {
 protected:
  void RunTest(bool use_fd, bool expect_use, const std::vector<std::string>& extra_args = {}) {
    std::string dex_location = GetScratchDir() + "/Dex2OatSwapTest.jar";
    std::string odex_location = GetOdexDir() + "/Dex2OatSwapTest.odex";

    Copy(GetTestDexFileName(), dex_location);

    std::vector<std::string> copy(extra_args);

    std::unique_ptr<ScratchFile> sf;
    if (use_fd) {
      sf.reset(new ScratchFile());
      copy.push_back(android::base::StringPrintf("--swap-fd=%d", sf->GetFd()));
    } else {
      std::string swap_location = GetOdexDir() + "/Dex2OatSwapTest.odex.swap";
      copy.push_back("--swap-file=" + swap_location);
    }
    GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kSpeed, copy);

    CheckValidity();
    ASSERT_TRUE(success_);
    CheckResult(expect_use);
  }

  virtual std::string GetTestDexFileName() {
    return Dex2oatEnvironmentTest::GetTestDexFileName("VerifierDeps");
  }

  virtual void CheckResult(bool expect_use) {
    if (kIsTargetBuild) {
      CheckTargetResult(expect_use);
    } else {
      CheckHostResult(expect_use);
    }
  }

  virtual void CheckTargetResult(bool expect_use ATTRIBUTE_UNUSED) {
    // TODO: Ignore for now, as we won't capture any output (it goes to the logcat). We may do
    //       something for variants with file descriptor where we can control the lifetime of
    //       the swap file and thus take a look at it.
  }

  virtual void CheckHostResult(bool expect_use) {
    if (!kIsTargetBuild) {
      if (expect_use) {
        EXPECT_NE(output_.find("Large app, accepted running with swap."), std::string::npos)
            << output_;
      } else {
        EXPECT_EQ(output_.find("Large app, accepted running with swap."), std::string::npos)
            << output_;
      }
    }
  }

  // Check whether the dex2oat run was really successful.
  virtual void CheckValidity() {
    if (kIsTargetBuild) {
      CheckTargetValidity();
    } else {
      CheckHostValidity();
    }
  }

  virtual void CheckTargetValidity() {
    // TODO: Ignore for now, as we won't capture any output (it goes to the logcat). We may do
    //       something for variants with file descriptor where we can control the lifetime of
    //       the swap file and thus take a look at it.
  }

  // On the host, we can get the dex2oat output. Here, look for "dex2oat took."
  virtual void CheckHostValidity() {
    EXPECT_NE(output_.find("dex2oat took"), std::string::npos) << output_;
  }
};

TEST_F(Dex2oatSwapTest, DoNotUseSwapDefaultSingleSmall) {
  RunTest(false /* use_fd */, false /* expect_use */);
  RunTest(true /* use_fd */, false /* expect_use */);
}

TEST_F(Dex2oatSwapTest, DoNotUseSwapSingle) {
  RunTest(false /* use_fd */, false /* expect_use */, { "--swap-dex-size-threshold=0" });
  RunTest(true /* use_fd */, false /* expect_use */, { "--swap-dex-size-threshold=0" });
}

TEST_F(Dex2oatSwapTest, DoNotUseSwapSmall) {
  RunTest(false /* use_fd */, false /* expect_use */, { "--swap-dex-count-threshold=0" });
  RunTest(true /* use_fd */, false /* expect_use */, { "--swap-dex-count-threshold=0" });
}

TEST_F(Dex2oatSwapTest, DoUseSwapSingleSmall) {
  RunTest(false /* use_fd */,
          true /* expect_use */,
          { "--swap-dex-size-threshold=0", "--swap-dex-count-threshold=0" });
  RunTest(true /* use_fd */,
          true /* expect_use */,
          { "--swap-dex-size-threshold=0", "--swap-dex-count-threshold=0" });
}

class Dex2oatSwapUseTest : public Dex2oatSwapTest {
 protected:
  void CheckHostResult(bool expect_use) OVERRIDE {
    if (!kIsTargetBuild) {
      if (expect_use) {
        EXPECT_NE(output_.find("Large app, accepted running with swap."), std::string::npos)
            << output_;
      } else {
        EXPECT_EQ(output_.find("Large app, accepted running with swap."), std::string::npos)
            << output_;
      }
    }
  }

  std::string GetTestDexFileName() OVERRIDE {
    // Use Statics as it has a handful of functions.
    return CommonRuntimeTest::GetTestDexFileName("Statics");
  }

  void GrabResult1() {
    if (!kIsTargetBuild) {
      native_alloc_1_ = ParseNativeAlloc();
      swap_1_ = ParseSwap(false /* expected */);
    } else {
      native_alloc_1_ = std::numeric_limits<size_t>::max();
      swap_1_ = 0;
    }
  }

  void GrabResult2() {
    if (!kIsTargetBuild) {
      native_alloc_2_ = ParseNativeAlloc();
      swap_2_ = ParseSwap(true /* expected */);
    } else {
      native_alloc_2_ = 0;
      swap_2_ = std::numeric_limits<size_t>::max();
    }
  }

 private:
  size_t ParseNativeAlloc() {
    std::regex native_alloc_regex("dex2oat took.*native alloc=[^ ]+ \\(([0-9]+)B\\)");
    std::smatch native_alloc_match;
    bool found = std::regex_search(output_, native_alloc_match, native_alloc_regex);
    if (!found) {
      EXPECT_TRUE(found);
      return 0;
    }
    if (native_alloc_match.size() != 2U) {
      EXPECT_EQ(native_alloc_match.size(), 2U);
      return 0;
    }

    std::istringstream stream(native_alloc_match[1].str());
    size_t value;
    stream >> value;

    return value;
  }

  size_t ParseSwap(bool expected) {
    std::regex swap_regex("dex2oat took[^\\n]+swap=[^ ]+ \\(([0-9]+)B\\)");
    std::smatch swap_match;
    bool found = std::regex_search(output_, swap_match, swap_regex);
    if (found != expected) {
      EXPECT_EQ(expected, found);
      return 0;
    }

    if (!found) {
      return 0;
    }

    if (swap_match.size() != 2U) {
      EXPECT_EQ(swap_match.size(), 2U);
      return 0;
    }

    std::istringstream stream(swap_match[1].str());
    size_t value;
    stream >> value;

    return value;
  }

 protected:
  size_t native_alloc_1_;
  size_t native_alloc_2_;

  size_t swap_1_;
  size_t swap_2_;
};

TEST_F(Dex2oatSwapUseTest, CheckSwapUsage) {
  // The `native_alloc_2_ >= native_alloc_1_` assertion below may not
  // hold true on some x86 systems; disable this test while we
  // investigate (b/29259363).
  TEST_DISABLED_FOR_X86();

  RunTest(false /* use_fd */,
          false /* expect_use */);
  GrabResult1();
  std::string output_1 = output_;

  output_ = "";

  RunTest(false /* use_fd */,
          true /* expect_use */,
          { "--swap-dex-size-threshold=0", "--swap-dex-count-threshold=0" });
  GrabResult2();
  std::string output_2 = output_;

  if (native_alloc_2_ >= native_alloc_1_ || swap_1_ >= swap_2_) {
    EXPECT_LT(native_alloc_2_, native_alloc_1_);
    EXPECT_LT(swap_1_, swap_2_);

    LOG(ERROR) << output_1;
    LOG(ERROR) << output_2;
  }
}

class Dex2oatVeryLargeTest : public Dex2oatTest {
 protected:
  void CheckFilter(CompilerFilter::Filter input ATTRIBUTE_UNUSED,
                   CompilerFilter::Filter result ATTRIBUTE_UNUSED) OVERRIDE {
    // Ignore, we'll do our own checks.
  }

  void RunTest(CompilerFilter::Filter filter,
               bool expect_large,
               const std::vector<std::string>& extra_args = {}) {
    std::string dex_location = GetScratchDir() + "/DexNoOat.jar";
    std::string odex_location = GetOdexDir() + "/DexOdexNoOat.odex";

    Copy(GetDexSrc1(), dex_location);

    GenerateOdexForTest(dex_location, odex_location, filter, extra_args);

    CheckValidity();
    ASSERT_TRUE(success_);
    CheckResult(dex_location, odex_location, filter, expect_large);
  }

  void CheckResult(const std::string& dex_location,
                   const std::string& odex_location,
                   CompilerFilter::Filter filter,
                   bool expect_large) {
    // Host/target independent checks.
    std::string error_msg;
    std::unique_ptr<OatFile> odex_file(OatFile::Open(odex_location.c_str(),
                                                     odex_location.c_str(),
                                                     nullptr,
                                                     nullptr,
                                                     false,
                                                     /*low_4gb*/false,
                                                     dex_location.c_str(),
                                                     &error_msg));
    ASSERT_TRUE(odex_file.get() != nullptr) << error_msg;
    if (expect_large) {
      // Note: we cannot check the following:
      //   EXPECT_TRUE(CompilerFilter::IsAsGoodAs(CompilerFilter::kVerifyAtRuntime,
      //                                          odex_file->GetCompilerFilter()));
      // The reason is that the filter override currently happens when the dex files are
      // loaded in dex2oat, which is after the oat file has been started. Thus, the header
      // store cannot be changed, and the original filter is set in stone.

      for (const OatDexFile* oat_dex_file : odex_file->GetOatDexFiles()) {
        std::unique_ptr<const DexFile> dex_file = oat_dex_file->OpenDexFile(&error_msg);
        ASSERT_TRUE(dex_file != nullptr);
        uint32_t class_def_count = dex_file->NumClassDefs();
        ASSERT_LT(class_def_count, std::numeric_limits<uint16_t>::max());
        for (uint16_t class_def_index = 0; class_def_index < class_def_count; ++class_def_index) {
          OatFile::OatClass oat_class = oat_dex_file->GetOatClass(class_def_index);
          EXPECT_EQ(oat_class.GetType(), OatClassType::kOatClassNoneCompiled);
        }
      }

      // If the input filter was "below," it should have been used.
      if (!CompilerFilter::IsAsGoodAs(CompilerFilter::kVerifyAtRuntime, filter)) {
        EXPECT_EQ(odex_file->GetCompilerFilter(), filter);
      }
    } else {
      EXPECT_EQ(odex_file->GetCompilerFilter(), filter);
    }

    // Host/target dependent checks.
    if (kIsTargetBuild) {
      CheckTargetResult(expect_large);
    } else {
      CheckHostResult(expect_large);
    }
  }

  void CheckTargetResult(bool expect_large ATTRIBUTE_UNUSED) {
    // TODO: Ignore for now. May do something for fd things.
  }

  void CheckHostResult(bool expect_large) {
    if (!kIsTargetBuild) {
      if (expect_large) {
        EXPECT_NE(output_.find("Very large app, downgrading to verify-at-runtime."),
                  std::string::npos)
            << output_;
      } else {
        EXPECT_EQ(output_.find("Very large app, downgrading to verify-at-runtime."),
                  std::string::npos)
            << output_;
      }
    }
  }

  // Check whether the dex2oat run was really successful.
  void CheckValidity() {
    if (kIsTargetBuild) {
      CheckTargetValidity();
    } else {
      CheckHostValidity();
    }
  }

  void CheckTargetValidity() {
    // TODO: Ignore for now.
  }

  // On the host, we can get the dex2oat output. Here, look for "dex2oat took."
  void CheckHostValidity() {
    EXPECT_NE(output_.find("dex2oat took"), std::string::npos) << output_;
  }
};

TEST_F(Dex2oatVeryLargeTest, DontUseVeryLarge) {
  RunTest(CompilerFilter::kVerifyNone, false);
  RunTest(CompilerFilter::kVerifyAtRuntime, false);
  RunTest(CompilerFilter::kInterpretOnly, false);
  RunTest(CompilerFilter::kSpeed, false);

  RunTest(CompilerFilter::kVerifyNone, false, { "--very-large-app-threshold=1000000" });
  RunTest(CompilerFilter::kVerifyAtRuntime, false, { "--very-large-app-threshold=1000000" });
  RunTest(CompilerFilter::kInterpretOnly, false, { "--very-large-app-threshold=1000000" });
  RunTest(CompilerFilter::kSpeed, false, { "--very-large-app-threshold=1000000" });
}

TEST_F(Dex2oatVeryLargeTest, UseVeryLarge) {
  RunTest(CompilerFilter::kVerifyNone, false, { "--very-large-app-threshold=100" });
  RunTest(CompilerFilter::kVerifyAtRuntime, false, { "--very-large-app-threshold=100" });
  RunTest(CompilerFilter::kInterpretOnly, true, { "--very-large-app-threshold=100" });
  RunTest(CompilerFilter::kSpeed, true, { "--very-large-app-threshold=100" });
}

// Regressin test for b/35665292.
TEST_F(Dex2oatVeryLargeTest, SpeedProfileNoProfile) {
  // Test that dex2oat doesn't crash with speed-profile but no input profile.
  RunTest(CompilerFilter::kSpeedProfile, false);
}

class Dex2oatLayoutTest : public Dex2oatTest {
 protected:
  void CheckFilter(CompilerFilter::Filter input ATTRIBUTE_UNUSED,
                   CompilerFilter::Filter result ATTRIBUTE_UNUSED) OVERRIDE {
    // Ignore, we'll do our own checks.
  }

  // Emits a profile with a single dex file with the given location and a single class index of 1.
  void GenerateProfile(const std::string& test_profile,
                       const std::string& dex_location,
                       uint32_t checksum) {
    int profile_test_fd = open(test_profile.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    CHECK_GE(profile_test_fd, 0);

    ProfileCompilationInfo info;
    std::string profile_key = ProfileCompilationInfo::GetProfileDexFileKey(dex_location);
    info.AddClassIndex(profile_key, checksum, dex::TypeIndex(1));
    bool result = info.Save(profile_test_fd);
    close(profile_test_fd);
    ASSERT_TRUE(result);
  }

  void RunTest() {
    std::string dex_location = GetScratchDir() + "/DexNoOat.jar";
    std::string profile_location = GetScratchDir() + "/primary.prof";
    std::string odex_location = GetOdexDir() + "/DexOdexNoOat.odex";

    Copy(GetDexSrc2(), dex_location);
    const char* location = dex_location.c_str();
    std::string error_msg;
    std::vector<std::unique_ptr<const DexFile>> dex_files;
    ASSERT_TRUE(DexFile::Open(location, location, true, &error_msg, &dex_files));
    EXPECT_EQ(dex_files.size(), 1U);
    std::unique_ptr<const DexFile>& dex_file = dex_files[0];
    GenerateProfile(profile_location, dex_location, dex_file->GetLocationChecksum());

    const std::vector<std::string>& extra_args = { "--profile-file=" + profile_location };
    GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kSpeedProfile, extra_args);

    CheckValidity();
    ASSERT_TRUE(success_);
    CheckResult(dex_location, odex_location);
  }
  void CheckResult(const std::string& dex_location, const std::string& odex_location) {
    // Host/target independent checks.
    std::string error_msg;
    std::unique_ptr<OatFile> odex_file(OatFile::Open(odex_location.c_str(),
                                                     odex_location.c_str(),
                                                     nullptr,
                                                     nullptr,
                                                     false,
                                                     /*low_4gb*/false,
                                                     dex_location.c_str(),
                                                     &error_msg));
    ASSERT_TRUE(odex_file.get() != nullptr) << error_msg;

    const char* location = dex_location.c_str();
    std::vector<std::unique_ptr<const DexFile>> dex_files;
    ASSERT_TRUE(DexFile::Open(location, location, true, &error_msg, &dex_files));
    EXPECT_EQ(dex_files.size(), 1U);
    std::unique_ptr<const DexFile>& old_dex_file = dex_files[0];

    for (const OatDexFile* oat_dex_file : odex_file->GetOatDexFiles()) {
      std::unique_ptr<const DexFile> new_dex_file = oat_dex_file->OpenDexFile(&error_msg);
      ASSERT_TRUE(new_dex_file != nullptr);
      uint32_t class_def_count = new_dex_file->NumClassDefs();
      ASSERT_LT(class_def_count, std::numeric_limits<uint16_t>::max());
      ASSERT_GE(class_def_count, 2U);

      // The new layout swaps the classes at indexes 0 and 1.
      std::string old_class0 = old_dex_file->PrettyType(old_dex_file->GetClassDef(0).class_idx_);
      std::string old_class1 = old_dex_file->PrettyType(old_dex_file->GetClassDef(1).class_idx_);
      std::string new_class0 = new_dex_file->PrettyType(new_dex_file->GetClassDef(0).class_idx_);
      std::string new_class1 = new_dex_file->PrettyType(new_dex_file->GetClassDef(1).class_idx_);
      EXPECT_EQ(old_class0, new_class1);
      EXPECT_EQ(old_class1, new_class0);
    }

    EXPECT_EQ(odex_file->GetCompilerFilter(), CompilerFilter::kSpeedProfile);
  }

    // Check whether the dex2oat run was really successful.
    void CheckValidity() {
      if (kIsTargetBuild) {
        CheckTargetValidity();
      } else {
        CheckHostValidity();
      }
    }

    void CheckTargetValidity() {
      // TODO: Ignore for now.
    }

    // On the host, we can get the dex2oat output. Here, look for "dex2oat took."
    void CheckHostValidity() {
      EXPECT_NE(output_.find("dex2oat took"), std::string::npos) << output_;
    }
  };

TEST_F(Dex2oatLayoutTest, TestLayout) {
  RunTest();
}

class Dex2oatWatchdogTest : public Dex2oatTest {
 protected:
  void RunTest(bool expect_success, const std::vector<std::string>& extra_args = {}) {
    std::string dex_location = GetScratchDir() + "/Dex2OatSwapTest.jar";
    std::string odex_location = GetOdexDir() + "/Dex2OatSwapTest.odex";

    Copy(GetTestDexFileName(), dex_location);

    std::vector<std::string> copy(extra_args);

    std::string swap_location = GetOdexDir() + "/Dex2OatSwapTest.odex.swap";
    copy.push_back("--swap-file=" + swap_location);
    GenerateOdexForTest(dex_location,
                        odex_location,
                        CompilerFilter::kSpeed,
                        copy,
                        expect_success);
  }

  std::string GetTestDexFileName() {
    return GetDexSrc1();
  }
};

TEST_F(Dex2oatWatchdogTest, TestWatchdogOK) {
  // Check with default.
  RunTest(true);

  // Check with ten minutes.
  RunTest(true, { "--watchdog-timeout=600000" });
}

TEST_F(Dex2oatWatchdogTest, TestWatchdogTrigger) {
  // Check with ten milliseconds.
  RunTest(false, { "--watchdog-timeout=10" });
}

}  // namespace art
