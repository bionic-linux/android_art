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

#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "common_runtime_test.h"

#include "base/logging.h"
#include "base/macros.h"
#include "base/mutex-inl.h"
#include "class_linker.h"
#include "dex_file-inl.h"
#include "dex2oat_environment_test.h"
#include "dex2oat_return_codes.h"
#include "jit/profile_compilation_info.h"
#include "method_reference.h"
#include "oat.h"
#include "oat_file.h"
#include "utils.h"

namespace art {

struct ImageSizes {
  size_t art_size = 0;
  size_t oat_size = 0;
  size_t vdex_size = 0;
};

std::ostream& operator<<(std::ostream& os, const ImageSizes& sizes) {
  os << "art=" << sizes.art_size << " oat=" << sizes.oat_size << " vdex=" << sizes.vdex_size;
  return os;
}

class Dex2oatImageTest : public CommonRuntimeTest {
 public:
  virtual void TearDown() OVERRIDE {}

 protected:
  // Visitors take method and type references
  template <typename MethodVisitor, typename ClassVisitor>
  void VisitLibcoreDexes(const MethodVisitor& method_visitor, const ClassVisitor& class_visitor) {
    for (std::string dex : GetLibCoreDexFileNames()) {
      std::vector<std::unique_ptr<const DexFile>> dex_files;
      std::string error_msg;
      CHECK(DexFile::Open(dex.c_str(), dex, /*verify_checksum*/ false, &error_msg, &dex_files))
          << error_msg;
      for (const std::unique_ptr<const DexFile>& dex_file : dex_files) {
        for (size_t i = 0; i < dex_file->NumMethodIds(); ++i) {
          method_visitor(MethodReference(dex_file.get(), i));
        }
        for (size_t i = 0; i < dex_file->NumTypeIds(); ++i) {
          class_visitor(TypeReference(dex_file.get(), dex::TypeIndex(i)));
        }
      }
    }
  }

  static void WriteLine(File* file, std::string line) {
    line += '\n';
    EXPECT_TRUE(file->WriteFully(&line[0], line.length()));
  }

  void GenerateAllClasses(File* out_file) {
    VisitLibcoreDexes(VoidFunctor(),
                      [out_file](TypeReference ref) {
      WriteLine(out_file,
                ref.dex_file->GetTypeDescriptor(ref.dex_file->GetTypeId(ref.type_index)));
    });
  }

  void AddRuntimeArg(std::vector<std::string>& args, const std::string& arg) {
    args.push_back("--runtime-arg");
    args.push_back(arg);
  }

  ImageSizes CompileImageAndGetSizes(const std::vector<std::string>& extra_args) {
    ImageSizes ret;
    ScratchFile scratch;
    std::string scratch_dir = scratch.GetFilename();
    while (!scratch_dir.empty() && scratch_dir.back() != '/') {
      scratch_dir.pop_back();
    }
    CHECK(!scratch_dir.empty()) << "No directory " << scratch.GetFilename();
    std::string error_msg;
    if (!CompileBootImage(extra_args, scratch.GetFilename(), &error_msg)) {
      LOG(ERROR) << "Failed to compile image " << scratch.GetFilename() << error_msg;
    }
    std::string art_file = scratch.GetFilename() + ".art";
    std::string oat_file = scratch.GetFilename() + ".oat";
    std::string vdex_file = scratch.GetFilename() + ".vdex";
    ret.art_size = GetFileSizeBytes(art_file);
    ret.oat_size = GetFileSizeBytes(oat_file);
    ret.vdex_size = GetFileSizeBytes(vdex_file);
    CHECK_GT(ret.art_size, 0u) << art_file;
    CHECK_GT(ret.oat_size, 0u) << oat_file;
    CHECK_GT(ret.vdex_size, 0u) << vdex_file;
    scratch.Close();
    // Clear image files since we compile the image multiple times and don't want to leave any
    // artifacts behind.
    ClearDirectory(scratch_dir.c_str(), /*recursive*/ false);
    return ret;
  }

  bool CompileBootImage(const std::vector<std::string>& extra_args,
                        const std::string& image_file_name_prefix,
                        std::string* error_msg) {
    Runtime* const runtime = Runtime::Current();
    std::vector<std::string> argv;
    argv.push_back(runtime->GetCompilerExecutable());
    AddRuntimeArg(argv, "-Xms64m");
    AddRuntimeArg(argv, "-Xmx64m");
    std::vector<std::string> dex_files = GetLibCoreDexFileNames();
    for (const std::string& dex_file : dex_files) {
      argv.push_back("--dex-file=" + dex_file);
      argv.push_back("--dex-location=" + dex_file);
    }
    if (runtime->IsJavaDebuggable()) {
      argv.push_back("--debuggable");
    }
    runtime->AddCurrentRuntimeFeaturesAsDex2OatArguments(&argv);

    argv.push_back("--compiler-filter=speed");
    AddRuntimeArg(argv, "-Xverify:softfail");

    if (!kIsTargetBuild) {
      argv.push_back("--host");
    }

    ScratchFile file;
    const std::string image_prefix = file.GetFilename();

    argv.push_back("--image=" + image_file_name_prefix + ".art");
    argv.push_back("--oat-file=" + image_file_name_prefix + ".oat");
    argv.push_back("--oat-location=" + image_file_name_prefix + ".oat");
    argv.push_back("--base=0x60000000");

    std::vector<std::string> compiler_options = runtime->GetCompilerOptions();
    argv.insert(argv.end(), compiler_options.begin(), compiler_options.end());

    // We must set --android-root.
    const char* android_root = getenv("ANDROID_ROOT");
    CHECK(android_root != nullptr);
    argv.push_back("--android-root=" + std::string(android_root));
    argv.insert(argv.end(), extra_args.begin(), extra_args.end());

    for (std::string arg : argv) {
      std::cerr << arg << std::endl;
    }

    return RunDex2Oat(argv, error_msg);
  }

  int RunDex2Oat(const std::vector<std::string>& args, std::string* error_msg) {
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
      setenv("ANDROID_LOG_TAGS", "*:f", 1);
      dup2(link[1], STDERR_FILENO);
      close(link[0]);
      close(link[1]);
      std::vector<const char*> c_args;
      for (const std::string& str : args) {
        c_args.push_back(str.c_str());
      }
      c_args.push_back(nullptr);
      execv(c_args[0], const_cast<char* const*>(c_args.data()));
      exit(1);
      UNREACHABLE();
    } else {
      close(link[1]);
      char buffer[128];
      memset(buffer, 0, 128);
      ssize_t bytes_read = 0;

      while (TEMP_FAILURE_RETRY(bytes_read = read(link[0], buffer, 128)) > 0) {
        *error_msg += std::string(buffer, bytes_read);
      }
      close(link[0]);
      int status = -1;
      if (waitpid(pid, &status, 0) != -1) {
        return (status == 0);
      }
      return false;
    }
  }
};

TEST_F(Dex2oatImageTest, TestModesAndFilters) {
  ImageSizes full_sizes = CompileImageAndGetSizes({});
  std::cout << "Full compile image sizes " << full_sizes << std::endl;
  ImageSizes empty_sizes;
  {
    ScratchFile image_classes;
    GenerateAllClasses(image_classes.GetFile());
    // EXPECT_TRUE(image_classes.GetFile()->Flush());
    empty_sizes = CompileImageAndGetSizes({"--image-classes=" + image_classes.GetFilename()});
    image_classes.Close();
  }
  std::cout << "Empty compile image sizes " << empty_sizes << std::endl;
}

}  // namespace art
