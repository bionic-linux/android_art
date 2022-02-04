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
#include "code_simulator_container.h"

using namespace vixl::aarch64;  // NOLINT(build/namespaces)

namespace art {

extern "C" void artDeliverPendingExceptionFromCode(Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" void artTestSuspendFromCode(Thread* self) REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" mirror::Array* artAllocArrayFromCodeResolvedRosAlloc(
    mirror::Class* klass, int32_t component_count, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" mirror ::Object* artAllocObjectFromCodeResolvedRosAlloc(
    mirror ::Class* klass, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" uint64_t artQuickToInterpreterBridge(
    ArtMethod* method, Thread* self, ArtMethod** sp)
        REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" const void* artQuickResolutionTrampoline(
    ArtMethod* called, mirror::Object* receiver, Thread* self, ArtMethod** sp)
    REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" uint64_t artQuickGenericJniTrampoline(Thread* self,
                                                 ArtMethod** managed_sp,
                                                 uintptr_t* reserved_area)
    REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" uint64_t artQuickGenericJniEndTrampoline(Thread* self,
                                                    jvalue result,
                                                    uint64_t result_fp)
    REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" uint64_t artQuickGenericJniTrampolineSimulator(uint64_t native_code_ptr,
                                                          void* simulated_reserved_area,
                                                          void* out_fp_result)
    REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" void artThrowDivZeroFromCode(Thread* self) REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" void artContextCopyForLongJump(Context* context, uintptr_t* gprs, uintptr_t* fprs);

extern "C" const void* artInstrumentationMethodEntryFromCode(ArtMethod* method,
                                                             mirror::Object* this_object,
                                                             Thread* self,
                                                             ArtMethod** sp)
    REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" TwoWordReturn artInstrumentationMethodExitFromCode(Thread* self,
                                                              ArtMethod** sp,
                                                              uint64_t* gpr_result,
                                                              uint64_t* fpr_result)
    REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" uint64_t artQuickProxyInvokeHandler(
    ArtMethod* proxy_method, mirror::Object* receiver, Thread* self, ArtMethod** sp)
    REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" uint64_t artInvokeObsoleteMethod(ArtMethod* method, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_);

extern "C" void artTestSuspendFromCode(Thread* self) REQUIRES_SHARED(Locks::mutator_lock_);

namespace arm64 {

BasicCodeSimulatorArm64* BasicCodeSimulatorArm64::CreateBasicCodeSimulatorArm64() {
  if (kCanSimulate) {
    BasicCodeSimulatorArm64* simulator = new BasicCodeSimulatorArm64();
    simulator->InitInstructionSimulator();
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


Simulator* BasicCodeSimulatorArm64::CreateNewInstructionSimualtor(SimStack::Allocated&& stack) {
  return new Simulator(decoder_.get(), stdout, std::move(stack));
}

void BasicCodeSimulatorArm64::InitInstructionSimulator() {
  SimStack stack_builder;
  stack_builder.SetLimitGuardSize(0x4000);
  stack_builder.SetUsableSize(0x400000);
  SimStack::Allocated stack = stack_builder.Allocate();

  simulator_.reset(CreateNewInstructionSimualtor(std::move(stack)));
  if (VLOG_IS_ON(simulator)) {
    simulator_->SetColouredTrace(true);
    simulator_->SetTraceParameters(LOG_DISASM | LOG_WRITE | LOG_REGS);
  }
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

// This is a placeholder function which is never executed; its address is used to intercept
// native call as part of genericJNI trampoline.
NO_RETURN static void artArm64SimulatorGenericJNIPlaceholder(
    uint64_t native_code_ptr ATTRIBUTE_UNUSED,
    ArtMethod** simulated_reserved_area ATTRIBUTE_UNUSED,
    Thread* self ATTRIBUTE_UNUSED) {
  UNREACHABLE();
}

// This class is used to generate arm64 assembly entrypoints during runtime - to be used
// for simulator.
//
// Note: this is likely going to be a temporary solution untill .o file linking is implemented.
class VIXLAsmQuickEntryPointBuilder {
 public:
  explicit VIXLAsmQuickEntryPointBuilder(vixl::aarch64::MacroAssembler* masm) : masm_(masm) {}

  using GeneratorFuncType = void (VIXLAsmQuickEntryPointBuilder::*)();

  // Initializes a typed entrypoint/stub (stub_pointer) with a code, generated by a specified
  // generator function (genetor_func) from this class.
  template <GeneratorFuncType generator_func, typename StubFuncType>
  static void GenerateStub(StubFuncType* stub_pointer) {
    *stub_pointer = reinterpret_cast<StubFuncType>(GenerateEntryPoint<generator_func>());
  }

  friend void SimulatorEntryPointsManagerArm64::InitCustomEntryPoints();

 private:
  const vixl::aarch64::Register xIP0 = x16;
  const vixl::aarch64::Register xIP1 = x17;
  const vixl::aarch64::Register xSELF = x19;
  const vixl::aarch64::Register xLR = lr;
  const vixl::aarch64::Register wMR = w20;
  const vixl::aarch64::Register xSUSPEND = x21;
  const vixl::aarch64::Register xFP = x29;

#define __ masm_->

  //
  // The helpers below mirror the assembly code in .S files for arm64 target and generate
  // almost* identical code (except for address of C++ symbols generation).
  //
  // They need to be updated if original .S files are updated.
  //

  //
  // Helpers from asm_support_arm64.S
  //

  void Emit_INCREASE_FRAME(size_t frame_adjustment) {
    __ sub(sp, sp, frame_adjustment);
    // .cfi_adjust_cfa_offset (\frame_adjustment)
  }

  void Emit_DECREASE_FRAME(size_t frame_adjustment) {
    __ add(sp, sp, frame_adjustment);
    // .cfi_adjust_cfa_offset -(\frame_adjustment)
  }

  void Emit_SAVE_REG(vixl::aarch64::Register reg, size_t offset) {
    __ str(reg, MemOperand(sp, offset));
    // .cfi_rel_offset \reg, (\offset)
  }

  void Emit_RESTORE_REG_BASE(vixl::aarch64::Register base,
                             vixl::aarch64::Register reg,
                             size_t offset) {
    __ ldr(reg, MemOperand(base, offset));
    // .cfi_restore \reg
  }

  void Emit_RESTORE_REG(vixl::aarch64::Register reg, size_t offset) {
    Emit_RESTORE_REG_BASE(sp, reg, offset);
  }

  void Emit_SAVE_TWO_REGS_BASE(vixl::aarch64::Register base,
                               vixl::aarch64::Register reg1,
                               vixl::aarch64::Register reg2,
                               size_t offset) {
    __ stp(reg1, reg2, MemOperand(base, offset));
    // .cfi_rel_offset \reg1, (\offset)
    // .cfi_rel_offset \reg2, (\offset) + 8
  }

  void Emit_SAVE_TWO_REGS(vixl::aarch64::Register reg1,
                          vixl::aarch64::Register reg2,
                          size_t offset) {
    Emit_SAVE_TWO_REGS_BASE(sp, reg1, reg2, offset);
  }

  void Emit_RESTORE_TWO_REGS_BASE(vixl::aarch64::Register base,
                                  vixl::aarch64::Register reg1,
                                  vixl::aarch64::Register reg2,
                                  size_t offset) {
    __ ldp(reg1, reg2, MemOperand(base, offset));
    // .cfi_restore \reg1
    // .cfi_restore \reg2
  }

  void Emit_RESTORE_TWO_REGS(vixl::aarch64::Register reg1,
                             vixl::aarch64::Register reg2,
                             size_t offset) {
    Emit_RESTORE_TWO_REGS_BASE(sp, reg1, reg2, offset);
  }

  void Emit_LOAD_RUNTIME_INSTANCE(vixl::aarch64::Register reg) {
    __ Mov(reg, AddressOf(Runtime::Current()));
    /// __ adrp(reg, _ZN3art7Runtime9instance_E);
    /// __ ldr(reg, MemOperand(reg, #:lo12:_ZN3art7Runtime9instance_E));
  }

  void Emit_REFRESH_MARKING_REGISTER() {
    // Emit nothing, read barriers are not supported.
  }

  void Emit_REFRESH_SUSPEND_CHECK_REGISTER() {
    __ ldr(xSUSPEND, MemOperand(xSELF, THREAD_SUSPEND_TRIGGER_OFFSET));
  }

  /*
   * Macro that sets up the callee save frame to conform with
   * Runtime::CreateCalleeSaveMethod(kSaveRefsOnly).
   */
  void Emit_SETUP_SAVE_REFS_ONLY_FRAME() {
    // art::Runtime* xIP0 = art::Runtime::instance_;
    // Our registers aren't intermixed - just spill in order.
    Emit_LOAD_RUNTIME_INSTANCE(xIP0);

    // ArtMethod* xIP0 = Runtime::instance_->callee_save_methods_[kSaveRefOnly];
    __ ldr(xIP0, MemOperand(xIP0, RUNTIME_SAVE_REFS_ONLY_METHOD_OFFSET));

    Emit_INCREASE_FRAME(96);

    // GP callee-saves.
    // x20 paired with ArtMethod* - see below.
    Emit_SAVE_TWO_REGS(x21, x22, 16);
    Emit_SAVE_TWO_REGS(x23, x24, 32);
    Emit_SAVE_TWO_REGS(x25, x26, 48);
    Emit_SAVE_TWO_REGS(x27, x28, 64);
    Emit_SAVE_TWO_REGS(x29, xLR, 80);

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveRefsOnly].
    // Note: We could avoid saving X20 in the case of Baker read
    // barriers, as it is overwritten by REFRESH_MARKING_REGISTER
    // later; but it's not worth handling this special case.
    __ stp(xIP0, x20, MemOperand(sp));

    // Place sp in Thread::Current()->top_quick_frame.
    __ mov(xIP0, sp);
    __ str(xIP0, MemOperand(xSELF, THREAD_TOP_QUICK_FRAME_OFFSET));
  }

  // TODO: Probably no need to restore registers preserved by aapcs64.
  void Emit_RESTORE_SAVE_REFS_ONLY_FRAME() {
    // Callee-saves.
    // Note: Likewise, we could avoid restoring X20 in the case of Baker
    // read barriers, as it is overwritten by REFRESH_MARKING_REGISTER
    // later; but it's not worth handling this special case.
    Emit_RESTORE_REG(x20, 8);
    Emit_RESTORE_TWO_REGS(x21, x22, 16);
    Emit_RESTORE_TWO_REGS(x23, x24, 32);
    Emit_RESTORE_TWO_REGS(x25, x26, 48);
    Emit_RESTORE_TWO_REGS(x27, x28, 64);
    Emit_RESTORE_TWO_REGS(x29, xLR, 80);

    Emit_DECREASE_FRAME(96);
  }

  void Emit_SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL(vixl::aarch64::Register base) {
    // Stack alignment filler [\base, #8].
    // FP args.
    __ stp(d0, d1, MemOperand(base, 16));
    __ stp(d2, d3, MemOperand(base, 32));
    __ stp(d4, d5, MemOperand(base, 48));
    __ stp(d6, d7, MemOperand(base, 64));

    // Core args.
    Emit_SAVE_TWO_REGS_BASE(base, x1, x2, 80);
    Emit_SAVE_TWO_REGS_BASE(base, x3, x4, 96);
    Emit_SAVE_TWO_REGS_BASE(base, x5, x6, 112);

    // x7, Callee-saves.
    // Note: We could avoid saving X20 in the case of Baker read
    // barriers, as it is overwritten by REFRESH_MARKING_REGISTER
    // later; but it's not worth handling this special case.
    Emit_SAVE_TWO_REGS_BASE(base, x7, x20, 128);
    Emit_SAVE_TWO_REGS_BASE(base, x21, x22, 144);
    Emit_SAVE_TWO_REGS_BASE(base, x23, x24, 160);
    Emit_SAVE_TWO_REGS_BASE(base, x25, x26, 176);
    Emit_SAVE_TWO_REGS_BASE(base, x27, x28, 192);

    // x29(callee-save) and LR.
    Emit_SAVE_TWO_REGS_BASE(base, x29, xLR, 208);
  }

  void Emit_RESTORE_SAVE_REFS_AND_ARGS_FRAME_INTERNAL(vixl::aarch64::Register base) {
    // FP args.
    __ ldp(d0, d1, MemOperand(base, 16));
    __ ldp(d2, d3, MemOperand(base, 32));
    __ ldp(d4, d5, MemOperand(base, 48));
    __ ldp(d6, d7, MemOperand(base, 64));

    // Core args.
    Emit_RESTORE_TWO_REGS_BASE(base, x1, x2, 80);
    Emit_RESTORE_TWO_REGS_BASE(base, x3, x4, 96);
    Emit_RESTORE_TWO_REGS_BASE(base, x5, x6, 112);

    // x7, Callee-saves.
    // Note: Likewise, we could avoid restoring X20 in the case of Baker
    // read barriers, as it is overwritten by REFRESH_MARKING_REGISTER
    // later; but it's not worth handling this special case.
    Emit_RESTORE_TWO_REGS_BASE(base, x7, x20, 128);
    Emit_RESTORE_TWO_REGS_BASE(base, x21, x22, 144);
    Emit_RESTORE_TWO_REGS_BASE(base, x23, x24, 160);
    Emit_RESTORE_TWO_REGS_BASE(base, x25, x26, 176);
    Emit_RESTORE_TWO_REGS_BASE(base, x27, x28, 192);

    // x29(callee-save) and LR.
    Emit_RESTORE_TWO_REGS_BASE(base, x29, xLR, 208);
  }

  void Emit_RESTORE_SAVE_REFS_AND_ARGS_FRAME() {
    Emit_RESTORE_SAVE_REFS_AND_ARGS_FRAME_INTERNAL(sp);
    Emit_DECREASE_FRAME(FRAME_SIZE_SAVE_REFS_AND_ARGS);
  }

  void Emit_SAVE_ALL_CALLEE_SAVES(size_t offset) {
    // FP callee-saves.
    __ stp(d8, d9,   MemOperand(sp, (0 + offset)));
    __ stp(d10, d11, MemOperand(sp, (16 + offset)));
    __ stp(d12, d13, MemOperand(sp, (32 + offset)));
    __ stp(d14, d15, MemOperand(sp, (48 + offset)));

    // GP callee-saves
    Emit_SAVE_TWO_REGS(x19, x20, (64 + offset));
    Emit_SAVE_TWO_REGS(x21, x22, (80 + offset));
    Emit_SAVE_TWO_REGS(x23, x24, (96 + offset));
    Emit_SAVE_TWO_REGS(x25, x26, (112 + offset));
    Emit_SAVE_TWO_REGS(x27, x28, (128 + offset));
    Emit_SAVE_TWO_REGS(x29, xLR, (144 + offset));
  }

  /*
   * Macro that sets up the callee save frame to conform with
   * Runtime::CreateCalleeSaveMethod(kSaveAllCalleeSaves)
   */
  void Emit_SETUP_SAVE_ALL_CALLEE_SAVES_FRAME() {
    // art::Runtime* xIP0 = art::Runtime::instance_;
    // Our registers aren't intermixed - just spill in order.
    Emit_LOAD_RUNTIME_INSTANCE(xIP0);

    // ArtMethod* xIP0 = Runtime::instance_->callee_save_methods_[kSaveAllCalleeSaves];
    __ ldr(xIP0, MemOperand(xIP0, RUNTIME_SAVE_ALL_CALLEE_SAVES_METHOD_OFFSET));

    Emit_INCREASE_FRAME(176);

    // Stack alignment filler [sp, #8].
    Emit_SAVE_ALL_CALLEE_SAVES(16);

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveAllCalleeSaves].
    __ str(xIP0, MemOperand(sp));
    // Place sp in Thread::Current()->top_quick_frame.
    __ mov(xIP0, sp);
    __ str(xIP0, MemOperand(xSELF, THREAD_TOP_QUICK_FRAME_OFFSET));
  }

  void Emit_PREPARE_AND_DO_LONG_JUMP() {
    // Reserve space for the gprs + fprs;
    Emit_INCREASE_FRAME(ARM64_LONG_JUMP_CONTEXT_SIZE);

    __ ldr(x0, MemOperand(xSELF, THREAD_LONG_JUMP_CONTEXT_OFFSET));
    __ mov(x1, sp);
    __ add(x2, sp, ARM64_LONG_JUMP_GPRS_SIZE);

    Emit_Call(artContextCopyForLongJump);  // Context* context, uintptr_t* gprs, uintptr_t* fprs

    __ mov(x0, sp);
    __ add(x1, sp, ARM64_LONG_JUMP_GPRS_SIZE);

    auto ep_manager = Runtime::Current()->GetCodeSimulatorContainer()->GetEntryPointsManager();
    Emit_Call(ep_manager->GetLongJumpStub());

    __ brk(0);  // Unreached
  }

  /*
   * Macro that calls through to artDeliverPendingExceptionFromCode, where the pending
   * exception is Thread::Current()->exception_ when the runtime method frame is ready.
   */
  void Emit_DELIVER_PENDING_EXCEPTION_FRAME_READY() {
    __ mov(x0, xSELF);

    // Point of no return.
    Emit_Call(artDeliverPendingExceptionFromCode);  // artDeliverPendingExceptionFromCode(Thread*)
    Emit_PREPARE_AND_DO_LONG_JUMP();
  }
  /*
   * Macro that calls through to artDeliverPendingExceptionFromCode, where the pending
   * exception is Thread::Current()->exception_.
   */
  void Emit_DELIVER_PENDING_EXCEPTION() {
    Emit_SETUP_SAVE_ALL_CALLEE_SAVES_FRAME();
    Emit_DELIVER_PENDING_EXCEPTION_FRAME_READY();
  }

  void Emit_RETURN_OR_DELIVER_PENDING_EXCEPTION_REG(vixl::aarch64::Register reg) {
    vixl::aarch64::Label l1;
    __ ldr(reg, MemOperand(xSELF, THREAD_EXCEPTION_OFFSET));   // Get exception field.
    __ cbnz(reg, &l1);
    __ ret();

    __ Bind(&l1);
    Emit_DELIVER_PENDING_EXCEPTION();
  }

  void Emit_RETURN_OR_DELIVER_PENDING_EXCEPTION() {
    Emit_RETURN_OR_DELIVER_PENDING_EXCEPTION_REG(xIP0);
  }

  //
  // From quick_entrypoints_arm64.S
  //

  void Emit_SAVE_TWO_REGS_INCREASE_FRAME(vixl::aarch64::Register reg1,
                                         vixl::aarch64::Register reg2,
                                         size_t frame_adjustment) {
    __ stp(reg1, reg2, MemOperand(sp, -(frame_adjustment), PreIndex));
    // .cfi_adjust_cfa_offset (\frame_adjustment)
    // .cfi_rel_offset \reg1, 0
    // .cfi_rel_offset \reg2, 8
  }

  void Emit_RESTORE_TWO_REGS_DECREASE_FRAME(vixl::aarch64::Register reg1,
                                            vixl::aarch64::Register reg2,
                                            size_t frame_adjustment) {
    __ ldp(reg1, reg2, MemOperand(sp, frame_adjustment, PostIndex));
    // .cfi_restore \reg1
    // .cfi_restore \reg2
    // .cfi_adjust_cfa_offset -(\frame_adjustment)
  }

  // x4, x5, <padding>, x19, x20, x21, FP, LR saved.
  static constexpr size_t INVOKE_SAVE_SIZE = 8 * 8;

  /*
   * Macro that sets up the callee save frame to conform with
   * Runtime::CreateCalleeSaveMethod(kSaveRefsAndArgs).
   *
   * TODO This is probably too conservative - saving FP & LR.
   */
  void Emit_SETUP_SAVE_REFS_AND_ARGS_FRAME() {
    // art::Runtime* xIP0 = art::Runtime::instance_;
    // Our registers aren't intermixed - just spill in order.
    Emit_LOAD_RUNTIME_INSTANCE(xIP0);

    // ArtMethod* xIP0 = Runtime::instance_->callee_save_methods_[kSaveRefAndArgs];
    __ ldr(xIP0, MemOperand(xIP0, RUNTIME_SAVE_REFS_AND_ARGS_METHOD_OFFSET));

    Emit_INCREASE_FRAME(FRAME_SIZE_SAVE_REFS_AND_ARGS);
    Emit_SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL(sp);

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveRefsAndArgs].
    __ str(xIP0, MemOperand(sp));
    // Place sp in Thread::Current()->top_quick_frame.
    __ mov(xIP0, sp);
    __ str(xIP0, MemOperand(xSELF, THREAD_TOP_QUICK_FRAME_OFFSET));
  }

  void Emit_SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_X0() {
    Emit_INCREASE_FRAME(FRAME_SIZE_SAVE_REFS_AND_ARGS);
    Emit_SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL(sp);
    __ str(x0, MemOperand(sp));  // Store ArtMethod* to bottom of stack.
  }

  /*
   * Macro that sets up the callee save frame to conform with
   * Runtime::CreateCalleeSaveMethod(kSaveEverything)
   * when the SP has already been decremented by FRAME_SIZE_SAVE_EVERYTHING
   * and saving registers x29 and LR is handled elsewhere.
   */
  void Emit_SETUP_SAVE_EVERYTHING_FRAME_DECREMENTED_SP_SKIP_X29_LR(
      size_t runtime_method_offset = RUNTIME_SAVE_EVERYTHING_METHOD_OFFSET) {
    // Save FP registers.
    __ stp(d0, d1, MemOperand(sp, 16));
    __ stp(d2, d3, MemOperand(sp, 32));
    __ stp(d4, d5, MemOperand(sp, 48));
    __ stp(d6, d7, MemOperand(sp, 64));
    __ stp(d8, d9, MemOperand(sp, 80));
    __ stp(d10, d11, MemOperand(sp, 96));
    __ stp(d12, d13, MemOperand(sp, 112));
    __ stp(d14, d15, MemOperand(sp, 128));
    __ stp(d16, d17, MemOperand(sp, 144));
    __ stp(d18, d19, MemOperand(sp, 160));
    __ stp(d20, d21, MemOperand(sp, 176));
    __ stp(d22, d23, MemOperand(sp, 192));
    __ stp(d24, d25, MemOperand(sp, 208));
    __ stp(d26, d27, MemOperand(sp, 224));
    __ stp(d28, d29, MemOperand(sp, 240));
    __ stp(d30, d31, MemOperand(sp, 256));

    // Save core registers.
    Emit_SAVE_TWO_REGS(x0, x1, 272);
    Emit_SAVE_TWO_REGS(x2, x3, 288);
    Emit_SAVE_TWO_REGS(x4, x5, 304);
    Emit_SAVE_TWO_REGS(x6, x7, 320);
    Emit_SAVE_TWO_REGS(x8, x9, 336);
    Emit_SAVE_TWO_REGS(x10, x11, 352);
    Emit_SAVE_TWO_REGS(x12, x13, 368);
    Emit_SAVE_TWO_REGS(x14, x15, 384);
    Emit_SAVE_TWO_REGS(x16, x17, 400);  // Do not save the platform register.
    Emit_SAVE_TWO_REGS(x19, x20, 416);
    Emit_SAVE_TWO_REGS(x21, x22, 432);
    Emit_SAVE_TWO_REGS(x23, x24, 448);
    Emit_SAVE_TWO_REGS(x25, x26, 464);
    Emit_SAVE_TWO_REGS(x27, x28, 480);

    // art::Runtime* xIP0 = art::Runtime::instance_;
    Emit_LOAD_RUNTIME_INSTANCE(xIP0);

    // ArtMethod* xIP0 = Runtime::instance_->callee_save_methods_[kSaveEverything];
    __ ldr(xIP0, MemOperand(xIP0, runtime_method_offset));

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveEverything].
    __ str(xIP0, MemOperand(sp));
    // Place sp in Thread::Current()->top_quick_frame.
    __ mov(xIP0, sp);
    __ str(xIP0, MemOperand(xSELF, THREAD_TOP_QUICK_FRAME_OFFSET));
  }

  void Emit_SETUP_SAVE_EVERYTHING_FRAME(
      size_t runtime_method_offset = RUNTIME_SAVE_EVERYTHING_METHOD_OFFSET) {
    Emit_INCREASE_FRAME(512);
    Emit_SAVE_TWO_REGS(x29, xLR, 496);
    Emit_SETUP_SAVE_EVERYTHING_FRAME_DECREMENTED_SP_SKIP_X29_LR(runtime_method_offset);
  }

  void Emit_RESTORE_SAVE_EVERYTHING_FRAME_KEEP_X0() {
    // Restore FP registers.
    __ ldp(d0, d1,   MemOperand(sp, 16));
    __ ldp(d2, d3,   MemOperand(sp, 32));
    __ ldp(d4, d5,   MemOperand(sp, 48));
    __ ldp(d6, d7,   MemOperand(sp, 64));
    __ ldp(d8, d9,   MemOperand(sp, 80));
    __ ldp(d10, d11, MemOperand(sp, 96));
    __ ldp(d12, d13, MemOperand(sp, 112));
    __ ldp(d14, d15, MemOperand(sp, 128));
    __ ldp(d16, d17, MemOperand(sp, 144));
    __ ldp(d18, d19, MemOperand(sp, 160));
    __ ldp(d20, d21, MemOperand(sp, 176));
    __ ldp(d22, d23, MemOperand(sp, 192));
    __ ldp(d24, d25, MemOperand(sp, 208));
    __ ldp(d26, d27, MemOperand(sp, 224));
    __ ldp(d28, d29, MemOperand(sp, 240));
    __ ldp(d30, d31, MemOperand(sp, 256));

    // Restore core registers, except x0.
    Emit_RESTORE_REG(x1, 280);
    Emit_RESTORE_TWO_REGS(x2,  x3, 288);
    Emit_RESTORE_TWO_REGS(x4,  x5, 304);
    Emit_RESTORE_TWO_REGS(x6,  x7, 320);
    Emit_RESTORE_TWO_REGS(x8,  x9, 336);
    Emit_RESTORE_TWO_REGS(x10, x11, 352);
    Emit_RESTORE_TWO_REGS(x12, x13, 368);
    Emit_RESTORE_TWO_REGS(x14, x15, 384);
    Emit_RESTORE_TWO_REGS(x16, x17, 400);  // Do not restore the platform register.
    Emit_RESTORE_TWO_REGS(x19, x20, 416);
    Emit_RESTORE_TWO_REGS(x21, x22, 432);
    Emit_RESTORE_TWO_REGS(x23, x24, 448);
    Emit_RESTORE_TWO_REGS(x25, x26, 464);
    Emit_RESTORE_TWO_REGS(x27, x28, 480);
    Emit_RESTORE_TWO_REGS(x29, xLR, 496);

    Emit_DECREASE_FRAME(512);
  }

  void Emit_RESTORE_SAVE_EVERYTHING_FRAME() {
    Emit_RESTORE_REG(x0, 272);
    Emit_RESTORE_SAVE_EVERYTHING_FRAME_KEEP_X0();
  }

  template <typename T>
  void Emit_NO_ARG_RUNTIME_EXCEPTION_SAVE_EVERYTHING(T* fn) {
    Emit_SETUP_SAVE_EVERYTHING_FRAME();       // save all registers as basis for long jump context
    __ mov(x0, xSELF);                        // pass Thread::Current
                                              // \cxx_name(Thread*)
    Emit_Call(fn);                            // (uint32_t type_idx, Method* method, Thread*)

    Emit_PREPARE_AND_DO_LONG_JUMP();
  }

  template <typename T>
  void Emit_ONE_ARG_RUNTIME_EXCEPTION(T* fn) {
    Emit_SETUP_SAVE_ALL_CALLEE_SAVES_FRAME();  // save all registers as basis for long jump context.
    __ mov(x1, xSELF);                     // pass Thread::Current.
    Emit_Call(fn);                     // \cxx_name(arg, Thread*).
    Emit_PREPARE_AND_DO_LONG_JUMP();
  }

  void Emit_art_quick_throw_div_zero() {
    Emit_NO_ARG_RUNTIME_EXCEPTION_SAVE_EVERYTHING(artThrowDivZeroFromCode);
  }

  void Emit_INVOKE_STUB_CREATE_FRAME() {
    Emit_SAVE_TWO_REGS_INCREASE_FRAME(x4, x5, INVOKE_SAVE_SIZE);
    Emit_SAVE_REG(x19, 24);
    Emit_SAVE_TWO_REGS(x20, x21, 32);
    Emit_SAVE_TWO_REGS(xFP, xLR, 48);

    __ mov(xFP, sp);                              // Use xFP for frame p, as it's callee-saved.
    // .cfi_def_cfa_register xFP

    __ add(x10, x2, (__SIZEOF_POINTER__ + 0xf));  // Reserve space for ArtMethod*, arguments
    __ and_(x10, x10, ~0xf);                      // andround up for 16-byte stack alignment.
    __ sub(sp, sp, x10);                          // Adjust SP for ArtMethod*, args and alignment
                                                  // padding.

    __ mov(xSELF, x3);                            // Move thread pointer into SELF register.

    // Copy arguments into stack frame.
    // Use simple copy routine for now.
    // 4 bytes per slot.
    // X1 - source address
    // W2 - args length
    // X9 - destination address.
    // W10 - temporary
    __ add(x9, sp, 8);                            // Destination address is bottom of stack + null.

    vixl::aarch64::Label l1, l2;

    // Copy parameters into the stack. Use numeric label as this is a macro and Clang's assembler
    // does not have unique-id variables.
    __ Bind(&l1);
    __ cbz(w2, &l2);
    __ sub(w2, w2, 4);                            // Need 65536 bytes of range.
    __ ldr(w10, MemOperand(x1, x2));
    __ str(w10, MemOperand(x9, x2));
    __ b(&l1);

    __ Bind(&l2);
    // Store null into ArtMethod* at bottom of frame.
    __ str(xzr, MemOperand(sp));
  }

  void Emit_INVOKE_STUB_CALL_AND_RETURN() {
    Emit_REFRESH_MARKING_REGISTER();
    Emit_REFRESH_SUSPEND_CHECK_REGISTER();

    // load method-> METHOD_QUICK_CODE_OFFSET
    __ ldr(x9, MemOperand(x0, ART_METHOD_QUICK_CODE_OFFSET_64));
    // Branch to method.
    __ blr(x9);

    // Pop the ArtMethod* (null), arguments and alignment padding from the stack.
    __ mov(sp, xFP);
    // .cfi_def_cfa_register sp

    // Restore saved registers including value address and shorty address.
    Emit_RESTORE_REG(x19, 24);
    Emit_RESTORE_TWO_REGS(x20, x21, 32);
    Emit_RESTORE_TWO_REGS(xFP, xLR, 48);
    Emit_RESTORE_TWO_REGS_DECREASE_FRAME(x4, x5, INVOKE_SAVE_SIZE);

    // Store result (w0/x0/s0/d0) appropriately, depending on resultType.
    __ ldrb(w10, MemOperand(x5));

    // Check the return type and store the correct register into the jvalue in memory.
    // Use numeric label as this is a macro and Clang's assembler does not have unique-id variables.

    vixl::aarch64::Label l1, l2, l3;

    // Don't set anything for a void type.
    __ cmp(w10, 'V');
    __ b(&l1, eq);

    // Is it a double?
    __ cmp(w10, 'D');
    __ b(&l2, eq);

    // Is it a float?
    __ cmp(w10, 'F');
    __ b(&l3, eq);

    // Just store x0. Doesn't matter if it is 64 or 32 bits.
    __ str(x0, MemOperand(x4));


    __ Bind(&l1);  // Finish up.
    __ ret();


    __ Bind(&l2);  // Store double.
    __ str(d0, MemOperand(x4));
    __ ret();


    __ Bind(&l3);  // Store float.
    __ str(s0, MemOperand(x4));
    __ ret();
  }

  // Macro for loading a parameter into a register.
  //  counter - the register with offset into these tables
  //  size - the size of the register - 4 or 8 bytes.
  //  register - the name of the register to be loaded.
  template <typename T>
  void Emit_LOADREG(vixl::aarch64::Register counter,
                    size_t size,
                    T& reg,
                    vixl::aarch64::Label* return_label) {
    __ ldr(reg, MemOperand(x9, size, vixl::aarch64::PostIndex));
    __ add(counter, counter, 12);
    __ b(return_label);
  }

 public:
  /*
   *  extern"C" void art_quick_invoke_stub(ArtMethod *method,   x0
   *                                       uint32_t  *args,     x1
   *                                       uint32_t argsize,    w2
   *                                       Thread *self,        x3
   *                                       JValue *result,      x4
   *                                       char   *shorty);     x5
   *  +----------------------+
   *  |                      |
   *  |  C/C++ frame         |
   *  |       LR''           |
   *  |       FP''           | <- SP'
   *  +----------------------+
   *  +----------------------+
   *  |        x28           | <- TODO: Remove callee-saves.
   *  |         :            |
   *  |        x19           |
   *  |        SP'           |
   *  |        X5            |
   *  |        X4            |        Saved registers
   *  |        LR'           |
   *  |        FP'           | <- FP
   *  +----------------------+
   *  | uint32_t out[n-1]    |
   *  |    :      :          |        Outs
   *  | uint32_t out[0]      |
   *  | ArtMethod*           | <- SP  value=null
   *  +----------------------+
   *
   * Outgoing registers:
   *  x0    - Method*
   *  x1-x7 - integer parameters.
   *  d0-d7 - Floating point parameters.
   *  xSELF = self
   *  SP = & of ArtMethod*
   *  x1 = "this" pointer.
   *
   */
  void Emit_art_quick_invoke_stub() {
    // Spill registers as per AACPS64 calling convention.
    Emit_INVOKE_STUB_CREATE_FRAME();

    vixl::aarch64::Label LfillRegisters, LstoreW2, LstoreX2, LstoreS0, LstoreD0;
    vixl::aarch64::Label LcallFunction, LisDouble, Ladvance4, LisLong, Ladvance8, LisOther;
    // vixl::aarch64::Label LstoreW1_2, LstoreX1_2, LstoreS0_2, LstoreD0_2, LcallFunction2;

    // Fill registers x/w1 to x/w7 and s/d0 to s/d7 with parameters.
    // Parse the passed shorty to determine which register to load.
    // Load addresses for routines that load WXSD registers.
    __ adr(x11, &LstoreW2);
    __ adr(x12, &LstoreX2);
    __ adr(x13, &LstoreS0);
    __ adr(x14, &LstoreD0);

    // Initialize routine offsets to 0 for integers and floats.
    // x8 for integers, x15 for floating point.
    __ Mov(x8, 0);
    __ Mov(x15, 0);

    __ add(x10, x5, 1);                           // Load shorty addr, plus one to skip return val.
    // Load "this" parameter, and increment arg pointer.
    __ ldr(w1, MemOperand(x9, 4, vixl::aarch64::PostIndex));


    // Loop to fill registers.
    __ Bind(&LfillRegisters);
    __ ldrb(w17, MemOperand(x10, 1, PostIndex));  // Load next char in signature, and increment.
    __ cbz(w17, &LcallFunction);                  // Exit at end of signature. Shorty 0 terminated.

    __ cmp(w17, 'F');                             // is this a float?
    __ b(&LisDouble, ne);

    __ cmp(x15, 8 * 12);                          // Skip this load if all registers full.
    __ b(&Ladvance4, eq);

    __ add(x17, x13, x15);                        // Calculate subroutine to jump to.
    __ br(x17);


    __ Bind(&LisDouble);
    __ cmp(w17, 'D');                             // is this a double?
    __ b(&LisLong, ne);

    __ cmp(x15, 8 * 12);                          // Skip this load if all registers full.
    __ b(&Ladvance8, eq);

    __ add(x17, x14, x15);                        // Calculate subroutine to jump to.
    __ br(x17);


    __ Bind(&LisLong);
    __ cmp(w17, 'J');                             // is this a long?
    __ b(&LisOther, ne);

    __ cmp(x8, 6 * 12);                           // Skip this load if all registers full.
    __ b(&Ladvance8, eq);

    __ add(x17, x12, x8);                         // Calculate subroutine to jump to.
    __ br(x17);


    __ Bind(&LisOther);                           // Everything else takes one vReg.
    __ cmp(x8, 6 * 12);                           // Skip this load if all registers full.
    __ b(&Ladvance4, eq);

    __ add(x17, x11, x8);                         // Calculate subroutine to jump to.
    __ br(x17);


    __ Bind(&Ladvance4);
    __ add(x9, x9, 4);
    __ b(&LfillRegisters);


    __ Bind(&Ladvance8);
    __ add(x9, x9, 8);
    __ b(&LfillRegisters);


    // Store ints.
    __ Bind(&LstoreW2);
    Emit_LOADREG(x8, 4, w2, &LfillRegisters);
    Emit_LOADREG(x8, 4, w3, &LfillRegisters);
    Emit_LOADREG(x8, 4, w4, &LfillRegisters);
    Emit_LOADREG(x8, 4, w5, &LfillRegisters);
    Emit_LOADREG(x8, 4, w6, &LfillRegisters);
    Emit_LOADREG(x8, 4, w7, &LfillRegisters);

    // Store longs.
    __ Bind(&LstoreX2);
    Emit_LOADREG(x8, 8, x2, &LfillRegisters);
    Emit_LOADREG(x8, 8, x3, &LfillRegisters);
    Emit_LOADREG(x8, 8, x4, &LfillRegisters);
    Emit_LOADREG(x8, 8, x5, &LfillRegisters);
    Emit_LOADREG(x8, 8, x6, &LfillRegisters);
    Emit_LOADREG(x8, 8, x7, &LfillRegisters);

    // Store singles.
    __ Bind(&LstoreS0);
    Emit_LOADREG(x15, 8, s0, &LfillRegisters);
    Emit_LOADREG(x15, 8, s1, &LfillRegisters);
    Emit_LOADREG(x15, 8, s2, &LfillRegisters);
    Emit_LOADREG(x15, 8, s3, &LfillRegisters);
    Emit_LOADREG(x15, 8, s4, &LfillRegisters);
    Emit_LOADREG(x15, 8, s5, &LfillRegisters);
    Emit_LOADREG(x15, 8, s6, &LfillRegisters);
    Emit_LOADREG(x15, 8, s7, &LfillRegisters);

    // Store doubles.
    __ Bind(&LstoreD0);
    Emit_LOADREG(x15, 8, d0, &LfillRegisters);
    Emit_LOADREG(x15, 8, d1, &LfillRegisters);
    Emit_LOADREG(x15, 8, d2, &LfillRegisters);
    Emit_LOADREG(x15, 8, d3, &LfillRegisters);
    Emit_LOADREG(x15, 8, d4, &LfillRegisters);
    Emit_LOADREG(x15, 8, d5, &LfillRegisters);
    Emit_LOADREG(x15, 8, d6, &LfillRegisters);
    Emit_LOADREG(x15, 8, d7, &LfillRegisters);


    __ Bind(&LcallFunction);

    Emit_INVOKE_STUB_CALL_AND_RETURN();
  }

  /*  extern"C"
   *     void art_quick_invoke_static_stub(ArtMethod *method,   x0
   *                                       uint32_t  *args,     x1
   *                                       uint32_t argsize,    w2
   *                                       Thread *self,        x3
   *                                       JValue *result,      x4
   *                                       char   *shorty);     x5
   */
  void Emit_art_quick_invoke_static_stub() {
    // Spill registers as per AACPS64 calling convention.
    Emit_INVOKE_STUB_CREATE_FRAME();

    vixl::aarch64::Label LfillRegisters2, LisDouble2, LisLong2, LisOther2, Ladvance4_2, Ladvance8_2;
    vixl::aarch64::Label LstoreW1_2, LstoreX1_2, LstoreS0_2, LstoreD0_2, LcallFunction2;

    // Fill registers x/w1 to x/w7 and s/d0 to s/d7 with parameters.
    // Parse the passed shorty to determine which register to load.
    // Load addresses for routines that load WXSD registers.
    __ adr(x11, &LstoreW1_2);
    __ adr(x12, &LstoreX1_2);
    __ adr(x13, &LstoreS0_2);
    __ adr(x14, &LstoreD0_2);

    // Initialize routine offsets to 0 for integers and floats.
    // x8 for integers, x15 for floating point.
    __ Mov(x8, 0);
    __ Mov(x15, 0);

    __ add(x10, x5, 1);            // Load shorty address, plus one to skip return value.


    // Loop to fill registers.
    __ Bind(&LfillRegisters2);

    // Load next character in signature, and increment.
    __ ldrb(w17, MemOperand(x10, 1, vixl::aarch64::PostIndex));
    __ cbz(w17, &LcallFunction2);  // Exit at end of signature. Shorty 0 terminated.

    __ cmp(w17, 'F');              // is this a float?
    __ b(&LisDouble2, ne);

    __ cmp(x15, 8 * 12);           // Skip this load if all registers full.
    __ b(&Ladvance4_2, eq);

    __ add(x17, x13, x15);         // Calculate subroutine to jump to.
    __ br(x17);


    __ Bind(&LisDouble2);
    __ cmp(w17, 'D');              // is this a double?
    __ b(&LisLong2, ne);

    __ cmp(x15, 8 * 12);           // Skip this load if all registers full.
    __ b(&Ladvance8_2, eq);

    __ add(x17, x14, x15);         // Calculate subroutine to jump to.
    __ br(x17);


    __ Bind(&LisLong2);
    __ cmp(w17, 'J');               // is this a long?
    __ b(&LisOther2, ne);

    __ cmp(x8, 7 * 12);            // Skip this load if all registers full.
    __ b(&Ladvance8_2, eq);

    __ add(x17, x12, x8);          // Calculate subroutine to jump to.
    __ br(x17);


    __ Bind(&LisOther2);          // Everything else takes one vReg.
    __ cmp(x8, 7 * 12);           // Skip this load if all registers full.
    __ b(&Ladvance4_2, eq);

    __ add(x17, x11, x8);         // Calculate subroutine to jump to.
    __ br(x17);


    __ Bind(&Ladvance4_2);
    __ add(x9, x9, 4);
    __ b(&LfillRegisters2);


    __ Bind(&Ladvance8_2);
    __ add(x9, x9, 8);
    __ b(&LfillRegisters2);


    // Store ints.
    __ Bind(&LstoreW1_2);
    Emit_LOADREG(x8, 4, w1, &LfillRegisters2);
    Emit_LOADREG(x8, 4, w2, &LfillRegisters2);
    Emit_LOADREG(x8, 4, w3, &LfillRegisters2);
    Emit_LOADREG(x8, 4, w4, &LfillRegisters2);
    Emit_LOADREG(x8, 4, w5, &LfillRegisters2);
    Emit_LOADREG(x8, 4, w6, &LfillRegisters2);
    Emit_LOADREG(x8, 4, w7, &LfillRegisters2);

    // Store longs.
    __ Bind(&LstoreX1_2);
    Emit_LOADREG(x8, 8, x1, &LfillRegisters2);
    Emit_LOADREG(x8, 8, x2, &LfillRegisters2);
    Emit_LOADREG(x8, 8, x3, &LfillRegisters2);
    Emit_LOADREG(x8, 8, x4, &LfillRegisters2);
    Emit_LOADREG(x8, 8, x5, &LfillRegisters2);
    Emit_LOADREG(x8, 8, x6, &LfillRegisters2);
    Emit_LOADREG(x8, 8, x7, &LfillRegisters2);

    // Store singles.
    __ Bind(&LstoreS0_2);
    Emit_LOADREG(x15, 4, s0, &LfillRegisters2);
    Emit_LOADREG(x15, 4, s1, &LfillRegisters2);
    Emit_LOADREG(x15, 4, s2, &LfillRegisters2);
    Emit_LOADREG(x15, 4, s3, &LfillRegisters2);
    Emit_LOADREG(x15, 4, s4, &LfillRegisters2);
    Emit_LOADREG(x15, 4, s5, &LfillRegisters2);
    Emit_LOADREG(x15, 4, s6, &LfillRegisters2);
    Emit_LOADREG(x15, 4, s7, &LfillRegisters2);

    // Store doubles.
    __ Bind(&LstoreD0_2);
    Emit_LOADREG(x15, 8, d0, &LfillRegisters2);
    Emit_LOADREG(x15, 8, d1, &LfillRegisters2);
    Emit_LOADREG(x15, 8, d2, &LfillRegisters2);
    Emit_LOADREG(x15, 8, d3, &LfillRegisters2);
    Emit_LOADREG(x15, 8, d4, &LfillRegisters2);
    Emit_LOADREG(x15, 8, d5, &LfillRegisters2);
    Emit_LOADREG(x15, 8, d6, &LfillRegisters2);
    Emit_LOADREG(x15, 8, d7, &LfillRegisters2);

    __ Bind(&LcallFunction2);

    Emit_INVOKE_STUB_CALL_AND_RETURN();
  }

  /*
   * Called by managed code that is attempting to call a method on a proxy class. On entry
   * x0 holds the proxy method and x1 holds the receiver; The frame size of the invoked proxy
   * method agrees with a ref and args callee save frame.
   */
  // .extern artQuickProxyInvokeHandler
  void Emit_art_quick_proxy_invoke_handler() {
    Emit_SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_X0();

    vixl::aarch64::Label Lexception_in_proxy;
    __ mov(x2, xSELF);                   // pass Thread::Current
    __ mov(x3, sp);                      // pass SP
    Emit_Call(artQuickProxyInvokeHandler);  // (Method* proxy method, receiver, Thread*, SP)
    __ ldr(x2, MemOperand(xSELF, THREAD_EXCEPTION_OFFSET));
    __ cbnz(x2, &Lexception_in_proxy);    // success if no exception is pending
    Emit_RESTORE_SAVE_REFS_AND_ARGS_FRAME();    // Restore frame
    Emit_REFRESH_MARKING_REGISTER();
    __ fmov(d0, x0);                      // Store result in d0 in case it was float or double
    __ ret();                                 // return on success

    __ Bind(&Lexception_in_proxy);
    Emit_RESTORE_SAVE_REFS_AND_ARGS_FRAME();
    Emit_DELIVER_PENDING_EXCEPTION();
  }

  void Emit_art_quick_resolution_trampoline() {
    Emit_SETUP_SAVE_REFS_AND_ARGS_FRAME();
    vixl::aarch64::Label l1;
    __ mov(x2, xSELF);
    __ mov(x3, sp);
    Emit_Call(artQuickResolutionTrampoline);  // (called, receiver, Thread*, SP)
    __ cbz(x0, &l1);
    __ mov(xIP0, x0);                         // Remember returned code pointer in xIP0.
    __ ldr(x0, MemOperand(sp, 0));            // artQuickResolutionTrampoline puts
                                              // called method in *SP.
    Emit_RESTORE_SAVE_REFS_AND_ARGS_FRAME();
    Emit_REFRESH_MARKING_REGISTER();
    __ br(xIP0);

    __ Bind(&l1);
    Emit_RESTORE_SAVE_REFS_AND_ARGS_FRAME();
    Emit_DELIVER_PENDING_EXCEPTION();
  }

  void Emit_art_quick_generic_jni_trampoline() {
    Emit_SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_X0();

    vixl::aarch64::Label Lexception_in_native;
    // Save SP , so we can have static CFI info.
    __ mov(x28, sp);
    // .cfi_def_cfa_register x28

    // This looks the same, but is different: this will be updated to point to the bottom
    // of the frame when the handle scope is inserted.
    __ mov(xFP, sp);

    __ Mov(xIP0, 5120);
    __ sub(sp, sp, xIP0);

    // prepare for artQuickGenericJniTrampoline call
    // (Thread*, managed_sp, reserved_area)
    //    x0         x1            x2   <= C calling convention
    //  xSELF       xFP            sp   <= where they are

    __ mov(x0, xSELF);  // SP for the managed frame.
    __ mov(x1, xFP);  // reserved area for arguments and other saved data (up to managed frame)
    __ mov(x2, sp);

    Emit_Call(artQuickGenericJniTrampoline);

    // The C call will have registered the complete save-frame on success.
    // The result of the call is:
    //     x0: pointer to native code, 0 on error.
    //     The bottom of the reserved area contains values for arg registers,
    //     hidden arg register and SP for out args for the call.

    __ cbz(x0, &Lexception_in_native);

    // x0 as first argument - code pointer.
    __ mov(x1, sp);
    __ mov(x2, xSELF);
    // Call to a placeholder function, simulator will intercept it and process both the native
    // call and a call to artQuickGenericJniEndTrampoline.
    Emit_Call(artArm64SimulatorGenericJNIPlaceholder);

    // Pending exceptions possible.
    __ ldr(x2, MemOperand(xSELF, THREAD_EXCEPTION_OFFSET));   // Get exception field.
    __ cbnz(x2, &Lexception_in_native);

    // Tear down the alloca.
    __ mov(sp, x28);
    // .cfi_remember_state
    // .cfi_def_cfa_register sp

    // Tear down the callee-save frame.
    Emit_RESTORE_SAVE_REFS_AND_ARGS_FRAME();
    Emit_REFRESH_MARKING_REGISTER();

    // store into fpr, for when it's a fpr return...
    __ fmov(d0, x0);
    __ ret();

    // Undo the unwinding information from above since it doesn't apply below.
    // .cfi_restore_state
    // .cfi_def_cfa x28, FRAME_SIZE_SAVE_REFS_AND_ARGS
    __ Bind(&Lexception_in_native);
    // Move to x1 then sp to please assembler.
    __ ldr(x1, MemOperand(xSELF, THREAD_TOP_QUICK_FRAME_OFFSET));
    __ sub(sp, x1, 1);  // Remove the GenericJNI tag.

    auto ep_manager = Runtime::Current()->GetCodeSimulatorContainer()->GetEntryPointsManager();
    Emit_Call(ep_manager->GetPendingExceptionStub());
  }

  void Emit_art_deliver_pending_exception() {
    // This will create a new save-all frame, required by the runtime.
    Emit_DELIVER_PENDING_EXCEPTION();
  }

  /*
   * On entry x0 is uintptr_t* gprs_ and x1 is uint64_t* fprs_.
   * Both must reside on the stack, between current SP and target SP.
   * IP0 and IP1 shall be clobbered rather than retrieved from gprs_.
   */
  void Emit_art_quick_do_long_jump() {
    // Load FPRs
    __ ldp(d0, d1, MemOperand(x1, 0));
    __ ldp(d2, d3, MemOperand(x1, 16));
    __ ldp(d4, d5, MemOperand(x1, 32));
    __ ldp(d6, d7, MemOperand(x1, 48));
    __ ldp(d8, d9, MemOperand(x1, 64));
    __ ldp(d10, d11, MemOperand(x1, 80));
    __ ldp(d12, d13, MemOperand(x1, 96));
    __ ldp(d14, d15, MemOperand(x1, 112));
    __ ldp(d16, d17, MemOperand(x1, 128));
    __ ldp(d18, d19, MemOperand(x1, 144));
    __ ldp(d20, d21, MemOperand(x1, 160));
    __ ldp(d22, d23, MemOperand(x1, 176));
    __ ldp(d24, d25, MemOperand(x1, 192));
    __ ldp(d26, d27, MemOperand(x1, 208));
    __ ldp(d28, d29, MemOperand(x1, 224));
    __ ldp(d30, d31, MemOperand(x1, 240));

    // Load GPRs. Delay loading x0, x1 because x0 is used as gprs_.
    __ ldp(x2, x3, MemOperand(x0, 16));
    __ ldp(x4, x5, MemOperand(x0, 32));
    __ ldp(x6, x7, MemOperand(x0, 48));
    __ ldp(x8, x9, MemOperand(x0, 64));
    __ ldp(x10, x11, MemOperand(x0, 80));
    __ ldp(x12, x13, MemOperand(x0, 96));
    __ ldp(x14, x15, MemOperand(x0, 112));
    // Do not load IP0 (x16) and IP1 (x17), these shall be clobbered below.
    // Don't load the platform register (x18) either.
    __ ldr(x19, MemOperand(x0, 152));           // xSELF.
    __ ldp(x20, x21, MemOperand(x0, 160));      // For Baker RB, wMR (w20) is reloaded below.
    __ ldp(x22, x23, MemOperand(x0, 176));
    __ ldp(x24, x25, MemOperand(x0, 192));
    __ ldp(x26, x27, MemOperand(x0, 208));
    __ ldp(x28, x29, MemOperand(x0, 224));
    __ ldp(x30, xIP0, MemOperand(x0, 240));     // LR and SP, load SP to IP0.

    // Load PC to IP1, it's at the end (after the space for the unused XZR).
    __ ldr(xIP1, MemOperand(x0, 33*8));

    // Load x0, x1.
    __ ldp(x0, x1, MemOperand(x0, 0));

    // Set SP. Do not access fprs_ and gprs_ from now, they are below SP.
    __ mov(sp, xIP0);

    Emit_REFRESH_MARKING_REGISTER();
    Emit_REFRESH_SUSPEND_CHECK_REGISTER();

    __ br(xIP1);
  }

  // Macro to facilitate adding new allocation entrypoints.
  template <typename T>
  void Emit_TWO_ARG_DOWNCALL(T* fn) {
    Emit_SETUP_SAVE_REFS_ONLY_FRAME();  // save callee saves in case of GC
    __ mov(x2, xSELF);                  // pass Thread::Current

    Emit_Call(fn);

    Emit_RESTORE_SAVE_REFS_ONLY_FRAME();
    Emit_REFRESH_MARKING_REGISTER();
    __ ret();
  }

  void Emit_art_quick_alloc_array_resolved() {
    Emit_TWO_ARG_DOWNCALL(artAllocArrayFromCodeResolvedRosAlloc);
  }

  /*
   * Called by managed code when the thread has been asked to suspend.
   */
  void Emit_art_quick_test_suspend() {
                                        // Save callee saves for stack crawl.
    Emit_SETUP_SAVE_EVERYTHING_FRAME(RUNTIME_SAVE_EVERYTHING_FOR_SUSPEND_CHECK_METHOD_OFFSET);
    __ mov(x0, xSELF);
    Emit_Call(artTestSuspendFromCode);       // (Thread*)
    Emit_RESTORE_SAVE_EVERYTHING_FRAME();
    Emit_REFRESH_MARKING_REGISTER();
    Emit_REFRESH_SUSPEND_CHECK_REGISTER();
    __ ret();
  }

  /*
   * Called to bridge from the quick to interpreter ABI. On entry the arguments match those
   * of a quick call:
   * x0 = method being called/to bridge to.
   * x1..x7, d0..d7 = arguments to that method.
   */
  void Emit_art_quick_to_interpreter_bridge() {
      // Set up frame and save arguments.
    Emit_SETUP_SAVE_REFS_AND_ARGS_FRAME();

    //  x0 will contain mirror::ArtMethod* method.
    __ mov(x1, xSELF);                          // How to get Thread::Current() ???
    __ mov(x2, sp);

    // uint64_t artQuickToInterpreterBridge(mirror::ArtMethod* method, Thread* self,
    //                                      mirror::ArtMethod** sp)
    Emit_Call(artQuickToInterpreterBridge);
    /// __ bl(artQuickToInterpreterBridge);

    Emit_RESTORE_SAVE_REFS_AND_ARGS_FRAME();  // TODO: no need to restore arguments in this case.
    Emit_REFRESH_MARKING_REGISTER();

    __ fmov(d0, x0);

    Emit_RETURN_OR_DELIVER_PENDING_EXCEPTION();
  }

  /*
   * Called to attempt to execute an obsolete method.
   */
  void Emit_art_invoke_obsolete_method_stub() {
    Emit_ONE_ARG_RUNTIME_EXCEPTION(artInvokeObsoleteMethod);
  }

  //
  // Instrumentation-related stubs
  //
  //  .extern artInstrumentationMethodEntryFromCode
  void Emit_art_quick_instrumentation_entry() {
    Emit_SETUP_SAVE_REFS_AND_ARGS_FRAME();
    vixl::aarch64::Label l1;

    __ mov(x20, x0);             // Preserve method reference in a callee-save.

    __ mov(x2, xSELF);
    __ mov(x3, sp);  // Pass SP
    Emit_Call(artInstrumentationMethodEntryFromCode);  // (Method*, Object*, Thread*, SP)

    __ mov(xIP0, x0);            // x0 = result of call.
    __ mov(x0, x20);             // Reload method reference.

    Emit_RESTORE_SAVE_REFS_AND_ARGS_FRAME();  // Note: will restore xSELF
    Emit_REFRESH_MARKING_REGISTER();
    __ cbz(xIP0, &l1);            // Deliver the pending exception if method is null.
    auto ep_manager = Runtime::Current()->GetCodeSimulatorContainer()->GetEntryPointsManager();
    // Jump to art_quick_deoptimize.
    __ Mov(xLR, AddressOf(ep_manager->GetInstrumentationExitStub()));
    __ br(xIP0);                // Tail-call method with lr set to art_quick_instrumentation_exit.

    __ Bind(&l1);
    Emit_DELIVER_PENDING_EXCEPTION();
  }

  void Emit_art_quick_instrumentation_exit() {
    __ Mov(xLR, 0);             // Clobber LR for later checks.
    Emit_SETUP_SAVE_EVERYTHING_FRAME();

    vixl::aarch64::Label Ldo_deliver_instrumentation_exception, Ldeoptimize;

    __ add(x3, sp, 16);         // Pass floating-point result pointer, in kSaveEverything frame.
    __ add(x2, sp, 272);        // Pass integer result pointer, in kSaveEverything frame.
    __ mov(x1, sp);              // Pass SP.
    __ mov(x0, xSELF);           // Pass Thread.
    Emit_Call(artInstrumentationMethodExitFromCode);    // (Thread*, SP, gpr_res*, fpr_res*)

    __ cbz(x0, &Ldo_deliver_instrumentation_exception);
                              // Handle error
    __ cbnz(x1, &Ldeoptimize);
    // Normal return.
    __ str(x0, MemOperand(sp, FRAME_SIZE_SAVE_EVERYTHING - 8));
                              // Set return pc.
    Emit_RESTORE_SAVE_EVERYTHING_FRAME();
    Emit_REFRESH_MARKING_REGISTER();
    __ br(lr);

    __ Bind(&Ldo_deliver_instrumentation_exception);
    Emit_DELIVER_PENDING_EXCEPTION_FRAME_READY();

    __ Bind(&Ldeoptimize);
    __ str(x1, MemOperand(sp, FRAME_SIZE_SAVE_EVERYTHING - 8));
                              // Set return pc.
    Emit_RESTORE_SAVE_EVERYTHING_FRAME();

    auto ep_manager = Runtime::Current()->GetCodeSimulatorContainer()->GetEntryPointsManager();
    // Jump to art_quick_deoptimize.
    __ Mov(xIP0, AddressOf(ep_manager->GetDeoptimizeStub()));
    __ br(xIP0);
  }

  /*
   * Instrumentation has requested that we deoptimize into the interpreter. The deoptimization
   * will long jump to the upcall with a special exception of -1.
   */
  void Emit_art_quick_deoptimize() {
    Emit_SETUP_SAVE_EVERYTHING_FRAME();
    __ mov(x0, xSELF);          // Pass thread.

    auto ep_manager = Runtime::Current()->GetCodeSimulatorContainer()->GetEntryPointsManager();
    Emit_Call(ep_manager->GetDeoptimizeStub());      // (Thread*)
    Emit_PREPARE_AND_DO_LONG_JUMP();
  }

  //
  // Private helpers of the VIXLAsmQuickEntryPointBuilder class.
  //

  template <typename T>
  void Emit_Call(T* fn) {
    __ Mov(xIP0, AddressOf(fn));
    __ blr(xIP0);
  }

  template<typename T>
  static int64_t AddressOf(T* arg) {
    return reinterpret_cast<int64_t>(arg);
  }

  static constexpr bool kDumpDisassembly = false;
  // Whether to insert nops before the actual code of the entrypoint; useful when
  // searching for the EPs in disassembly.
  static constexpr bool kGenerateDebugNops = true;
  static constexpr size_t kEntryPointMaxSize = 1024;

  template <GeneratorFuncType func>
  static char* GenerateEntryPoint() {
    vixl::aarch64::MacroAssembler masm;
    VIXLAsmQuickEntryPointBuilder obj(&masm);
    {
      vixl::EmissionCheckScope eas(
          &masm, kEntryPointMaxSize, vixl::EmissionCheckScope::kMaximumSize);

      if (kGenerateDebugNops) {
        static constexpr size_t kNumberOfNops = 10;
        for (size_t i = 0; i < kNumberOfNops; i++) {
          masm.Nop();
        }
      }

      (obj.*func)();
    }

    masm.FinalizeCode();

    MaybePrintDisassemblyHelper(&masm);

    size_t code_size = masm.GetSizeOfCodeGenerated();
    char* buffer = new char[code_size];
    MemoryRegion code(buffer, code_size);
    MemoryRegion from(masm.GetBuffer()->GetStartAddress<void*>(), code_size);

    code.CopyFrom(0, from);
    return buffer;
  }

  static void MaybePrintDisassemblyHelper(Assembler* vasm) {
    if (!kDumpDisassembly) {
      return;
    }
    PrintDisassembler disasm(stdout);
    uint8_t* start_addr = vasm->GetBuffer()->GetStartAddress<uint8_t*>();
    const vixl::aarch64::Instruction* instr =
        reinterpret_cast<const vixl::aarch64::Instruction*>(start_addr);
    disasm.DisassembleBuffer(instr, vasm->GetBuffer()->GetSizeInBytes());
  }

#undef __

  vixl::aarch64::MacroAssembler* masm_;
};

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
       Simulator(decoder, stream, std::move(stack)) {}
  virtual ~CustomSimulator() {}

  uint8_t* GetStackBase() {
    return reinterpret_cast<uint8_t*>(memory_.GetStack().GetBase());
  }

  size_t GetStackSize() {
    return memory_.GetStack().GetBase() - memory_.GetStack().GetLimit();
  }

  // TODO(simulator): Maybe integrate these into vixl?
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

  template<typename T>
  static void* AddressOf(T* arg) {
    return reinterpret_cast<void*>(arg);
  }

  // Override Simulator::VisitUnconditionalBranchToRegister to handle any runtime invokes
  // which can be simulated.
  void VisitUnconditionalBranchToRegister(const vixl::aarch64::Instruction* instr) override
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (instr->Mask(UnconditionalBranchToRegisterMask) == BLR) {
      void* target = reinterpret_cast<void*>(ReadXRegister(instr->GetRn()));
      auto next_instr = instr->GetNextInstruction();
      if (target == AddressOf(artQuickResolutionTrampoline)) {
        WriteLr(next_instr);
        RuntimeCallNonVoid(artQuickResolutionTrampoline);
      } else if (target == AddressOf(artQuickToInterpreterBridge)) {
        WriteLr(next_instr);
        RuntimeCallNonVoid(artQuickToInterpreterBridge);
      } else if (target == reinterpret_cast<const void*>(artQuickGenericJniTrampoline)) {
        WriteLr(next_instr);
        RuntimeCallNonVoid(artQuickGenericJniTrampoline);
      } else if (target == reinterpret_cast<const void*>(artArm64SimulatorGenericJNIPlaceholder)) {
        WriteLr(next_instr);

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
      } else if (target == AddressOf(artThrowDivZeroFromCode)) {
        WriteLr(next_instr);
        RuntimeCallVoid(artThrowDivZeroFromCode);
      } else if (target == AddressOf(artDeliverPendingExceptionFromCode)) {
        WriteLr(next_instr);
        RuntimeCallVoid(artDeliverPendingExceptionFromCode);
      } else if (target == AddressOf(artContextCopyForLongJump)) {
        WriteLr(next_instr);
        RuntimeCallVoid(artContextCopyForLongJump);
      } else if (target == AddressOf(artInstrumentationMethodEntryFromCode)) {
        WriteLr(next_instr);
        RuntimeCallNonVoid(artInstrumentationMethodEntryFromCode);
      } else if (target == AddressOf(artInstrumentationMethodExitFromCode)) {
        WriteLr(next_instr);

        Thread* self = reinterpret_cast<Thread*>(ReadXRegister(0));
        ArtMethod** sp = reinterpret_cast<ArtMethod**>(ReadXRegister(1));
        uintptr_t* gpr_result = reinterpret_cast<uintptr_t*>(ReadXRegister(2));
        uintptr_t* fpr_result = reinterpret_cast<uintptr_t*>(ReadXRegister(3));

        TwoWordReturn res = artInstrumentationMethodExitFromCode(self, sp, gpr_result, fpr_result);

        // Method pointer.
        WriteXRegister(0, res.lo);
        // Code pointer.
        WriteXRegister(1, res.hi);
      } else if (target == AddressOf(artQuickProxyInvokeHandler)) {
        WriteLr(next_instr);
        RuntimeCallNonVoid(artQuickProxyInvokeHandler);
      } else if (target == AddressOf(artInvokeObsoleteMethod)) {
        WriteLr(next_instr);
        RuntimeCallNonVoid(artInvokeObsoleteMethod);
      } else {
        // For branching to fixed addresses or labels, nothing has changed.
        Simulator::VisitUnconditionalBranchToRegister(instr);
        return;
      }
      WritePc(next_instr);  // aarch64 return
      return;
    }
    Simulator::VisitUnconditionalBranchToRegister(instr);
    return;
  }
};

CodeSimulatorArm64* CodeSimulatorArm64::CreateCodeSimulatorArm64() {
  if (kCanSimulate) {
    CodeSimulatorArm64* simulator = new CodeSimulatorArm64;
    simulator->InitInstructionSimulator();
    return simulator;
  } else {
    return nullptr;
  }
}

CodeSimulatorArm64::CodeSimulatorArm64() : BasicCodeSimulatorArm64() {}

CustomSimulator* CodeSimulatorArm64::GetSimulator() {
  return reinterpret_cast<CustomSimulator*>(simulator_.get());
}

Simulator* CodeSimulatorArm64::CreateNewInstructionSimualtor(SimStack::Allocated&& stack) {
  return new CustomSimulator(decoder_.get(), stdout, std::move(stack));
}

uint8_t* CodeSimulatorArm64::GetStackBase() {
  return GetSimulator()->GetStackBase();
}

size_t CodeSimulatorArm64::GetStackSize() {
  return GetSimulator()->GetStackSize();
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

  auto ep_manager = Runtime::Current()->GetCodeSimulatorContainer()->GetEntryPointsManager();
  int64_t quick_code;

  if (isStatic) {
    quick_code = reinterpret_cast<int64_t>(ep_manager->GetInvokeStaticStub());
  } else {
    quick_code = reinterpret_cast<int64_t>(ep_manager->GetInvokeStub());
  }

  DCHECK_NE(quick_code, 0);
  RunFrom(quick_code);
}

SimulatorEntryPointsManagerArm64*
SimulatorEntryPointsManagerArm64::CreateSimulatorEntryPointsManagerArm64() {
  if (kCanSimulate) {
    return new SimulatorEntryPointsManagerArm64();
  } else {
    return nullptr;
  }
}

SimulatorEntryPointsManagerArm64::~SimulatorEntryPointsManagerArm64() {
  DeleteEntryPointBuffer(invoke_stub_);
  DeleteEntryPointBuffer(invoke_static_stub_);
  DeleteEntryPointBuffer(long_jump_stub_);
  DeleteEntryPointBuffer(pending_exception_stub_);
  DeleteEntryPointBuffer(deoptimize_stub_);
  DeleteEntryPointBuffer(instrumentation_entry_stub_);
  DeleteEntryPointBuffer(instrumentation_exit_stub_);
  DeleteEntryPointBuffer(proxy_invoke_stub_);
  DeleteEntryPointBuffer(invoke_obsolete_stub_);

  DeleteEntryPointBuffer(entry_points_.pQuickResolutionTrampoline);
  DeleteEntryPointBuffer(entry_points_.pQuickToInterpreterBridge);
  DeleteEntryPointBuffer(entry_points_.pQuickGenericJniTrampoline);
  DeleteEntryPointBuffer(entry_points_.pThrowDivZero);
}

void SimulatorEntryPointsManagerArm64::InitCustomEntryPoints() {
  VIXLAsmQuickEntryPointBuilder::GenerateStub<
      &VIXLAsmQuickEntryPointBuilder::Emit_art_quick_invoke_stub>(&invoke_stub_);
  VIXLAsmQuickEntryPointBuilder::GenerateStub<
      &VIXLAsmQuickEntryPointBuilder::Emit_art_quick_invoke_static_stub>(&invoke_static_stub_);
  VIXLAsmQuickEntryPointBuilder::GenerateStub<
      &VIXLAsmQuickEntryPointBuilder::Emit_art_quick_do_long_jump>(&long_jump_stub_);
  VIXLAsmQuickEntryPointBuilder::GenerateStub<
      &VIXLAsmQuickEntryPointBuilder::Emit_art_deliver_pending_exception>(&pending_exception_stub_);
  VIXLAsmQuickEntryPointBuilder::GenerateStub<
      &VIXLAsmQuickEntryPointBuilder::Emit_art_quick_deoptimize>(&deoptimize_stub_);
  VIXLAsmQuickEntryPointBuilder::GenerateStub<
      &VIXLAsmQuickEntryPointBuilder::Emit_art_quick_instrumentation_exit>(
          &instrumentation_exit_stub_);
  VIXLAsmQuickEntryPointBuilder::GenerateStub<
      &VIXLAsmQuickEntryPointBuilder::Emit_art_quick_instrumentation_entry>(
          &instrumentation_entry_stub_);
  VIXLAsmQuickEntryPointBuilder::GenerateStub<
      &VIXLAsmQuickEntryPointBuilder::Emit_art_quick_proxy_invoke_handler>(&proxy_invoke_stub_);
  VIXLAsmQuickEntryPointBuilder::GenerateStub<
      &VIXLAsmQuickEntryPointBuilder::Emit_art_invoke_obsolete_method_stub>(&invoke_obsolete_stub_);

  VIXLAsmQuickEntryPointBuilder::GenerateStub<
      &VIXLAsmQuickEntryPointBuilder::Emit_art_quick_resolution_trampoline>(
          &entry_points_.pQuickResolutionTrampoline);
  VIXLAsmQuickEntryPointBuilder::GenerateStub<
      &VIXLAsmQuickEntryPointBuilder::Emit_art_quick_to_interpreter_bridge>(
          &entry_points_.pQuickToInterpreterBridge);
  VIXLAsmQuickEntryPointBuilder::GenerateStub<
      &VIXLAsmQuickEntryPointBuilder::Emit_art_quick_generic_jni_trampoline>(
          &entry_points_.pQuickGenericJniTrampoline);
  VIXLAsmQuickEntryPointBuilder::GenerateStub<
     &VIXLAsmQuickEntryPointBuilder::Emit_art_quick_throw_div_zero>(&entry_points_.pThrowDivZero);
}

void SimulatorEntryPointsManagerArm64::UpdateOthersEntryPoints(
    QuickEntryPoints* others_entry_points) const {
  others_entry_points->pQuickResolutionTrampoline = entry_points_.pQuickResolutionTrampoline;
  others_entry_points->pQuickToInterpreterBridge = entry_points_.pQuickToInterpreterBridge;
  others_entry_points->pQuickGenericJniTrampoline = entry_points_.pQuickGenericJniTrampoline;
  others_entry_points->pThrowDivZero = entry_points_.pThrowDivZero;
}

#endif

}  // namespace arm64
}  // namespace art
