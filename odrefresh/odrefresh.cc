/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <dirent.h>
#include <ftw.h>
#include <sysexits.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <queue>
#include <string>
#include <sstream>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/macros.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android/log.h>
#include <arch/instruction_set.h>
#include <base/bit_utils.h>
#include <base/file_utils.h>
#include <base/globals.h>
#include <base/macros.h>
#include <base/os.h>
#include <base/string_view_cpp20.h>
#include <base/unix_file/fd_file.h>
#include <base/utils.h>
#include <com_android_apex.h>
#include <exec_utils.h>
#include <palette/palette_types.h>
#include <palette/palette.h>

#include "../dexoptanalyzer/dexoptanalyzer.h"

namespace art {
namespace odrefresh {
namespace {

static void UsageErrorV(const char* fmt, va_list ap) {
  std::string error;
  android::base::StringAppendV(&error, fmt, ap);
  if (isatty(fileno(stderr))) {
    std::cerr << error << std::endl;
  } else {
    LOG(ERROR) << error;
  }
}

static void UsageError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UsageErrorV(fmt, ap);
  va_end(ap);
}

NO_RETURN static void ArgumentError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UsageErrorV(fmt, ap);
  va_end(ap);
  UsageError("Try '--help' for more information.");
  exit(EX_USAGE);
}

NO_RETURN static void UsageHelp(const char* argv0) {
  std::string name(Basename(argv0));
  UsageError("Usage: %s ACTION", name.c_str());
  UsageError("On-device refresh tool for boot class path extensions and system server");
  UsageError("following an update of the ART APEX.");
  UsageError("");
  UsageError("Valid ACTION choices are:");
  UsageError("");
  UsageError("--check          Check compilation artifacts are up to date.");
  UsageError("--compile        Compile boot class path extensions and system_server jars");
  UsageError("                 when necessary).");
  UsageError("--force-compile  Unconditionally compile the boot class path extensions and");
  UsageError("                 system_server jars.");
  UsageError("--help           Display this help information.");
  exit(EX_USAGE);
}

static std::string Concatenate(std::initializer_list<std::string_view> args) {
  std::stringstream ss;
  for (auto arg : args) {
    ss << arg;
  }
  return ss.str();
}

static std::string QuotePath(std::string_view path) {
  return Concatenate({"'", path, "'"});
}

static void EraseFiles(std::vector<std::unique_ptr<File>>& files) {
  for (auto& file : files) {
    file->Erase(/*unlink=*/ true);
  }
}

static bool MoveOrEraseFiles(std::vector<std::unique_ptr<File>>& files,
                             std::string_view output_directory_path) {
  for (auto& file : files) {
    const std::string file_basename(Basename(file->GetPath()));
    const std::string output_file_path = Concatenate({output_directory_path, "/", file_basename});
    const std::string input_file_path = file->GetPath();

    std::unique_ptr<File> output_file(OS::CreateEmptyFileWriteOnly(output_file_path.c_str()));
    if (output_file == nullptr) {
      PLOG(ERROR) << "Failed to open " << QuotePath(output_file_path);
      EraseFiles(files);
      output_file->Erase();
      return false;
    }

    const size_t file_bytes = file->GetLength();
    if (!output_file->Copy(file.get(), /*offset=*/ 0, file_bytes)) {
      PLOG(ERROR) << "Failed to copy " << QuotePath(file->GetPath())
                  << " to " << QuotePath(output_file_path);
      EraseFiles(files);
      output_file->Erase();
      return false;
    }

    if (!file->Erase(/*unlink=*/ true)) {
      PLOG(ERROR) << "Failed to erase " << QuotePath(file->GetPath());
      EraseFiles(files);
      output_file->Erase();
      return false;
    }

    if (output_file->FlushCloseOrErase() != 0) {
      PLOG(ERROR) << "Failed to flush and close file " << QuotePath(output_file_path);
      EraseFiles(files);
    }
  }
  return true;
}

}  // namespace

enum class ZygoteKind : uint8_t {
  kZygote32 = 0,
  kZygote32_64 = 1,
  kZygote64_32 = 2,
  kZygote64 = 3
};

bool ParseZygoteKind(const char* input, ZygoteKind* zygote_kind) {
  std::string_view z(input);
  if (z == "zygote32") {
    *zygote_kind = ZygoteKind::kZygote32;
    return true;
  } else if (z == "zygote32_64") {
    *zygote_kind = ZygoteKind::kZygote32_64;
    return true;
  } else if (z == "zygote64_32") {
    *zygote_kind = ZygoteKind::kZygote64_32;
    return true;
  } else if (z == "zygote64") {
    *zygote_kind = ZygoteKind::kZygote64;
    return true;
  }
  return false;
}

class OdrArtifacts {
 public:
  static OdrArtifacts ForBootImageExtension(const std::string& image_location) {
    return OdrArtifacts(image_location, "oat");
  }

  static OdrArtifacts ForSystemServer(const std::string& image_location) {
    return OdrArtifacts(image_location, "odex");
  }

  const std::string& ImageLocation() const { return image_location_; }
  const std::string& OatLocation() const { return oat_location_; }
  const std::string& VdexLocation() const { return vdex_location_; }

 private:
  OdrArtifacts(const std::string& image_location, const char* aot_extension)
    : image_location_{image_location},
      oat_location_{ReplaceFileExtension(image_location, aot_extension)},
      vdex_location_{ReplaceFileExtension(image_location, "vdex")} {}

  const std::string image_location_;
  const std::string oat_location_;
  const std::string vdex_location_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(OdrArtifacts);
};

class OdrConfig final {
 public:
  OdrConfig() : dry_run_(false), isa_(InstructionSet::kNone) {}

  const std::string& GetApexInfoListFile() const { return apex_info_list_file_; }

  std::vector<InstructionSet> GetBootExtensionIsas() const {
    const auto [isa32, isa64] = GetInstructionSets();
    switch (zygote_kind_) {
      case ZygoteKind::kZygote32: return { isa32 };
      case ZygoteKind::kZygote32_64: return { isa32, isa64 };
      case ZygoteKind::kZygote64_32: return { isa32, isa64 };
      case ZygoteKind::kZygote64: return { isa64 };
    }
  }

  InstructionSet GetSystemServerIsa() const {
    const auto [isa32, isa64] = GetInstructionSets();
    switch (zygote_kind_) {
      case ZygoteKind::kZygote32: return isa32;
      case ZygoteKind::kZygote32_64: return isa32;
      case ZygoteKind::kZygote64_32: return isa64;
      case ZygoteKind::kZygote64: return isa64;
    }
  }

  const std::vector<std::string>& GetDex2oatBootclasspath() const { return dex2oat_bootclasspath_; }

  std::string GetDex2Oat() const {
    switch (zygote_kind_) {
      case ZygoteKind::kZygote32:
        return art_bin_dir_ + "/dex2oat32";
      case ZygoteKind::kZygote32_64:
      case ZygoteKind::kZygote64_32:
      case ZygoteKind::kZygote64:
        return art_bin_dir_ + "/dex2oat64";
    }
  }

  std::string GetDexOptAnalyzer() const {
    return art_bin_dir_ + "/dexoptanalyzer";
  }

  bool GetDryRun() const { return dry_run_; }

  const std::vector<std::string>& GetSystemServerClasspath() const {
    return system_server_classpath_;
  }

  const std::string& GetUpdatableBcpPackagesFile() const { return updatable_bcp_packages_file_; }

  void SetApexInfoListFile(const std::string& file_path) { apex_info_list_file_ = file_path; }
  void SetArtBinDir(const std::string& art_bin_dir) { art_bin_dir_ = art_bin_dir; }

  void SetDex2oatBootclasspath(const std::string& v) {
    dex2oat_bootclasspath_ = android::base::Split(v, ":");
  }

  void SetDryRun() { dry_run_ = true; }
  void SetIsa(const InstructionSet isa) { isa_ = isa; }

  void SetSystemServerClasspath(const std::string& v) {
    system_server_classpath_ = android::base::Split(v, ":");
  }

  void SetUpdatableBcpPackagesFile(const std::string& file) { updatable_bcp_packages_file_ = file; }
  void SetZygoteKind(const ZygoteKind zygote_kind) { zygote_kind_ = zygote_kind; }

 private:
  std::pair<InstructionSet, InstructionSet> GetInstructionSets() const {
    switch (isa_) {
      case art::InstructionSet::kArm:
      case art::InstructionSet::kArm64:
        return std::make_pair(art::InstructionSet::kArm, art::InstructionSet::kArm64);
      case art::InstructionSet::kX86:
      case art::InstructionSet::kX86_64:
        return std::make_pair(art::InstructionSet::kX86, art::InstructionSet::kX86_64);
      case art::InstructionSet::kThumb2:
      case art::InstructionSet::kNone:
        LOG(FATAL) << "Invalid instruction set " << isa_;
        return std::make_pair(art::InstructionSet::kNone, art::InstructionSet::kNone);
    }
  }

  std::string apex_info_list_file_;
  std::string art_bin_dir_;
  std::vector<std::string> dex2oat_bootclasspath_;
  bool dry_run_;
  InstructionSet isa_;
  std::vector<std::string> system_server_classpath_;
  std::string updatable_bcp_packages_file_;
  ZygoteKind zygote_kind_;

  DISALLOW_COPY_AND_ASSIGN(OdrConfig);
};

class OnDeviceRefresh final {
 private:
  // Maximum execution time for odrefresh from start to end.
  static constexpr time_t kMaximumExecutionSeconds = 300;

  // Maximum execution time for any child process spawned.
  static constexpr time_t kMaxChildProcessSeconds = 90;

  const OdrConfig& config_;

  std::string boot_extension_output_dir_;
  std::vector<std::string> boot_extension_compilable_jars_;

  std::string systemserver_output_dir_;
  std::vector<std::string> systemserver_compilable_jars_;

  const time_t start_time_;

 public:
  explicit OnDeviceRefresh(const OdrConfig& config) : config_(config), start_time_(time(nullptr)) {
    std::string art_apex_data = GetArtApexData();
    boot_extension_output_dir_ = Concatenate({art_apex_data, "/system/framework"});
    for (const auto& component : config_.GetDex2oatBootclasspath()) {
      if (IsCompilableBootExtension(component)) {
        boot_extension_compilable_jars_.emplace_back(component);
      }
    }

    systemserver_output_dir_ = Concatenate({art_apex_data, "/system/framework/oat"});
    for (const auto& component : config_.GetSystemServerClasspath()) {
      if (IsCompilableSystemServerJar(component)) {
        systemserver_compilable_jars_.emplace_back(component);
      }
    }
    systemserver_compilable_jars_.emplace_back(GetAndroidRoot() + "/framework/services.jar");
  }

  time_t GetExecutionTimeUsed() const {
    return time(nullptr) - start_time_;
  }

  time_t GetExecutionTimeRemaining() const {
    return kMaximumExecutionSeconds - GetExecutionTimeUsed();
  }

  time_t GetSubprocessTimeout() const {
    return std::max(GetExecutionTimeRemaining(), kMaxChildProcessSeconds);
  }

  // Read apex_info_list.xml from input stream and determine if the ART APEX
  // listed is the factory installed version.
  static bool IsFactoryApex(const std::string& apex_info_list_xml_path) {
    auto info_list = com::android::apex::readApexInfoList(apex_info_list_xml_path.c_str());
    if (!info_list.has_value()) {
      LOG(FATAL) << "Failed to process " << QuotePath(apex_info_list_xml_path);
    }

    for (const com::android::apex::ApexInfo& info : info_list->getApexInfo()) {
      if (info.getIsActive() && info.getModuleName() == "com.android.art") {
        return info.getIsFactory();
      }
    }

    LOG(FATAL) << "Failed to find active com.android.art in " << QuotePath(apex_info_list_xml_path);
    return false;
  }

  static void AddDex2OatBootTimeArguments(std::vector<std::string>* args) {
    static constexpr std::pair<const char*, const char*> kPropertyArgPairs[] = {
      std::make_pair("dalvik.vm.boot-dex2oat-cpu-set", "--cpu-set="),
      std::make_pair("dalvik.vm.boot-dex2oat-threads", "-j"),
    };
    for (auto property_arg_pair : kPropertyArgPairs) {
      auto [property, arg] = property_arg_pair;
      std::string value = android::base::GetProperty(property, {});
      if (!value.empty()) {
        args->push_back(arg + value);
      }
    }
  }

  bool CheckSystemServerArtifacts(bool on_system) const {
    std::vector<std::string> classloader_context;
    for (const std::string& jar_path : systemserver_compilable_jars_) {
      std::vector<std::string> args;
      args.emplace_back(config_.GetDexOptAnalyzer());
      args.emplace_back("--dex-file=" + jar_path);

      const std::string image_location = GetSystemServerImageLocation(on_system, jar_path);

      // odrefresh produces image files, but these are not guaranteed for those pre-installed on
      // system.
      if (!on_system && !OS::FileExists(image_location.c_str(), true)) {
        LOG(INFO) << "Missing image file: " << QuotePath(image_location);
        return false;
      }

      // Generate set of artifacts that are output by compilation.
      OdrArtifacts artifacts = OdrArtifacts::ForSystemServer(image_location);

      // Associate inputs and outputs with dexoptanalyzer arguments.
      std::pair<const std::string, const char*> location_args[] = {
        std::make_pair(artifacts.OatLocation(), "--oat-fd="),
        std::make_pair(artifacts.VdexLocation(), "--vdex-fd="),
        std::make_pair(jar_path, "--zip-fd=")
      };

      // Open file descriptors for dexoptanalyzer file inputs and add to the command-line.
      std::vector<std::unique_ptr<File>> files;
      for (const auto& location_arg : location_args) {
        auto& [location, arg] = location_arg;
        std::unique_ptr<File> file(OS::OpenFileForReading(location.c_str()));
        if (file == nullptr) {
          PLOG(ERROR) << "Failed to open \"" << location << "\"";
          return false;
        }
        args.emplace_back(android::base::StringPrintf("%s%d", arg, file->Fd()));
        files.emplace_back(file.release());
      }

      const std::string basename(Basename(jar_path));
      const std::string root = GetAndroidRoot();
      const std::string profile_file = Concatenate({root, "/framework/", basename, ".prof"});
      if (OS::FileExists(profile_file.c_str())) {
        args.emplace_back("--compiler-filter=speed-profile");
      } else {
        args.emplace_back("--compiler-filter=speed");
      }

      args.emplace_back(
        Concatenate({"--image=", GetBootImage(), ":", GetBootImageExtensionImage(on_system)}));
      args.emplace_back(
        Concatenate({"--isa=", GetInstructionSetString(config_.GetSystemServerIsa())}));
      args.emplace_back("--runtime-arg");
      args.emplace_back(
        "-Xbootclasspath:" + android::base::Join(config_.GetDex2oatBootclasspath(), ':'));
      args.emplace_back(Concatenate({
        "--class-loader-context=PCL[", android::base::Join(classloader_context, ':'), "]"}));

      classloader_context.emplace_back(jar_path);

      LOG(INFO) << "Checking " << jar_path << ": " << android::base::Join(args, ' ');
      if (config_.GetDryRun()) {
        return true;
      }

      std::string error_msg;
      bool timed_out = false;
      const time_t timeout = GetSubprocessTimeout();
      const int dexoptanalyzer_result = ExecAndReturnCode(args, timeout, &timed_out, &error_msg);
      if (dexoptanalyzer_result == -1) {
        LOG(ERROR) << "Unexpected exit from dexoptanalyzer: " << error_msg;
        if (timed_out) {
          // TODO(oth): record metric for timeout.
        }
        return false;
      }
      LOG(INFO) << "dexoptanalyzer returned " << dexoptanalyzer_result;

      bool unexpected_result = true;
      switch (static_cast<dexoptanalyzer::ReturnCode>(dexoptanalyzer_result)) {
        case art::dexoptanalyzer::ReturnCode::kNoDexOptNeeded:
          unexpected_result = false;
          break;

        // Recompile needed
        case art::dexoptanalyzer::ReturnCode::kDex2OatFromScratch:
        case art::dexoptanalyzer::ReturnCode::kDex2OatForBootImageOat:
        case art::dexoptanalyzer::ReturnCode::kDex2OatForFilterOat:
        case art::dexoptanalyzer::ReturnCode::kDex2OatForBootImageOdex:
        case art::dexoptanalyzer::ReturnCode::kDex2OatForFilterOdex:
          return false;

        // Unexpected issues (note no default-case here to catch missing enum values, but the
        // return code from dexoptanalyzer may also be outside expected values, such as a
        // process crash.
        case art::dexoptanalyzer::ReturnCode::kFlattenClassLoaderContextSuccess:
        case art::dexoptanalyzer::ReturnCode::kErrorInvalidArguments:
        case art::dexoptanalyzer::ReturnCode::kErrorCannotCreateRuntime:
        case art::dexoptanalyzer::ReturnCode::kErrorUnknownDexOptNeeded:
          break;
      }

      if (unexpected_result) {
        LOG(ERROR) << "Unexpected result from dexoptanalyzer: " << dexoptanalyzer_result;
        return false;
      }
    }
    return true;
  }

  // Check the validity of system server artifacts on both /system and /data.
  // Returns true if any valid artifacts are found.
  bool CheckSystemServerArtifacts() const {
    return (CheckSystemServerArtifacts(/*on_system=*/ true) ||
            CheckSystemServerArtifacts(/*on_system=*/ false));
  }

  // Check the validity of boot class path extension artifacts.
  //
  // If `on_system` is true, this method checks for artifacts on the /system partition and will
  // remove artifacts from /data if the /system versions are good.
  //
  // If `on_system` is false, this method checks for artifacts on the /data partition and will
  // only remove those artifacts if checking them times out.
  //
  // Returns true if artifacts exist and are valid according to dexoptanalyzer.
  bool CheckBootExtensionArtifacts(const InstructionSet isa, bool on_system) const {
    const std::string dex_file = boot_extension_compilable_jars_.front();
    const std::string image_location = GetBootImageExtensionImage(on_system);

    std::vector<std::string> args;
    args.emplace_back(config_.GetDexOptAnalyzer());
    args.emplace_back("--validate-bcp");
    args.emplace_back(Concatenate({"--image=", GetBootImage(), ":", image_location}));
    args.emplace_back(Concatenate({"--isa=", GetInstructionSetString(isa)}));
    args.emplace_back("--runtime-arg");
    args.emplace_back(
      "-Xbootclasspath:" + android::base::Join(config_.GetDex2oatBootclasspath(), ':'));

    LOG(INFO) << "Checking " << dex_file << ": " << android::base::Join(args, ' ');
    if (config_.GetDryRun()) {
      return true;
    }

    std::string error_msg;
    bool timed_out = false;
    const time_t timeout = GetSubprocessTimeout();
    const int dexoptanalyzer_result = ExecAndReturnCode(args, timeout, &timed_out, &error_msg);
    if (dexoptanalyzer_result == -1) {
      LOG(ERROR) << "Unexpected exit from dexoptanalyzer: " << error_msg;
      if (timed_out) {
        // TODO(oth): record metric for timeout.
        if (!on_system) {
          LOG(INFO) << "Removing suspect boot extension artifacts.";
          RemoveArtifacts(OdrArtifacts::ForBootImageExtension(image_location));
        }
      }
      return false;
    }
    auto rc = static_cast<dexoptanalyzer::ReturnCode>(dexoptanalyzer_result);
    if (rc == dexoptanalyzer::ReturnCode::kNoDexOptNeeded) {
      // Boot extension artifacts on /system look good, clean the equivalents on /data.
      // When runtime starts it uses the apexdata files in preference to /system (if they exist).
      const std::string apexdata_image_location = GetBootImageExtensionImage(false);
      LOG(INFO) << "Removing unneeded artifacts for " << QuotePath(apexdata_image_location);
      RemoveArtifacts(OdrArtifacts::ForBootImageExtension(apexdata_image_location));
      return true;
    }
    if (!on_system) {
      LOG(INFO) << "Removing stale boot extension artifacts.";
      RemoveArtifacts(OdrArtifacts::ForBootImageExtension(image_location));
    }
    return false;
  }

  // Check whether boot extension artifacts for `isa` are valid on system partition or in apexdata.
  // Returns true if valid boot externsion artifacts are valid.
  bool CheckBootExtensionArtifacts(const InstructionSet isa) const {
    return (CheckBootExtensionArtifacts(isa, /*on_system=*/ true) ||
            CheckBootExtensionArtifacts(isa, /*on_system=*/ false));
  }

  static bool GetFreeSpace(const char* path, uint64_t* bytes) {
    struct statvfs sv;
    if (statvfs(path, &sv) != 0) {
      PLOG(ERROR) << "statvfs '" << path << "'";
      return false;
    }
    *bytes = sv.f_bfree * sv.f_bsize;
    return true;
  }

  static bool GetUsedSpace(const char* path, uint64_t* bytes) {
    *bytes = 0;

    std::queue<std::string> unvisited;
    unvisited.push(path);
    while (!unvisited.empty()) {
      std::string current = unvisited.front();
      std::unique_ptr<DIR, int (*)(DIR*)> dir(opendir(current.c_str()), closedir);
      for (auto entity = readdir(dir.get()); entity != nullptr; entity = readdir(dir.get())) {
        if (entity->d_name[0] == '.') {
          continue;
        }
        std::string entity_name = Concatenate({current, "/", entity->d_name});
        if (entity->d_type == DT_DIR) {
          unvisited.push(entity_name.c_str());
        } else if (entity->d_type == DT_REG) {
          // RoundUp file size to number of blocks.
          *bytes += RoundUp(OS::GetFileSizeBytes(entity_name.c_str()), 512);
        } else {
          LOG(FATAL) << "Unsupported directory entry type: " << static_cast<int>(entity->d_type);
        }
      }
      unvisited.pop();
    }
    return true;
  }

  static void ReportSpace() {
    uint64_t bytes;
    std::string data_dir = GetArtApexData();
    if (GetUsedSpace(data_dir.c_str(), &bytes)) {
      LOG(INFO) << "Used space " << bytes << " bytes.";
    }
    if (GetFreeSpace(data_dir.c_str(), &bytes)) {
      LOG(INFO) << "Available space " << bytes << " bytes.";
    }
  }

  int CheckArtifacts(bool is_system_apex) {
    for (const InstructionSet isa : config_.GetBootExtensionIsas()) {
      bool okay = false;
      if (is_system_apex) {
        // Check for artifacts on /system first.
        okay = CheckBootExtensionArtifacts(isa, /*on_system=*/ true);
      }
      if (!okay && !CheckBootExtensionArtifacts(isa, /*on_system=*/ false)) {
        LOG(INFO) << "Boot extensions (" << GetInstructionSetString(isa) << ") status: Out-of-date";
        return ExitCode::kCompilationRequired;
      }
      LOG(INFO) << "Boot extensions (" << GetInstructionSetString(isa) << ") status: OK";
    }

    if (is_system_apex && CheckSystemServerArtifacts(/*on_system=*/ true)) {
      LOG(INFO) << "System server on /system status: OK";
      return ExitCode::kOkay;
    }
    if (CheckSystemServerArtifacts(/*on_system=*/ false)) {
      LOG(INFO) << "System server on /data status: OK)";
      return ExitCode::kOkay;
    }
    LOG(INFO) << "System server status: Out-of-date";
    return ExitCode::kCompilationRequired;
  }

  static int RemoveArtifact(const char* fpath, const struct stat*, int typeflag, struct FTW* ftw) {
    switch (typeflag) {
      case FTW_F:
      case FTW_SL:
      case FTW_SLN:
        if (unlink(fpath)) {
          PLOG(FATAL) << "Failed unlink(\"" << fpath << "\")";
        }
        return 0;

      case FTW_DP:
        if (ftw->level == 0) {
          return 0;
        }
        if (rmdir(fpath) != 0) {
          PLOG(FATAL) << "Failed rmdir(\"" << fpath << "\")";
        }
        return 0;

      case FTW_DNR:
        LOG(FATAL) << "Inaccessible directory \"" << fpath << "\"";
        return -1;

      case FTW_NS:
        LOG(FATAL) << "Failed stat() \"" << fpath << "\"";
        return -1;

      default:
        LOG(FATAL) << "Unexpected typeflag " << typeflag << "for \"" << fpath << "\"";
        return -1;
    }
  }

  void RemoveArtifactsOrDie() const {
    // Remove everything under ArtApexDataDir
    std::string data_dir = GetArtApexData();

    // Perform depth first traversal removing artifacts.
    nftw(data_dir.c_str(), RemoveArtifact, 1, FTW_DEPTH | FTW_MOUNT);
  }

  void RemoveArtifacts(const OdrArtifacts& artifacts) const {
    for (const auto& location : { artifacts.ImageLocation(),
                                  artifacts.OatLocation(),
                                  artifacts.VdexLocation()}) {
      if (OS::FileExists(location.c_str()) && TEMP_FAILURE_RETRY(unlink(location.c_str())) != 0) {
        PLOG(ERROR) << "Failed to remove: " << QuotePath(location);
      }
    }
  }

  void RemoveStagingFilesOrDie(const char* staging_dir) const {
    if (OS::DirectoryExists(staging_dir)) {
      nftw(staging_dir, RemoveArtifact, 1, FTW_DEPTH | FTW_MOUNT);
    }
  }

  static bool IsCompilableBootExtension(const std::string_view& jar_path) {
    // Jar files in com.android.i18n are considered compilable because the APEX is not updatable.
    if (StartsWith(jar_path, "/apex/com.android.i18n")) {
      return true;
    }

    // Do not compile extensions from updatable APEXes.
    return !StartsWith(jar_path, "/apex");
  }

  static bool IsCompilableSystemServerJar(const std::string_view& jar_path) {
    // Do not compile jar files from updatable APEXes.
    return !StartsWith(jar_path, "/apex");
  }

  // Create all directory and all required parents.
  static void EnsureDirectoryExists(std::string_view absolute_path) {
    CHECK(absolute_path.size() > 0 && absolute_path[0] == '/');
    std::string path;
    VisitTokens(absolute_path, '/', [&path](std::string_view directory) {
      path.append("/").append(directory);
      if (!OS::DirectoryExists(path.c_str())) {
        if (mkdir(path.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
          PLOG(FATAL) << "Could not create directory: " << path.c_str();
        }
      }
    });
  }

  static std::string GetBootImage() {
    // Typically "/apex/com.android.art/javalib/boot.art".
    return GetArtRoot() + "/javalib/boot.art";
  }

  std::string GetBootImageExtensionImage(bool on_system) const {
    CHECK(!boot_extension_compilable_jars_.empty());
    std::string_view basename = Basename(boot_extension_compilable_jars_[0], ".jar");
    if (on_system) {
      // Typically "/system/framework/boot-framework.art".
      return Concatenate({GetAndroidRoot(), "/framework/boot-", basename, ".art"});
    } else {
      // Typically "/data/misc/apexdata/system/framework/boot-framework.art".
      return Concatenate({boot_extension_output_dir_, "/boot-", basename, ".art"});
    }
  }

  std::string GetBootImageExtensionImageLocation(const InstructionSet isa) const {
    // Typically "/data/misc/apexdata/system/framework/<arch>/boot-framework.art".
    return GetSystemImageFilename(GetBootImageExtensionImage(/*on_system=*/ false).c_str(), isa);
  }

  std::string GetSystemServerImageLocation(bool on_system, const std::string& jar_path) const {
    std::string_view basename = Basename(jar_path, ".jar");
    const char* isa_str = GetInstructionSetString(config_.GetSystemServerIsa());
    if (on_system) {
      // Typically "/system/framework/oat/<arch>/<basename>.art".
      return Concatenate({GetAndroidRoot(), "/framework/oat/", isa_str, "/", basename, ".art"});
    } else {
      // Typically "/data/misc/apexdata/system/framework/oat/<arch>/<basename>.art".
      return Concatenate({systemserver_output_dir_, "/", isa_str, "/", basename, ".art"});
    }
  }

  std::string GetStagingLocation(const std::string_view& staging_dir,
                                 const std::string_view& path) const {
    return Concatenate({staging_dir, "/", Basename(path)});
  }

  bool CompileBootExtensionArtifacts(const InstructionSet isa,
                                     std::string_view staging_dir,
                                     std::string* error_msg) const {
    std::vector<std::string> args;
    args.push_back(config_.GetDex2Oat());

    // Convert davlik.vm.boot-dex2oat-* properties to arguments.
    AddDex2OatBootTimeArguments(&args);

    // Add optional arguments.
    const std::string boot_profile_file(GetAndroidRoot() + "/etc/boot-image.prof");
    if (OS::FileExists(boot_profile_file.c_str())) {
      args.emplace_back(Concatenate({"--profile-file=", boot_profile_file}));
    } else {
      LOG(WARNING) << "Missing boot image profile: " << QuotePath(boot_profile_file);
    }
    const std::string dirty_image_objects_file(GetAndroidRoot() + "/etc/dirty-image-objects");
    if (OS::FileExists(dirty_image_objects_file.c_str())) {
      args.emplace_back(Concatenate({"--dirty-image-objects=", dirty_image_objects_file}));
    } else {
      LOG(WARNING) << "Missing dirty objects file : " << QuotePath(dirty_image_objects_file);
    }

    // Set boot-image and expectation of compiling boot classpath extensions.
    args.emplace_back("--boot-image=" + GetBootImage());

    // Add boot extensions to compile.
    for (const std::string& component : boot_extension_compilable_jars_) {
      args.emplace_back("--dex-file=" + component);
    }

    args.emplace_back("--runtime-arg");
    const std::string bcp = android::base::Join(config_.GetDex2oatBootclasspath(), ':');
    args.emplace_back(Concatenate({"-Xbootclasspath:", bcp}));
    args.emplace_back("--avoid-storing-invocation");
    args.emplace_back("--compiler-filter=speed-profile");
    args.emplace_back("--generate-debug-info");
    args.emplace_back("--image-format=lz4hc");
    args.emplace_back("--strip");
    args.emplace_back("--boot-image=" + GetBootImage());
    args.emplace_back("--android-root=out/empty");
    args.emplace_back("--abort-on-hard-verifier-error");
    args.emplace_back(Concatenate({"--instruction-set=", GetInstructionSetString(isa)}));
    args.emplace_back("--generate-mini-debug-info");

    // Compile as a single image for fewer files and slightly less memory overhead.
    args.emplace_back("--single-image");

    const std::string image_location = GetBootImageExtensionImageLocation(isa);
    const OdrArtifacts artifacts = OdrArtifacts::ForBootImageExtension(image_location);
    args.emplace_back("--oat-location=" + artifacts.OatLocation());

    const std::pair<const std::string, const char*> location_kind_pairs[] = {
      std::make_pair(artifacts.ImageLocation(), "image"),
      std::make_pair(artifacts.OatLocation(), "oat"),
      std::make_pair(artifacts.VdexLocation(), "output-vdex")
    };

    std::vector<std::unique_ptr<File>> output_files;
    for (const auto& location_kind_pair : location_kind_pairs) {
      auto& [location, kind] = location_kind_pair;
      const std::string staging_location = GetStagingLocation(staging_dir, location);
      std::unique_ptr<File> file(OS::CreateEmptyFile(staging_location.c_str()));
      if (file == nullptr) {
        PLOG(ERROR) << "Failed to create " << kind << " file: " << staging_location;
        EraseFiles(output_files);
        return false;
      }
      args.emplace_back(android::base::StringPrintf("--%s-fd=%d", kind, file->Fd()));
      output_files.emplace_back(std::move(file));
    }

    EnsureDirectoryExists(Dirname(image_location));

    const time_t timeout = GetSubprocessTimeout();
    const std::string cmd_line = android::base::Join(args, ' ');
    LOG(INFO) << "Compiling boot extensions (" << isa << "): " << cmd_line
              << " [timeout " << timeout << "s]";
    if (config_.GetDryRun()) {
      return true;
    }

    bool timed_out = false;
    if (ExecAndReturnCode(args, timeout, &timed_out, error_msg) != 0) {
      if (timed_out) {
        // TODO(oth): record timeout event for compiling boot extension
      }
      EraseFiles(output_files);
      return false;
    }

    if (!MoveOrEraseFiles(output_files, Dirname(image_location))) {
      return false;
    }

    return true;
  }

  bool CompileSystemServerArtifacts(const std::string_view staging_dir,
                                    std::string* error_msg) const {
    std::vector<std::string> classloader_context;

    for (const std::string& jar : systemserver_compilable_jars_) {
      std::vector<std::string> args;
      args.emplace_back(config_.GetDex2Oat());

      // Convert davlik.vm.boot-dex2oat-* properties to arguments.
      AddDex2OatBootTimeArguments(&args);

      args.emplace_back("--dex-file=" + jar);

      const std::string image_location = GetSystemServerImageLocation(/*on_system=*/ false, jar);
      if (classloader_context.empty()) {
        // All images are in the same directory, only need to check for first iteration.
        EnsureDirectoryExists(Dirname(image_location));
      }

      std::vector<std::unique_ptr<File>> output_files;
      OdrArtifacts artifacts = OdrArtifacts::ForSystemServer(image_location);
      const std::pair<const std::string, const char*> location_kind_pairs[] = {
        std::make_pair(artifacts.ImageLocation(), "app-image"),
        std::make_pair(artifacts.OatLocation(), "oat"),
        std::make_pair(artifacts.VdexLocation(), "output-vdex")
      };

      for (const auto& location_kind_pair : location_kind_pairs) {
        auto& [location, kind] = location_kind_pair;
        const std::string staging_location = GetStagingLocation(staging_dir, location);
        std::unique_ptr<File> file(OS::CreateEmptyFile(staging_location.c_str()));
        if (file == nullptr) {
          PLOG(ERROR) << "Failed to create " << kind << " file: " << staging_location;
          EraseFiles(output_files);
          return false;
        }
        args.emplace_back(android::base::StringPrintf("--%s-fd=%d", kind, file->Fd()));
        output_files.emplace_back(std::move(file));
      }
      args.emplace_back("--oat-location=" + artifacts.OatLocation());

      std::string jar_name(Basename(jar));
      const std::string profile_file =
        Concatenate({GetAndroidRoot(), "/framework/", jar_name, ".prof"});
      if (OS::FileExists(profile_file.c_str())) {
        args.emplace_back("--profile-file=" + profile_file);
        args.emplace_back("--compiler-filter=speed-profile");
      } else {
        args.emplace_back("--compiler-filter=speed");
      }

      if (!config_.GetUpdatableBcpPackagesFile().empty()) {
        args.emplace_back("--updatable-bcp-packages-file=" + config_.GetUpdatableBcpPackagesFile());
      }

      const char* isa_str = GetInstructionSetString(config_.GetSystemServerIsa());
      args.emplace_back(Concatenate({"--instruction-set=", isa_str}));

      args.emplace_back("--runtime-arg");
      const std::string bcp = android::base::Join(config_.GetDex2oatBootclasspath(), ':');
      args.emplace_back(Concatenate({"-Xbootclasspath:", bcp}));
      const std::string context_path = android::base::Join(classloader_context, ':');
      args.emplace_back(Concatenate({"--class-loader-context=PCL[", context_path, "]"}));
      const std::string extension_image = GetBootImageExtensionImage(/*on_system=*/ false);
      args.emplace_back(Concatenate({"--boot-image=", GetBootImage(), ":", extension_image}));

      args.emplace_back("--avoid-storing-invocation");
      args.emplace_back("--android-root=out/empty");
      args.emplace_back("--abort-on-hard-verifier-error");
      args.emplace_back("--generate-mini-debug-info");
      args.emplace_back("--compilation-reason=boot");
      args.emplace_back("--copy-dex-files=false");
      args.emplace_back("--image-format=lz4hc");
      args.emplace_back("--resolve-startup-const-strings=true");

      const time_t timeout = GetSubprocessTimeout();
      const std::string cmd_line = android::base::Join(args, ' ');
      LOG(INFO) << "Compiling " << jar << ": " << cmd_line << " [timeout " << timeout << "s]";
      if (config_.GetDryRun()) {
        return true;
      }

      bool timed_out = false;
      if (!Exec(args, error_msg)) {
        if (timed_out) {
          // TODO(oth): record timeout event for compiling boot extension
        }
        EraseFiles(output_files);
        return false;
      }

      if (!MoveOrEraseFiles(output_files, Dirname(image_location))) {
        return false;
      }

      classloader_context.emplace_back(jar);
    }

    return true;
  }

  int Compile(bool force_compile) const {
    ReportSpace();  // TODO(oth): Factor available space into compilation logic.

    // Clean-up existing files.
    if (force_compile) {
      RemoveArtifactsOrDie();
    }

    // Create staging area and assign label for generating compilation artifacts.
    const char* staging_dir;
    if (PaletteCreateOdrefreshStagingDirectory(&staging_dir) != PaletteStatus::kOkay) {
      return ExitCode::kCompilationFailed;
    }

    std::string error_msg;

    for (const InstructionSet isa : config_.GetBootExtensionIsas()) {
      if (!force_compile && CheckBootExtensionArtifacts(isa)) {
        continue;  // Boot extension artifacts look good, skip compilation.
      }
      if (!CompileBootExtensionArtifacts(isa, staging_dir, &error_msg)) {
        LOG(ERROR) << "BCP compilation failed: " << error_msg;
        RemoveStagingFilesOrDie(staging_dir);
        return ExitCode::kCompilationFailed;
      }
    }

    if (force_compile|| !CheckSystemServerArtifacts()) {
      if (!CompileSystemServerArtifacts(staging_dir, &error_msg)) {
        LOG(ERROR) << "system_server compilation failed: " << error_msg;
        RemoveStagingFilesOrDie(staging_dir);
        return ExitCode::kCompilationFailed;
      }
    }

    return ExitCode::kOkay;
  }

  static bool ArgumentMatches(const char* arg, const char* prefix, std::string* value) {
    if (StartsWith(arg, prefix)) {
      *value = std::string(arg + strlen(prefix));
      return true;
    }
    return false;
  }

  static bool ArgumentEquals(const char* arg, const char* expected) {
    return strcmp(arg, expected) == 0;
  }

  static int InitializeHostConfig(int argc, const char** argv, OdrConfig* config) {
    std::string current_binary;
    if (argv[0][0] == '/') {
      current_binary = argv[0];
    } else {
      std::vector<char> buf(PATH_MAX);
      if (getcwd(buf.data(), buf.size()) == nullptr) {
        PLOG(FATAL) << "Failed getwd()";
      }
      current_binary = Concatenate({buf.data(), "/", argv[0]});
    }
    config->SetArtBinDir(std::string(Dirname(current_binary)));

    int n = 1;
    for (; n < argc - 1; ++n) {
      const char* arg = argv[n];
      std::string value;
      if (ArgumentMatches(arg, "--android-root=", &value)) {
        setenv("ANDROID_ROOT", value.c_str(), 1);
      } else if (ArgumentMatches(arg, "--android-art-root=", &value)) {
        setenv("ANDROID_ART_ROOT", value.c_str(), 1);
      } else if (ArgumentMatches(arg, "--apex-info-list=", &value)) {
        config->SetApexInfoListFile(value);
      } else if (ArgumentMatches(arg, "--art-apex-data=", &value)) {
        setenv("ART_APEX_DATA", value.c_str(), 1);
      } else if (ArgumentMatches(arg, "--dex2oat-bootclasspath=", &value)) {
        config->SetDex2oatBootclasspath(value);
      } else if (ArgumentEquals(arg, "--dry-run")) {
        config->SetDryRun();
      } else if (ArgumentMatches(arg, "--isa=", &value)) {
        config->SetIsa(GetInstructionSetFromString(value.c_str()));
      } else if (ArgumentMatches(arg, "--system-server-classpath=", &value)) {
        config->SetSystemServerClasspath(arg);
      } else if (ArgumentMatches(arg, "--updatable-bcp-packages-file=", &value)) {
        config->SetUpdatableBcpPackagesFile(value);
      } else if (ArgumentMatches(arg, "--zygote-arch=", &value)) {
        ZygoteKind zygote_kind;
        if (!ParseZygoteKind(value.c_str(), &zygote_kind)) {
          ArgumentError("Unrecognized zygote kind: '%s'", value.c_str());
        }
        config->SetZygoteKind(zygote_kind);
      } else {
        UsageError("Unrecognized argument: '%s'", arg);
      }
    }
    return n;
  }

  static void InitializeTargetConfig(OdrConfig* config) {
    config->SetApexInfoListFile("/apex/apex-info-list.xml");
    config->SetArtBinDir(art::GetArtBinDir());
    config->SetDex2oatBootclasspath(getenv("DEX2OATBOOTCLASSPATH"));
    config->SetSystemServerClasspath(getenv("SYSTEMSERVERCLASSPATH"));

    std::string abi = android::base::GetProperty("ro.product.cpu.abi", {});
    InstructionSet is = GetInstructionSetFromString(abi.c_str());
    if (is == InstructionSet::kNone) {
      LOG(FATAL) << "Unknown abi: '" << abi << "'";
    }
    config->SetIsa(is);

    std::string zygote = android::base::GetProperty("ro.zygote", {});
    ZygoteKind zygote_kind;
    if (!ParseZygoteKind(zygote.c_str(), &zygote_kind)) {
      LOG(FATAL) << "Unknown zygote: " << QuotePath(zygote);
    }
    config->SetZygoteKind(zygote_kind);

    std::string updatable_packages =
      android::base::GetProperty("dalvik.vm.dex2oat-updatable-bcp-packages-file", {});
    config->SetUpdatableBcpPackagesFile(updatable_packages);
  }

  static int main(int argc, const char** argv) {
    OdrConfig config;
    if (kIsTargetBuild) {
      InitializeTargetConfig(&config);
      argv += 1;
      argc -= 1;
    } else {
      __android_log_set_logger(__android_log_stderr_logger);
      int n = InitializeHostConfig(argc, argv, &config);
      argv += n;
      argc -= n;
    }

    if (argc != 1) {
      UsageError("Expected 1 argument, but have %d.", argc);
    }

    OnDeviceRefresh odr(config);
    bool is_system_apex = OnDeviceRefresh::IsFactoryApex(config.GetApexInfoListFile());
    for (int i = 0; i < argc; ++i) {
      std::string_view action(argv[i]);
      if (action == "--check") {
        return odr.CheckArtifacts(is_system_apex);
      } else if (action == "--compile") {
        return odr.Compile(/*force_compile=*/ false);
      } else if (action == "--force-compile") {
        return odr.Compile(/*force_compile=*/ true);
      } else if (action == "--help") {
        UsageHelp(argv[0]);
      } else {
        UsageError("Unknown argument: ", argv[i]);
      }
    }
    return ExitCode::kOkay;
  }

  DISALLOW_COPY_AND_ASSIGN(OnDeviceRefresh);
};

}  // namespace odrefresh
}  // namespace art

int main(int argc, const char** argv) {
  return art::odrefresh::OnDeviceRefresh::main(argc, argv);
}
