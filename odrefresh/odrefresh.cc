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

#include "odrefresh.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iosfwd>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "android-base/file.h"
#include "android-base/logging.h"
#include "android-base/macros.h"
#include "android-base/parseint.h"
#include "android-base/properties.h"
#include "android-base/result.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "android/log.h"
#include "arch/instruction_set.h"
#include "base/file_utils.h"
#include "base/globals.h"
#include "base/macros.h"
#include "base/os.h"
#include "base/string_view_cpp20.h"
#include "base/unix_file/fd_file.h"
#include "com_android_apex.h"
#include "com_android_art.h"
#include "dex/art_dex_file_loader.h"
#include "dexoptanalyzer.h"
#include "exec_utils.h"
#include "libdexopt.h"
#include "log/log.h"
#include "odr_artifacts.h"
#include "odr_common.h"
#include "odr_compilation_log.h"
#include "odr_config.h"
#include "odr_dexopt.h"
#include "odr_fs_utils.h"
#include "odr_metrics.h"
#include "odrefresh/odrefresh.h"
#include "palette/palette.h"
#include "palette/palette_types.h"

#include "aidl/com/android/art/CompilerFilter.h"
#include "aidl/com/android/art/DexoptBcpExtArgs.h"
#include "aidl/com/android/art/DexoptSystemServerArgs.h"
#include "aidl/com/android/art/Isa.h"

namespace art {
namespace odrefresh {

namespace apex = com::android::apex;
namespace art_apex = com::android::art;

using aidl::com::android::art::CompilerFilter;
using aidl::com::android::art::DexoptBcpExtArgs;
using aidl::com::android::art::DexoptSystemServerArgs;
using aidl::com::android::art::Isa;
using android::base::Result;

namespace {

// Name of cache info file in the ART Apex artifact cache.
constexpr const char* kCacheInfoFile = "cache-info.xml";

void EraseFiles(const std::vector<std::unique_ptr<File>>& files) {
  for (auto& file : files) {
    file->Erase(/*unlink=*/true);
  }
}

// Moves `files` to the directory `output_directory_path`.
//
// If any of the files cannot be moved, then all copies of the files are removed from both
// the original location and the output location.
//
// Returns true if all files are moved, false otherwise.
bool MoveOrEraseFiles(const std::vector<std::unique_ptr<File>>& files,
                      std::string_view output_directory_path) {
  std::vector<std::unique_ptr<File>> output_files;
  for (auto& file : files) {
    const std::string file_basename(android::base::Basename(file->GetPath()));
    const std::string output_file_path = Concatenate({output_directory_path, "/", file_basename});
    const std::string input_file_path = file->GetPath();

    output_files.emplace_back(OS::CreateEmptyFileWriteOnly(output_file_path.c_str()));
    if (output_files.back() == nullptr) {
      PLOG(ERROR) << "Failed to open " << QuotePath(output_file_path);
      output_files.pop_back();
      EraseFiles(output_files);
      EraseFiles(files);
      return false;
    }

    static constexpr mode_t kFileMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    if (fchmod(output_files.back()->Fd(), kFileMode) != 0) {
      PLOG(ERROR) << "Could not set file mode on " << QuotePath(output_file_path);
      EraseFiles(output_files);
      EraseFiles(files);
      return false;
    }

    const size_t file_bytes = file->GetLength();
    if (!output_files.back()->Copy(file.get(), /*offset=*/0, file_bytes)) {
      PLOG(ERROR) << "Failed to copy " << QuotePath(file->GetPath()) << " to "
                  << QuotePath(output_file_path);
      EraseFiles(output_files);
      EraseFiles(files);
      return false;
    }

    if (!file->Erase(/*unlink=*/true)) {
      PLOG(ERROR) << "Failed to erase " << QuotePath(file->GetPath());
      EraseFiles(output_files);
      EraseFiles(files);
      return false;
    }

    if (output_files.back()->FlushCloseOrErase() != 0) {
      PLOG(ERROR) << "Failed to flush and close file " << QuotePath(output_file_path);
      EraseFiles(output_files);
      EraseFiles(files);
      return false;
    }
  }
  return true;
}

// Gets the `ApexInfo` associated with the currently active ART APEX.
std::optional<apex::ApexInfo> GetArtApexInfo(const std::vector<apex::ApexInfo>& info_list) {
  auto it = std::find_if(info_list.begin(), info_list.end(), [](const apex::ApexInfo& info) {
    return info.getModuleName() == "com.android.art";
  });
  return it != info_list.end() ? std::make_optional(*it) : std::nullopt;
}

// Returns cache provenance information based on the current APEX version and filesystem
// information.
art_apex::ModuleInfo GenerateModuleInfo(const apex::ApexInfo& apex_info) {
  // The lastUpdateMillis is an addition to ApexInfoList.xsd to support samegrade installs.
  int64_t last_update_millis =
      apex_info.hasLastUpdateMillis() ? apex_info.getLastUpdateMillis() : 0;
  return art_apex::ModuleInfo{apex_info.getModuleName(),
                              apex_info.getVersionCode(),
                              apex_info.getVersionName(),
                              last_update_millis};
}

// Returns cache provenance information for all APEXes.
std::vector<art_apex::ModuleInfo> GenerateModuleInfoList(
    const std::vector<apex::ApexInfo>& apex_info_list) {
  std::vector<art_apex::ModuleInfo> module_info_list;
  std::transform(apex_info_list.begin(),
                 apex_info_list.end(),
                 std::back_inserter(module_info_list),
                 GenerateModuleInfo);
  return module_info_list;
}

bool CheckComponents(const std::vector<art_apex::Component>& expected_components,
                     const std::vector<art_apex::Component>& actual_components,
                     std::string* error_msg) {
  if (expected_components.size() != actual_components.size()) {
    return false;
  }

  for (size_t i = 0; i < expected_components.size(); ++i) {
    const art_apex::Component& expected = expected_components[i];
    const art_apex::Component& actual = actual_components[i];

    if (expected.getFile() != actual.getFile()) {
      *error_msg = android::base::StringPrintf("Component %zu file differs ('%s' != '%s')",
                                               i,
                                               expected.getFile().c_str(),
                                               actual.getFile().c_str());
      return false;
    }
    if (expected.getSize() != actual.getSize()) {
      *error_msg =
          android::base::StringPrintf("Component %zu size differs (%" PRIu64 " != %" PRIu64 ")",
                                      i,
                                      expected.getSize(),
                                      actual.getSize());
      return false;
    }
    if (expected.getChecksums() != actual.getChecksums()) {
      *error_msg = android::base::StringPrintf("Component %zu checksums differ ('%s' != '%s')",
                                               i,
                                               expected.getChecksums().c_str(),
                                               actual.getChecksums().c_str());
      return false;
    }
  }

  return true;
}

std::vector<art_apex::Component> GenerateComponents(const std::vector<std::string>& jars) {
  std::vector<art_apex::Component> components;

  ArtDexFileLoader loader;
  for (const std::string& path : jars) {
    struct stat sb;
    if (stat(path.c_str(), &sb) == -1) {
      PLOG(ERROR) << "Failed to get component: " << QuotePath(path);
      return {};
    }

    std::vector<uint32_t> checksums;
    std::vector<std::string> dex_locations;
    std::string error_msg;
    if (!loader.GetMultiDexChecksums(path.c_str(), &checksums, &dex_locations, &error_msg)) {
      LOG(ERROR) << "Failed to get components: " << error_msg;
      return {};
    }

    std::ostringstream oss;
    for (size_t i = 0; i < checksums.size(); ++i) {
      if (i != 0) {
        oss << ';';
      }
      oss << android::base::StringPrintf("%08x", checksums[i]);
    }
    const std::string checksum = oss.str();

    components.emplace_back(art_apex::Component{path, static_cast<uint64_t>(sb.st_size), checksum});
  }

  return components;
}

// Checks whether a group of artifacts exists. Returns true if all are present, false otherwise.
bool ArtifactsExist(const OdrArtifacts& artifacts,
                    bool check_art_file,
                    /*out*/ std::string* error_msg) {
  std::vector<const char*> paths{artifacts.OatPath().c_str(), artifacts.VdexPath().c_str()};
  if (check_art_file) {
    paths.push_back(artifacts.ImagePath().c_str());
  }
  for (const char* path : paths) {
    if (!OS::FileExists(path)) {
      if (errno == EACCES) {
        PLOG(ERROR) << "Failed to stat() " << path;
      }
      *error_msg = "Missing file: " + QuotePath(path);
      return false;
    }
  }
  return true;
}

bool PrepareDex2OatConcurrencyArguments(/*out*/ int* threads, /*out*/ std::vector<int>* cpu_set) {
  DCHECK(threads);
  DCHECK(cpu_set && cpu_set->empty());
  *threads = android::base::GetIntProperty("dalvik.vm.boot-dex2oat-threads",
                                           /*default_value=*/ 0,
                                           /*min=*/ 1);

  std::string cpu_set_spec = android::base::GetProperty("dalvik.vm.boot-dex2oat-cpu-set", "");
  if (cpu_set_spec.empty()) {
    return true;
  }
  for (auto& str : android::base::Split(cpu_set_spec, ",")) {
    int id;
    if (!android::base::ParseInt(str, &id, 0)) {
      LOG(ERROR) << "Invalid CPU set spec: " << cpu_set_spec;
      return false;
    }
    cpu_set->push_back(id);
  }
  return true;
}

bool PrepareDex2OatProfileIfExists(/*inout*/ int* profile_fd,
                                   /*inout*/ std::vector<std::unique_ptr<File>>* output_files,
                                   const std::string& profile_path) {
  std::unique_ptr<File> profile_file(OS::OpenFileForReading(profile_path.c_str()));
  if (profile_file && profile_file->IsOpened()) {
    *profile_fd = profile_file->Fd();
    output_files->push_back(std::move(profile_file));
    return true;
  } else {
    return false;
  }
}

bool PrepareBootClasspathFds(/*inout*/ std::vector<int>& boot_classpath_fds,
                             /*inout*/ std::vector<std::unique_ptr<File>>& output_files,
                             const std::vector<std::string>& bcp_jars) {
  for (const std::string& jar : bcp_jars) {
    // Special treatment for Compilation OS. JARs in staged APEX may not be visible to Android, and
    // may only be visible in the VM where the staged APEX is mounted. On the contrary, JARs in
    // /system is not available by path in the VM, and can only made available via (remote) FDs.
    if (StartsWith(jar, "/apex/")) {
      boot_classpath_fds.emplace_back(-1);
    } else {
      std::unique_ptr<File> jar_file(OS::OpenFileForReading(jar.c_str()));
      if (!jar_file || !jar_file->IsValid()) {
        LOG(ERROR) << "Failed to open a BCP jar " << jar;
        return false;
      }
      boot_classpath_fds.emplace_back(jar_file->Fd());
      output_files.push_back(std::move(jar_file));
    }
  }
  return true;
}

void PrepareCompiledBootClasspathFdsIfAny(
    /*inout*/ DexoptSystemServerArgs& dexopt_args,
    /*inout*/ std::vector<std::unique_ptr<File>>& output_files,
    const std::vector<std::string>& bcp_jars,
    const InstructionSet isa,
    bool on_system) {
  std::vector<int> bcp_image_fds;
  std::vector<int> bcp_oat_fds;
  std::vector<int> bcp_vdex_fds;
  std::vector<std::unique_ptr<File>> opened_files;
  bool added_any = false;
  for (const std::string& jar : bcp_jars) {
    std::string image_path = GetBootImagePath(on_system, jar);
    image_path = image_path.empty() ? "" : GetSystemImageFilename(image_path.c_str(), isa);
    std::unique_ptr<File> image_file(OS::OpenFileForReading(image_path.c_str()));
    if (image_file && image_file->IsValid()) {
      bcp_image_fds.push_back(image_file->Fd());
      opened_files.push_back(std::move(image_file));
      added_any = true;
    } else {
      bcp_image_fds.push_back(-1);
    }

    std::string oat_path = ReplaceFileExtension(image_path, "oat");
    std::unique_ptr<File> oat_file(OS::OpenFileForReading(oat_path.c_str()));
    if (oat_file && oat_file->IsValid()) {
      bcp_oat_fds.push_back(oat_file->Fd());
      opened_files.push_back(std::move(oat_file));
      added_any = true;
    } else {
      bcp_oat_fds.push_back(-1);
    }

    std::string vdex_path = ReplaceFileExtension(image_path, "vdex");
    std::unique_ptr<File> vdex_file(OS::OpenFileForReading(vdex_path.c_str()));
    if (vdex_file && vdex_file->IsValid()) {
      bcp_vdex_fds.push_back(vdex_file->Fd());
      opened_files.push_back(std::move(vdex_file));
      added_any = true;
    } else {
      bcp_vdex_fds.push_back(-1);
    }
  }
  // Add same amount of FDs as BCP JARs, or none.
  if (added_any) {
    std::move(opened_files.begin(), opened_files.end(), std::back_inserter(output_files));

    dexopt_args.bootClasspathImageFds = bcp_image_fds;
    dexopt_args.bootClasspathVdexFds = bcp_vdex_fds;
    dexopt_args.bootClasspathOatFds = bcp_oat_fds;
  }
}

std::string GetStagingLocation(const std::string& staging_dir, const std::string& path) {
  return Concatenate({staging_dir, "/", android::base::Basename(path)});
}

WARN_UNUSED bool CheckCompilationSpace() {
  // Check the available storage space against an arbitrary threshold because dex2oat does not
  // report when it runs out of storage space and we do not want to completely fill
  // the users data partition.
  //
  // We do not have a good way of pre-computing the required space for a compilation step, but
  // typically observe 16MB as the largest size of an AOT artifact. Since there are three
  // AOT artifacts per compilation step - an image file, executable file, and a verification
  // data file - the threshold is three times 16MB.
  static constexpr uint64_t kMinimumSpaceForCompilation = 3 * 16 * 1024 * 1024;

  uint64_t bytes_available;
  const std::string& art_apex_data_path = GetArtApexData();
  if (!GetFreeSpace(art_apex_data_path, &bytes_available)) {
    return false;
  }

  if (bytes_available < kMinimumSpaceForCompilation) {
    LOG(WARNING) << "Low space for " << QuotePath(art_apex_data_path) << " (" << bytes_available
                 << " bytes)";
    return false;
  }

  return true;
}

Isa InstructionSetToAidlIsa(InstructionSet isa) {
  switch (isa) {
    case InstructionSet::kArm:
      return Isa::ARM;
    case InstructionSet::kThumb2:
      return Isa::THUMB2;
    case InstructionSet::kArm64:
      return Isa::ARM64;
    case InstructionSet::kX86:
      return Isa::X86;
    case InstructionSet::kX86_64:
      return Isa::X86_64;
    default:
      UNREACHABLE();
  }
}

CompilerFilter CompilerFilterStringToAidl(const std::string& compiler_filter) {
  if (compiler_filter == "speed-profile") {
    return CompilerFilter::SPEED_PROFILE;
  } else if (compiler_filter == "speed") {
    return CompilerFilter::SPEED;
  } else if (compiler_filter == "verify") {
    return CompilerFilter::VERIFY;
  } else {
    return CompilerFilter::UNSUPPORTED;
  }
}

}  // namespace

OnDeviceRefresh::OnDeviceRefresh(const OdrConfig& config)
    : OnDeviceRefresh(config,
                      Concatenate({config.GetArtifactDirectory(), "/", kCacheInfoFile}),
                      std::make_unique<ExecUtils>(),
                      std::move(OdrDexopt::Create(config, std::make_unique<ExecUtils>()))) {}

OnDeviceRefresh::OnDeviceRefresh(const OdrConfig& config,
                                 const std::string& cache_info_filename,
                                 std::unique_ptr<ExecUtils> exec_utils,
                                 std::unique_ptr<OdrDexopt> odr_dexopt)
    : config_{config},
      cache_info_filename_{cache_info_filename},
      start_time_{time(nullptr)},
      exec_utils_{std::move(exec_utils)},
      odr_dexopt_{std::move(odr_dexopt)} {
  for (const std::string& jar : android::base::Split(config_.GetDex2oatBootClasspath(), ":")) {
    // Boot class path extensions are those not in the ART APEX. Updatable APEXes should not
    // have DEX files in the DEX2OATBOOTCLASSPATH. At the time of writing i18n is a non-updatable
    // APEX and so does appear in the DEX2OATBOOTCLASSPATH.
    if (!LocationIsOnArtModule(jar)) {
      boot_extension_compilable_jars_.emplace_back(jar);
    }
  }

  systemserver_compilable_jars_ = android::base::Split(config_.GetSystemServerClasspath(), ":");
  boot_classpath_jars_ = android::base::Split(config_.GetBootClasspath(), ":");
}

time_t OnDeviceRefresh::GetExecutionTimeUsed() const {
  return time(nullptr) - start_time_;
}

time_t OnDeviceRefresh::GetExecutionTimeRemaining() const {
  return std::max(static_cast<time_t>(0), config_.GetMaxExecutionSeconds() - GetExecutionTimeUsed());
}

time_t OnDeviceRefresh::GetSubprocessTimeout() const {
  return std::min(GetExecutionTimeRemaining(), config_.GetMaxChildProcessSeconds());
}

std::optional<std::vector<apex::ApexInfo>> OnDeviceRefresh::GetApexInfoList() const {
  std::optional<apex::ApexInfoList> info_list =
      apex::readApexInfoList(config_.GetApexInfoListFile().c_str());
  if (!info_list.has_value()) {
    return std::nullopt;
  }

  std::vector<apex::ApexInfo> filtered_info_list;
  std::copy_if(info_list->getApexInfo().begin(),
               info_list->getApexInfo().end(),
               std::back_inserter(filtered_info_list),
               [](const apex::ApexInfo& info) { return info.getIsActive(); });
  return filtered_info_list;
}

std::optional<art_apex::CacheInfo> OnDeviceRefresh::ReadCacheInfo() const {
  return art_apex::read(cache_info_filename_.c_str());
}

void OnDeviceRefresh::WriteCacheInfo() const {
  if (OS::FileExists(cache_info_filename_.c_str())) {
    if (unlink(cache_info_filename_.c_str()) != 0) {
      PLOG(ERROR) << "Failed to unlink() file " << QuotePath(cache_info_filename_);
    }
  }

  const std::string dir_name = android::base::Dirname(cache_info_filename_);
  if (!EnsureDirectoryExists(dir_name)) {
    LOG(ERROR) << "Could not create directory: " << QuotePath(dir_name);
    return;
  }

  std::optional<std::vector<apex::ApexInfo>> apex_info_list = GetApexInfoList();
  if (!apex_info_list.has_value()) {
    LOG(ERROR) << "Could not update " << QuotePath(cache_info_filename_) << " : no APEX info";
    return;
  }

  std::optional<apex::ApexInfo> art_apex_info = GetArtApexInfo(apex_info_list.value());
  if (!art_apex_info.has_value()) {
    LOG(ERROR) << "Could not update " << QuotePath(cache_info_filename_) << " : no ART APEX info";
    return;
  }

  art_apex::ModuleInfo art_module_info = GenerateModuleInfo(art_apex_info.value());
  std::vector<art_apex::ModuleInfo> module_info_list =
      GenerateModuleInfoList(apex_info_list.value());

  std::optional<std::vector<art_apex::Component>> bcp_components =
      GenerateBootClasspathComponents();
  if (!bcp_components.has_value()) {
    LOG(ERROR) << "No boot classpath components.";
    return;
  }

  std::optional<std::vector<art_apex::Component>> bcp_compilable_components =
      GenerateBootExtensionCompilableComponents();
  if (!bcp_compilable_components.has_value()) {
    LOG(ERROR) << "No boot classpath extension compilable components.";
    return;
  }

  std::optional<std::vector<art_apex::Component>> system_server_components =
      GenerateSystemServerComponents();
  if (!system_server_components.has_value()) {
    LOG(ERROR) << "No system_server extension components.";
    return;
  }

  std::ofstream out(cache_info_filename_.c_str());
  art_apex::CacheInfo info({art_module_info},
                           {art_apex::ModuleInfoList(module_info_list)},
                           {art_apex::Classpath(bcp_components.value())},
                           {art_apex::Classpath(bcp_compilable_components.value())},
                           {art_apex::Classpath(system_server_components.value())});

  art_apex::write(out, info);
}

void OnDeviceRefresh::ReportNextBootAnimationProgress(uint32_t current_compilation) const {
  uint32_t number_of_compilations =
      config_.GetBootExtensionIsas().size() + systemserver_compilable_jars_.size();
  // We arbitrarily show progress until 90%, expecting that our compilations
  // take a large chunk of boot time.
  uint32_t value = (90 * current_compilation) / number_of_compilations;
  android::base::SetProperty("service.bootanim.progress", std::to_string(value));
}

std::vector<art_apex::Component> OnDeviceRefresh::GenerateBootClasspathComponents() const {
  return GenerateComponents(boot_classpath_jars_);
}

std::vector<art_apex::Component> OnDeviceRefresh::GenerateBootExtensionCompilableComponents()
    const {
  return GenerateComponents(boot_extension_compilable_jars_);
}

std::vector<art_apex::Component> OnDeviceRefresh::GenerateSystemServerComponents() const {
  return GenerateComponents(systemserver_compilable_jars_);
}

std::string OnDeviceRefresh::GetBootImageExtensionImage(bool on_system) const {
  CHECK(!boot_extension_compilable_jars_.empty());
  const std::string leading_jar = boot_extension_compilable_jars_[0];
  return GetBootImagePath(on_system, leading_jar);
}

std::string OnDeviceRefresh::GetBootImageExtensionImagePath(bool on_system,
                                                            const InstructionSet isa) const {
  // Typically "/data/misc/apexdata/com.android.art/dalvik-cache/<isa>/boot-framework.art".
  return GetSystemImageFilename(GetBootImageExtensionImage(on_system).c_str(), isa);
}

std::string OnDeviceRefresh::GetSystemServerImagePath(bool on_system,
                                                      const std::string& jar_path) const {
  if (on_system) {
    if (LocationIsOnApex(jar_path)) {
      return GetSystemOdexFilenameForApex(jar_path, config_.GetSystemServerIsa());
    }
    const std::string jar_name = android::base::Basename(jar_path);
    const std::string image_name = ReplaceFileExtension(jar_name, "art");
    const char* isa_str = GetInstructionSetString(config_.GetSystemServerIsa());
    // Typically "/system/framework/oat/<isa>/services.art".
    return Concatenate({GetAndroidRoot(), "/framework/oat/", isa_str, "/", image_name});
  } else {
    // Typically
    // "/data/misc/apexdata/.../dalvik-cache/<isa>/system@framework@services.jar@classes.art".
    const std::string image = GetApexDataImage(jar_path.c_str());
    return GetSystemImageFilename(image.c_str(), config_.GetSystemServerIsa());
  }
}

WARN_UNUSED bool OnDeviceRefresh::RemoveBootExtensionArtifactsFromData(InstructionSet isa) const {
  if (config_.GetDryRun()) {
    LOG(INFO) << "Removal of bcp extension artifacts on /data skipped (dry-run).";
    return true;
  }

  const std::string apexdata_image_location =
      GetBootImageExtensionImagePath(/*on_system=*/false, isa);
  LOG(INFO) << "Removing boot class path artifacts on /data for "
            << QuotePath(apexdata_image_location);
  return RemoveArtifacts(OdrArtifacts::ForBootImageExtension(apexdata_image_location));
}

WARN_UNUSED bool OnDeviceRefresh::RemoveSystemServerArtifactsFromData() const {
  if (config_.GetDryRun()) {
    LOG(INFO) << "Removal of system_server artifacts on /data skipped (dry-run).";
    return true;
  }

  bool success = true;
  for (const std::string& jar_path : systemserver_compilable_jars_) {
    const std::string image_location = GetSystemServerImagePath(/*on_system=*/false, jar_path);
    const OdrArtifacts artifacts = OdrArtifacts::ForSystemServer(image_location);
    LOG(INFO) << "Removing system_server artifacts on /data for " << QuotePath(jar_path);
    success &= RemoveArtifacts(artifacts);
  }
  return success;
}

WARN_UNUSED bool OnDeviceRefresh::RemoveArtifacts(const OdrArtifacts& artifacts) const {
  bool success = true;
  for (const auto& location : {artifacts.ImagePath(), artifacts.OatPath(), artifacts.VdexPath()}) {
    if (config_.GetDryRun()) {
      LOG(INFO) << "Removing " << QuotePath(location) << " (dry-run).";
      continue;
    }

    if (OS::FileExists(location.c_str()) && unlink(location.c_str()) != 0) {
      PLOG(ERROR) << "Failed to remove: " << QuotePath(location);
      success = false;
    }
  }
  return success;
}

WARN_UNUSED bool OnDeviceRefresh::RemoveArtifactsDirectory() const {
  if (config_.GetDryRun()) {
    LOG(INFO) << "Directory " << QuotePath(config_.GetArtifactDirectory())
              << " and contents would be removed (dry-run).";
    return true;
  }
  return RemoveDirectory(config_.GetArtifactDirectory());
}

WARN_UNUSED bool OnDeviceRefresh::BootExtensionArtifactsExist(
    bool on_system,
    const InstructionSet isa,
    /*out*/ std::string* error_msg) const {
  const std::string apexdata_image_location = GetBootImageExtensionImagePath(on_system, isa);
  const OdrArtifacts artifacts = OdrArtifacts::ForBootImageExtension(apexdata_image_location);
  return ArtifactsExist(artifacts, /*check_art_file=*/true, error_msg);
}

WARN_UNUSED bool OnDeviceRefresh::SystemServerArtifactsExist(bool on_system,
                                                             /*out*/ std::string* error_msg) const {
  for (const std::string& jar_path : systemserver_compilable_jars_) {
    const std::string image_location = GetSystemServerImagePath(on_system, jar_path);
    const OdrArtifacts artifacts = OdrArtifacts::ForSystemServer(image_location);
    // .art files are optional and are not generated for all jars by the build system.
    const bool check_art_file = !on_system;
    if (!ArtifactsExist(artifacts, check_art_file, error_msg)) {
      return false;
    }
  }
  return true;
}

WARN_UNUSED bool OnDeviceRefresh::CheckBootExtensionArtifactsAreUpToDate(
    OdrMetrics& metrics,
    const InstructionSet isa,
    const apex::ApexInfo& art_apex_info,
    const std::optional<art_apex::CacheInfo>& cache_info,
    /*out*/ bool* cleanup_required) const {
  std::string error_msg;

  if (art_apex_info.getIsFactory()) {
    LOG(INFO) << "Factory ART APEX mounted.";

    // ART is not updated, so we can use the artifacts on /system. Check if they exist.
    if (BootExtensionArtifactsExist(/*on_system=*/true, isa, &error_msg)) {
      // We don't need the artifacts on /data since we can use those on /system.
      *cleanup_required = true;
      return true;
    }

    LOG(INFO) << "Incomplete boot extension artifacts on /system. " << error_msg;
    LOG(INFO) << "Checking cache.";
  }

  if (!cache_info.has_value()) {
    // If the cache info file does not exist, it means on-device compilation has not been done
    // before.
    PLOG(INFO) << "No prior cache-info file: " << QuotePath(cache_info_filename_);
    metrics.SetTrigger(OdrMetrics::Trigger::kMissingArtifacts);
    *cleanup_required = true;
    return false;
  }

  // Check whether the current cache ART module info differs from the current ART module info.
  const art_apex::ModuleInfo* cached_art_info = cache_info->getFirstArtModuleInfo();

  if (cached_art_info == nullptr) {
    LOG(INFO) << "Missing ART APEX info from cache-info.";
    metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
    *cleanup_required = true;
    return false;
  }

  if (cached_art_info->getVersionCode() != art_apex_info.getVersionCode()) {
    LOG(INFO) << "ART APEX version code mismatch (" << cached_art_info->getVersionCode()
              << " != " << art_apex_info.getVersionCode() << ").";
    metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
    *cleanup_required = true;
    return false;
  }

  if (cached_art_info->getVersionName() != art_apex_info.getVersionName()) {
    LOG(INFO) << "ART APEX version name mismatch (" << cached_art_info->getVersionName()
              << " != " << art_apex_info.getVersionName() << ").";
    metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
    *cleanup_required = true;
    return false;
  }

  // Check lastUpdateMillis for samegrade installs. If `cached_art_info` is missing the
  // lastUpdateMillis field then it is not current with the schema used by this binary so treat
  // it as a samegrade update. Otherwise check whether the lastUpdateMillis changed.
  const int64_t cached_art_last_update_millis =
      cached_art_info->hasLastUpdateMillis() ? cached_art_info->getLastUpdateMillis() : -1;
  if (cached_art_last_update_millis != art_apex_info.getLastUpdateMillis()) {
    LOG(INFO) << "ART APEX last update time mismatch (" << cached_art_last_update_millis
              << " != " << art_apex_info.getLastUpdateMillis() << ").";
    metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
    *cleanup_required = true;
    return false;
  }

  // Check boot class components.
  //
  // This checks the size and checksums of odrefresh compilable files on the DEX2OATBOOTCLASSPATH
  // (the Odrefresh constructor determines which files are compilable). If the number of files
  // there changes, or their size or checksums change then compilation will be triggered.
  //
  // The boot class components may change unexpectedly, for example an OTA could update
  // framework.jar.
  const std::vector<art_apex::Component> expected_bcp_compilable_components =
      GenerateBootExtensionCompilableComponents();
  if (expected_bcp_compilable_components.size() != 0 &&
      (!cache_info->hasDex2oatBootClasspath() ||
       !cache_info->getFirstDex2oatBootClasspath()->hasComponent())) {
    LOG(INFO) << "Missing Dex2oatBootClasspath components.";
    metrics.SetTrigger(OdrMetrics::Trigger::kDexFilesChanged);
    *cleanup_required = true;
    return false;
  }

  const std::vector<art_apex::Component>& bcp_compilable_components =
      cache_info->getFirstDex2oatBootClasspath()->getComponent();
  if (!CheckComponents(expected_bcp_compilable_components, bcp_compilable_components, &error_msg)) {
    LOG(INFO) << "Dex2OatClasspath components mismatch: " << error_msg;
    metrics.SetTrigger(OdrMetrics::Trigger::kDexFilesChanged);
    *cleanup_required = true;
    return false;
  }

  // Cache info looks good, check all compilation artifacts exist.
  if (!BootExtensionArtifactsExist(/*on_system=*/false, isa, &error_msg)) {
    LOG(INFO) << "Incomplete boot extension artifacts. " << error_msg;
    metrics.SetTrigger(OdrMetrics::Trigger::kMissingArtifacts);
    *cleanup_required = true;
    return false;
  }

  *cleanup_required = false;
  return true;
}

WARN_UNUSED bool OnDeviceRefresh::CheckSystemServerArtifactsAreUpToDate(
    OdrMetrics& metrics,
    const std::vector<apex::ApexInfo>& apex_info_list,
    const std::optional<art_apex::CacheInfo>& cache_info,
    /*out*/ bool* cleanup_required) const {
  std::string error_msg;

  if (std::all_of(apex_info_list.begin(),
                  apex_info_list.end(),
                  [](const apex::ApexInfo& apex_info) { return apex_info.getIsFactory(); })) {
    LOG(INFO) << "Factory APEXes mounted.";

    // APEXes are not updated, so we can use the artifacts on /system. Check if they exist.
    if (SystemServerArtifactsExist(/*on_system=*/true, &error_msg)) {
      // We don't need the artifacts on /data since we can use those on /system.
      *cleanup_required = true;
      return true;
    }

    LOG(INFO) << "Incomplete system server artifacts on /system. " << error_msg;
    LOG(INFO) << "Checking cache.";
  }

  if (!cache_info.has_value()) {
    // If the cache info file does not exist, it means on-device compilation has not been done
    // before.
    PLOG(INFO) << "No prior cache-info file: " << QuotePath(cache_info_filename_);
    metrics.SetTrigger(OdrMetrics::Trigger::kMissingArtifacts);
    *cleanup_required = true;
    return false;
  }

  // Check whether the current cached module info differs from the current module info.
  const art_apex::ModuleInfoList* cached_module_info_list = cache_info->getFirstModuleInfoList();

  if (cached_module_info_list == nullptr) {
    LOG(INFO) << "Missing APEX info list from cache-info.";
    metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
    *cleanup_required = true;
    return false;
  }

  std::unordered_map<std::string, const art_apex::ModuleInfo*> cached_module_info_map;
  for (const art_apex::ModuleInfo& module_info : cached_module_info_list->getModuleInfo()) {
    if (!module_info.hasName()) {
      LOG(INFO) << "Unexpected module info from cache-info. Missing module name.";
      metrics.SetTrigger(OdrMetrics::Trigger::kUnknown);
      *cleanup_required = true;
      return false;
    }
    cached_module_info_map[module_info.getName()] = &module_info;
  }

  for (const apex::ApexInfo& current_apex_info : apex_info_list) {
    auto it = cached_module_info_map.find(current_apex_info.getModuleName());
    if (it == cached_module_info_map.end()) {
      LOG(INFO) << "Missing APEX info from cache-info (" << current_apex_info.getModuleName()
                << ").";
      metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
      *cleanup_required = true;
      return false;
    }

    const art_apex::ModuleInfo* cached_module_info = it->second;

    if (cached_module_info->getVersionCode() != current_apex_info.getVersionCode()) {
      LOG(INFO) << "APEX (" << current_apex_info.getModuleName() << ") version code mismatch ("
                << cached_module_info->getVersionCode()
                << " != " << current_apex_info.getVersionCode() << ").";
      metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
      *cleanup_required = true;
      return false;
    }

    if (cached_module_info->getVersionName() != current_apex_info.getVersionName()) {
      LOG(INFO) << "APEX (" << current_apex_info.getModuleName() << ") version name mismatch ("
                << cached_module_info->getVersionName()
                << " != " << current_apex_info.getVersionName() << ").";
      metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
      *cleanup_required = true;
      return false;
    }

    if (!cached_module_info->hasLastUpdateMillis() ||
        cached_module_info->getLastUpdateMillis() != current_apex_info.getLastUpdateMillis()) {
      LOG(INFO) << "APEX (" << current_apex_info.getModuleName() << ") last update time mismatch ("
                << cached_module_info->getLastUpdateMillis()
                << " != " << current_apex_info.getLastUpdateMillis() << ").";
      metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
      *cleanup_required = true;
      return false;
    }
  }

  // Check system server components.
  //
  // This checks the size and checksums of odrefresh compilable files on the
  // SYSTEMSERVERCLASSPATH (the Odrefresh constructor determines which files are compilable). If
  // the number of files there changes, or their size or checksums change then compilation will be
  // triggered.
  //
  // The system_server components may change unexpectedly, for example an OTA could update
  // services.jar.
  const std::vector<art_apex::Component> expected_system_server_components =
      GenerateSystemServerComponents();
  if (expected_system_server_components.size() != 0 &&
      (!cache_info->hasSystemServerClasspath() ||
       !cache_info->getFirstSystemServerClasspath()->hasComponent())) {
    LOG(INFO) << "Missing SystemServerClasspath components.";
    metrics.SetTrigger(OdrMetrics::Trigger::kDexFilesChanged);
    *cleanup_required = true;
    return false;
  }

  const std::vector<art_apex::Component>& system_server_components =
      cache_info->getFirstSystemServerClasspath()->getComponent();
  if (!CheckComponents(expected_system_server_components, system_server_components, &error_msg)) {
    LOG(INFO) << "SystemServerClasspath components mismatch: " << error_msg;
    metrics.SetTrigger(OdrMetrics::Trigger::kDexFilesChanged);
    *cleanup_required = true;
    return false;
  }

  const std::vector<art_apex::Component> expected_bcp_components =
      GenerateBootClasspathComponents();
  if (expected_bcp_components.size() != 0 &&
      (!cache_info->hasBootClasspath() || !cache_info->getFirstBootClasspath()->hasComponent())) {
    LOG(INFO) << "Missing BootClasspath components.";
    metrics.SetTrigger(OdrMetrics::Trigger::kDexFilesChanged);
    *cleanup_required = true;
    return false;
  }

  const std::vector<art_apex::Component>& bcp_components =
      cache_info->getFirstBootClasspath()->getComponent();
  if (!CheckComponents(expected_bcp_components, bcp_components, &error_msg)) {
    LOG(INFO) << "BootClasspath components mismatch: " << error_msg;
    metrics.SetTrigger(OdrMetrics::Trigger::kDexFilesChanged);
    // Boot classpath components can be dependencies of system_server components, so system_server
    // components need to be recompiled if boot classpath components are changed.
    *cleanup_required = true;
    return false;
  }

  if (!SystemServerArtifactsExist(/*on_system=*/false, &error_msg)) {
    LOG(INFO) << "Incomplete system_server artifacts. " << error_msg;
    // No clean-up is required here: we have boot extension artifacts. The method
    // `SystemServerArtifactsExistOnData()` checks in compilation order so it is possible some of
    // the artifacts are here. We likely ran out of space compiling the system_server artifacts.
    // Any artifacts present are usable.
    metrics.SetTrigger(OdrMetrics::Trigger::kMissingArtifacts);
    *cleanup_required = false;
    return false;
  }

  *cleanup_required = false;
  return true;
}

WARN_UNUSED ExitCode OnDeviceRefresh::CheckArtifactsAreUpToDate(
    OdrMetrics& metrics,
    /*out*/ std::vector<InstructionSet>* compile_boot_extensions,
    /*out*/ bool* compile_system_server) const {
  metrics.SetStage(OdrMetrics::Stage::kCheck);

  // Clean-up helper used to simplify clean-ups and handling failures there.
  auto cleanup_and_compile_all = [&, this]() {
    *compile_boot_extensions = config_.GetBootExtensionIsas();
    *compile_system_server = true;
    return RemoveArtifactsDirectory() ? ExitCode::kCompilationRequired : ExitCode::kCleanupFailed;
  };

  std::optional<std::vector<apex::ApexInfo>> apex_info_list = GetApexInfoList();
  if (!apex_info_list.has_value()) {
    // This should never happen, further up-to-date checks are not possible if it does.
    LOG(ERROR) << "Could not get APEX info.";
    metrics.SetTrigger(OdrMetrics::Trigger::kUnknown);
    return cleanup_and_compile_all();
  }

  std::optional<apex::ApexInfo> art_apex_info = GetArtApexInfo(apex_info_list.value());
  if (!art_apex_info.has_value()) {
    // This should never happen, further up-to-date checks are not possible if it does.
    LOG(ERROR) << "Could not get ART APEX info.";
    metrics.SetTrigger(OdrMetrics::Trigger::kUnknown);
    return cleanup_and_compile_all();
  }

  // Record ART APEX version for metrics reporting.
  metrics.SetArtApexVersion(art_apex_info->getVersionCode());

  // Log the version so there's a starting point for any issues reported (b/197489543).
  LOG(INFO) << "ART APEX version " << art_apex_info->getVersionCode();

  // Record ART APEX last update milliseconds (used in compilation log).
  metrics.SetArtApexLastUpdateMillis(art_apex_info->getLastUpdateMillis());

  std::optional<art_apex::CacheInfo> cache_info = ReadCacheInfo();
  if (!cache_info.has_value() && OS::FileExists(cache_info_filename_.c_str())) {
    // This should not happen unless odrefresh is updated to a new version that is not
    // compatible with an old cache-info file. Further up-to-date checks are not possible if it
    // does.
    PLOG(ERROR) << "Failed to parse cache-info file: " << QuotePath(cache_info_filename_);
    metrics.SetTrigger(OdrMetrics::Trigger::kUnknown);
    return cleanup_and_compile_all();
  }

  InstructionSet system_server_isa = config_.GetSystemServerIsa();
  bool cleanup_required;

  for (const InstructionSet isa : config_.GetBootExtensionIsas()) {
    cleanup_required = false;
    if (!CheckBootExtensionArtifactsAreUpToDate(
            metrics, isa, art_apex_info.value(), cache_info, &cleanup_required)) {
      compile_boot_extensions->push_back(isa);
      // system_server artifacts are invalid without valid boot extension artifacts.
      if (isa == system_server_isa) {
        *compile_system_server = true;
        if (!RemoveSystemServerArtifactsFromData()) {
          return ExitCode::kCleanupFailed;
        }
      }
    }
    if (cleanup_required) {
      if (!RemoveBootExtensionArtifactsFromData(isa)) {
        return ExitCode::kCleanupFailed;
      }
    }
  }

  cleanup_required = false;
  if (!*compile_system_server &&
      !CheckSystemServerArtifactsAreUpToDate(
          metrics, apex_info_list.value(), cache_info, &cleanup_required)) {
    *compile_system_server = true;
  }
  if (cleanup_required) {
    if (!RemoveSystemServerArtifactsFromData()) {
      return ExitCode::kCleanupFailed;
    }
  }

  return (!compile_boot_extensions->empty() || *compile_system_server) ?
             ExitCode::kCompilationRequired :
             ExitCode::kOkay;
}

WARN_UNUSED bool OnDeviceRefresh::CompileBootExtensionArtifacts(const InstructionSet isa,
                                                                const std::string& staging_dir,
                                                                OdrMetrics& metrics,
                                                                uint32_t* dex2oat_invocation_count,
                                                                std::string* error_msg) const {
  ScopedOdrCompilationTimer compilation_timer(metrics);

  DexoptBcpExtArgs dexopt_args;
  dexopt_args.isa = InstructionSetToAidlIsa(isa);

  std::vector<std::unique_ptr<File>> readonly_files_raii;
  const std::string boot_profile_file(GetAndroidRoot() + "/etc/boot-image.prof");
  if (!PrepareDex2OatProfileIfExists(&dexopt_args.profileFd, &readonly_files_raii,
                                     boot_profile_file)) {
    LOG(ERROR) << "Missing expected profile for boot extension: " << boot_profile_file;
    return false;
  }

  const std::string dirty_image_objects_file(GetAndroidRoot() + "/etc/dirty-image-objects");
  if (OS::FileExists(dirty_image_objects_file.c_str())) {
    std::unique_ptr<File> file(OS::OpenFileForReading(dirty_image_objects_file.c_str()));
    dexopt_args.dirtyImageObjectsFd = file->Fd();
    readonly_files_raii.push_back(std::move(file));
  } else {
    LOG(WARNING) << "Missing dirty objects file : " << QuotePath(dirty_image_objects_file);
  }

  // Add boot extensions to compile.
  for (const std::string& component : boot_extension_compilable_jars_) {
    std::unique_ptr<File> file(OS::OpenFileForReading(component.c_str()));
    dexopt_args.dexPaths.emplace_back(component);
    dexopt_args.dexFds.emplace_back(file->Fd());
    readonly_files_raii.push_back(std::move(file));
  }

  auto bcp_jars = android::base::Split(config_.GetDex2oatBootClasspath(), ":");
  dexopt_args.bootClasspaths = bcp_jars;
  if (!PrepareBootClasspathFds(dexopt_args.bootClasspathFds,
                               readonly_files_raii,
                               bcp_jars)) {
    return false;
  }

  const std::string image_location = GetBootImageExtensionImagePath(/*on_system=*/false, isa);
  const OdrArtifacts artifacts = OdrArtifacts::ForBootImageExtension(image_location);
  CHECK_EQ(GetApexDataOatFilename(boot_extension_compilable_jars_.front().c_str(), isa),
           artifacts.OatPath());

  dexopt_args.oatLocation = artifacts.OatPath();
  const std::pair<const std::string, int*> location_kind_pairs[] = {
      std::make_pair(artifacts.ImagePath(), &dexopt_args.imageFd),
      std::make_pair(artifacts.OatPath(), &dexopt_args.oatFd),
      std::make_pair(artifacts.VdexPath(), &dexopt_args.vdexFd)};
  std::vector<std::unique_ptr<File>> staging_files;
  for (const auto& location_kind_pair : location_kind_pairs) {
    auto& [location, out_ptr] = location_kind_pair;
    const std::string staging_location = GetStagingLocation(staging_dir, location);
    std::unique_ptr<File> staging_file(OS::CreateEmptyFile(staging_location.c_str()));
    if (staging_file == nullptr) {
      PLOG(ERROR) << "Failed to create file: " << staging_location;
      metrics.SetStatus(OdrMetrics::Status::kIoError);
      EraseFiles(staging_files);
      return false;
    }

    if (fchmod(staging_file->Fd(), S_IRUSR | S_IWUSR) != 0) {
      PLOG(ERROR) << "Could not set file mode on " << QuotePath(staging_location);
      metrics.SetStatus(OdrMetrics::Status::kIoError);
      EraseFiles(staging_files);
      return false;
    }

    *out_ptr = staging_file->Fd();
    staging_files.emplace_back(std::move(staging_file));
  }

  const std::string install_location = android::base::Dirname(image_location);
  if (!EnsureDirectoryExists(install_location)) {
    metrics.SetStatus(OdrMetrics::Status::kIoError);
    return false;
  }

  if (!PrepareDex2OatConcurrencyArguments(&dexopt_args.threads, &dexopt_args.cpuSet)) {
    return false;
  }

  const time_t timeout = GetSubprocessTimeout();
  LOG(INFO) << "Compiling boot extensions (" << isa << "): " << dexopt_args.toString()
            << " [timeout " << timeout << "s]";
  if (config_.GetDryRun()) {
    LOG(INFO) << "Compilation skipped (dry-run).";
    return true;
  }

  bool timed_out = false;
  int dex2oat_exit_code = odr_dexopt_->DexoptBcpExtension(
      dexopt_args, timeout, &timed_out, error_msg);

  if (dex2oat_exit_code != 0) {
    if (timed_out) {
      metrics.SetStatus(OdrMetrics::Status::kTimeLimitExceeded);
    } else {
      metrics.SetStatus(OdrMetrics::Status::kDex2OatError);
    }
    EraseFiles(staging_files);
    return false;
  }

  if (!MoveOrEraseFiles(staging_files, install_location)) {
    metrics.SetStatus(OdrMetrics::Status::kInstallFailed);
    return false;
  }

  *dex2oat_invocation_count = *dex2oat_invocation_count + 1;
  ReportNextBootAnimationProgress(*dex2oat_invocation_count);

  return true;
}

WARN_UNUSED bool OnDeviceRefresh::CompileSystemServerArtifacts(const std::string& staging_dir,
                                                               OdrMetrics& metrics,
                                                               uint32_t* dex2oat_invocation_count,
                                                               std::string* error_msg) const {
  ScopedOdrCompilationTimer compilation_timer(metrics);
  std::vector<std::string> classloader_context;

  const std::string dex2oat = config_.GetDex2Oat();
  const InstructionSet isa = config_.GetSystemServerIsa();
  for (const std::string& jar : systemserver_compilable_jars_) {
    std::vector<std::unique_ptr<File>> readonly_files_raii;
    DexoptSystemServerArgs dexopt_args;
    dexopt_args.isa = InstructionSetToAidlIsa(isa);

    std::unique_ptr<File> dex_file(OS::OpenFileForReading(jar.c_str()));

    dexopt_args.dexPath = jar;
    dexopt_args.dexFd = dex_file->Fd();
    readonly_files_raii.push_back(std::move(dex_file));

    dexopt_args.isa = InstructionSetToAidlIsa(isa);
    const std::string jar_name(android::base::Basename(jar));
    const std::string profile = Concatenate({GetAndroidRoot(), "/framework/", jar_name, ".prof"});
    std::string compiler_filter =
        android::base::GetProperty("dalvik.vm.systemservercompilerfilter", "speed");
    if (compiler_filter == "speed-profile") {
      // Use speed-profile only if profile is provided, otherwise fallback to speed.
      if (PrepareDex2OatProfileIfExists(&dexopt_args.profileFd, &readonly_files_raii, profile)) {
        dexopt_args.compilerFilter = CompilerFilter::SPEED_PROFILE;
      } else {
        dexopt_args.compilerFilter = CompilerFilter::SPEED;
      }
    } else {
      dexopt_args.compilerFilter = CompilerFilterStringToAidl(compiler_filter);
    }

    const std::string image_location = GetSystemServerImagePath(/*on_system=*/false, jar);
    const std::string install_location = android::base::Dirname(image_location);
    if (classloader_context.empty()) {
      // All images are in the same directory, we only need to check on the first iteration.
      if (!EnsureDirectoryExists(install_location)) {
        metrics.SetStatus(OdrMetrics::Status::kIoError);
        return false;
      }
    }

    OdrArtifacts artifacts = OdrArtifacts::ForSystemServer(image_location);
    CHECK_EQ(artifacts.OatPath(), GetApexDataOdexFilename(jar.c_str(), isa));

    const std::pair<const std::string, int*> location_kind_pairs[] = {
        std::make_pair(artifacts.ImagePath(), &dexopt_args.imageFd),
        std::make_pair(artifacts.OatPath(), &dexopt_args.oatFd),
        std::make_pair(artifacts.VdexPath(), &dexopt_args.vdexFd)};

    std::vector<std::unique_ptr<File>> staging_files;
    for (const auto& location_kind_pair : location_kind_pairs) {
      auto& [location, out_ptr] = location_kind_pair;
      const std::string staging_location = GetStagingLocation(staging_dir, location);
      std::unique_ptr<File> staging_file(OS::CreateEmptyFile(staging_location.c_str()));
      if (staging_file == nullptr) {
        PLOG(ERROR) << "Failed to create file: " << staging_location;
        metrics.SetStatus(OdrMetrics::Status::kIoError);
        EraseFiles(staging_files);
        return false;
      }
      *out_ptr = staging_file->Fd();
      staging_files.emplace_back(std::move(staging_file));
    }
    dexopt_args.oatLocation = artifacts.OatPath();

    auto bcp_jars = android::base::Split(config_.GetBootClasspath(), ":");
    dexopt_args.bootClasspaths = bcp_jars;
    if (!PrepareBootClasspathFds(dexopt_args.bootClasspathFds,
                                 readonly_files_raii,
                                 bcp_jars)) {
      return false;
    }
    std::string unused_error_msg;
    // If the boot extension artifacts are not on /data, then boot extensions are not re-compiled
    // and the artifacts must exist on /system.
    bool boot_image_on_system =
        !BootExtensionArtifactsExist(/*on_system=*/false, isa, &unused_error_msg);
    PrepareCompiledBootClasspathFdsIfAny(
        dexopt_args, readonly_files_raii, bcp_jars, isa, boot_image_on_system);
    dexopt_args.isBootImageOnSystem = boot_image_on_system;

    dexopt_args.classloaderContext = classloader_context;
    if (!classloader_context.empty()) {
      std::vector<int> fds;
      for (const std::string& path : classloader_context) {
        std::unique_ptr<File> file(OS::OpenFileForReading(path.c_str()));
        if (!file->IsValid()) {
          PLOG(ERROR) << "Failed to open classloader context " << path;
          metrics.SetStatus(OdrMetrics::Status::kIoError);
          return false;
        }
        fds.emplace_back(file->Fd());
        readonly_files_raii.emplace_back(std::move(file));
      }
      dexopt_args.classloaderFds = fds;
    }

    if (!PrepareDex2OatConcurrencyArguments(&dexopt_args.threads, &dexopt_args.cpuSet)) {
      return false;
    }

    const time_t timeout = GetSubprocessTimeout();
    LOG(INFO) << "Compiling " << jar << ": " << dexopt_args.toString() << " [timeout " << timeout
              << "s]";
    if (config_.GetDryRun()) {
      LOG(INFO) << "Compilation skipped (dry-run).";
      return true;
    }

    bool timed_out = false;
    int dex2oat_exit_code = odr_dexopt_->DexoptSystemServer(
        dexopt_args, timeout, &timed_out, error_msg);

    if (dex2oat_exit_code != 0) {
      if (timed_out) {
        metrics.SetStatus(OdrMetrics::Status::kTimeLimitExceeded);
      } else {
        metrics.SetStatus(OdrMetrics::Status::kDex2OatError);
      }
      EraseFiles(staging_files);
      return false;
    }

    if (!MoveOrEraseFiles(staging_files, install_location)) {
      metrics.SetStatus(OdrMetrics::Status::kInstallFailed);
      return false;
    }

    *dex2oat_invocation_count = *dex2oat_invocation_count + 1;
    ReportNextBootAnimationProgress(*dex2oat_invocation_count);
    classloader_context.emplace_back(jar);
  }

  return true;
}

WARN_UNUSED ExitCode
OnDeviceRefresh::Compile(OdrMetrics& metrics,
                         const std::vector<InstructionSet>& compile_boot_extensions,
                         bool compile_system_server) const {
  const char* staging_dir = nullptr;
  metrics.SetStage(OdrMetrics::Stage::kPreparation);

  if (!config_.GetStagingDir().empty()) {
    staging_dir = config_.GetStagingDir().c_str();
  } else {
    // Create staging area and assign label for generating compilation artifacts.
    if (PaletteCreateOdrefreshStagingDirectory(&staging_dir) != PALETTE_STATUS_OK) {
      metrics.SetStatus(OdrMetrics::Status::kStagingFailed);
      return ExitCode::kCleanupFailed;
    }
  }

  // Emit cache info before compiling. This can be used to throttle compilation attempts later.
  WriteCacheInfo();

  std::string error_msg;

  uint32_t dex2oat_invocation_count = 0;
  ReportNextBootAnimationProgress(dex2oat_invocation_count);

  const auto& bcp_instruction_sets = config_.GetBootExtensionIsas();
  DCHECK(!bcp_instruction_sets.empty() && bcp_instruction_sets.size() <= 2);
  for (const InstructionSet isa : compile_boot_extensions) {
    auto stage = (isa == bcp_instruction_sets.front()) ? OdrMetrics::Stage::kPrimaryBootClasspath :
                                                         OdrMetrics::Stage::kSecondaryBootClasspath;
    metrics.SetStage(stage);
    if (!CheckCompilationSpace()) {
      metrics.SetStatus(OdrMetrics::Status::kNoSpace);
      // Return kOkay so odsign will keep and sign whatever we have been able to compile.
      return ExitCode::kOkay;
    }

    if (!CompileBootExtensionArtifacts(
            isa, staging_dir, metrics, &dex2oat_invocation_count, &error_msg)) {
      LOG(ERROR) << "Compilation of BCP failed: " << error_msg;
      if (!config_.GetDryRun() && !RemoveDirectory(staging_dir)) {
        return ExitCode::kCleanupFailed;
      }
      return ExitCode::kCompilationFailed;
    }
  }

  if (compile_system_server) {
    metrics.SetStage(OdrMetrics::Stage::kSystemServerClasspath);

    if (!CheckCompilationSpace()) {
      metrics.SetStatus(OdrMetrics::Status::kNoSpace);
      // Return kOkay so odsign will keep and sign whatever we have been able to compile.
      return ExitCode::kOkay;
    }

    if (!CompileSystemServerArtifacts(
            staging_dir, metrics, &dex2oat_invocation_count, &error_msg)) {
      LOG(ERROR) << "Compilation of system_server failed: " << error_msg;
      if (!config_.GetDryRun() && !RemoveDirectory(staging_dir)) {
        return ExitCode::kCleanupFailed;
      }
      return ExitCode::kCompilationFailed;
    }
  }

  metrics.SetStage(OdrMetrics::Stage::kComplete);
  return ExitCode::kCompilationSuccess;
}

}  // namespace odrefresh
}  // namespace art
