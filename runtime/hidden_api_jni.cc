/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "hidden_api_jni.h"
#include "hidden_api.h"

#if defined(__linux__)

#include <dlfcn.h>
#include <link.h>

#include <mutex>

#include "android-base/logging.h"

#include "unwindstack/Regs.h"
#include "unwindstack/RegsGetLocal.h"
#include "unwindstack/Memory.h"
#include "unwindstack/Unwinder.h"

#include "base/bit_utils.h"
#include "base/file_utils.h"
#include "base/globals.h"
#include "base/memory_type_table.h"
#include "base/string_view_cpp20.h"

namespace art {
namespace hiddenapi {

namespace {

// The maximum number of frames to back trace through when performing CorePlatformAPI checks of
// native code.
static constexpr size_t kMaxFrames = 3;

static std::mutex gUnwindingMutex;

struct UnwindHelper {
  explicit UnwindHelper(size_t max_depth)
      : memory_(unwindstack::Memory::CreateProcessMemory(getpid())),
        jit_(memory_),
        dex_(memory_),
        unwinder_(max_depth, &maps_, memory_) {
    CHECK(maps_.Parse());
    unwinder_.SetJitDebug(&jit_, unwindstack::Regs::CurrentArch());
    unwinder_.SetDexFiles(&dex_, unwindstack::Regs::CurrentArch());
    unwinder_.SetResolveNames(false);
    unwindstack::Elf::SetCachingEnabled(false);
  }

  unwindstack::Unwinder* Unwinder() { return &unwinder_; }

 private:
  unwindstack::LocalMaps maps_;
  std::shared_ptr<unwindstack::Memory> memory_;
  unwindstack::JitDebug jit_;
  unwindstack::DexFiles dex_;
  unwindstack::Unwinder unwinder_;
};

static UnwindHelper& GetUnwindHelper() {
  static UnwindHelper helper(kMaxFrames);
  return helper;
}

}  // namespace

enum class SharedObjectKind {
  kRuntime = 0,
  kApexModule = 1,
  kOther = 2
};

std::ostream& operator<<(std::ostream& os, SharedObjectKind kind) {
  switch (kind) {
    case SharedObjectKind::kRuntime:
      os << "Runtime";
      break;
    case SharedObjectKind::kApexModule:
      os << "APEX Module";
      break;
    case SharedObjectKind::kOther:
      os << "Other";
      break;
  }
  return os;
}

// Class holding Cached ranges of loaded shared objects to facilitate checks of field and method
// resolutions within the Core Platform API for native callers.
class CodeRangeCache final {
 public:
  static CodeRangeCache& GetSingleton() {
    static CodeRangeCache Singleton;
    return Singleton;
  }

  SharedObjectKind GetSharedObjectKind(void* pc) {
    uintptr_t address = reinterpret_cast<uintptr_t>(pc);
    SharedObjectKind kind;
    if (Find(address, &kind)) {
      return kind;
    }
    return SharedObjectKind::kOther;
  }

  bool HasCache() const {
    return memory_type_table_.Size() != 0;
  }

  void BuildCache() {
    DCHECK(!HasCache());
    art::MemoryTypeTable<SharedObjectKind>::Builder builder;
    builder_ = &builder;
    libjavacore_loaded_ = false;
    libnativehelper_loaded_ = false;
    libopenjdk_loaded_ = false;

    // Iterate over ELF headers populating table_builder with executable ranges.
    dl_iterate_phdr(VisitElfInfo, this);
    memory_type_table_ = builder_->Build();

    // Check expected libraries loaded when iterating headers.
    CHECK(libjavacore_loaded_);
    CHECK(libnativehelper_loaded_);
    CHECK(libopenjdk_loaded_);
    builder_ = nullptr;
  }

  void DropCache() {
    memory_type_table_ = {};
  }

 private:
  CodeRangeCache() {}

  bool Find(uintptr_t address, SharedObjectKind* kind) const {
    const art::MemoryTypeRange<SharedObjectKind>* range = memory_type_table_.Lookup(address);
    if (range == nullptr) {
      return false;
    }
    *kind = range->Type();
    return true;
  }

  static int VisitElfInfo(struct dl_phdr_info *info, size_t size ATTRIBUTE_UNUSED, void *data)
      NO_THREAD_SAFETY_ANALYSIS {
    auto cache = reinterpret_cast<CodeRangeCache*>(data);
    art::MemoryTypeTable<SharedObjectKind>::Builder* builder = cache->builder_;

    for (size_t i = 0u; i < info->dlpi_phnum; ++i) {
      const ElfW(Phdr)& phdr = info->dlpi_phdr[i];
      if (phdr.p_type != PT_LOAD || ((phdr.p_flags & PF_X) != PF_X)) {
        continue;  // Skip anything other than code pages
      }
      uintptr_t start = info->dlpi_addr + phdr.p_vaddr;
      const uintptr_t limit = art::RoundUp(start + phdr.p_memsz, art::kPageSize);
      SharedObjectKind kind = GetKind(info->dlpi_name, start, limit);
      art::MemoryTypeRange<SharedObjectKind> range{start, limit, kind};
      if (!builder->Add(range)) {
        LOG(WARNING) << "Overlapping/invalid range found in ELF headers: " << range;
      }
    }

    // Update sanity check state.
    std::string_view dlpi_name{info->dlpi_name};
    if (!cache->libjavacore_loaded_) {
      cache->libjavacore_loaded_ = art::EndsWith(dlpi_name, kLibjavacore);
    }
    if (!cache->libnativehelper_loaded_) {
      cache->libnativehelper_loaded_ = art::EndsWith(dlpi_name, kLibnativehelper);
    }
    if (!cache->libopenjdk_loaded_) {
      cache->libopenjdk_loaded_ = art::EndsWith(dlpi_name, kLibopenjdk);
    }

    return 0;
  }

  static SharedObjectKind GetKind(const char* so_name, uintptr_t start, uintptr_t limit) {
    uintptr_t runtime_method = reinterpret_cast<uintptr_t>(CodeRangeCache::GetKind);
    if (runtime_method >= start && runtime_method < limit) {
      return SharedObjectKind::kRuntime;
    }
    return art::LocationIsOnApex(so_name) ? SharedObjectKind::kApexModule
                                          : SharedObjectKind::kOther;
  }

  art::MemoryTypeTable<SharedObjectKind> memory_type_table_;

  // Table builder, only valid during BuildCache().
  art::MemoryTypeTable<SharedObjectKind>::Builder* builder_;

  // Sanity checking state.
  bool libjavacore_loaded_;
  bool libnativehelper_loaded_;
  bool libopenjdk_loaded_;

  static constexpr std::string_view kLibjavacore = "libjavacore.so";
  static constexpr std::string_view kLibnativehelper = "libnativehelper.so";
  static constexpr std::string_view kLibopenjdk = art::kIsDebugBuild ? "libopenjdkd.so"
                                                                     : "libopenjdk.so";

  DISALLOW_COPY_AND_ASSIGN(CodeRangeCache);
};

// The current JNI stack marker: a ScopedJniStackMarker should be present on entry through JNI methods
// that we are interested in. This value is stack local because plumbing the value through the JNI
// interfaces would be intrusive to the implementation, ie checked JNI interface methods do some
// sanity checking and the call the base JNI interface. The methods signatures are fixed by the
// JNI specification, hence we track the stack marker at entry as a thread local value.
thread_local ScopedJniStackMarker* tlsJniStackMarker = nullptr;

ScopedJniStackMarker::ScopedJniStackMarker() : caller_pc_(nullptr) {
  if (tlsJniStackMarker == nullptr) {
    if (art::kIsTargetBuild) {
      tlsJniStackMarker = this;
      auto policy = Runtime::Current()->GetCorePlatformApiEnforcementPolicy();
      if (policy != hiddenapi::EnforcementPolicy::kDisabled) {
        caller_pc_ = CaptureCallerPc();
      }
    }
  }
}

ScopedJniStackMarker::~ScopedJniStackMarker() {
  if (tlsJniStackMarker == this) {
    tlsJniStackMarker = nullptr;
  }
}

ScopedJniStackMarker* ScopedJniStackMarker::Current() {
  return tlsJniStackMarker;
}

void* ScopedJniStackMarker::CaptureCallerPc() {
  CHECK_EQ(tlsJniStackMarker, this);
  std::lock_guard<std::mutex> guard(gUnwindingMutex);
  unwindstack::Unwinder* unwinder = GetUnwindHelper().Unwinder();
  std::unique_ptr<unwindstack::Regs> regs(unwindstack::Regs::CreateFromLocal());
  RegsGetLocal(regs.get());
  unwinder->SetRegs(regs.get());
  unwinder->Unwind();
  for (auto it = unwinder->frames().begin(); it != unwinder->frames().end(); ++it) {
    // Unwind to frame above the tlsJniStackMarker. The stack markers should be on the first frame
    // calling JNI methods.
    if (it->sp > reinterpret_cast<uint64_t>(this)) {
      return reinterpret_cast<void*>(it->pc);
    }
  }
  // We should always be unwinding enough frames to get past the stack marker.
  DCHECK(false);
  return nullptr;
}

// Approved native callers can resolve method and field id's via JNI. Check the first caller
// outside of the JNI library who will have called Get(Static)?(Field|Member)ID(). The presence of
// checked JNI means we need to walk frames as the internal methods can be called directly from an
// external shared-object or indirectly (via checked JNI) from an external shared-object.
bool ScopedJniStackMarker::IsCallerApproved() {
  if (caller_pc_ != nullptr) {
    SharedObjectKind kind = CodeRangeCache::GetSingleton().GetSharedObjectKind(caller_pc_);
    return (kind == SharedObjectKind::kRuntime) || (kind == SharedObjectKind::kApexModule);
  }
  return true;
}

void JniInitializeNativeCallerCheck() {
  // This method should be called only once and before there are multiple runtime threads.
  DCHECK(!CodeRangeCache::GetSingleton().HasCache());
  CodeRangeCache::GetSingleton().BuildCache();
}

void JniShutdownNativeCallerCheck() {
  CodeRangeCache::GetSingleton().DropCache();
}

}  // namespace hiddenapi
}  // namespace art

#else  // __linux__

namespace art {
namespace hiddenapi {

ScopedJniStackMarker::ScopedJniStackMarker() {}

ScopedJniStackMarker::~ScopedJniStackMarker() {}

bool ScopedJniStackMarker::IsCallerApproved() { return false; }

ScopedJniStackMarker* ScopedJniStackMarker::Current() { return nullptr; }

void JniInitializeNativeCallerCheck() {}

void JniShutdownNativeCallerCheck() {}

}  // namespace hiddenapi
}  // namespace art

#endif  // __linux__
