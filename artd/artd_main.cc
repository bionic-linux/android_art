/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
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
  if (options.is_pre_reboot) {
    android::base::SetDefaultTag("artd_pre_reboot");
  }

  auto artd = ndk::SharedRefBase::make<art::artd::Artd>(std::move(options));

  LOG(INFO) << "Starting artd";

  if (auto ret = artd->Start(); !ret.ok()) {
    LOG(ERROR) << "Unable to start artd: " << ret.error();
    exit(1);
  }

  ABinderProcess_joinThreadPool();

  LOG(INFO) << "artd shutting down";

  return 0;
}
