/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "profiling_info.h"
#include <atomic>
#include <ios>
#include <sstream>

#include "art_method-inl.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "dex/dex_instruction.h"
#include "dex/primitive.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "jvalue-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "thread.h"

namespace art {

ProfilingInfo::ProfilingInfo(ArtMethod* method, const std::vector<uint32_t>& entries)
      : baseline_hotness_count_(0),
        method_(method),
        saved_entry_point_(nullptr),
        number_of_inline_caches_(entries.size()),
        number_of_parameters_(method_->GetNumberOfParameters()),
        current_inline_uses_(0),
        is_method_being_compiled_(false),
        is_osr_method_being_compiled_(false) {
  memset(&cache_, 0, number_of_inline_caches_ * sizeof(InlineCache));
  for (size_t i = 0; i < number_of_inline_caches_; ++i) {
    cache_[i].dex_pc_ = entries[i];
  }
  ParameterInfo* params = GetParameterInfoArray();
  DCHECK_ALIGNED(params, alignof(ParameterInfo));
  // First char of shorty is return type.
  const char* param_shorty = method->GetShorty() + 1;
  for (size_t i = 0; i < number_of_parameters_; ++i) {
    new (reinterpret_cast<void*>(params + i)) ParameterInfo(Primitive::GetType(param_shorty[i]));
  }
}

bool ProfilingInfo::Create(Thread* self, ArtMethod* method, bool retry_allocation) {
  // Walk over the dex instructions of the method and keep track of
  // instructions we are interested in profiling.
  DCHECK(!method->IsNative());

  std::vector<uint32_t> entries;
  for (const DexInstructionPcPair& inst : method->DexInstructions()) {
    switch (inst->Opcode()) {
      case Instruction::INVOKE_VIRTUAL:
      case Instruction::INVOKE_VIRTUAL_RANGE:
      case Instruction::INVOKE_VIRTUAL_QUICK:
      case Instruction::INVOKE_VIRTUAL_RANGE_QUICK:
      case Instruction::INVOKE_INTERFACE:
      case Instruction::INVOKE_INTERFACE_RANGE:
        entries.push_back(inst.DexPc());
        break;

      default:
        break;
    }
  }

  // We always create a `ProfilingInfo` object, even if there is no instruction we are
  // interested in. The JIT code cache internally uses it.

  // Allocate the `ProfilingInfo` object int the JIT's data space.
  jit::JitCodeCache* code_cache = Runtime::Current()->GetJit()->GetCodeCache();
  return code_cache->AddProfilingInfo(self, method, entries, retry_allocation) != nullptr;
}

InlineCache* ProfilingInfo::GetInlineCache(uint32_t dex_pc) {
  // TODO: binary search if array is too long.
  for (size_t i = 0; i < number_of_inline_caches_; ++i) {
    if (cache_[i].dex_pc_ == dex_pc) {
      return &cache_[i];
    }
  }
  LOG(FATAL) << "No inline cache found for "  << ArtMethod::PrettyMethod(method_) << "@" << dex_pc;
  UNREACHABLE();
}

void ParameterInfo::AddParameterValue(Thread* self, const JValue& val) {
  if (LIKELY(is_megamorphic_.load(std::memory_order::memory_order_relaxed)) ||
      type_ == Primitive::kPrimNot) {
    return;
  }
  {
    ReaderMutexLock rmu(self, mutex_);
    if (is_megamorphic_.load(std::memory_order::memory_order_seq_cst)) {
      return;
    }
    auto end = data_.cbegin() + num_set_;
    if (std::find(data_.cbegin(), end, val) != end) {
      return;
    }
  }
  {
    WriterMutexLock wmu(self, mutex_);
    if (is_megamorphic_.load(std::memory_order::memory_order_seq_cst)) {
      return;
    }
    auto end = data_.begin() + num_set_;
    auto found = std::find(data_.begin(), end, val);
    if (found != end) {
      return;
    } else if (num_set_ == kMegamorphicParameterLimit) {
      is_megamorphic_ = true;
    } else {
      num_set_++;
      *found = val;
    }
  }
}

ParameterInfo::ParameterInfo(Primitive::Type type)
    : type_(type),
      is_megamorphic_(type_ == Primitive::kPrimNot),
      num_set_(0),
      mutex_("ParameterInfo Mutex", LockLevel::kGenericBottomLock) {
  DCHECK_NE(type_, Primitive::kPrimVoid);
  data_.fill(JValue());
}

ProfilingInfo::~ProfilingInfo() {
  // manually run the destructors of the parameter-infos
  ParameterInfo* params = GetParameterInfoArray();
  for (size_t i = 0; i < number_of_parameters_; i++) {
    (params + i)->~ParameterInfo();
  }
}

std::ostream& operator<<(std::ostream& os, ParameterInfo& pi) {
  ReaderMutexLock rmu(Thread::Current(), pi.mutex_);
  os << "ProfilingInfo[type: " << pi.type_ << ", megamorphic: " << std::boolalpha
     << pi.is_megamorphic_ << ", {";
  for (size_t i = 0; i < pi.num_set_; i++) {
    os << std::hex << pi.data_[i].GetJ() << ", ";
  }
  os << "}]";
  return os;
}
void ProfilingInfo::AddParameterInfo(Thread* self, ShadowFrame *sf) {
  ParameterInfo* params = GetParameterInfoArray();
  size_t regs = sf->NumberOfVRegs();
  const char* arg_start = method_->GetShorty() + 1;
  const char* cur_arg = arg_start + strlen(arg_start) - 1;
  // TODO This is terrible.
  if (number_of_parameters_ > 0) {
    // ssize so it's signed and we can do the >= 0 check.
    for (ssize_t i = number_of_parameters_ - 1; i >= 0; --i, --cur_arg) {
      if (Primitive::Is64BitType(Primitive::GetType(*cur_arg))) {
        CHECK_GE(regs, 2u);
        regs -= 2;
        params[i].AddParameterValue(self, JValue::FromPrimitive(sf->GetVRegLong(regs)));
      } else {
        CHECK_GE(regs, 1u);
        regs -= 1;
        params[i].AddParameterValue(self, JValue::FromPrimitive(sf->GetVReg(regs)));
      }
    }
  }
}

void ProfilingInfo::AddInvokeInfo(uint32_t dex_pc, mirror::Class* cls) {
  InlineCache* cache = GetInlineCache(dex_pc);
  for (size_t i = 0; i < InlineCache::kIndividualCacheSize; ++i) {
    mirror::Class* existing = cache->classes_[i].Read<kWithoutReadBarrier>();
    mirror::Class* marked = ReadBarrier::IsMarked(existing);
    if (marked == cls) {
      // Receiver type is already in the cache, nothing else to do.
      return;
    } else if (marked == nullptr) {
      // Cache entry is empty, try to put `cls` in it.
      // Note: it's ok to spin on 'existing' here: if 'existing' is not null, that means
      // it is a stalled heap address, which will only be cleared during SweepSystemWeaks,
      // *after* this thread hits a suspend point.
      GcRoot<mirror::Class> expected_root(existing);
      GcRoot<mirror::Class> desired_root(cls);
      auto atomic_root = reinterpret_cast<Atomic<GcRoot<mirror::Class>>*>(&cache->classes_[i]);
      if (!atomic_root->CompareAndSetStrongSequentiallyConsistent(expected_root, desired_root)) {
        // Some other thread put a class in the cache, continue iteration starting at this
        // entry in case the entry contains `cls`.
        --i;
      } else {
        // We successfully set `cls`, just return.
        return;
      }
    }
  }
  // Unsuccessfull - cache is full, making it megamorphic. We do not DCHECK it though,
  // as the garbage collector might clear the entries concurrently.
}

}  // namespace art
