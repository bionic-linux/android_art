/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <stdlib.h>

#include <memory>
#include <string>

#include "android-base/logging.h"
#include "android-base/macros.h"
#include "android-base/strings.h"
#include "android/binder_interface_utils.h"
#include "android/binder_process.h"
#include "artd.h"

namespace art {
namespace artd {
namespace {

using ::android::base::ConsumePrefix;

constexpr int kErrorUsage = 100;

[[noreturn]] void ParseError(const std::string& error_msg) {
  LOG(ERROR) << error_msg;
  std::cerr << error_msg << "\n";
  exit(kErrorUsage);
}

Options ParseOptions(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "--pre-reboot") {
      options.is_pre_reboot = true;
    } else if (arg == "--pre-reboot-wrapper") {
      options.is_pre_reboot_wrapper = true;
    } else if (ConsumePrefix(&arg, "--in-fd=")) {
      options.in_fd = std::stoi(std::string(arg));
    } else if (ConsumePrefix(&arg, "--out-fd=")) {
      options.out_fd = std::stoi(std::string(arg));
    } else {
      ParseError("Unknown option " + std::string(arg));
    }
  }
  return options;
}

}  // namespace
}  // namespace artd
}  // namespace art

int main([[maybe_unused]] int argc, char* argv[]) {
  android::base::InitLogging(argv);

  art::artd::Options options = art::artd::ParseOptions(argc, argv);

  if (options.is_pre_reboot_wrapper) {
    android::base::SetDefaultTag("artd_pre_reboot_wrapper");
    auto artd_wrapper = ndk::SharedRefBase::make<art::artd::ArtdPreRebootWrapper>();
    LOG(INFO) << "Starting artd wrapper";
    android::base::Result<void> ret = artd_wrapper->Start();
    if (!ret.ok()) {
      LOG(ERROR) << "artd wrapper failed: " << ret.error();
      exit(1);
    }
  } else if (options.is_pre_reboot) {
    android::base::SetDefaultTag("artd_pre_reboot");
    auto artd = ndk::SharedRefBase::make<art::artd::Artd>(std::move(options));
    LOG(INFO) << "Starting artd_pre_reboot";
    android::base::Result<void> ret = artd->StartRawBinder();
    if (!ret.ok()) {
      LOG(ERROR) << "artd_pre_reboot failed: " << ret.error();
      exit(1);
    }
  } else {
    auto artd = ndk::SharedRefBase::make<art::artd::Artd>(std::move(options));
    LOG(INFO) << "Starting artd";
    android::base::Result<void> ret = artd->Start();
    if (!ret.ok()) {
      LOG(ERROR) << "Unable to start artd: " << ret.error();
      exit(1);
    }
    ABinderProcess_joinThreadPool();
  }

  LOG(INFO) << "artd shutting down";

  return 0;
}
