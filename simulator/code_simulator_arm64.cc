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

#include "code_simulator_arm64.h"

#include "arch/arm64/asm_support_arm64.h"
#include "arch/instruction_set.h"
#include "base/memory_region.h"
#include "base/utils.h"
#include "entrypoints/quick/runtime_entrypoints_list.h"

#include "code_simulator_container.h"

using namespace vixl::aarch64;  // NOLINT(build/namespaces)

namespace art {

// Enable the simulator debugger, disabled by default.
static constexpr bool kSimDebuggerEnabled = false;

extern "C" const void* GetQuickInvokeStub();
extern "C" const void* GetQuickInvokeStaticStub();

namespace arm64 {

BasicCodeSimulatorArm64* BasicCodeSimulatorArm64::CreateBasicCodeSimulatorArm64(size_t stack_size) {
  if (kCanSimulate) {
    BasicCodeSimulatorArm64* simulator = new BasicCodeSimulatorArm64();
    simulator->InitInstructionSimulator(stack_size);
    return simulator;
  } else {
    return nullptr;
  }
}

BasicCodeSimulatorArm64::BasicCodeSimulatorArm64()
    : BasicCodeSimulator(), decoder_(nullptr), simulator_(nullptr) {
  CHECK(kCanSimulate);
  decoder_.reset(new Decoder());
}


Simulator* BasicCodeSimulatorArm64::CreateNewInstructionSimulator(SimStack::Allocated&& stack) {
  return new Simulator(decoder_.get(), stdout, std::move(stack));
}

void BasicCodeSimulatorArm64::InitInstructionSimulator(size_t stack_size) {
  SimStack stack_builder;
  stack_builder.SetUsableSize(stack_size);

  // Protected regions are added for the simulator in Thread::InstallSimulatorImplicitProtection()
  // so disable them for the simulator here.
  stack_builder.SetLimitGuardSize(0);
  stack_builder.SetBaseGuardSize(0);

  // Align the stack to a page so we can install protected regions using mprotect.
  stack_builder.AlignToBytesLog2(log2(MemMap::GetPageSize()));

  SimStack::Allocated stack = stack_builder.Allocate();
  simulator_.reset(CreateNewInstructionSimulator(std::move(stack)));

  // VIXL simulator will print a warning by default if it gets an instruction with any special
  // behavior in terms of memory model - not only those with exclusive access.
  //
  // TODO: Update this once the behavior is resolved in VIXL.
  simulator_->SilenceExclusiveAccessWarning();

  if (VLOG_IS_ON(simulator)) {
    // Only trace the main thread. Multiple threads tracing simulation at the same time can ruin
    // the output trace, making it difficult to read.
    // TODO(Simulator): Support tracing multiple threads at the same time.
    if (::art::GetTid() == static_cast<uint32_t>(getpid())) {
      simulator_->SetTraceParameters(LOG_DISASM | LOG_WRITE | LOG_REGS);
    }
  }

  simulator_->SetColouredTrace(true);
  simulator_->SetDebuggerEnabled(kSimDebuggerEnabled);
}

void BasicCodeSimulatorArm64::RunFrom(intptr_t code_buffer) {
  simulator_->RunFrom(reinterpret_cast<const vixl::aarch64::Instruction*>(code_buffer));
}

bool BasicCodeSimulatorArm64::GetCReturnBool() const {
  return simulator_->ReadWRegister(0);
}

int32_t BasicCodeSimulatorArm64::GetCReturnInt32() const {
  return simulator_->ReadWRegister(0);
}

int64_t BasicCodeSimulatorArm64::GetCReturnInt64() const {
  return simulator_->ReadXRegister(0);
}

#ifdef ART_USE_SIMULATOR

//
// Special registers defined in asm_support_arm64.s.
//

// Frame Pointer.
static const unsigned kFp = 29;
// Stack Pointer.
static const unsigned kSp = 31;

class CustomSimulator final: public Simulator {
 public:
  CustomSimulator(Decoder* decoder, FILE* stream, SimStack::Allocated stack) :
       Simulator(decoder, stream, std::move(stack)) {
    // Setup all C++ entrypoint functions to be intercepted.
    RegisterBranchInterception(artQuickResolutionTrampoline);
    RegisterBranchInterception(artQuickToInterpreterBridge);
    RegisterBranchInterception(artQuickGenericJniTrampoline);
    RegisterBranchInterception(artThrowDivZeroFromCode);
    RegisterBranchInterception(artDeliverPendingExceptionFromCode);
    RegisterBranchInterception(artContextCopyForLongJump);
    RegisterBranchInterception(artQuickProxyInvokeHandler);
    RegisterBranchInterception(artInvokeObsoleteMethod);
    RegisterBranchInterception(artMethodExitHook);
    RegisterBranchInterception(artAllocArrayFromCodeResolvedRosAlloc);
    RegisterBranchInterception(artTestSuspendFromCode);
    RegisterBranchInterception(artAllocObjectFromCodeInitializedRosAlloc);
    RegisterBranchInterception(artAllocObjectFromCodeResolvedRosAlloc);
    RegisterBranchInterception(artResolveTypeFromCode);
    RegisterBranchInterception(artThrowClassCastExceptionForObject);
    RegisterBranchInterception(artInstanceOfFromCode);
    RegisterBranchInterception(artThrowArrayBoundsFromCode);
    RegisterBranchInterception(artThrowNullPointerExceptionFromCode);
    RegisterBranchInterception(artThrowStringBoundsFromCode);
    RegisterBranchInterception(artDeoptimizeFromCompiledCode);
    RegisterBranchInterception(artResolveTypeAndVerifyAccessFromCode);
    RegisterBranchInterception(artIsAssignableFromCode);
    RegisterBranchInterception(artThrowArrayStoreException);
    RegisterBranchInterception(artInitializeStaticStorageFromCode);
    RegisterBranchInterception(artResolveStringFromCode);
    RegisterBranchInterception(artAllocObjectFromCodeWithChecksRosAlloc);
    RegisterBranchInterception(artInvokePolymorphic);
    RegisterBranchInterception(artLockObjectFromCode);
    RegisterBranchInterception(artUnlockObjectFromCode);
    RegisterBranchInterception(artDeliverExceptionFromCode);
    RegisterBranchInterception(artStringBuilderAppend);
    RegisterBranchInterception(fmodf);
    RegisterBranchInterception(fmod);
    RegisterBranchInterception(artAllocArrayFromCodeResolvedRosAllocInstrumented);
    RegisterBranchInterception(artAllocObjectFromCodeInitializedRosAllocInstrumented);
    RegisterBranchInterception(artAllocObjectFromCodeWithChecksRosAllocInstrumented);
    RegisterBranchInterception(artAllocObjectFromCodeResolvedRosAllocInstrumented);
    RegisterBranchInterception(artResolveTypeAndVerifyAccessFromCode);
    RegisterBranchInterception(artGetByteStaticFromCompiledCode);
    RegisterBranchInterception(artGetCharStaticFromCompiledCode);
    RegisterBranchInterception(artGet32StaticFromCompiledCode);
    RegisterBranchInterception(artGet64StaticFromCompiledCode);
    RegisterBranchInterception(artGetObjStaticFromCompiledCode);
    RegisterBranchInterception(artGetByteInstanceFromCompiledCode);
    RegisterBranchInterception(artGetCharInstanceFromCompiledCode);
    RegisterBranchInterception(artGet32InstanceFromCompiledCode);
    RegisterBranchInterception(artGet64InstanceFromCompiledCode);
    RegisterBranchInterception(artGetObjInstanceFromCompiledCode);
    RegisterBranchInterception(artSet8StaticFromCompiledCode);
    RegisterBranchInterception(artSet16StaticFromCompiledCode);
    RegisterBranchInterception(artSet32StaticFromCompiledCode);
    RegisterBranchInterception(artSet64StaticFromCompiledCode);
    RegisterBranchInterception(artSetObjStaticFromCompiledCode);
    RegisterBranchInterception(artSet8InstanceFromCompiledCode);
    RegisterBranchInterception(artSet16InstanceFromCompiledCode);
    RegisterBranchInterception(artSet32InstanceFromCompiledCode);
    RegisterBranchInterception(artSet64InstanceFromCompiledCode);
    RegisterBranchInterception(artSetObjInstanceFromCompiledCode);
    RegisterBranchInterception(artResolveMethodHandleFromCode);
    RegisterBranchInterception(artResolveMethodTypeFromCode);
    RegisterBranchInterception(artAllocStringObjectRosAlloc);
    RegisterBranchInterception(artDeoptimizeIfNeeded);
    RegisterBranchInterception(artInvokeCustom);

    RegisterTwoWordReturnInterception(artInvokeSuperTrampolineWithAccessCheck);
    RegisterTwoWordReturnInterception(artInvokeStaticTrampolineWithAccessCheck);
    RegisterTwoWordReturnInterception(artInvokeInterfaceTrampoline);
    RegisterTwoWordReturnInterception(artInvokeVirtualTrampolineWithAccessCheck);
    RegisterTwoWordReturnInterception(artInvokeDirectTrampolineWithAccessCheck);
    RegisterTwoWordReturnInterception(artInvokeInterfaceTrampolineWithAccessCheck);

    RegisterBranchInterception(artArm64SimulatorGenericJNIPlaceholder,
                               [this]([[maybe_unused]] uint64_t addr)
                               REQUIRES_SHARED(Locks::mutator_lock_) {
      uint64_t native_code_ptr = static_cast<uint64_t>(ReadXRegister(0));
      ArtMethod** simulated_reserved_area = reinterpret_cast<ArtMethod**>(ReadXRegister(1));
      Thread* self = reinterpret_cast<Thread*>(ReadXRegister(2));

      uint64_t fp_result = 0.0;
      int64_t gpr_result = artQuickGenericJniTrampolineSimulator(
          native_code_ptr,
          reinterpret_cast<void*>(simulated_reserved_area),
          reinterpret_cast<void*>(&fp_result));

      jvalue jval;
      jval.j = gpr_result;
      uint64_t result_end = artQuickGenericJniEndTrampoline(self, jval, fp_result);

      WriteXRegister(0, result_end);
      WriteDRegister(0, bit_cast<double>(result_end));
    });
  }

  virtual ~CustomSimulator() {}

  uint8_t* GetStackBase() {
    return reinterpret_cast<uint8_t*>(memory_.GetStack().GetBase());
  }


  // TODO(Simulator): Maybe integrate these into vixl?
  int64_t get_sp() const {
    return ReadRegister<int64_t>(kSp, Reg31IsStackPointer);
  }

  int64_t get_x(int32_t n) const {
    return ReadRegister<int64_t>(n, Reg31IsStackPointer);
  }

  int64_t get_lr() const {
    return ReadRegister<int64_t>(kLinkRegCode);
  }

  int64_t get_fp() const {
    return ReadXRegister(kFp);
  }

  // Register a branch interception to a function which returns TwoWordReturn. VIXL does not
  // currently support returning composite types from runtime calls so this is a specialised case.
  template <typename... P>
  void RegisterTwoWordReturnInterception(TwoWordReturn (*func)(P...)) {
    RegisterBranchInterception(reinterpret_cast<void (*)()>(func),
                               [this, func]([[maybe_unused]] uint64_t addr) {
      ABI abi;
      std::tuple<P...> arguments{
          ReadGenericOperand<P>(abi.GetNextParameterGenericOperand<P>())...};

      TwoWordReturn res = DoRuntimeCall(func, arguments, __local_index_sequence_for<P...>{});

      // Method pointer.
      WriteXRegister(0, res.lo);
      // Code pointer.
      WriteXRegister(1, res.hi);
    });
  }
};

CodeSimulatorArm64* CodeSimulatorArm64::CreateCodeSimulatorArm64(size_t stack_size) {
  if (kCanSimulate) {
    CodeSimulatorArm64* simulator = new CodeSimulatorArm64;
    simulator->InitInstructionSimulator(stack_size);

    return simulator;
  } else {
    return nullptr;
  }
}

CodeSimulatorArm64::CodeSimulatorArm64() : BasicCodeSimulatorArm64() {}

CustomSimulator* CodeSimulatorArm64::GetSimulator() {
  return reinterpret_cast<CustomSimulator*>(simulator_.get());
}

Simulator* CodeSimulatorArm64::CreateNewInstructionSimulator(SimStack::Allocated&& stack) {
  return new CustomSimulator(decoder_.get(), stdout, std::move(stack));
}

void CodeSimulatorArm64::Invoke(ArtMethod* method,
                                uint32_t* args,
                                uint32_t args_size_in_bytes,
                                Thread* self,
                                JValue* result,
                                const char* shorty,
                                bool isStatic)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  // ARM64 simulator only supports 64-bit host machines. Because:
  //   1) vixl simulator is not tested on 32-bit host machines.
  //   2) Data structures in ART have different representations for 32/64-bit machines.
  DCHECK(sizeof(args) == sizeof(int64_t));

  if (VLOG_IS_ON(simulator)) {
    VLOG(simulator) << "\nVIXL_SIMULATOR simulate: " << method->PrettyMethod();
  }

  /*  extern "C"
   *     void art_quick_invoke_static_stub(ArtMethod *method,   x0
   *                                       uint32_t  *args,     x1
   *                                       uint32_t argsize,    w2
   *                                       Thread *self,        x3
   *                                       JValue *result,      x4
   *                                       char   *shorty);     x5 */
  CustomSimulator* simulator = GetSimulator();
  size_t arg_no = 0;
  simulator->WriteXRegister(arg_no++, reinterpret_cast<uint64_t>(method));
  simulator->WriteXRegister(arg_no++, reinterpret_cast<uint64_t>(args));
  simulator->WriteWRegister(arg_no++, args_size_in_bytes);
  simulator->WriteXRegister(arg_no++, reinterpret_cast<uint64_t>(self));
  simulator->WriteXRegister(arg_no++, reinterpret_cast<uint64_t>(result));
  simulator->WriteXRegister(arg_no++, reinterpret_cast<uint64_t>(shorty));

  // The simulator will stop (and return from RunFrom) when it encounters pc == 0.
  simulator->WriteLr(0);

  int64_t quick_code;

  if (isStatic) {
    quick_code = reinterpret_cast<int64_t>(GetQuickInvokeStaticStub());
  } else {
    quick_code = reinterpret_cast<int64_t>(GetQuickInvokeStub());
  }

  DCHECK_NE(quick_code, 0);
  RunFrom(quick_code);
}

int64_t CodeSimulatorArm64::GetStackPointer() {
  return GetSimulator()->get_sp();
}

uint8_t* CodeSimulatorArm64::GetStackBaseInternal() {
  return GetSimulator()->GetStackBase();
}

#endif

}  // namespace arm64
}  // namespace art
