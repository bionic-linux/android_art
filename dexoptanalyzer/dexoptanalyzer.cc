/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <string>

#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "compiler_filter.h"
#include "dex_file.h"
#include "dexoptanalyzer_return_codes.h"
#include "noop_compiler_callbacks.h"
#include "oat_file_assistant.h"
#include "os.h"
#include "runtime.h"
#include "thread-inl.h"
#include "utils.h"

namespace art {

static int original_argc;
static char** original_argv;

static std::string CommandLine() {
  std::vector<std::string> command;
  for (int i = 0; i < original_argc; ++i) {
    command.push_back(original_argv[i]);
  }
  return android::base::Join(command, ' ');
}

static void UsageErrorV(const char* fmt, va_list ap) {
  std::string error;
  android::base::StringAppendV(&error, fmt, ap);
  LOG(ERROR) << error;
}

static void UsageError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UsageErrorV(fmt, ap);
  va_end(ap);
}

NO_RETURN static void Usage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UsageErrorV(fmt, ap);
  va_end(ap);

  UsageError("Command: %s", CommandLine().c_str());
  UsageError("  Performs a dexopt analysis of the boot image or on the given dex file and returns");
  UsageError("  whether or not a dexopt is needed.");
  UsageError("Usage: dexoptanalyzer [options]...");
  UsageError("");
  UsageError("  --dex-file=<filename>: the dex file which should be analyzed.");
  UsageError("");
  UsageError("  --isa=<string>: the instruction set for which the analysis should be performed.");
  UsageError("");
  UsageError("  --compiler-filter=<string>: the target compiler filter to be used as reference");
  UsageError("       when deciding if the dex file needs to be optimized.");
  UsageError("");
  UsageError("  --assume-profile-changed: assumes the profile information has changed");
  UsageError("       when deciding if the dex file needs to be optimized.");
  UsageError("");
  UsageError("  --image=<filename>: optional, the image to be used to decide if the associated");
  UsageError("       oat file is up to date. Defaults to $ANDROID_ROOT/framework/boot.art.");
  UsageError("       Example: --image=/system/framework/boot.art");
  UsageError("");
  UsageError("  --android-data=<directory>: optional, the directory which should be used as");
  UsageError("       android-data. By default ANDROID_DATA env variable is used.");
  UsageError("");
  UsageError("  --check-boot-image: check whether the boot image is up to date or needs to");
  UsageError("       be regenerated. Cannot be used with --dex-file.");
  UsageError("");
  UsageError("Return code:");
  UsageError("  To make it easier to integrate with the internal tools this command will make");
  UsageError("  available its result (dexoptNeeded) as the exit/return code. i.e. it will not");
  UsageError("  return 0 for success and a non zero values for errors as the conventional");
  UsageError("  commands. The values and meaning of the exit codes can be found in");
  UsageError("  art/dexoptanalyzer/include/dexoptanalyzer_return_codes.h.");

  exit(static_cast<int>(dexoptanalyzer::ExitStatus::kErrorInvalidArguments));
}

class DexoptAnalyzer FINAL {
 public:
  DexoptAnalyzer() : isa_(InstructionSet::kNone),
                     compiler_filter_(CompilerFilter::Filter::kVerifyNone),
                     assume_profile_changed_(false),
                     mode_(Mode::kGetDexOptNeeded) {}

  void ParseArgs(int argc, char **argv) {
    original_argc = argc;
    original_argv = argv;

    InitLogging(argv, Runtime::Aborter);
    // Skip over the command name.
    argv++;
    argc--;

    if (argc == 0) {
      Usage("No arguments specified");
    }

    for (int i = 0; i < argc; ++i) {
      const StringPiece option(argv[i]);
      if (option == "--assume-profile-changed") {
        assume_profile_changed_ = true;
      } else if (option.starts_with("--dex-file=")) {
        dex_file_ = option.substr(strlen("--dex-file=")).ToString();
      } else if (option.starts_with("--compiler-filter=")) {
        std::string filter_str = option.substr(strlen("--compiler-filter=")).ToString();
        if (!CompilerFilter::ParseCompilerFilter(filter_str.c_str(), &compiler_filter_)) {
          Usage("Invalid compiler filter '%s'", option.data());
        }
      } else if (option.starts_with("--isa=")) {
        std::string isa_str = option.substr(strlen("--isa=")).ToString();
        isa_ = GetInstructionSetFromString(isa_str.c_str());
        if (isa_ == kNone) {
          Usage("Invalid isa '%s'", option.data());
        }
      } else if (option.starts_with("--image=")) {
        image_ = option.substr(strlen("--image=")).ToString();
      } else if (option.starts_with("--android-data=")) {
        // Overwrite android-data if needed (oat file assistant relies on a valid directory to
        // compute dalvik-cache folder). This is mostly used in tests.
        std::string new_android_data = option.substr(strlen("--android-data=")).ToString();
        setenv("ANDROID_DATA", new_android_data.c_str(), 1);
      } else if (option.starts_with("--check-boot-image")) {
        mode_ = Mode::kCheckBootImage;
      } else {
        Usage("Unknown argument '%s'", option.data());
      }
    }

    if (mode_ == Mode::kCheckBootImage && !dex_file_.empty()) {
      UsageError("--dex-file incompatible with --check-boot-image");
    }

    if (image_.empty()) {
      // If we don't receive the image, try to use the default one.
      // Tests may specify a different image (e.g. core image).
      std::string error_msg;
      image_ = GetDefaultBootImageLocation(&error_msg);

      if (image_.empty()) {
        LOG(ERROR) << error_msg;
        Usage("--image unspecified and ANDROID_ROOT not set or image file does not exist.");
      }
    }
  }

  dexoptanalyzer::ExitStatus Run() {
    switch (mode_) {
      case Mode::kGetDexOptNeeded:
        return GetDexOptNeeded();
      case Mode::kCheckBootImage:
        return CheckBootImage();
    }
    LOG(FATAL) << "Unreachable";
    UNREACHABLE();
  }

 private:
  enum Mode {
    kGetDexOptNeeded,
    kCheckBootImage,
  };

  bool CreateRuntime(bool relocate,
      const std::initializer_list<std::pair<const char*, void*>>& extra_options = {}) {
    RuntimeOptions options;
    // The image could be custom, so make sure we explicitly pass it.
    std::string img = "-Ximage:" + image_;
    options.push_back(std::make_pair(img.c_str(), nullptr));
    // The instruction set of the image should match the instruction set we will test.
    const void* isa_opt = reinterpret_cast<const void*>(GetInstructionSetString(isa_));
    options.push_back(std::make_pair("imageinstructionset", isa_opt));
     // Disable libsigchain. We don't don't need it to evaluate DexOptNeeded status.
    options.push_back(std::make_pair("-Xno-sig-chain", nullptr));
    // Pretend we are a compiler so that we can re-use the same infrastructure to load a different
    // ISA image and minimize the amount of things that get started.
    class NoopCompilerCallbacksWithRelocate FINAL : public CompilerCallbacks {
     public:
      explicit NoopCompilerCallbacksWithRelocate(bool relocation_possible)
          : CompilerCallbacks(CompilerCallbacks::CallbackMode::kCompileApp),
            relocation_possible_(relocation_possible) {}
      ~NoopCompilerCallbacksWithRelocate() {}

      void MethodVerified(verifier::MethodVerifier* verifier ATTRIBUTE_UNUSED) OVERRIDE {
      }

      void ClassRejected(ClassReference ref ATTRIBUTE_UNUSED) OVERRIDE {}

      bool IsRelocationPossible() OVERRIDE { return relocation_possible_; }

      verifier::VerifierDeps* GetVerifierDeps() const OVERRIDE { return nullptr; }

     private:
      bool relocation_possible_;

      DISALLOW_COPY_AND_ASSIGN(NoopCompilerCallbacksWithRelocate);
    };
    NoopCompilerCallbacksWithRelocate callbacks(relocate);
    options.push_back(std::make_pair("compilercallbacks", &callbacks));
    options.push_back(std::make_pair(relocate ? "-Xrelocate" : "-Xnorelocate", nullptr));

    for (auto& pair : extra_options) {
      options.push_back(pair);
    }

    if (!Runtime::Create(options, false)) {
      LOG(ERROR) << "Unable to initialize runtime";
      return false;
    }
    // Runtime::Create acquired the mutator_lock_ that is normally given away when we
    // Runtime::Start. Give it away now.
    Thread::Current()->TransitionFromRunnableToSuspended(kNative);

    return true;
  }

  dexoptanalyzer::ExitStatus CheckBootImage() {
    // Disable image compilation and fallback to running out of jars.
    // TODO: Re-implement this with something that does not require to start a runtime. b/36556109
    return CreateRuntime(true, { std::make_pair("-Xnoimage-dex2oat", nullptr),
                                 std::make_pair("-Xno-dex-file-fallback", nullptr) })
        ? dexoptanalyzer::ExitStatus::kNoDexOptNeeded
        : dexoptanalyzer::ExitStatus::kBootImageError;
  }

  dexoptanalyzer::ExitStatus GetDexOptNeeded() {
    // If the file does not exist there's nothing to do.
    // This is a fast path to avoid creating the runtime (b/34385298).
    if (!OS::FileExists(dex_file_.c_str())) {
      return dexoptanalyzer::ExitStatus::kNoDexOptNeeded;
    }
    // Make sure we don't attempt to relocate. The tool should only retrieve the DexOptNeeded
    // status and not attempt to relocate the boot image.
    if (!CreateRuntime(false)) {
      return dexoptanalyzer::ExitStatus::kErrorCannotCreateRuntime;
    }
    OatFileAssistant oat_file_assistant(dex_file_.c_str(), isa_, /*load_executable*/ false);
    // Always treat elements of the bootclasspath as up-to-date.
    // TODO(calin): this check should be in OatFileAssistant.
    if (oat_file_assistant.IsInBootClassPath()) {
      return dexoptanalyzer::ExitStatus::kNoDexOptNeeded;
    }
    int dexoptNeeded = oat_file_assistant.GetDexOptNeeded(
        compiler_filter_, assume_profile_changed_);

    // Convert OatFileAssitant codes to dexoptanalyzer codes.
    switch (dexoptNeeded) {
      case OatFileAssistant::kNoDexOptNeeded:
        return dexoptanalyzer::ExitStatus::kNoDexOptNeeded;
      case OatFileAssistant::kDex2OatFromScratch:
        return dexoptanalyzer::ExitStatus::kDex2OatFromScratch;
      case OatFileAssistant::kDex2OatForBootImage:
        return dexoptanalyzer::ExitStatus::kDex2OatForBootImageOat;
      case OatFileAssistant::kDex2OatForFilter:
        return dexoptanalyzer::ExitStatus::kDex2OatForFilterOat;
      case OatFileAssistant::kDex2OatForRelocation:
        return dexoptanalyzer::ExitStatus::kDex2OatForRelocationOat;

      case -OatFileAssistant::kDex2OatForBootImage:
        return dexoptanalyzer::ExitStatus::kDex2OatForBootImageOdex;
      case -OatFileAssistant::kDex2OatForFilter:
        return dexoptanalyzer::ExitStatus::kDex2OatForFilterOdex;
      case -OatFileAssistant::kDex2OatForRelocation:
        return dexoptanalyzer::ExitStatus::kDex2OatForRelocationOdex;
      default:
        LOG(ERROR) << "Unknown dexoptNeeded " << dexoptNeeded;
        return dexoptanalyzer::ExitStatus::kErrorUnknownDexOptNeeded;
    }
  }

  std::string dex_file_;
  InstructionSet isa_;
  CompilerFilter::Filter compiler_filter_;
  bool assume_profile_changed_;
  std::string image_;
  Mode mode_;
};

static int dexoptAnalyze(int argc, char** argv) {
  DexoptAnalyzer analyzer;

  // Parse arguments. Argument mistakes will lead to exit(kErrorInvalidArguments) in UsageError.
  analyzer.ParseArgs(argc, argv);
  return static_cast<int>(analyzer.Run());
}

}  // namespace art

int main(int argc, char **argv) {
  return art::dexoptAnalyze(argc, argv);
}
