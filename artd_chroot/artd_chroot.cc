/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "artd_chroot.h"

#include <linux/mount.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <filesystem>
#include <system_error>

#include "aidl/com/android/server/art/BnArtdChroot.h"
#include "android-base/errors.h"
#include "android-base/file.h"
#include "android-base/logging.h"
#include "android-base/result.h"
#include "android-base/strings.h"
#include "android/binder_auto_utils.h"
#include "android/binder_manager.h"
#include "android/binder_process.h"
#include "base/file_utils.h"
#include "base/macros.h"
#include "base/os.h"
#include "exec_utils.h"
#include "fstab/fstab.h"
#include "tools/cmdline_builder.h"
#include "tools/tools.h"

namespace art {
namespace artd_chroot {

namespace {

using ::android::base::Error;
using ::android::base::Join;
using ::android::base::Result;
using ::android::base::StringReplace;
using ::android::fs_mgr::FstabEntry;
using ::art::tools::CmdlineBuilder;
using ::art::tools::GetProcMountsDescendantsOfPath;
using ::ndk::ScopedAStatus;

using std::literals::operator""s;  // NOLINT

constexpr const char* kServiceName = "artd_chroot";
constexpr const char* kChrootDir = "/mnt/pre_reboot_dexopt";
constexpr mode_t kMode = 0755;

Result<std::string> GetArtExec() {
  std::string error_msg;
  std::string art_root = GetArtRootSafe(&error_msg);
  if (!error_msg.empty()) {
    return Error() << error_msg;
  }
  return art_root + "/bin/art_exec";
}

Result<void> CreateDirs(const std::string& path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec) {
    return Errorf("Failed to create dir '{}': {}", path, ec.message());
  }
  return {};
}

Result<void> BindMountRecursive(const std::string& source, const std::string& target) {
  OR_RETURN(CreateDirs(target));
  if (mount(source.c_str(),
            target.c_str(),
            /*fs_type=*/nullptr,
            MS_BIND | MS_REC,
            /*data=*/nullptr) != 0) {
    return ErrnoErrorf("Failed to recursively bind-mount '{}' at '{}'", source, target);
  }
  return {};
}

}  // namespace

ScopedAStatus ArtdChroot::setUp() {
  LOG(INFO) << "Hello world from artd_chroot";
  std::lock_guard<std::mutex> lock(mu_);
  OR_RETURN_NON_FATAL(SetUpChroot());
  return ScopedAStatus::ok();
}

ScopedAStatus ArtdChroot::tearDown() {
  std::lock_guard<std::mutex> lock(mu_);
  OR_RETURN_NON_FATAL(TearDownChroot());
  return ScopedAStatus::ok();
}

Result<void> ArtdChroot::Start() {
  ScopedAStatus status = ScopedAStatus::fromStatus(
      AServiceManager_registerLazyService(this->asBinder().get(), kServiceName));
  if (!status.isOk()) {
    return Error() << status.getDescription();
  }

  ABinderProcess_startThreadPool();

  return {};
}

Result<void> ArtdChroot::SetUpChroot() const {
  // Set the default permission mode for new files and dirs to be `kMode`.
  umask(~kMode & 0777);

  // In case there is some leftover.
  OR_RETURN(TearDownChroot());

  // Prepare the root dir of chroot.
  OR_RETURN(CreateDirs(kChrootDir));
  if (mount(/*source=*/"tmpfs",
            kChrootDir,
            /*fs_type=*/"tmpfs",
            MS_NODEV | MS_NOEXEC | MS_NOSUID,
            ART_FORMAT("mode={:#o}", kMode).c_str()) != 0) {
    return ErrnoErrorf("Failed to mount tmpfs at '{}'", kChrootDir);
  }
  // Prevent repeated bind-mounts when bind-mounting an ancestor of `kChrootDir` at a descendant of
  // `kChrootDir`.
  if (mount(/*source=*/nullptr,
            kChrootDir,
            /*fs_type=*/nullptr,
            MS_UNBINDABLE,
            /*data=*/nullptr) != 0) {
    return ErrnoErrorf("Failed to make mount unbindable for '{}'", kChrootDir);
  }

  std::vector<std::string> tmp_dirs = {
      "/apex",
      "/linkerconfig",
      "/artd_tmp",
  };

  for (const std::string& dir : tmp_dirs) {
    OR_RETURN(CreateDirs(kChrootDir + dir));
  }

  std::vector<std::string> bind_mount_srcs = {
      // System partitions.
      "/system",
      "/system_ext",
      "/vendor",
      "/product",
      // Data partitions.
      "/data",
      "/mnt/expand",
      // Linux API filesystems.
      "/dev",
      "/proc",
      "/sys",
      // For apexd to query staged APEX sessions.
      "/metadata",
  };

  for (const std::string& src : bind_mount_srcs) {
    OR_RETURN(BindMountRecursive(src, kChrootDir + src));
  }

  // Generate empty linker config to suppress warnings.
  if (!android::base::WriteStringToFile("", kChrootDir + "/linkerconfig/ld.config.txt"s)) {
    PLOG(WARNING) << "Failed to generate empty linker config to suppress warnings";
  }

  CmdlineBuilder args;
  args.Add(OR_RETURN(GetArtExec()))
      .Add("--chroot=%s", kChrootDir)
      .Add("--")
      .Add("/system/bin/apexd")
      .Add("--otachroot-bootstrap");

  LOG(INFO) << "Running apexd: " << Join(args.Get(), /*separator=*/" ");

  std::string error_msg;
  if (!Exec(args.Get(), &error_msg)) {
    return Errorf("Failed to run apexd: {}", error_msg);
  }

  LOG(INFO) << "apexd returned code 0";

  args = CmdlineBuilder();
  args.Add(OR_RETURN(GetArtExec()))
      .Add("--chroot=%s", kChrootDir)
      .Add("--")
      .Add("/apex/com.android.runtime/bin/linkerconfig")
      .Add("--target")
      .Add("/linkerconfig");

  LOG(INFO) << "Running linkerconfig: " << Join(args.Get(), /*separator=*/" ");

  if (!Exec(args.Get(), &error_msg)) {
    return Errorf("Failed to run linkerconfig: {}", error_msg);
  }

  LOG(INFO) << "linkerconfig returned code 0";

  return {};
}

Result<void> ArtdChroot::TearDownChroot() const {
  if (OS::FileExists((kChrootDir + "/system/bin/apexd"s).c_str())) {
    CmdlineBuilder args;
    args.Add(OR_RETURN(GetArtExec()))
        .Add("--chroot=%s", kChrootDir)
        .Add("--")
        .Add("/system/bin/apexd")
        .Add("--unmount-all");

    LOG(INFO) << "Running apexd: " << Join(args.Get(), /*separator=*/" ");

    std::string error_msg;
    if (Exec(args.Get(), &error_msg)) {
      LOG(INFO) << "apexd returned code 0";
    } else {
      // Maybe apexd is not executable because a previous setup/teardown failed halfway. Continue to
      // try unmounting.
      LOG(WARNING) << "Failed to run apexd: " << error_msg;
    }
  }

  // The list is in mount order.
  std::vector<FstabEntry> entries = OR_RETURN(GetProcMountsDescendantsOfPath(kChrootDir));
  if (!entries.empty()) {
    CHECK_EQ(entries[0].mount_point, kChrootDir);
    // Don't propagate unmount events to the sources.
    if (mount(/*source=*/nullptr,
              kChrootDir,
              /*fs_type=*/nullptr,
              MS_UNBINDABLE | MS_REC,
              /*data=*/nullptr) != 0) {
      // We must not continue if this fails, or the unmount events will be propagated and brick the
      // device.
      return ErrnoErrorf("Failed to recursively make mount unbindable for '{}'", kChrootDir);
    }
  }
  for (auto it = entries.rbegin(); it != entries.rend(); it++) {
    if (umount(it->mount_point.c_str()) != 0) {
      return ErrnoErrorf("Failed to umount '{}'", it->mount_point);
    }
  }
  std::error_code ec;
  std::filesystem::remove_all(kChrootDir, ec);
  if (ec) {
    return Errorf("Failed to remove dir '{}': {}", kChrootDir, ec.message());
  }
  return {};
}

}  // namespace artd_chroot
}  // namespace art
