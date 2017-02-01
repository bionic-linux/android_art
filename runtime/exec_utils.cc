/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "utils.h"

#include <inttypes.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <memory>

#include "android-base/stringprintf.h"
#include "android-base/strings.h"

#include "base/stl_util.h"
#include "base/unix_file/fd_file.h"
#include "dex_file-inl.h"
#include "dex_instruction.h"
#include "oat_quick_method_header.h"
#include "os.h"
#include "scoped_thread_state_change-inl.h"
#include "utf-inl.h"

#if defined(__APPLE__)
#include "AvailabilityMacros.h"  // For MAC_OS_X_VERSION_MAX_ALLOWED
#include <sys/syscall.h>
#include <crt_externs.h>
#endif

#if defined(__linux__)
#include <linux/unistd.h>
#endif

namespace art {

using android::base::StringAppendF;
using android::base::StringPrintf;

int ExecAndReturnCode(std::vector<std::string>& arg_vector, std::string* error_msg) {
  const std::string command_line(android::base::Join(arg_vector, ' '));
  CHECK_GE(arg_vector.size(), 1U) << command_line;

  // Convert the args to char pointers.
  const char* program = arg_vector[0].c_str();
  std::vector<char*> args;
  for (size_t i = 0; i < arg_vector.size(); ++i) {
    const std::string& arg = arg_vector[i];
    char* arg_str = const_cast<char*>(arg.c_str());
    CHECK(arg_str != nullptr) << i;
    args.push_back(arg_str);
  }
  args.push_back(nullptr);

  // fork and exec
  pid_t pid = fork();
  if (pid == 0) {
    // no allocation allowed between fork and exec

    // change process groups, so we don't get reaped by ProcessManager
    setpgid(0, 0);

    // (b/30160149): protect subprocesses from modifications to LD_LIBRARY_PATH, etc.
    // Use the snapshot of the environment from the time the runtime was created.
    char** envp = (Runtime::Current() == nullptr) ? nullptr : Runtime::Current()->GetEnvSnapshot();
    if (envp == nullptr) {
      execv(program, &args[0]);
    } else {
      execve(program, &args[0], envp);
    }
    PLOG(ERROR) << "Failed to execve(" << command_line << ")";
    // _exit to avoid atexit handlers in child.
    _exit(1);
  } else {
    if (pid == -1) {
      *error_msg = StringPrintf("Failed to execv(%s) because fork failed: %s",
                                command_line.c_str(), strerror(errno));
      return -1;
    }

    // wait for subprocess to finish
    int status = -1;
    pid_t got_pid = TEMP_FAILURE_RETRY(waitpid(pid, &status, 0));
    if (got_pid != pid) {
      *error_msg = StringPrintf("Failed after fork for execv(%s) because waitpid failed: "
                                "wanted %d, got %d: %s",
                                command_line.c_str(), pid, got_pid, strerror(errno));
      return -1;
    }
    if (WIFEXITED(status)) {
      return WEXITSTATUS(status);
    }
    return -1;
  }
}

bool Exec(std::vector<std::string>& arg_vector, std::string* error_msg) {
  int status = ExecAndReturnCode(arg_vector, error_msg);
  if (status != 0) {
    const std::string command_line(android::base::Join(arg_vector, ' '));
    *error_msg = StringPrintf("Failed execv(%s) because non-0 exit status",
                              command_line.c_str());
    return false;
  }
  return true;
}

}  // namespace art
