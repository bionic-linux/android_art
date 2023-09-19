/*
 * Copyright (C) 2012 The Android Open Source Project
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

#include <iomanip>

#include "callee_save_frame.h"
#include "jit/jit.h"
#include "runtime.h"
#include "thread-inl.h"

namespace art {

extern "C" void artDeoptimizeIfNeeded(Thread* self, uintptr_t result, bool is_ref)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  instrumentation::Instrumentation* instr = Runtime::Current()->GetInstrumentation();
  DCHECK(!self->IsExceptionPending());

  ArtMethod** sp = self->GetManagedStack()->GetTopQuickFrame();
  DCHECK(sp != nullptr && (*sp)->IsRuntimeMethod());

  DeoptimizationMethodType type = instr->GetDeoptimizationMethodType(*sp);
  JValue jvalue;
  jvalue.SetJ(result);
  instr->DeoptimizeIfNeeded(self, sp, type, jvalue, is_ref);
}

extern "C" void artTestSuspendFromCode(Thread* self) REQUIRES_SHARED(Locks::mutator_lock_) {
  // Called when there is a pending checkpoint or suspend request.
  ScopedQuickEntrypointChecks sqec(self);
  self->CheckSuspend();

  // We could have other dex instructions at the same dex pc as suspend and we need to execute
  // those instructions. So we should start executing from the current dex pc.
  ArtMethod** sp = self->GetManagedStack()->GetTopQuickFrame();
  JValue result;
  result.SetJ(0);
  Runtime::Current()->GetInstrumentation()->DeoptimizeIfNeeded(
      self, sp, DeoptimizationMethodType::kKeepDexPc, result, /* is_ref= */ false);
}

extern "C" void artImplicitSuspendFromCode(Thread* self) REQUIRES_SHARED(Locks::mutator_lock_) {
  if (kRuntimeISA == InstructionSet::kArm64) {
    // Compare x0-x7 saved to `Thread` in the `SuspensionHandler` with x0-x7 spilled to the
    // `kSaveEverything` transition frame to check for register corruption. Bug: 291839153
    ArrayRef<uint64_t> saved_regs = self->GetSavedRegsArray();
    ArtMethod** sp = self->GetManagedStack()->GetTopQuickFrame();
    static constexpr size_t kCoreSpills =
        arm64::Arm64CalleeSaveFrame::GetCoreSpills(CalleeSaveType::kSaveEverything);
    static constexpr size_t kFrameSize =
        arm64::Arm64CalleeSaveFrame::GetFrameSize(CalleeSaveType::kSaveEverything);
    static constexpr size_t kX0Offset = kFrameSize - POPCOUNT(kCoreSpills) * sizeof(uint64_t);
    uint64_t* saved_core_registers_in_frame =
        reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(sp) + (kX0Offset));
    uint64_t diff = 0;
    for (size_t i = 0; i != saved_regs.size(); ++i) {
      diff |= saved_regs[i] ^ saved_core_registers_in_frame[i];
    }
    if (UNLIKELY(diff != 0u)) {
      std::ostringstream oss;
      oss << std::hex << std::setfill('0');
      for (size_t i = 0; i != saved_regs.size(); ++i) {
        oss << " x" << i << "=0x" << std::setw(16) << saved_regs[i]
            << "~0x" << std::setw(16) << saved_core_registers_in_frame[i];
      }
      LOG(FATAL) << "Detected bug 291839153:" << oss.str();
    }
  }

  // Called when there is a pending checkpoint or suspend request.
  ScopedQuickEntrypointChecks sqec(self);
  self->CheckSuspend(/*implicit=*/ true);

  // We could have other dex instructions at the same dex pc as suspend and we need to execute
  // those instructions. So we should start executing from the current dex pc.
  ArtMethod** sp = self->GetManagedStack()->GetTopQuickFrame();
  JValue result;
  result.SetJ(0);
  Runtime::Current()->GetInstrumentation()->DeoptimizeIfNeeded(
      self, sp, DeoptimizationMethodType::kKeepDexPc, result, /* is_ref= */ false);
}

extern "C" void artCompileOptimized(ArtMethod* method, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedQuickEntrypointChecks sqec(self);
  // It is important this method is not suspended due to:
  // * It is called on entry, and object parameters are in locations that are
  //   not marked in the stack map.
  // * Async deoptimization does not expect runtime methods other than the
  //   suspend entrypoint before executing the first instruction of a Java
  //   method.
  ScopedAssertNoThreadSuspension sants("Enqueuing optimized compilation");
  Runtime::Current()->GetJit()->EnqueueOptimizedCompilation(method, self);
}

}  // namespace art
