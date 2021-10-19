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

// A test to verify that the compilation artifacts built in the system image for all system server
// jars are used. If will fail if the artifacts are rejected by the runtime or odrefresh has run.
// This test should only run on a clean system without any APEX (including com.android.art.testing)
// installed on data, which otherwise will trigger odrefresh.

#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <string>
#include <unordered_set>
#include <vector>

#include "android-base/process.h"
#include "android-base/result.h"
#include "android-base/strings.h"
#include "arch/instruction_set.h"
#include "base/os.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "oat_file_assistant.h"
#include "procinfo/process.h"
#include "procinfo/process_map.h"

namespace art {

using ::testing::IsSubsetOf;

android::base::Result<std::vector<std::string>> GetSystemServerArtifacts() {
  const char* env_value = getenv("SYSTEMSERVERCLASSPATH");
  if (env_value == nullptr) {
    return ErrnoErrorf("Unable to get SYSTEMSERVERCLASSPATH");
  }
  if (kRuntimeISA == InstructionSet::kNone) {
    return ErrnoErrorf("Unable to get system server ISA");
  }
  std::vector<std::string> artifacts;
  for (const std::string& jar : android::base::Split(env_value, ":")) {
    std::string error_msg;
    std::string odex_file;

    if (!OatFileAssistant::DexLocationToOdexFilename(jar, kRuntimeISA, &odex_file, &error_msg)) {
      return Errorf("Failed to get odex filename. {}", error_msg);
    }

    if (!OS::FileExists(odex_file.c_str())) {
      if (errno == EACCES) {
        return ErrnoErrorf("Failed to stat() {}", odex_file);
      }
      // Dexpreopting is probably disabled. No need to report missing artifacts here because
      // artifact generation is already checked at build time.
      continue;
    }

    artifacts.push_back(odex_file);
  }
  return artifacts;
}

std::vector<pid_t> GetPidByName(const std::string& process_name) {
  std::vector<pid_t> results;
  for (pid_t pid : android::base::AllPids{}) {
    android::procinfo::ProcessInfo process_info;
    std::string error;
    if (!android::procinfo::GetProcessInfo(pid, &process_info, &error)) {
      continue;
    }
    if (process_info.name == process_name) {
      results.push_back(pid);
    }
  }
  return results;
}

android::base::Result<std::vector<std::string>> GetSystemServerArtifactsMappedOdexes() {
  std::vector<pid_t> pids = GetPidByName("system_server");
  if (pids.size() != 1) {
    return Errorf("There should be exactly one `system_server` process, got {}", pids.size());
  }
  pid_t pid = pids[0];
  std::vector<android::procinfo::MapInfo> maps;
  if (!android::procinfo::ReadProcessMaps(pid, &maps)) {
    return ErrnoErrorf("Failed to get mapped memory regions of `system_server`");
  }
  std::vector<std::string> odexes;
  for (const android::procinfo::MapInfo& map : maps) {
    if ((map.flags & PROT_EXEC) && android::base::EndsWith(map.name, ".odex")) {
      odexes.push_back(map.name);
    }
  }
  return odexes;
}

TEST(DexpreoptTest, ForSystemServer) {
  android::base::Result<std::vector<std::string>> system_server_artifacts_or =
      GetSystemServerArtifacts();
  ASSERT_RESULT_OK(system_server_artifacts_or);
  std::vector<std::string> system_server_artifacts = system_server_artifacts_or.value();

  if (system_server_artifacts.empty()) {
    // Skip the test if dexpreopting is disabled.
    return;
  }

  android::base::Result<std::vector<std::string>> mapped_odexes_or =
      GetSystemServerArtifactsMappedOdexes();
  ASSERT_RESULT_OK(mapped_odexes_or);
  std::vector<std::string> mapped_odexes = mapped_odexes_or.value();

  EXPECT_THAT(system_server_artifacts, IsSubsetOf(mapped_odexes));
}

}  // namespace art
