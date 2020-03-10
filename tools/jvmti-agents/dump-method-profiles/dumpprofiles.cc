// Copyright (C) 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <android-base/logging.h>
#include "android-base/macros.h"
#include "android-base/stringprintf.h"
#include <fcntl.h>
#include <jni.h>
#include <jvmti.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <unistd.h>

#include <atomic>
#include <functional>
#include <iomanip>
#include <iostream>
#include <istream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "android-base/unique_fd.h"

// Our old glibc prebuilt doesn't include this function so just make a compat version here.
#if defined(__linux__) && !defined(__NR_memfd_create) && !defined(ART_TARGET_ANDROID)
static int memfd_create(const char* name ATTRIBUTE_UNUSED, unsigned int flags ATTRIBUTE_UNUSED) {
  FILE* f = tmpfile();
  CHECK(f != nullptr);
  return fileno(f);
}
#endif

namespace fieldnull {

#define CHECK_JVMTI(x) CHECK_EQ((x), JVMTI_ERROR_NONE)

// Special art ti-version number. We will use this as a fallback if we cannot get a regular JVMTI
// env.
static constexpr jint kArtTiVersion = JVMTI_VERSION_1_2 | 0x40000000;

static JavaVM* java_vm = nullptr;

template <typename T>
static void Dealloc(jvmtiEnv* env, T* t) {
  env->Deallocate(reinterpret_cast<unsigned char*>(t));
}

template <typename T, typename... Rest>
static void Dealloc(jvmtiEnv* env, T* t, Rest... rs) {
  Dealloc(env, t);
  Dealloc(env, rs...);
}

static void DeallocParams(jvmtiEnv* env, jvmtiParamInfo* params, jint n_params) {
  for (jint i = 0; i < n_params; i++) {
    Dealloc(env, params[i].name);
  }
}

// The extension function to get the internal data
static jvmtiError (*VisitMethodArgumentProfiles)(
    jvmtiEnv* env,
    jclass selector,
    void (*visitor_no_profile)(
        jvmtiEnv* jvmti, JNIEnv* jni, jclass decl_class, jmethodID meth, void* thunk),
    void (*visitor_profile)(jvmtiEnv* jvmti,
                            JNIEnv* jni,
                            jclass decl_class,
                            jmethodID meth,
                            jint count,
                            jint num_parameters,
                            jboolean* method_parameter_megamorphic,
                            const char* const value_field,
                            jint* num_recorded_parameter_values,
                            jvalue** parameter_values,
                            void* thunk),
    void* thunk) = nullptr;

void Print(android::base::unique_fd& ufd, const std::string_view& str) {
  TEMP_FAILURE_RETRY(write(ufd, str.data(), str.size()));
}

static jint SetupJvmtiEnv(JavaVM* vm, jvmtiEnv** jvmti) {
  jint res = 0;
  res = vm->GetEnv(reinterpret_cast<void**>(jvmti), JVMTI_VERSION_1_1);

  if (res != JNI_OK || *jvmti == nullptr) {
    LOG(ERROR) << "Unable to access JVMTI, error code " << res;
    return vm->GetEnv(reinterpret_cast<void**>(jvmti), kArtTiVersion);
  }
  return res;
}

struct VisitData {
  bool first;
  android::base::unique_fd& ufd;
};

static void VisitMethod(jvmtiEnv* jvmti_env,
                        JNIEnv* jni ATTRIBUTE_UNUSED,
                        jclass decl,
                        jmethodID meth,
                        jint cnt,
                        jint num_params,
                        jboolean* megamorphic,
                        const char* const value_field,
                        jint* num_values,
                        jvalue** values,
                        void* thunk) {
  VisitData* data  = reinterpret_cast<VisitData*>(thunk);
  auto print = [&](const std::string_view& sv) { Print(data->ufd, sv); };
  if (!data->first) {
    print(", ");
  }
  data->first = false;
  bool first_param = true;
  char* class_name;
  char* method_name;
  char* method_sig;
  CHECK_JVMTI(jvmti_env->GetClassSignature(decl, &class_name, nullptr));
  CHECK_JVMTI(jvmti_env->GetMethodName(meth, &method_name, &method_sig, nullptr));
  std::ostringstream oss;
  oss << " { \"name\": \"" << class_name << "->" << method_name << method_sig << "\", "
      << "\"baselineHotnessCount\": " << cnt << ", \"param_info\": [";
  Dealloc(jvmti_env, class_name, method_name, method_sig);
  for (jint i = 0; i < num_params; i++, first_param = false) {
    if (!first_param) {
      oss << ", ";
    }
    oss << "{ \"megamorphic\": " << ((megamorphic[i]) ? "true" : "false") << ", \"type\": \""
        << value_field[i] << "\", \"values\": [";
    bool first_param_value = true;
    for (jint j = 0; j < num_values[i]; j++, first_param_value = false) {
      if (!first_param_value) {
        oss << ", ";
      }
      switch (value_field[i]) {
        case 'V':
        case 'L':
        default:
          LOG(FATAL) << "Unexpected type " << value_field[i];
          break;
        case 'Z':
          oss << static_cast<bool>(values[i][j].z); break;
        case 'B':
          oss << values[i][j].b; break;
        case 'C':
          oss << values[i][j].c; break;
        case 'S':
          oss << values[i][j].s; break;
        case 'I':
          oss << values[i][j].i; break;
        case 'F':
          oss << values[i][j].f; break;
        case 'J':
          oss << values[i][j].j; break;
        case 'D':
          oss << values[i][j].d; break;
      }
    }
    oss << "] }";
  }
  oss << "] }";
  print(oss.str());
}

static void DataDumpRequestCb(jvmtiEnv* jvmti) {
  LOG(WARNING) << "Dumping profiles!";
  android::base::unique_fd ufd(memfd_create("DataDumpMemfd", 0));
  Print(ufd, "{ \"methods\" : [ ");
  VisitData visit { .first = true, .ufd = ufd };
  VisitMethodArgumentProfiles(jvmti, nullptr, nullptr, &VisitMethod, &visit);
  Print(ufd, "] }");
  char* res_file;
  CHECK_JVMTI(jvmti->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&res_file)));
  android::base::unique_fd real_output(open(res_file, O_CREAT|O_WRONLY, 0644));
  if (!real_output.ok()) {
    PLOG(FATAL) << "Failed to open " << res_file << " due to " << strerror(errno);
  }
  TEMP_FAILURE_RETRY(fsync(ufd));
  struct stat fs;
  CHECK_EQ(TEMP_FAILURE_RETRY(fstat(ufd, &fs)), 0) << strerror(errno);
  CHECK_EQ(TEMP_FAILURE_RETRY(lseek(ufd, 0, SEEK_SET)), 0) << strerror(errno);
  LOG(WARNING) << "Dumping " << fs.st_size << " bytes.";
  CHECK_GE(TEMP_FAILURE_RETRY(sendfile(real_output, ufd, nullptr, fs.st_size)), 0) << strerror(errno);
}

static void VMDeathCb(jvmtiEnv* jvmti, JNIEnv* env ATTRIBUTE_UNUSED) {
  DataDumpRequestCb(jvmti);
}

static void VMInitCb(jvmtiEnv* jvmti, JNIEnv* env ATTRIBUTE_UNUSED, jobject thr ATTRIBUTE_UNUSED) {
  CHECK_JVMTI(jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, nullptr));
  CHECK_JVMTI(
      jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_DATA_DUMP_REQUEST, nullptr));
}

static jint AgentStart(JavaVM* vm, char* options, bool is_onload) {
  android::base::InitLogging(/* argv= */ nullptr);
  java_vm = vm;
  jvmtiEnv* jvmti = nullptr;
  if (SetupJvmtiEnv(vm, &jvmti) != JNI_OK) {
    LOG(ERROR) << "Could not get JVMTI env or ArtTiEnv!";
    return JNI_ERR;
  }
  jvmtiCapabilities caps {
    .can_tag_objects = 1,
  };
  CHECK_JVMTI(jvmti->AddCapabilities(&caps));
  jvmtiEventCallbacks cb {
    .VMInit = VMInitCb,
    .VMDeath = VMDeathCb,
    .DataDumpRequest = DataDumpRequestCb,
  };

  // Save the environment.
  unsigned char* saved_options;
  CHECK_JVMTI(jvmti->Allocate(strlen(options), &saved_options));
  strcpy(reinterpret_cast<char*>(saved_options), options);
  CHECK_JVMTI(jvmti->SetEnvironmentLocalStorage(saved_options));

  // Get the extensions.
  jint n_ext = 0;
  jvmtiExtensionFunctionInfo* infos = nullptr;
  if (jvmti->GetExtensionFunctions(&n_ext, &infos) != JVMTI_ERROR_NONE) {
    return JNI_ERR;
  }
  for (jint i = 0; i < n_ext; i++) {
    jvmtiExtensionFunctionInfo* cur_info = &infos[i];
    if (strcmp("com.android.art.internal.visit_method_profiling_info", cur_info->id) == 0) {
      VisitMethodArgumentProfiles =
          reinterpret_cast<decltype(VisitMethodArgumentProfiles)>(cur_info->func);
    }
    // Cleanup the cur_info
    DeallocParams(jvmti, cur_info->params, cur_info->param_count);
    Dealloc(jvmti, cur_info->id, cur_info->short_description, cur_info->params, cur_info->errors);
  }
  // Cleanup the array.
  Dealloc(jvmti, infos);

  if (VisitMethodArgumentProfiles == nullptr) {
    return JNI_ERR;
  }

  CHECK_JVMTI(jvmti->SetEventCallbacks(&cb, sizeof(cb)));
  if (is_onload) {
    CHECK_JVMTI(jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, nullptr));
  } else {
    CHECK_JVMTI(jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, nullptr));
    CHECK_JVMTI(
        jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_DATA_DUMP_REQUEST, nullptr));
  }
  return JNI_OK;
}

// Late attachment (e.g. 'am attach-agent').
extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm,
                                                 char* options,
                                                 void* reserved ATTRIBUTE_UNUSED) {
  return AgentStart(vm, options, /*is_onload=*/false);
}

// Early attachment
extern "C" JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* jvm,
                                               char* options,
                                               void* reserved ATTRIBUTE_UNUSED) {
  return AgentStart(jvm, options, /*is_onload=*/true);
}

}  // namespace fieldnull
