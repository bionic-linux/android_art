/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include "dalvik_system_DexFile.h"

#include <sstream>

#include "android-base/stringprintf.h"

#include "base/file_utils.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "class_linker.h"
#include <class_loader_context.h>
#include "common_throws.h"
#include "compiler_filter.h"
#include "dex/art_dex_file_loader.h"
#include "dex/dex_file-inl.h"
#include "dex/dex_file_loader.h"
#include "jni_internal.h"
#include "mirror/class_loader.h"
#include "mirror/object-inl.h"
#include "mirror/string.h"
#include "native_util.h"
#include "nativehelper/jni_macros.h"
#include "nativehelper/scoped_local_ref.h"
#include "nativehelper/scoped_utf_chars.h"
#include "oat_file.h"
#include "oat_file_assistant.h"
#include "oat_file_manager.h"
#include "os.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "utils.h"
#include "well_known_classes.h"
#include "zip_archive.h"

namespace art {

using android::base::StringPrintf;

DexFileCookie* DexFileCookieFromAddr(jlong addr) {
  return reinterpret_cast<DexFileCookie*>(static_cast<uintptr_t>(addr));
}

jlong DexFileCookieToAddr(DexFileCookie* cookie) {
  return static_cast<jlong>(reinterpret_cast<uintptr_t>(cookie));
}

static void FreeDexFileCookie(DexFileCookie* cookie) {
  Runtime* const runtime = Runtime::Current();
  ClassLinker* const class_linker = runtime->GetClassLinker();
  {
    ScopedObjectAccess soa(Thread::Current());
    for (size_t i = 0; i < cookie->dex_files.size(); ++i) {
      const DexFile* dex_file = cookie->dex_files[i].get();
      if (class_linker->IsDexFileRegistered(soa.Self(), *dex_file)) {
        // We cannot delete the dex file beceause it is still in use.
        LOG(FATAL) << "FreeDexFileCookie failed to close all dex files";
        UNREACHABLE();
      }
      cookie->dex_files[i].reset();
    }
  }

  // oat_file can be null if we are running without dex2oat.
  if (cookie->oat_file != nullptr) {
    VLOG(class_linker) << "Unregistering " << cookie->oat_file;
    runtime->GetOatFileManager().UnRegisterAndDeleteOatFile(cookie->oat_file);
  }

  delete cookie;
}

// A smart pointer that provides read-only access to a Java string's UTF chars.
// Unlike libcore's NullableScopedUtfChars, this will *not* throw NullPointerException if
// passed a null jstring. The correct idiom is:
//
//   NullableScopedUtfChars name(env, javaName);
//   if (env->ExceptionCheck()) {
//       return null;
//   }
//   // ... use name.c_str()
//
// TODO: rewrite to get rid of this, or change ScopedUtfChars to offer this option.
class NullableScopedUtfChars {
 public:
  NullableScopedUtfChars(JNIEnv* env, jstring s) : mEnv(env), mString(s) {
    mUtfChars = (s != nullptr) ? env->GetStringUTFChars(s, nullptr) : nullptr;
  }

  ~NullableScopedUtfChars() {
    if (mUtfChars) {
      mEnv->ReleaseStringUTFChars(mString, mUtfChars);
    }
  }

  const char* c_str() const {
    return mUtfChars;
  }

  size_t size() const {
    return strlen(mUtfChars);
  }

  // Element access.
  const char& operator[](size_t n) const {
    return mUtfChars[n];
  }

 private:
  JNIEnv* mEnv;
  jstring mString;
  const char* mUtfChars;

  // Disallow copy and assignment.
  NullableScopedUtfChars(const NullableScopedUtfChars&);
  void operator=(const NullableScopedUtfChars&);
};

static std::unique_ptr<MemMap> AllocateDexMemoryMap(JNIEnv* env, jint start, jint end) {
  if (end <= start) {
    ScopedObjectAccess soa(env);
    ThrowWrappedIOException("Bad range");
    return nullptr;
  }

  std::string error_message;
  size_t length = static_cast<size_t>(end - start);
  std::unique_ptr<MemMap> dex_mem_map(MemMap::MapAnonymous("DEX data",
                                                           nullptr,
                                                           length,
                                                           PROT_READ | PROT_WRITE,
                                                           /* low_4gb */ false,
                                                           /* reuse */ false,
                                                           &error_message));
  if (dex_mem_map == nullptr) {
    ScopedObjectAccess soa(env);
    ThrowWrappedIOException("%s", error_message.c_str());
  }
  return dex_mem_map;
}

static const DexFile* CreateDexFile(JNIEnv* env, std::unique_ptr<MemMap> dex_mem_map) {
  std::string location = StringPrintf("Anonymous-DexFile@%p-%p",
                                      dex_mem_map->Begin(),
                                      dex_mem_map->End());
  std::string error_message;
  const ArtDexFileLoader dex_file_loader;
  std::unique_ptr<const DexFile> dex_file(dex_file_loader.Open(location,
                                                               0,
                                                               std::move(dex_mem_map),
                                                               /* verify */ true,
                                                               /* verify_location */ true,
                                                               &error_message));
  if (dex_file == nullptr) {
    ScopedObjectAccess soa(env);
    ThrowWrappedIOException("%s", error_message.c_str());
    return nullptr;
  }

  if (!dex_file->DisableWrite()) {
    ScopedObjectAccess soa(env);
    ThrowWrappedIOException("Failed to make dex file read-only");
    return nullptr;
  }

  return dex_file.release();
}

static jlong CreateSingleDexFileCookie(JNIEnv* env, std::unique_ptr<MemMap> data) {
  std::unique_ptr<const DexFile> dex_file(CreateDexFile(env, std::move(data)));
  if (dex_file.get() == nullptr) {
    DCHECK(env->ExceptionCheck());
    return DexFileCookieToAddr(nullptr);
  }
  DexFileCookie* cookie = new DexFileCookie;
  cookie->dex_files.push_back(std::move(dex_file));
  cookie->oat_file = nullptr;
  return DexFileCookieToAddr(cookie);
}

static jlong DexFile_createCookieWithDirectBuffer(JNIEnv* env,
                                                  jclass,
                                                  jobject buffer,
                                                  jint start,
                                                  jint end) {
  uint8_t* base_address = reinterpret_cast<uint8_t*>(env->GetDirectBufferAddress(buffer));
  if (base_address == nullptr) {
    ScopedObjectAccess soa(env);
    ThrowWrappedIOException("dexFileBuffer not direct");
    return DexFileCookieToAddr(nullptr);
  }

  std::unique_ptr<MemMap> dex_mem_map(AllocateDexMemoryMap(env, start, end));
  if (dex_mem_map == nullptr) {
    DCHECK(Thread::Current()->IsExceptionPending());
    return DexFileCookieToAddr(nullptr);
  }

  size_t length = static_cast<size_t>(end - start);
  memcpy(dex_mem_map->Begin(), base_address, length);
  return CreateSingleDexFileCookie(env, std::move(dex_mem_map));
}

static jlong DexFile_createCookieWithArray(JNIEnv* env,
                                           jclass,
                                           jbyteArray buffer,
                                           jint start,
                                           jint end) {
  std::unique_ptr<MemMap> dex_mem_map(AllocateDexMemoryMap(env, start, end));
  if (dex_mem_map == nullptr) {
    DCHECK(Thread::Current()->IsExceptionPending());
    return DexFileCookieToAddr(nullptr);
  }

  auto destination = reinterpret_cast<jbyte*>(dex_mem_map.get()->Begin());
  env->GetByteArrayRegion(buffer, start, end - start, destination);
  return CreateSingleDexFileCookie(env, std::move(dex_mem_map));
}

// TODO(calin): clean up the unused parameters (here and in libcore).
static jlong DexFile_openDexFileNative(JNIEnv* env,
                                       jclass,
                                       jstring javaSourceName,
                                       jstring javaOutputName ATTRIBUTE_UNUSED,
                                       jint flags ATTRIBUTE_UNUSED,
                                       jobject class_loader,
                                       jobjectArray dex_elements) {
  ScopedUtfChars sourceName(env, javaSourceName);
  if (sourceName.c_str() == nullptr) {
    DCHECK(env->ExceptionCheck());
    return DexFileCookieToAddr(nullptr);
  }

  Runtime* const runtime = Runtime::Current();
  std::unique_ptr<DexFileCookie> cookie(new DexFileCookie());
  std::vector<std::string> error_msgs;

  cookie->dex_files = runtime->GetOatFileManager().OpenDexFilesFromOat(sourceName.c_str(),
                                                                       class_loader,
                                                                       dex_elements,
                                                                       /*out*/ &cookie->oat_file,
                                                                       /*out*/ &error_msgs);

  if (cookie->dex_files.empty()) {
    ScopedObjectAccess soa(env);
    CHECK(!error_msgs.empty());
    // The most important message is at the end. So set up nesting by going forward, which will
    // wrap the existing exception as a cause for the following one.
    auto it = error_msgs.begin();
    auto itEnd = error_msgs.end();
    for ( ; it != itEnd; ++it) {
      ThrowWrappedIOException("%s", it->c_str());
    }
    cookie.reset();
  }
  return DexFileCookieToAddr(cookie.release());
}

static jlong DexFile_getNativeFinalizer(JNIEnv*, jclass) {
  return static_cast<jlong>(reinterpret_cast<uintptr_t>(&FreeDexFileCookie));
}

static jlong DexFile_getNativeSize(JNIEnv*, jclass, jlong cookie_addr) {
  DexFileCookie* cookie = DexFileCookieFromAddr(cookie_addr);
  uint64_t file_size = 0;
  for (auto& dex_file : cookie->dex_files) {
    if (dex_file) {
      file_size += dex_file->GetHeader().file_size_;
    }
  }
  return static_cast<jlong>(file_size);
}

static jclass DexFile_defineClassNative(JNIEnv* env,
                                        jclass,
                                        jstring javaName,
                                        jobject javaLoader,
                                        jlong cookie_addr,
                                        jobject dexFile) {
  DexFileCookie* cookie = DexFileCookieFromAddr(cookie_addr);
  CHECK(cookie != nullptr);

  ScopedUtfChars class_name(env, javaName);
  if (class_name.c_str() == nullptr) {
    VLOG(class_linker) << "Failed to find class_name";
    return nullptr;
  }
  const std::string descriptor(DotToDescriptor(class_name.c_str()));
  const size_t hash(ComputeModifiedUtf8Hash(descriptor.c_str()));
  for (auto& dex_file : cookie->dex_files) {
    const DexFile::ClassDef* dex_class_def =
        OatDexFile::FindClassDef(*dex_file, descriptor.c_str(), hash);
    if (dex_class_def != nullptr) {
      ScopedObjectAccess soa(env);
      ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
      StackHandleScope<1> hs(soa.Self());
      Handle<mirror::ClassLoader> class_loader(
          hs.NewHandle(soa.Decode<mirror::ClassLoader>(javaLoader)));
      ObjPtr<mirror::DexCache> dex_cache =
          class_linker->RegisterDexFile(*dex_file, class_loader.Get());
      if (dex_cache == nullptr) {
        // OOME or InternalError (dexFile already registered with a different class loader).
        soa.Self()->AssertPendingException();
        return nullptr;
      }
      ObjPtr<mirror::Class> result = class_linker->DefineClass(soa.Self(),
                                                               descriptor.c_str(),
                                                               hash,
                                                               class_loader,
                                                               *dex_file,
                                                               *dex_class_def);
      // Add the used dex file. This only required for the DexFile.loadClass API since normal
      // class loaders already keep their dex files live.
      class_linker->InsertDexFileInToClassLoader(soa.Decode<mirror::Object>(dexFile),
                                                 class_loader.Get());
      if (result != nullptr) {
        VLOG(class_linker) << "DexFile_defineClassNative returning " << result
                           << " for " << class_name.c_str();
        return soa.AddLocalReference<jclass>(result);
      }
    }
  }
  VLOG(class_linker) << "Failed to find dex_class_def " << class_name.c_str();
  return nullptr;
}

// Needed as a compare functor for sets of const char
struct CharPointerComparator {
  bool operator()(const char *str1, const char *str2) const {
    return strcmp(str1, str2) < 0;
  }
};

// Note: this can be an expensive call, as we sort out duplicates in MultiDex files.
static jobjectArray DexFile_getClassNameList(JNIEnv* env, jclass, jlong cookie_addr) {
  DexFileCookie* cookie = DexFileCookieFromAddr(cookie_addr);
  CHECK(cookie != nullptr);

  // Push all class descriptors into a set. Use set instead of unordered_set as we want to
  // retrieve all in the end.
  std::set<const char*, CharPointerComparator> descriptors;
  for (auto& dex_file : cookie->dex_files) {
    for (size_t i = 0; i < dex_file->NumClassDefs(); ++i) {
      const DexFile::ClassDef& class_def = dex_file->GetClassDef(i);
      const char* descriptor = dex_file->GetClassDescriptor(class_def);
      descriptors.insert(descriptor);
    }
  }

  // Now create output array and copy the set into it.
  jobjectArray result = env->NewObjectArray(descriptors.size(),
                                            WellKnownClasses::java_lang_String,
                                            nullptr);
  if (result != nullptr) {
    auto it = descriptors.begin();
    auto it_end = descriptors.end();
    jsize i = 0;
    for (; it != it_end; it++, ++i) {
      std::string descriptor(DescriptorToDot(*it));
      ScopedLocalRef<jstring> jdescriptor(env, env->NewStringUTF(descriptor.c_str()));
      if (jdescriptor.get() == nullptr) {
        return nullptr;
      }
      env->SetObjectArrayElement(result, i, jdescriptor.get());
    }
  }
  return result;
}

static jint GetDexOptNeeded(JNIEnv* env,
                            const char* filename,
                            const char* instruction_set,
                            const char* compiler_filter_name,
                            const char* class_loader_context,
                            bool profile_changed,
                            bool downgrade) {
  if ((filename == nullptr) || !OS::FileExists(filename)) {
    LOG(ERROR) << "DexFile_getDexOptNeeded file '" << filename << "' does not exist";
    ScopedLocalRef<jclass> fnfe(env, env->FindClass("java/io/FileNotFoundException"));
    const char* message = (filename == nullptr) ? "<empty file name>" : filename;
    env->ThrowNew(fnfe.get(), message);
    return -1;
  }

  const InstructionSet target_instruction_set = GetInstructionSetFromString(instruction_set);
  if (target_instruction_set == InstructionSet::kNone) {
    ScopedLocalRef<jclass> iae(env, env->FindClass("java/lang/IllegalArgumentException"));
    std::string message(StringPrintf("Instruction set %s is invalid.", instruction_set));
    env->ThrowNew(iae.get(), message.c_str());
    return -1;
  }

  CompilerFilter::Filter filter;
  if (!CompilerFilter::ParseCompilerFilter(compiler_filter_name, &filter)) {
    ScopedLocalRef<jclass> iae(env, env->FindClass("java/lang/IllegalArgumentException"));
    std::string message(StringPrintf("Compiler filter %s is invalid.", compiler_filter_name));
    env->ThrowNew(iae.get(), message.c_str());
    return -1;
  }

  std::unique_ptr<ClassLoaderContext> context = nullptr;
  if (class_loader_context != nullptr) {
    context = ClassLoaderContext::Create(class_loader_context);

    if (context == nullptr) {
      ScopedLocalRef<jclass> iae(env, env->FindClass("java/lang/IllegalArgumentException"));
      std::string message(StringPrintf("Class loader context '%s' is invalid.",
                                       class_loader_context));
      env->ThrowNew(iae.get(), message.c_str());
      return -1;
    }
  }

  // TODO: Verify the dex location is well formed, and throw an IOException if
  // not?

  OatFileAssistant oat_file_assistant(filename, target_instruction_set, false);

  // Always treat elements of the bootclasspath as up-to-date.
  if (oat_file_assistant.IsInBootClassPath()) {
    return OatFileAssistant::kNoDexOptNeeded;
  }

  return oat_file_assistant.GetDexOptNeeded(filter,
                                            profile_changed,
                                            downgrade,
                                            context.get());
}

static jstring DexFile_getDexFileStatus(JNIEnv* env,
                                        jclass,
                                        jstring javaFilename,
                                        jstring javaInstructionSet) {
  ScopedUtfChars filename(env, javaFilename);
  if (env->ExceptionCheck()) {
    return nullptr;
  }

  ScopedUtfChars instruction_set(env, javaInstructionSet);
  if (env->ExceptionCheck()) {
    return nullptr;
  }

  const InstructionSet target_instruction_set = GetInstructionSetFromString(
      instruction_set.c_str());
  if (target_instruction_set == InstructionSet::kNone) {
    ScopedLocalRef<jclass> iae(env, env->FindClass("java/lang/IllegalArgumentException"));
    std::string message(StringPrintf("Instruction set %s is invalid.", instruction_set.c_str()));
    env->ThrowNew(iae.get(), message.c_str());
    return nullptr;
  }

  OatFileAssistant oat_file_assistant(filename.c_str(), target_instruction_set,
                                      false /* load_executable */);
  return env->NewStringUTF(oat_file_assistant.GetStatusDump().c_str());
}

static jint DexFile_getDexOptNeeded(JNIEnv* env,
                                    jclass,
                                    jstring javaFilename,
                                    jstring javaInstructionSet,
                                    jstring javaTargetCompilerFilter,
                                    jstring javaClassLoaderContext,
                                    jboolean newProfile,
                                    jboolean downgrade) {
  ScopedUtfChars filename(env, javaFilename);
  if (env->ExceptionCheck()) {
    return -1;
  }

  ScopedUtfChars instruction_set(env, javaInstructionSet);
  if (env->ExceptionCheck()) {
    return -1;
  }

  ScopedUtfChars target_compiler_filter(env, javaTargetCompilerFilter);
  if (env->ExceptionCheck()) {
    return -1;
  }

  NullableScopedUtfChars class_loader_context(env, javaClassLoaderContext);
  if (env->ExceptionCheck()) {
    return -1;
  }

  return GetDexOptNeeded(env,
                         filename.c_str(),
                         instruction_set.c_str(),
                         target_compiler_filter.c_str(),
                         class_loader_context.c_str(),
                         newProfile == JNI_TRUE,
                         downgrade == JNI_TRUE);
}

// public API
static jboolean DexFile_isDexOptNeeded(JNIEnv* env, jclass, jstring javaFilename) {
  ScopedUtfChars filename_utf(env, javaFilename);
  if (env->ExceptionCheck()) {
    return JNI_FALSE;
  }

  const char* filename = filename_utf.c_str();
  if ((filename == nullptr) || !OS::FileExists(filename)) {
    LOG(ERROR) << "DexFile_isDexOptNeeded file '" << filename << "' does not exist";
    ScopedLocalRef<jclass> fnfe(env, env->FindClass("java/io/FileNotFoundException"));
    const char* message = (filename == nullptr) ? "<empty file name>" : filename;
    env->ThrowNew(fnfe.get(), message);
    return JNI_FALSE;
  }

  OatFileAssistant oat_file_assistant(filename, kRuntimeISA, false);
  return oat_file_assistant.IsUpToDate() ? JNI_FALSE : JNI_TRUE;
}

static jboolean DexFile_isValidCompilerFilter(JNIEnv* env,
                                            jclass javeDexFileClass ATTRIBUTE_UNUSED,
                                            jstring javaCompilerFilter) {
  ScopedUtfChars compiler_filter(env, javaCompilerFilter);
  if (env->ExceptionCheck()) {
    return -1;
  }

  CompilerFilter::Filter filter;
  return CompilerFilter::ParseCompilerFilter(compiler_filter.c_str(), &filter)
      ? JNI_TRUE : JNI_FALSE;
}

static jboolean DexFile_isProfileGuidedCompilerFilter(JNIEnv* env,
                                                      jclass javeDexFileClass ATTRIBUTE_UNUSED,
                                                      jstring javaCompilerFilter) {
  ScopedUtfChars compiler_filter(env, javaCompilerFilter);
  if (env->ExceptionCheck()) {
    return -1;
  }

  CompilerFilter::Filter filter;
  if (!CompilerFilter::ParseCompilerFilter(compiler_filter.c_str(), &filter)) {
    return JNI_FALSE;
  }
  return CompilerFilter::DependsOnProfile(filter) ? JNI_TRUE : JNI_FALSE;
}

static jstring DexFile_getNonProfileGuidedCompilerFilter(JNIEnv* env,
                                                         jclass javeDexFileClass ATTRIBUTE_UNUSED,
                                                         jstring javaCompilerFilter) {
  ScopedUtfChars compiler_filter(env, javaCompilerFilter);
  if (env->ExceptionCheck()) {
    return nullptr;
  }

  CompilerFilter::Filter filter;
  if (!CompilerFilter::ParseCompilerFilter(compiler_filter.c_str(), &filter)) {
    return javaCompilerFilter;
  }

  CompilerFilter::Filter new_filter = CompilerFilter::GetNonProfileDependentFilterFrom(filter);

  // Filter stayed the same, return input.
  if (filter == new_filter) {
    return javaCompilerFilter;
  }

  // Create a new string object and return.
  std::string new_filter_str = CompilerFilter::NameOfFilter(new_filter);
  return env->NewStringUTF(new_filter_str.c_str());
}

static jstring DexFile_getSafeModeCompilerFilter(JNIEnv* env,
                                                 jclass javeDexFileClass ATTRIBUTE_UNUSED,
                                                 jstring javaCompilerFilter) {
  ScopedUtfChars compiler_filter(env, javaCompilerFilter);
  if (env->ExceptionCheck()) {
    return nullptr;
  }

  CompilerFilter::Filter filter;
  if (!CompilerFilter::ParseCompilerFilter(compiler_filter.c_str(), &filter)) {
    return javaCompilerFilter;
  }

  CompilerFilter::Filter new_filter = CompilerFilter::GetSafeModeFilterFrom(filter);

  // Filter stayed the same, return input.
  if (filter == new_filter) {
    return javaCompilerFilter;
  }

  // Create a new string object and return.
  std::string new_filter_str = CompilerFilter::NameOfFilter(new_filter);
  return env->NewStringUTF(new_filter_str.c_str());
}

static jboolean DexFile_isBackedByOatFile(JNIEnv*, jclass, jlong cookie_addr) {
  DexFileCookie* cookie = DexFileCookieFromAddr(cookie_addr);
  CHECK(cookie != nullptr);
  return cookie->oat_file != nullptr;
}

static jobjectArray DexFile_getDexFileOutputPaths(JNIEnv* env,
                                            jclass,
                                            jstring javaFilename,
                                            jstring javaInstructionSet) {
  ScopedUtfChars filename(env, javaFilename);
  if (env->ExceptionCheck()) {
    return nullptr;
  }

  ScopedUtfChars instruction_set(env, javaInstructionSet);
  if (env->ExceptionCheck()) {
    return nullptr;
  }

  const InstructionSet target_instruction_set = GetInstructionSetFromString(
      instruction_set.c_str());
  if (target_instruction_set == InstructionSet::kNone) {
    ScopedLocalRef<jclass> iae(env, env->FindClass("java/lang/IllegalArgumentException"));
    std::string message(StringPrintf("Instruction set %s is invalid.", instruction_set.c_str()));
    env->ThrowNew(iae.get(), message.c_str());
    return nullptr;
  }

  OatFileAssistant oat_file_assistant(filename.c_str(),
                                      target_instruction_set,
                                      false /* load_executable */);

  std::unique_ptr<OatFile> best_oat_file = oat_file_assistant.GetBestOatFile();
  if (best_oat_file == nullptr) {
    return nullptr;
  }

  std::string oat_filename = best_oat_file->GetLocation();
  std::string vdex_filename = GetVdexFilename(best_oat_file->GetLocation());

  ScopedLocalRef<jstring> jvdexFilename(env, env->NewStringUTF(vdex_filename.c_str()));
  if (jvdexFilename.get() == nullptr) {
    return nullptr;
  }
  ScopedLocalRef<jstring> joatFilename(env, env->NewStringUTF(oat_filename.c_str()));
  if (joatFilename.get() == nullptr) {
    return nullptr;
  }

  // Now create output array and copy the set into it.
  jobjectArray result = env->NewObjectArray(2,
                                            WellKnownClasses::java_lang_String,
                                            nullptr);
  env->SetObjectArrayElement(result, 0, jvdexFilename.get());
  env->SetObjectArrayElement(result, 1, joatFilename.get());

  return result;
}

static jlong DexFile_getStaticSizeOfDexFile(JNIEnv*, jclass, jlong cookie_addr) {
  DexFileCookie* cookie = DexFileCookieFromAddr(cookie_addr);

  uint64_t file_size = 0;
  for (auto& dex_file : cookie->dex_files) {
    file_size += dex_file->GetHeader().file_size_;
  }
  return static_cast<jlong>(file_size);
}

static JNINativeMethod gMethods[] = {
  NATIVE_METHOD(DexFile, getNativeFinalizer, "()J"),
  NATIVE_METHOD(DexFile, getNativeSize, "(J)J"),
  NATIVE_METHOD(DexFile,
                defineClassNative,
                "(Ljava/lang/String;"
                "Ljava/lang/ClassLoader;"
                "J"
                "Ldalvik/system/DexFile;"
                ")Ljava/lang/Class;"),
  NATIVE_METHOD(DexFile, getClassNameList, "(J)[Ljava/lang/String;"),
  NATIVE_METHOD(DexFile, isDexOptNeeded, "(Ljava/lang/String;)Z"),
  NATIVE_METHOD(DexFile, getDexOptNeeded,
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZZ)I"),
  NATIVE_METHOD(DexFile, openDexFileNative,
                "(Ljava/lang/String;"
                "Ljava/lang/String;"
                "I"
                "Ljava/lang/ClassLoader;"
                "[Ldalvik/system/DexPathList$Element;"
                ")J"),
  NATIVE_METHOD(DexFile, createCookieWithDirectBuffer,
                "(Ljava/nio/ByteBuffer;II)J"),
  NATIVE_METHOD(DexFile, createCookieWithArray, "([BII)J"),
  NATIVE_METHOD(DexFile, isValidCompilerFilter, "(Ljava/lang/String;)Z"),
  NATIVE_METHOD(DexFile, isProfileGuidedCompilerFilter, "(Ljava/lang/String;)Z"),
  NATIVE_METHOD(DexFile,
                getNonProfileGuidedCompilerFilter,
                "(Ljava/lang/String;)Ljava/lang/String;"),
  NATIVE_METHOD(DexFile,
                getSafeModeCompilerFilter,
                "(Ljava/lang/String;)Ljava/lang/String;"),
  NATIVE_METHOD(DexFile, isBackedByOatFile, "(J)Z"),
  NATIVE_METHOD(DexFile, getDexFileStatus,
                "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"),
  NATIVE_METHOD(DexFile, getDexFileOutputPaths,
                "(Ljava/lang/String;Ljava/lang/String;)[Ljava/lang/String;"),
  NATIVE_METHOD(DexFile, getStaticSizeOfDexFile, "(J)J")
};

void register_dalvik_system_DexFile(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("dalvik/system/DexFile");
}

}  // namespace art
