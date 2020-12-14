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
#include <sys/system_properties.h>
#include <unistd.h>

#include <cstdarg>
#include <fstream>
#include <iostream>
#include <queue>
#include <string>
#include <sstream>
#include <vector>

#include "android-base/file.h"
#include "android-base/logging.h"
#include "android-base/properties.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "base/bit_utils.h"
#include "base/file_utils.h"
#include "base/macros.h"
#include "base/os.h"
#include "base/string_view_cpp20.h"
#include "base/unix_file/fd_file.h"
#include "base/utils.h"
#include "exec_utils.h"
#include "palette/palette.h"
#include "palette/palette_types.h"

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
    std::unique_ptr<File> output_file(OS::CreateEmptyFileWriteOnly(output_file_path.c_str()));
    if (output_file == nullptr) {
      PLOG(ERROR) << "Failed to open '" << output_file_path << "'";
      EraseFiles(files);
      output_file->Erase();
      return false;
    }

    const size_t file_bytes = file->GetLength();
    if (!output_file->Copy(file.get(), /*offset=*/ 0, file_bytes)) {
      PLOG(ERROR) << "Failed to copy '" << file->GetPath() << "' to '" << output_file_path << "'";
      EraseFiles(files);
      output_file->Erase();
      return false;
    }

    if (!file->Erase(/*unlink=*/ true)) {
      PLOG(ERROR) << "Failed to erase '" << file->GetPath() << "'";
      EraseFiles(files);
      output_file->Erase();
      return false;
    }

    if (output_file->FlushCloseOrErase() != 0) {
      PLOG(ERROR) << "Failed to flush and close file '" << output_file_path << "'";
      EraseFiles(files);
    }
  }
  return true;
}

static void SetFileContext(std::string_view path, const char* context) {
  std::string terminated_path(path);  // Convert to a zero terminated string.
  enum PaletteStatus status = ::PaletteSetSELinuxFileContext(terminated_path.c_str(), context);
  DCHECK(status == kOkay || status == kNotSupported || status == kCheckErrno);
  if (status == PaletteStatus::kCheckErrno) {
    PLOG(ERROR) << "PaletteSetSELinuxFileContext failed.";
  }
}

static std::string GetDex2Oat32() {
  return GetArtBinDir() + "/dex2oat32";
}

static std::string GetDex2Oat64() {
  return GetArtBinDir() + "/dex2oat64";
}

static std::string GetDexOptAnalyzer() {
  return GetArtBinDir() + "/dexoptanalyzer";
}

}  // namespace

class OnDeviceRefresh final {
 private:
  // File security contexts
  static constexpr const char* kStagingContext = "u:object_r:apex_art_staging_data_file:s0";

  // Dex2Oat input paths
  static constexpr const char* kBootImageProfile = "/system/etc/boot-image.prof";
  static constexpr const char* kDirtyImageObjects = "/system/etc/dirty-image-objects";

  std::string dex2oat_;

  std::string updatable_bcp_packages_file_;
  std::string dex2oat_bcp_;

  std::vector<std::string> boot_extension_archs_;
  std::string boot_extension_output_dir_;
  std::vector<std::string> boot_extension_compilable_jars_;

  std::string systemserver_arch_;
  std::string systemserver_cp_;
  std::string systemserver_output_dir_;
  std::vector<std::string> systemserver_compilable_jars_;

  std::string staging_dir_;

 public:
  OnDeviceRefresh() {
    char value[PROP_VALUE_MAX];

    std::string arch32, arch64;
    GetProperty("ro.product.cpu.abi", value);
    if (StartsWith(value, "arm")) {
      arch32 = "arm";
      arch64 = "arm64";
    } else if (StartsWith(value, "x86")) {
      arch32 = "x86";
      arch64 = "x86_64";
    } else {
      LOG(FATAL) << "Unknown abi: " << value;
    }

    GetProperty("ro.zygote", value);
    std::string_view zygote_arch(value);
    if (zygote_arch == "zygote32") {
      boot_extension_archs_ = { arch32 };
      systemserver_arch_ = arch32;
      dex2oat_ = GetDex2Oat32();
    } else if (zygote_arch == "zygote32_64") {
      boot_extension_archs_ = { arch32, arch64 };
      systemserver_arch_ = arch32;
      dex2oat_ = GetDex2Oat64();
    } else if (zygote_arch == "zygote64_32") {
      boot_extension_archs_ = { arch32, arch64 };
      systemserver_arch_ = arch64;
      dex2oat_ = GetDex2Oat64();
    } else if (zygote_arch == "zygote64") {
      boot_extension_archs_ = { arch64 };
      systemserver_arch_ = arch64;
      dex2oat_ = GetDex2Oat64();
    } else {
      LOG(FATAL) << "Unknown zygote: " << zygote_arch;
    }

    if (GetOptionalProperty("dalvik.vm.dex2oat-updatable-bcp-packages-file", value)) {
      updatable_bcp_packages_file_ = value;
    }

    std::string art_apex_data = GetArtApexData();
    boot_extension_output_dir_ = Concatenate({art_apex_data, "/system/framework"});
    GetEnv("DEX2OATBOOTCLASSPATH", &dex2oat_bcp_);
    VisitTokens(dex2oat_bcp_, ':', [this](std::string_view component) {
      if (IsCompilableBootExtension(component)) {
        boot_extension_compilable_jars_.emplace_back(component);
      }
    });

    systemserver_output_dir_ = Concatenate({art_apex_data, "/system/framework/oat"});
    GetEnv("SYSTEMSERVERCLASSPATH", &systemserver_cp_);
    VisitTokens(systemserver_cp_, ':', [this](std::string_view component) {
      if (IsCompilableSystemServerJar(component)) {
        systemserver_compilable_jars_.emplace_back(component);
      }
    });

    staging_dir_ = Concatenate({art_apex_data, "/staging"});
  }

  static bool GetOptionalProperty(const char* name, char value[PROP_VALUE_MAX]) {
    value[0] = '\0';
    int length = __system_property_get(name, value);
    return length > 0 && length <= PROP_VALUE_MAX;
  }

  static void GetProperty(const char* name, char value[PROP_VALUE_MAX]) {
    if (!GetOptionalProperty(name, value)) {
      LOG(FATAL) << "Unable to get property \"" << name << "\"";
    }
  }

  static void GetEnv(const char* name, std::string* value) {
    const char* v = getenv(name);
    if (v == nullptr) {
      LOG(FATAL) << "Missing or invalid environment variable: " << name;
    }
    *value = v;
  }

  // Read apex_info_list.xml from input stream and determine if the ART APEX
  // listed is the factory installed version.
  bool IsFactoryApex(const char* apex_info_list_xml_path = "/apex/apex-info-list.xml") const {
    std::string buffer;
    android::base::ReadFileToString(apex_info_list_xml_path, &buffer);
    std::string_view apex_info_list_xml(buffer);
    CHECK_GT(apex_info_list_xml.size(), 0u) << "Failed to read " << apex_info_list_xml_path;

    // Walk apex-info elements.
    const std::string_view start_tag("<apex-info ");
    const std::string_view end_tag("</apex-info>");

    auto start_pos = apex_info_list_xml.find(start_tag);
    CHECK_NE(start_pos, std::string::npos) << "Start tag not found.";

    while (start_pos != std::string::npos) {
      start_pos += start_tag.size();  // Skip start of tag contents.
      auto end_pos = apex_info_list_xml.find(end_tag, start_pos);
      CHECK_NE(end_pos, std::string::npos) << "End tag not found.";

      std::string_view element = apex_info_list_xml.substr(start_pos, end_pos - start_pos);

      // Identify the active ART APEX
      if (element.find("moduleName=\"com.android.art\"") != std::string::npos &&
          element.find("isActive=\"true\"") != std::string::npos) {
        static constexpr const char* kIsFactory = "isFactory=\"true\"";
        static constexpr const char* kIsNotFactory = "isFactory=\"false\"";
        if (element.find(kIsFactory) != std::string::npos) {
          return true;
        } else {
          CHECK_NE(element.find(kIsNotFactory), std::string::npos)
            << "isFactory attribute not found.";
          return false;
        }
      }

      start_pos = apex_info_list_xml.find(start_tag, end_pos + end_tag.size());
    }

    LOG(FATAL) << "Active ART module was not found in " << apex_info_list_xml;
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

  bool CheckSystemServerJars() const {
    int missing_files = 0;
    for (const std::string& jar_path : systemserver_compilable_jars_) {
      const std::string app_image_path = GetSystemServerAppImageLocation(jar_path);
      for (auto extension : {"art", "odex", "vdex"}) {
        std::string path = ReplaceFileExtension(app_image_path, extension);
        if (!OS::FileExists(path.c_str(), true)) {
          LOG(INFO) << "Missing " << path;
          missing_files++;
        }
      }
    }

    if (missing_files != 0) {
      return false;
    }

    std::vector<std::string> classloader_context;
    for (const std::string& jar_path : systemserver_compilable_jars_) {
      std::vector<std::string> args;
      args.emplace_back(GetDexOptAnalyzer());
      args.emplace_back("--dex-file=" + jar_path);

      const std::string app_image_path = GetSystemServerAppImageLocation(jar_path);

      // Open file descriptors for dexoptanalyzer file inputs and add to the command-line.
      const std::string paths[3] = {
        ReplaceFileExtension(app_image_path, "odex"),
        ReplaceFileExtension(app_image_path, "vdex"),
        jar_path
      };
      constexpr const char* kFdArg[std::size(paths)] = {"--oat-fd=", "--vdex-fd=", "--zip-fd="};
      std::unique_ptr<File> files[std::size(paths)];
      for (size_t i = 0; i < std::size(paths); ++i) {
        files[i].reset(OS::OpenFileForReading(paths[i].c_str()));
        if (files[i] == nullptr) {
          PLOG(ERROR) << "Failed to open \"" << paths[i] << "\"";
          return false;
        }
        args.emplace_back(android::base::StringPrintf("%s%d", kFdArg[i], files[i]->Fd()));
      }

      const std::string basename(Basename(jar_path));
      std::string profile_file = Concatenate({"/system/framework/", basename, ".prof"});
      if (OS::FileExists(profile_file.c_str())) {
        args.emplace_back("--compiler-filter=speed-profile");
      } else {
        args.emplace_back("--compiler-filter=speed");
      }

      // TBD: app_image_path is not valid in the image list.
      args.emplace_back(
        Concatenate({"--image=", GetBootImage(), ":", GetBootImageExtensionImage()}));
      args.emplace_back("--isa=" + systemserver_arch_);
      args.emplace_back("--runtime-arg");
      args.emplace_back("-Xbootclasspath:" + dex2oat_bcp_);
      args.emplace_back(Concatenate({
        "--class-loader-context=PCL[", android::base::Join(classloader_context, ':'), "]"}));

      classloader_context.emplace_back(jar_path);

      LOG(INFO) << "Checking " << jar_path << ": " << android::base::Join(args, ' ');

      std::string error_msg;
      const int dexoptanalyzer_result = ExecAndReturnCode(args, &error_msg);
      switch (static_cast<dexoptanalyzer::ReturnCode>(dexoptanalyzer_result)) {
        case art::dexoptanalyzer::ReturnCode::kNoDexOptNeeded:
          break;

        // Recompile needed
        case art::dexoptanalyzer::ReturnCode::kDex2OatFromScratch:
        case art::dexoptanalyzer::ReturnCode::kDex2OatForBootImageOat:
        case art::dexoptanalyzer::ReturnCode::kDex2OatForFilterOat:
        case art::dexoptanalyzer::ReturnCode::kDex2OatForBootImageOdex:
        case art::dexoptanalyzer::ReturnCode::kDex2OatForFilterOdex:
          return false;

        // Unexpected issues
        case art::dexoptanalyzer::ReturnCode::kFlattenClassLoaderContextSuccess:
        case art::dexoptanalyzer::ReturnCode::kErrorInvalidArguments:
        case art::dexoptanalyzer::ReturnCode::kErrorCannotCreateRuntime:
        case art::dexoptanalyzer::ReturnCode::kErrorUnknownDexOptNeeded:

          LOG(ERROR) << "Unexpected result from dexoptanalyzer (" << dexoptanalyzer_result;
          return false;
      }
    }

    return true;
  }

  bool CheckBootExtensions(const std::string& isa) const {
    // odrefresh compiles boot extensions as single image so there is only one output file of each
    // type (.art, .oat, .vdex).
    const std::string image_path = GetBootImageExtensionImageLocation(isa);
    int missing_outputs = 0;
    for (const auto& extension : {"art", "oat", "vdex"}) {
      std::string output_path = ReplaceFileExtension(image_path, extension);
      if (!OS::FileExists(output_path.c_str(), true)) {
        LOG(INFO) << "Missing " << output_path;
        missing_outputs++;
      }
    }

    // TODO(oth): dexoptanalyzer check for boot image extensions.

    return missing_outputs == 0;
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

  int CheckArtifacts() {
    for (const std::string& arch : boot_extension_archs_) {
      if (!CheckBootExtensions(arch)) {
        return ExitCode::kCompilationRequired;
      }
    }
    if (!CheckSystemServerJars()) {
      return ExitCode::kCompilationRequired;
    }
    ReportSpace();  // TODO(oth): check available space.
    return ExitCode::kOkay;
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
    nftw(data_dir.c_str(), RemoveArtifact, /*__max_fd_count=*/ 4, FTW_DEPTH | FTW_MOUNT);
  }

  void RemoveStagingFilesOrDie() const {
    if (OS::DirectoryExists(staging_dir_.c_str())) {
      nftw(staging_dir_.c_str(), RemoveArtifact, /*__max_fd_count=*/ 4, FTW_DEPTH | FTW_MOUNT);
      if (rmdir(staging_dir_.c_str()) != 0) {
        PLOG(FATAL) << "Failed ot remove staging directory: '" << staging_dir_ << "'";
      }
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
        if (mkdir(path.c_str(), S_IRWXU | S_IRGRP | S_IROTH) != 0) {
          PLOG(FATAL) << "Could not create directory: " << path.c_str();
        }
      }
    });
  }

  static std::string GetBootImage() {
    // Typically /apex/com.android.art/javalib/boot.art.
    return GetArtRoot() + "/javalib/boot.art";
  }

  std::string GetBootImageExtensionImage(bool system = false) const {
    CHECK(!boot_extension_compilable_jars_.empty());
    std::string_view basename = Basename(boot_extension_compilable_jars_[0], ".jar");
    if (system) {
      // Typically /system/framework/boot-framework.art.
      return Concatenate({GetAndroidRoot(), "framework/boot-", basename, ".art"});
    } else {
      // Typically /data/misc/apexdata/system/framework/boot-framework.art.
      return Concatenate({boot_extension_output_dir_, "/boot-", basename, ".art"});
    }
  }

  std::string GetBootImageExtensionImageLocation(const std::string& isa) const {
    // Typically /data/misc/apexdata/system/framework/<arch>/boot-framework.art".
    CHECK(!boot_extension_compilable_jars_.empty());
    std::string_view basename = Basename(boot_extension_compilable_jars_[0], ".jar");
    return Concatenate({boot_extension_output_dir_, "/", isa, "/boot-", basename, ".art"});
  }

  std::string GetSystemServerAppImageLocation(const std::string& jar_path) const {
    // Typically /data/misc/apexdata/system/framework/oat/<systemserver_arch>/<jar_name>.art.
    std::string_view basename = Basename(jar_path, ".jar");
    return Concatenate({systemserver_output_dir_, "/", systemserver_arch_, "/", basename, ".art"});
  }

  std::string StagingLocation(const std::string_view& path) const {
    return Concatenate({staging_dir_, "/", Basename(path)});
  }

  bool CompileBootExtensions(const std::string& isa, std::string* error_msg) const {
    std::vector<std::string> args;
    args.push_back(dex2oat_);

    // Convert davlik.vm.boot-dex2oat-* properties to arguments.
    AddDex2OatBootTimeArguments(&args);

    // Add optional arguments.
    if (OS::FileExists(kBootImageProfile)) {
      args.emplace_back("--profile-file=" + std::string(kBootImageProfile));
    }
    if (OS::FileExists(kDirtyImageObjects)) {
      args.emplace_back("--dirty-image-objects=" + std::string(kDirtyImageObjects));
    }

    // Set boot-image and expectation of compiling boot classpath extensions.
    args.emplace_back("--boot-image=" + GetBootImage());

    // Add boot extensions to compile.
    for (const std::string& component : boot_extension_compilable_jars_) {
      args.emplace_back("--dex-file=" + component);
    }

    args.emplace_back("--runtime-arg");
    args.emplace_back("-Xbootclasspath:" + std::string(dex2oat_bcp_));
    args.emplace_back("--avoid-storing-invocation");
    args.emplace_back("--compiler-filter=speed-profile");
    args.emplace_back("--generate-debug-info");
    args.emplace_back("--image-format=lz4hc");
    args.emplace_back("--strip");
    args.emplace_back("--boot-image=" + GetBootImage());
    args.emplace_back("--android-root=out/empty");
    args.emplace_back("--abort-on-hard-verifier-error");
    args.emplace_back("--instruction-set=" + isa);
    args.emplace_back("--generate-mini-debug-info");

    // Compile as a single image for fewer files and slightly less memory overhead.
    args.emplace_back("--single-image");

    const std::string image_path = GetBootImageExtensionImageLocation(isa);
    const std::string oat_path = ReplaceFileExtension(image_path, "oat");
    const std::string vdex_path = ReplaceFileExtension(image_path, "vdex");

    args.emplace_back("--oat-location=" + oat_path);

    std::pair<const char*, const char*> path_kind_pairs[] = {
      std::make_pair(image_path.c_str(), "image"),
      std::make_pair(oat_path.c_str(), "oat"),
      std::make_pair(vdex_path.c_str(), "output-vdex")
    };

    std::vector<std::unique_ptr<File>> output_files;
    for (const auto& path_kind_pair : path_kind_pairs) {
      auto [path, kind] = path_kind_pair;
      std::unique_ptr<File> file(OS::CreateEmptyFile(StagingLocation(path).c_str()));
      if (file == nullptr) {
        PLOG(ERROR) << "Failed to create " << kind << "file: " << path;
        EraseFiles(output_files);
        return false;
      }
      args.emplace_back(android::base::StringPrintf("--%s-fd=%d", kind, file->Fd()));
      output_files.emplace_back(std::move(file));
    }

    EnsureDirectoryExists(Dirname(image_path));

    std::string cmd_line = android::base::Join(args, ' ');
    LOG(INFO) << "Compiling boot extensions (" << isa << "): " << cmd_line;
    if (!Exec(args, error_msg)) {
      EraseFiles(output_files);
      return false;
    }

    if (!MoveOrEraseFiles(output_files, Dirname(image_path))) {
      return false;
    }

    return true;
  }

  bool CompileSystemServerJars(std::string* error_msg) const {
    std::vector<std::string> classloader_context;

    for (const std::string& jar : systemserver_compilable_jars_) {
      std::vector<std::string> args;
      args.emplace_back(dex2oat_);

      // Convert davlik.vm.boot-dex2oat-* properties to arguments.
      AddDex2OatBootTimeArguments(&args);

      args.emplace_back("--dex-file=" + jar);

      const std::string app_image_path = GetSystemServerAppImageLocation(jar);
      const std::string oat_path = ReplaceFileExtension(app_image_path, "odex");
      const std::string vdex_path = ReplaceFileExtension(app_image_path, "vdex");

      if (classloader_context.empty()) {
        // All images are in the same directory, only need to check for first iteration.
        EnsureDirectoryExists(Dirname(app_image_path));
      }

      std::vector<std::unique_ptr<File>> output_files;

      std::pair<const char*, const char*> path_kind_pairs[] = {
        std::make_pair(app_image_path.c_str(), "app-image"),
        std::make_pair(oat_path.c_str(), "oat"),
        std::make_pair(vdex_path.c_str(), "output-vdex")
      };

      for (const auto& path_kind_pair : path_kind_pairs) {
        auto [path, kind] = path_kind_pair;
        std::unique_ptr<File> file(OS::CreateEmptyFile(StagingLocation(path).c_str()));
        if (file == nullptr) {
          PLOG(ERROR) << "Failed to create " << kind << " file: " << path;
          EraseFiles(output_files);
          return false;
        }
        args.emplace_back(android::base::StringPrintf("--%s-fd=%d", kind, file->Fd()));
        output_files.emplace_back(std::move(file));
      }
      args.emplace_back("--oat-location=" + oat_path);

      std::string jar_name(Basename(jar));
      const std::string profile_file = "/system/framework/" + jar_name + ".prof";
      if (OS::FileExists(profile_file.c_str())) {
        args.emplace_back("--profile-file=" + profile_file);
        args.emplace_back("--compiler-filter=speed-profile");
      } else {
        args.emplace_back("--compiler-filter=speed");
      }

      if (!updatable_bcp_packages_file_.empty()) {
        args.emplace_back("--updatable-bcp-packages-file=" + updatable_bcp_packages_file_);
      }

      args.emplace_back("--runtime-arg");
      args.emplace_back("-Xbootclasspath:" + dex2oat_bcp_);
      args.emplace_back("--avoid-storing-invocation");
      args.emplace_back(
        "--class-loader-context=PCL[" + android::base::Join(classloader_context, ':') + "]");
      args.emplace_back(
        Concatenate({"--boot-image=", GetBootImage(), ":", GetBootImageExtensionImage()}));

      args.emplace_back("--android-root=out/empty");
      args.emplace_back("--instruction-set=" + systemserver_arch_);
      args.emplace_back("--abort-on-hard-verifier-error");
      args.emplace_back("--generate-mini-debug-info");
      args.emplace_back("--compilation-reason=boot");
      args.emplace_back("--image-format=lz4");
      args.emplace_back("--resolve-startup-const-strings=true");

      LOG(INFO) << "Compiling " << jar << ": " << android::base::Join(args, ' ');

      if (!Exec(args, error_msg)) {
        EraseFiles(output_files);
        return false;
      }

      if (!MoveOrEraseFiles(output_files, Dirname(app_image_path))) {
        return false;
      }

      classloader_context.emplace_back(jar);
    }

    return true;
  }

  int Compile() const {
    // Clean-up existing files.
    RemoveArtifactsOrDie();

    // Create staging area and assign label for generating compilation artifacts.
    RemoveStagingFilesOrDie();
    EnsureDirectoryExists(staging_dir_);
    SetFileContext(staging_dir_, kStagingContext);

    std::string error_msg;

    for (const std::string& arch : boot_extension_archs_) {
      if (!CompileBootExtensions(arch, &error_msg)) {
        LOG(ERROR) << "BCP compilation failed: " << error_msg;
        RemoveStagingFilesOrDie();
        return ExitCode::kCompilationFailed;
      }
    }

    if (!CompileSystemServerJars(&error_msg)) {
      LOG(ERROR) << "system_server compilation failed: " << error_msg;
      RemoveStagingFilesOrDie();
      return ExitCode::kCompilationFailed;
    }

    // Staging area should have no files, but remove just in case.
    RemoveStagingFilesOrDie();

    return ExitCode::kOkay;
  }

  static int main(int argc, const char** argv) {
    if (argc != 2) {
      ArgumentError("Unexpected arguments presented.");
    }

    OnDeviceRefresh odr;
    for (int i = 1; i < argc; ++i) {
      std::string_view action(argv[i]);
      if (action == "--check") {
        // TODO(oth): remove this check, check artifacts on /system as they may be incomplete from
        // factory by design.
        if (odr.IsFactoryApex()) {
          return ExitCode::kOkay;
        }
        return odr.CheckArtifacts();
      } else if (action == "--force-check") {
        return odr.CheckArtifacts();
      } else if (action == "--compile") {
        if (odr.IsFactoryApex()) {
          return ExitCode::kOkay;
        }
        // TODO(oth): check freshness of artifacts before compiling. Can /system artifacts be used?
        return odr.Compile();
      } else if (action == "--force-compile") {
        return odr.Compile();
      } else if (action == "--help") {
        UsageHelp(argv[0]);
      } else {
        UsageError("Unknown argument: ", argv[i]);
      }
    }
    return ExitCode::kOkay;
  }
};

}  // namespace odrefresh
}  // namespace art

int main(int argc, const char** argv) {
  return art::odrefresh::OnDeviceRefresh::main(argc, argv);
}
