/*
 * Copyright 2016 The Android Open Source Project
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

#include <jni.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <sstream>

#include "jvmti.h"
#include "exec_utils.h"
#include "utils.h"

namespace art {

// Should we do a 'full_rewrite' with this test?
static constexpr bool kDoFullRewrite = true;

struct StressData {
  std::string dexter_cmd;
  std::string out_temp_dex;
  std::string in_temp_dex;
};

static void WriteToFile(const std::string& fname, jint data_len, const unsigned char* data) {
  std::ofstream file(fname, std::ios::binary | std::ios::out | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(data), data_len);
  file.flush();
}

static bool ReadIntoBuffer(const std::string& fname, /*out*/std::vector<unsigned char>* data) {
  std::ifstream file(fname, std::ios::binary | std::ios::in);
  file.seekg(0, std::ios::end);
  size_t len = file.tellg();
  data->resize(len);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(data->data()), len);
  return len != 0;
}

// TODO rewrite later.
static bool doExtractClassFromData(StressData* data,
                                   const std::string& class_name,
                                   jint in_len,
                                   const unsigned char* in_data,
                                   /*out*/std::vector<unsigned char>* dex) {
  // Write the dex file into a temporary file.
  WriteToFile(data->in_temp_dex, in_len, in_data);
  // Clear out file so even if something suppresses the exit value we will still detect dexter
  // failure.
  WriteToFile(data->out_temp_dex, 0, nullptr);
  // Have dexter do the extraction.
  std::vector<std::string> args;
  args.push_back(data->dexter_cmd);
  if (kDoFullRewrite) {
    args.push_back("-x");
    args.push_back("full_rewrite");
  }
  args.push_back("-e");
  args.push_back(class_name);
  args.push_back("-o");
  args.push_back(data->out_temp_dex);
  args.push_back(data->in_temp_dex);
  std::string error;
  if (ExecAndReturnCode(args, &error) != 0) {
    std::cerr << "unable to execute dexter: " << error << std::endl;
    return false;
  }
  return ReadIntoBuffer(data->out_temp_dex, dex);
}

// The hook we are using.
void JNICALL ClassFileLoadHookSecretNoOp(jvmtiEnv* jvmti,
                                         JNIEnv* jni_env ATTRIBUTE_UNUSED,
                                         jclass class_being_redefined ATTRIBUTE_UNUSED,
                                         jobject loader ATTRIBUTE_UNUSED,
                                         const char* name,
                                         jobject protection_domain ATTRIBUTE_UNUSED,
                                         jint class_data_len,
                                         const unsigned char* class_data,
                                         jint* new_class_data_len,
                                         unsigned char** new_class_data) {
  std::vector<unsigned char> out;
  std::string name_str(name);
  StressData* data = nullptr;
  CHECK_EQ(jvmti->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&data)),
           JVMTI_ERROR_NONE);
  if (doExtractClassFromData(data, name_str, class_data_len, class_data, /*out*/ &out)) {
    unsigned char* new_data;
    CHECK_EQ(JVMTI_ERROR_NONE, jvmti->Allocate(out.size(), &new_data));
    memcpy(new_data, out.data(), out.size());
    *new_class_data_len = static_cast<jint>(out.size());
    *new_class_data = new_data;
  } else {
    std::cerr << "Unable to extract class " << name_str << std::endl;
    *new_class_data_len = 0;
    *new_class_data = nullptr;
  }
}

// Options are ${DEXTER_BINARY},${TEMP_FILE_1},${TEMP_FILE_2}
static void ReadOptions(StressData* data, char* options) {
  std::string ops(options);
  data->dexter_cmd = ops.substr(0, ops.find(','));
  ops = ops.substr(ops.find(',') + 1);
  data->in_temp_dex = ops.substr(0, ops.find(','));
  ops = ops.substr(ops.find(',') + 1);
  data->out_temp_dex = ops;
}

extern "C" JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* vm,
                                               char* options,
                                               void* reserved ATTRIBUTE_UNUSED) {
  jvmtiEnv* jvmti = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&jvmti), JVMTI_VERSION_1_0)) {
    printf("Unable to get jvmti env!\n");
    return 1;
  }
  StressData* data = nullptr;
  if (JVMTI_ERROR_NONE != jvmti->Allocate(sizeof(StressData),
                                          reinterpret_cast<unsigned char**>(&data))) {
    std::cerr << "Unable to allocate data for stress test." << std::endl;
    return 1;
  }
  memset(data, 0, sizeof(StressData));
  // Read the options into the static variables that hold them.
  ReadOptions(data, options);
  // Save the data
  if (JVMTI_ERROR_NONE != jvmti->SetEnvironmentLocalStorage(data)) {
    std::cerr << "Unable to save stress test data." << std::endl;
    return 1;
  }

  // Just get all capabilities.
  jvmtiCapabilities caps;
  jvmti->GetPotentialCapabilities(&caps);
  jvmti->AddCapabilities(&caps);

  // Set load_hook callback and activate it
  jvmtiEventCallbacks cb;
  memset(&cb, 0, sizeof(cb));
  cb.ClassFileLoadHook = ClassFileLoadHookSecretNoOp;
  if (jvmti->SetEventCallbacks(&cb, sizeof(cb)) != JVMTI_ERROR_NONE) {
    std::cerr << "Unable to set class file load hook cb!" << std::endl;
    return 1;
  }
  if (jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                      JVMTI_EVENT_CLASS_FILE_LOAD_HOOK,
                                      nullptr) != JVMTI_ERROR_NONE) {
    std::cerr << "Unable to enable CLASS_FILE_LOAD_HOOK event!" << std::endl;
    return 1;
  }
  return 0;
}

}  // namespace art
