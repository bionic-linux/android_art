/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef ART_LIBARTBASE_BASE_READ_FILE_H_
#define ART_LIBARTBASE_BASE_READ_FILE_H_

#include <stdio.h>
#include <stdlib.h>

#include <functional>
#include <memory>
#include <string>

namespace art {

// Read lines from the given stream/file/fd, dropping comments and empty lines.
// Post-process each line with the given function.

template <typename T>
static void ReadCommentedInputStream(std::FILE* in_stream,
                                     std::function<std::string(const char*)>* process,
                                     T* output) {
  char* line = nullptr;
  size_t line_alloc = 0;
  ssize_t len = 0;
  while ((len = getline(&line, &line_alloc, in_stream)) > 0) {
    if (line[0] == '\0' || line[0] == '#' || line[0] == '\n') {
      continue;
    }
    if (line[len - 1] == '\n') {
      line[len - 1] = '\0';
    }
    if (process != nullptr) {
      std::string descriptor((*process)(line));
      output->insert(output->end(), descriptor);
    } else {
      output->insert(output->end(), line);
    }
  }
  free(line);
}

template <typename T>
bool ReadCommentedInputFromFile(const char* input_filename,
                                std::function<std::string(const char*)>* process,
                                T* output) {
  auto input_file = std::unique_ptr<FILE, decltype(&fclose)>{fopen(input_filename, "re"), fclose};
  if (!input_file) {
    LOG(ERROR) << "Failed to open input file " << input_filename;
    return false;
  }
  ReadCommentedInputStream<T>(input_file.get(), process, output);
  return true;
}

template <typename T>
bool ReadCommentedInputFromFd(int input_fd,
                              std::function<std::string(const char*)>* process,
                              T* output) {
  auto input_file = std::unique_ptr<FILE, decltype(&fclose)>{fdopen(input_fd, "r"), fclose};
  if (!input_file) {
    LOG(ERROR) << "Failed to re-open input fd from /prof/self/fd/" << input_fd;
    return false;
  }
  ReadCommentedInputStream<T>(input_file.get(), process, output);
  return true;
}

template <typename T>
std::unique_ptr<T> ReadCommentedInputFromFile(const char* input_filename,
                                              std::function<std::string(const char*)>* process) {
  std::unique_ptr<T> output(new T());
  ReadCommentedInputFromFile(input_filename, process, output.get());
  return output;
}

template <typename T>
std::unique_ptr<T> ReadCommentedInputFromFd(int input_fd,
                                            std::function<std::string(const char*)>* process) {
  std::unique_ptr<T> output(new T());
  ReadCommentedInputFromFd(input_fd, process, output.get());
  return output;
}

}  // namespace art

#endif  // ART_LIBARTBASE_BASE_READ_FILE_H_
