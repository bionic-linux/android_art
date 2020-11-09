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

#include "art_method.h"
#include "base/logging.h"
#include "class_linker.h"
#include "thread.h"

#include <string>
#include <cstring>
#include <math.h>

static constexpr bool kEnableSimulateMethodAllowList = true;

static const std::vector<std::string> simulate_method_allow_list = {
  // Add any run test method you want to simulate here, for example:

  // test/527-checker-array-access-simd/src/Main.java
  "Main.calcArraySum",
  "Main.checkByteCase",
  "Main.checkInt2Float",
  "Main.checkIntCase",
  "Main.checkSingleAccess",

  // test/530-checker-lse-simd/src/Main.java
  "Main.$noinline$vecgen",
  "Main.$noinline$test02",
  "Main.$noinline$test03",
  "Main.$noinline$test04",
  "Main.$noinline$test05",
  "Main.$noinline$test06",

  // test/550-checker-multiply-accumulate/src/Main.java
  "Main.SimdMulAdd",
  "Main.SimdMulSub",
  "Main.SimdMulMultipleUses",

  // test/623-checker-loop-regressions/src/Main.java
  "Main.string2Bytes",
  "Main.$noinline$stringToShorts",
  "Main.$noinline$ensureSlowPathFPSpillFill",

  // test/640-checker-boolean-simd/src/Main.java
  "Main.and(",
  "Main.not(",
  "Main.or(",
  "Main.xor(",

  // test/640-checker-simd/src/Main.java
  "SimdInt.$opt$inline$IntConstant32",
  "SimdInt.$opt$inline$IntConstant33",
  "SimdInt.$opt$inline$IntConstantMinus254",
  "SimdLong.$opt$inline$IntConstant64",
  "SimdLong.$opt$inline$IntConstant65",
  "SimdLong.$opt$inline$IntConstantMinus254",
  "SimdByte.add",
  "SimdByte.bounds",
  "SimdByte.div",
  "SimdByte.mul",
  "SimdByte.neg",
  "SimdByte.not",
  "SimdByte.sar2",
  "SimdByte.sar31",
  "SimdByte.shl4",
  "SimdByte.shl9",
  "SimdByte.shr2",
  "SimdByte.shr31",
  "SimdByte.shr32",
  "SimdByte.shr33",
  "SimdByte.sub",
  "SimdChar.add",
  "SimdChar.bounds",
  "SimdChar.div",
  "SimdChar.mul",
  "SimdChar.neg",
  "SimdChar.not",
  "SimdChar.sar2",
  "SimdChar.sar31",
  "SimdChar.shl4",
  "SimdChar.shr2",
  "SimdChar.shr31",
  "SimdChar.shr32",
  "SimdChar.shr33",
  "SimdChar.sub",
  "SimdDouble.abs",
  "SimdDouble.add",
  "SimdDouble.bounds",
  "SimdDouble.conv",
  "SimdDouble.div",
  "SimdDouble.mul",
  "SimdDouble.neg",
  "SimdDouble.sub",
  "SimdFloat.abs",
  "SimdFloat.add",
  "SimdFloat.bounds",
  "SimdFloat.conv",
  "SimdFloat.div",
  "SimdFloat.mul",
  "SimdFloat.neg",
  "SimdFloat.sub",
  "SimdInt.add",
  "SimdInt.bounds",
  "SimdInt.div",
  "SimdInt.mul",
  "SimdInt.neg",
  "SimdInt.not",
  "SimdInt.sar2",
  "SimdInt.shl4",
  "SimdInt.shr2",
  "SimdInt.shr32",
  "SimdInt.shr33",
  "SimdInt.shrMinus254",
  "SimdInt.sub",
  "SimdLong.add",
  "SimdLong.bounds",
  "SimdLong.div",
  "SimdLong.mul",
  "SimdLong.neg",
  "SimdLong.not",
  "SimdLong.sar2",
  "SimdLong.shl4",
  "SimdLong.shr2",
  "SimdLong.shr64",
  "SimdLong.shr65",
  "SimdLong.shrMinus254",
  "SimdLong.sub",
  "SimdShort.add",
  "SimdShort.add",
  "SimdShort.div",
  "SimdShort.mul",
  "SimdShort.neg",
  "SimdShort.not",
  "SimdShort.sar2",
  "SimdShort.sar31",
  "SimdShort.shl4",
  "SimdShort.shr2",
  "SimdShort.shr31",
  "SimdShort.shr32",
  "SimdShort.shr33",
  "SimdShort.sub",

  // test/645-checker-abs-simd/src/Main.java
  "Main.doitByte",
  "Main.doitCastedChar",
  "Main.doitChar",
  "Main.doitDouble",
  "Main.doitFloat",
  "Main.doitInt",
  "Main.doitLong",
  "Main.doitShort",

  // test/646-checker-simd-hadd/src/Main.java
  "HaddAltByte.halving_add_signed",
  "HaddAltByte.halving_add_signed_constant",
  "HaddAltByte.halving_add_unsigned",
  "HaddAltByte.halving_add_unsigned_constant",
  "HaddAltByte.rounding_halving_add_signed",
  "HaddAltByte.rounding_halving_add_unsigned",
  "HaddAltShort.halving_add_signed",
  "HaddAltShort.halving_add_signed_constant",
  "HaddAltShort.halving_add_unsigned",
  "HaddAltShort.halving_add_unsigned_constant",
  "HaddAltShort.rounding_halving_add_signed",
  "HaddAltShort.rounding_halving_add_unsigned",
  "HaddAltChar.halving_add_also_unsigned",
  "HaddAltChar.halving_add_also_unsigned_constant",
  "HaddAltChar.halving_add_unsigned",
  "HaddAltChar.halving_add_unsigned_constant",
  "HaddAltChar.rounding_halving_add_also_unsigned",
  "HaddAltChar.rounding_halving_add_unsigned",
  "HaddByte.halving_add_signed",
  "HaddByte.halving_add_signed_constant",
  "HaddByte.halving_add_unsigned",
  "HaddByte.halving_add_unsigned_constant",
  "HaddByte.rounding_halving_add_signed",
  "HaddByte.rounding_halving_add_unsigned",
  "HaddShort.halving_add_signed",
  "HaddShort.halving_add_signed_alt",
  "HaddShort.halving_add_signed_constant",
  "HaddShort.halving_add_unsigned",
  "HaddShort.halving_add_unsigned_constant",
  "HaddShort.rounding_halving_add_signed",
  "HaddShort.rounding_halving_add_signed_alt",
  "HaddShort.rounding_halving_add_signed_alt2",
  "HaddShort.rounding_halving_add_signed_alt3",
  "HaddShort.rounding_halving_add_unsigned",
  "HaddShort.rounding_halving_add_unsigned_alt",
  "HaddChar.halving_add_also_unsigned",
  "HaddChar.halving_add_also_unsigned_constant",
  "HaddChar.halving_add_unsigned",
  "HaddChar.halving_add_unsigned_constant",
  "HaddChar.rounding_halving_add_also_unsigned",
  "HaddChar.rounding_halving_add_unsigned"

  // test/655-checker-simd-arm-opt/src/Main.java
  "Main.sumArray",
  "Main.encodableConstants",

  // test/656-checker-simd-opt/src/Main.java
  "Main.stencil(int[], int[], int)",
  "Main.unroll(float[], float[])",
  "Main.intVectorLongInvariant",
  "Main.longCanBeDoneWithInt",
  "Main.longInductionReduction",

  // test/660-checker-simd-sad/src/Main.java
  "SimdSadByte.sadByte2Byte",
  "SimdSadByte.sadByte2ByteAlt",
  "SimdSadByte.sadByte2ByteAlt2",
  "SimdSadByte.sadByte2Int",
  "SimdSadByte.sadByte2IntAlt",
  "SimdSadByte.sadByte2IntAlt2",
  "SimdSadByte.sadByte2Long",
  "SimdSadByte.sadByte2LongAt1",
  "SimdSadByte.sadByte2Short",
  "SimdSadByte.sadByte2ShortAlt",
  "SimdSadByte.sadByte2ShortAlt2",
  "SimdSadChar.sadChar2Char",
  "SimdSadChar.sadChar2CharAlt",
  "SimdSadChar.sadChar2CharAlt2",
  "SimdSadChar.sadChar2Int",
  "SimdSadChar.sadChar2IntAlt",
  "SimdSadChar.sadChar2IntAlt2",
  "SimdSadInt.sadInt2Int",
  "SimdSadInt.sadInt2IntAlt",
  "SimdSadInt.sadInt2IntAlt2",
  "SimdSadShort.$inline$seven",
  "SimdSadShort.sadShort2Int",
  "SimdSadShort.sadShort2IntAlt",
  "SimdSadShort.sadShort2IntAlt2",
  "SimdSadShort.sadShort2IntConstant1",
  "SimdSadShort.sadShort2IntConstant2",
  "SimdSadShort.sadShort2IntConstant3",
  "SimdSadShort2.sadCastedChar2Int",
  "SimdSadShort2.sadCastedChar2IntAlt",
  "SimdSadShort2.sadCastedChar2IntAlt2",
  "SimdSadShort3.sadShort2IntCastedExprLeft",
  "SimdSadShort3.sadShort2IntCastedExprRight",
  "SimdSadShort3.sadShort2IntConstLeft",
  "SimdSadShort3.sadShort2IntConstRight",
  "SimdSadShort3.sadShort2IntInvariantLeft",
  "SimdSadShort3.sadShort2IntInvariantRight",
  "SimdSadShort3.sadShort2IntParamLeft",
  "SimdSadShort3.sadShort2IntParamRight",
  "SimdSadChar.sadChar2Long",
  "SimdSadChar.sadChar2LongAt1",
  "SimdSadInt.sadInt2Long",
  "SimdSadInt.sadInt2LongAt1",
  "SimdSadLong.sadLong2Long",
  "SimdSadLong.sadLong2LongAlt",
  "SimdSadLong.sadLong2LongAlt2",
  "SimdSadLong.sadLong2LongAt1",
  "SimdSadShort.sadShort2Long",
  "SimdSadShort.sadShort2LongAt1",
  "SimdSadShort2.sadCastedChar2Long",
  "SimdSadShort2.sadCastedChar2LongAt1",
  "SimdSadShort.sadShort2Short",
  "SimdSadShort.sadShort2ShortAlt",
  "SimdSadShort.sadShort2ShortAlt2",
  "SimdSadShort2.sadCastedChar2Short",
  "SimdSadShort2.sadCastedChar2ShortAlt",
  "SimdSadShort2.sadCastedChar2ShortAlt2",

  // test/661-checker-simd-reduc/src/Main.java
  "Main.reductionByte",
  "Main.reductionByteM1",
  "Main.reductionMinusByte",
  "Main.reductionChar",
  "Main.reductionCharM1",
  "Main.reductionMinusChar",
  "Main.reductionInt",
  "Main.reductionInt10",
  "Main.reductionIntChain",
  "Main.reductionIntM1",
  "Main.reductionIntToLoop",
  "Main.reductionMinusInt",
  "Main.reductionMinusInt10",
  "Main.reductionLong",
  "Main.reductionLongM1",
  "Main.reductionMinusLong",
  "Main.reductionMinusShort",
  "Main.reductionShort",
  "Main.reductionShortM1",

  // test/665-checker-simd-zero/src/Main.java
  "Main.zerob",
  "Main.zeroc",
  "Main.zerod",
  "Main.zerof",
  "Main.zeroi",
  "Main.zerol",
  "Main.zeros",
  "Main.zeroz",

  // test/667-checker-simd-alignment/src/Main.java
  "Main.staticallyAligned",
  "Main.staticallyAlignedN",
  "Main.staticallyMisaligned",
  "Main.staticallyMisalignedN",
  "Main.staticallyUnknownAligned",
  "Main.staticallyUnknownAlignedN",

  // test/684-checker-simd-dotprod/src/Main.java
  "other.TestByte.testDotProdComplex",
  "other.TestByte.testDotProdComplexSignedCastedToUnsigned",
  "other.TestByte.testDotProdComplexUnsigned",
  "other.TestByte.testDotProdComplexUnsignedCastedToSigned",
  "other.TestByte.testDotProdIntParam",
  "other.TestByte.testDotProdParamSigned",
  "other.TestByte.testDotProdParamUnsigned",
  "other.TestByte.testDotProdSignedToChar",
  "other.TestByte.testDotProdSignedWidening",
  "other.TestByte.testDotProdSimple",
  "other.TestByte.testDotProdSimpleCastedToChar",
  "other.TestByte.testDotProdSimpleCastedToShort",
  "other.TestByte.testDotProdSimpleCastedToSignedByte",
  "other.TestByte.testDotProdSimpleCastedToUnsignedByte",
  "other.TestByte.testDotProdSimpleUnsigned",
  "other.TestByte.testDotProdSimpleUnsignedCastedToChar",
  "other.TestByte.testDotProdSimpleUnsignedCastedToLong",
  "other.TestByte.testDotProdSimpleUnsignedCastedToShort",
  "other.TestByte.testDotProdSimpleUnsignedCastedToSignedByte",
  "other.TestByte.testDotProdSimpleUnsignedCastedToUnsignedByte",
  "other.TestByte.testDotProdUnsignedSigned",
  "other.TestCharShort.testDotProdComplex",
  "other.TestCharShort.testDotProdComplexSignedCastedToUnsigned",
  "other.TestCharShort.testDotProdComplexUnsigned",
  "other.TestCharShort.testDotProdComplexUnsignedCastedToSigned",
  "other.TestCharShort.testDotProdIntParam",
  "other.TestCharShort.testDotProdParamSigned",
  "other.TestCharShort.testDotProdParamUnsigned",
  "other.TestCharShort.testDotProdSignedNarrowerSigned",
  "other.TestCharShort.testDotProdSignedNarrowerUnsigned",
  "other.TestCharShort.testDotProdSignedToChar",
  "other.TestCharShort.testDotProdSignedToInt",
  "other.TestCharShort.testDotProdSimple",
  "other.TestCharShort.testDotProdSimpleCastedToChar",
  "other.TestCharShort.testDotProdSimpleCastedToShort",
  "other.TestCharShort.testDotProdSimpleMulCastedToSigned",
  "other.TestCharShort.testDotProdSimpleMulCastedToUnsigned",
  "other.TestCharShort.testDotProdSimpleUnsigned",
  "other.TestCharShort.testDotProdSimpleUnsignedCastedToChar",
  "other.TestCharShort.testDotProdSimpleUnsignedCastedToLong",
  "other.TestCharShort.testDotProdSimpleUnsignedCastedToShort",
  "other.TestCharShort.testDotProdSimpleUnsignedMulCastedToSigned",
  "other.TestCharShort.testDotProdSimpleUnsignedMulCastedToUnsigned",
  "other.TestCharShort.testDotProdUnsignedNarrowerSigned",
  "other.TestCharShort.testDotProdUnsignedNarrowerUnsigned",
  "other.TestCharShort.testDotProdUnsignedSigned",
  "other.TestVarious.testDotProdBothSignedUnsigned1",
  "other.TestVarious.testDotProdBothSignedUnsigned2",
  "other.TestVarious.testDotProdBothSignedUnsignedChar",
  "other.TestVarious.testDotProdBothSignedUnsignedDoubleLoad",
  "other.TestVarious.testDotProdByteToChar",
  "other.TestVarious.testDotProdConstLeft",
  "other.TestVarious.testDotProdConstRight",
  "other.TestVarious.testDotProdInt32",
  "other.TestVarious.testDotProdLoopInvariantConvRight",
  "other.TestVarious.testDotProdMixedSize",
  "other.TestVarious.testDotProdMixedSizeAndSign"
};
static const std::vector<std::string> avoid_simulation_method_list = {
  // For now, we can focus on simulating run test methods called by main().
  "main",
  "<clinit>",
  // Currently, we don't simulate Java library methods.
  "java.",
  "sun.",
  "dalvik.",
  "android.",
  "libcore.",
};

using namespace vixl::aarch64;  // NOLINT(build/namespaces)

namespace art {
namespace arm64 {

  // Special registers defined in asm_support_arm64.s.
  // Register holding Thread::current().
  static const unsigned kSelf = 19;
  // Marking register.
  static const unsigned kMR = 20;
  // Frame Pointer.
  static const unsigned kFp = 29;
  // Stack Pointer.
  static const unsigned kSp = 31;

class CustomSimulator final: public Simulator {
 public:
  CustomSimulator(Decoder* decoder, FILE* stream, SimStack::Allocated stack) :
       Simulator(decoder, stream, std::move(stack)),
       qpoints_(nullptr) {}
  virtual ~CustomSimulator() {}

  void SetEntryPoints(QuickEntryPoints* qpoints) {
    DCHECK(qpoints_ == nullptr);
    qpoints_ = qpoints;
  }

  template <typename R, typename... P>
  struct RuntimeCallHelper {
    static void Execute(Simulator* simulator, R (*f)(P...)) {
      simulator->RuntimeCallNonVoid(f);
    }
  };

  // Partial specialization when the return type is `void`.
  template <typename... P>
  struct RuntimeCallHelper<void, P...> {
    static void Execute(Simulator* simulator, void (*f)(P...)) {
      simulator->RuntimeCallVoid(f);
    }
  };

  // Override Simulator::VisitUnconditionalBranchToRegister to handle any runtime invokes
  // which can be simulated.
  void VisitUnconditionalBranchToRegister(const vixl::aarch64::Instruction* instr) override
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(qpoints_ != nullptr);
    if (instr->Mask(UnconditionalBranchToRegisterMask) == BR) {
      // The thunk mechansim code (LDR, BR) is generated by
      // CodeGeneratorARM64::InvokeRuntime()

      // Conceptually, the control flow works as if:
      // #########################################################################
      // Compiled Method (arm64)    |  THUNK (arm64) | Runtime Function (x86_64)
      // #########################################################################
      // BL kQuickTestSuspend@thunk -> LDR x16, [...]
      //                                BR x16 -------> art_quick_test_suspend
      //     ^                                               (x86 ret)
      //     |                                                   |
      //     +---------------------------------------------------+

      // Actual control flow: arm64 code <-> x86_64 runtime, intercepted by simulator.
      // ##########################################################################
      //              arm64 code in simulator      |         | ART Runtime (x86_64)
      // ##########################################################################
      // BL kQuickTestSuspend@thunk -> LDR x16, [...]
      //                                BR x16 ---> simulator ---> art_quick_test_suspend
      //     ^                                      (x86 call)          (x86 ret)
      //     |                                                              |
      //     +------------------------------------- simulator <-------------+
      //                                            (ARM ret)
      //

      const void* target = reinterpret_cast<const void*>(ReadXRegister(instr->GetRn()));
      auto lr = vixl::aarch64::Instruction::Cast(get_lr());
      if (target == reinterpret_cast<const void*>(qpoints_->pTestSuspend)) {
        RuntimeCallHelper<void>::Execute(this, qpoints_->pTestSuspend);
      } else {
        // For branching to fixed addresses or labels, nothing has changed.
        Simulator::VisitUnconditionalBranchToRegister(instr);
        return;
      }
      WritePc(lr);  // aarch64 return
      return;
    } else if (instr->Mask(UnconditionalBranchToRegisterMask) == BLR) {
      const void* target = reinterpret_cast<const void*>(ReadXRegister(instr->GetRn()));
      auto lr = instr->GetNextInstruction();
      if (target == reinterpret_cast<const void*>(qpoints_->pAllocObjectInitialized)) {
        RuntimeCallHelper<void *, mirror::Class *>::Execute(this, qpoints_->pAllocObjectInitialized);
      } else if (target == reinterpret_cast<const void*>(qpoints_->pAllocArrayResolved8) ||
                 target == reinterpret_cast<const void*>(qpoints_->pAllocArrayResolved16) ||
                 target == reinterpret_cast<const void*>(qpoints_->pAllocArrayResolved32) ||
                 target == reinterpret_cast<const void*>(qpoints_->pAllocArrayResolved64)) {
        RuntimeCallHelper<void *, mirror::Class *, int32_t>::Execute(this,
            reinterpret_cast<void *(*)(art::mirror::Class *, int)>(const_cast<void*>(target)));
      } else {
        // For branching to fixed addresses or labels, nothing has changed.
        Simulator::VisitUnconditionalBranchToRegister(instr);
        return;
      }
      WritePc(lr);  // aarch64 return
      return;
    }
    Simulator::VisitUnconditionalBranchToRegister(instr);
    return;
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

 private:
  QuickEntryPoints* qpoints_;
};

static const void* GetQuickCodeFromArtMethod(ArtMethod* method)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(!method->IsAbstract());
  DCHECK(!method->IsNative());
  DCHECK(Runtime::SimulatorMode());
  DCHECK(method->CanBeSimulated());

  ClassLinker* linker = Runtime::Current()->GetClassLinker();
  const void* code = method->GetOatMethodQuickCode(linker->GetImagePointerSize());
  if (code != nullptr) {
    return code;
  }
  return nullptr;
}

// VIXL has not been tested on 32bit architectures, so Simulator is not always
// available. To avoid linker error on these architectures, we check if we can simulate
// in the beginning of following methods, with compile time constant `kCanSimulate`.
// TODO: when Simulator is always available, remove the these checks.

CodeSimulatorArm64* CodeSimulatorArm64::CreateCodeSimulatorArm64() {
  if (kCanSimulate) {
    return new CodeSimulatorArm64();
  } else {
    return nullptr;
  }
}

CodeSimulatorArm64::CodeSimulatorArm64()
    : CodeSimulator(), decoder_(nullptr), simulator_(nullptr) {
  DCHECK(kCanSimulate);
  decoder_ = new Decoder();

  SimStack stack_builder;
  stack_builder.SetLimitGuardSize(0x4000);
  stack_builder.SetUsableSize(0x4000);
  SimStack::Allocated stack = stack_builder.Allocate();

  simulator_ = new CustomSimulator(decoder_, stdout, std::move(stack));

  // TODO: support SVE vector length instruction set features and use it here.
  simulator_->SetVectorLengthInBits(kArm64DefaultSVEVectorLength);

  if (VLOG_IS_ON(simulator)) {
    simulator_->SetColouredTrace(true);
    simulator_->SetTraceParameters(LOG_DISASM | LOG_WRITE);
  }
}

CodeSimulatorArm64::~CodeSimulatorArm64() {
  DCHECK(kCanSimulate);
  delete simulator_;
  delete decoder_;
}

void CodeSimulatorArm64::RunFrom(intptr_t code_buffer) {
  DCHECK(kCanSimulate);
  simulator_->RunFrom(reinterpret_cast<const vixl::aarch64::Instruction*>(code_buffer));
}

bool CodeSimulatorArm64::GetCReturnBool() const {
  DCHECK(kCanSimulate);
  return simulator_->ReadWRegister(0);
}

int32_t CodeSimulatorArm64::GetCReturnInt32() const {
  DCHECK(kCanSimulate);
  return simulator_->ReadWRegister(0);
}

int64_t CodeSimulatorArm64::GetCReturnInt64() const {
  DCHECK(kCanSimulate);
  return simulator_->ReadXRegister(0);
}

void CodeSimulatorArm64::Invoke(ArtMethod* method, uint32_t* args, uint32_t args_size_in_bytes,
                                Thread* self, JValue* result, const char* shorty, bool isStatic)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(kCanSimulate);
  // ARM64 simulator only supports 64-bit host machines. Because:
  //   1) vixl simulator is not tested on 32-bit host machines.
  //   2) Data structures in ART have different representations for 32/64-bit machines.
  DCHECK(sizeof(args) == sizeof(int64_t));

  if (VLOG_IS_ON(simulator)) {
    VLOG(simulator) << "\nVIXL_SIMULATOR simulate: " << method->PrettyMethod();
  }

  InitRegistersForInvokeStub(method, args, args_size_in_bytes, self, result, shorty, isStatic);

  int64_t quick_code = reinterpret_cast<int64_t>(GetQuickCodeFromArtMethod(method));
  RunFrom(quick_code);

  GetResultFromShorty(result, shorty);

  // Ensure simulation state is not carried over from one method to another.
  simulator_->ResetState();

  // Reset stack pointer.
  simulator_->WriteSp(saved_sp_);
}

void CodeSimulatorArm64::GetResultFromShorty(JValue* result, const char* shorty) {
  switch (shorty[0]) {
    case 'V':
      return;
    case 'D':
      result->SetD(simulator_->ReadDRegister(0));
      return;
    case 'F':
      result->SetF(simulator_->ReadSRegister(0));
      return;
    default:
      // Just store x0. Doesn't matter if it is 64 or 32 bits.
      result->SetJ(simulator_->ReadXRegister(0));
      return;
  }
}

// Init registers for invoking art_quick_invoke_stub:
//
//  extern"C" void art_quick_invoke_stub(ArtMethod *method,   x0
//                                       uint32_t  *args,     x1
//                                       uint32_t argsize,    w2
//                                       Thread *self,        x3
//                                       JValue *result,      x4
//                                       char   *shorty);     x5
//
// See art/runtime/arch/arm64/quick_entrypoints_arm64.S
//
//  +----------------------+
//  |                      |
//  |  C/C++ frame         |
//  |       LR''           |
//  |       FP''           | <- SP'
//  +----------------------+
//  +----------------------+
//  |        X28           |
//  |        :             |
//  |        X19 (*self)   |
//  |        SP'           |        Saved registers
//  |        X5 (*shorty)  |
//  |        X4 (*result)  |
//  |        LR'           |
//  |        FP'           | <- FP
//  +----------------------+
//  | uint32_t out[n-1]    |
//  |    :      :          |        Outs
//  | uint32_t out[0]      |
//  | ArtMethod*           | <- SP  value=null
//  +----------------------+
//
// Outgoing registers:
//  x0    - Current ArtMethod*
//  x1-x7 - integer parameters.
//  d0-d7 - Floating point parameters.
//  xSELF = self
//  SP = & of ArtMethod*
//  x1    - "this" pointer (for non-static method)
void CodeSimulatorArm64::InitRegistersForInvokeStub(ArtMethod* method, uint32_t* args,
                                                    uint32_t args_size_in_bytes, Thread* self,
                                                    JValue* result, const char* shorty,
                                                    bool isStatic)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(kCanSimulate);

  // Set registers x0, x4, x5, and x19.
  simulator_->WriteXRegister(0, reinterpret_cast<int64_t>(method));
  simulator_->WriteXRegister(kSelf, reinterpret_cast<int64_t>(self));
  simulator_->WriteXRegister(4, reinterpret_cast<int64_t>(result));
  simulator_->WriteXRegister(5, reinterpret_cast<int64_t>(shorty));

  // Stack Pointer here is not the real one in hardware. This will break stack overflow check.
  // Also note that the simulator stack is limited.
  saved_sp_ = simulator_->get_sp();
  // x4, x5, x19, x20 .. x28, SP, LR, FP saved (15 in total).
  const int64_t regs_save_size_in_bytes = kXRegSizeInBytes * 15;
  const int64_t frame_save_size = regs_save_size_in_bytes +
                                  kXRegSizeInBytes +  // ArtMethod*
                                  static_cast<int64_t>(args_size_in_bytes);
  // Comply with 16-byte alignment requirement for SP.
  void** new_sp = reinterpret_cast<void**>((saved_sp_ - frame_save_size) & (~0xfUL));

  simulator_->WriteSp(new_sp);

  // Store null into ArtMethod* at bottom of frame.
  *new_sp++ = nullptr;
  // Copy arguments into stack frame.
  std::memcpy(new_sp, args, args_size_in_bytes * sizeof(uint32_t));

  // Callee-saved registers.
  int64_t* save_registers = reinterpret_cast<int64_t*>(saved_sp_) + 3;
  save_registers[0] = simulator_->get_fp();
  save_registers[1] = simulator_->get_lr();
  save_registers[2] = simulator_->get_x(4);  // X4 (*result)
  save_registers[3] = simulator_->get_x(5);  // X5 (*shorty)
  save_registers[4] = saved_sp_;
  save_registers[5] = simulator_->get_x(kSelf);  // X19 (*self)
  for (unsigned int i = 6; i < 15; i++) {
    save_registers[i] = simulator_->get_x(i + 14);  // X20 .. X28
  }

  // Use xFP (Frame Pointer) now, as it's callee-saved.
  simulator_->WriteXRegister(kFp, saved_sp_ - regs_save_size_in_bytes);

  // Fill registers from args, according to shorty.
  static const unsigned kRegisterIndexLimit = 8;
  unsigned fpr_index = 0;
  unsigned gpr_index = 1;  // x1 ~ x7 integer parameters.
  shorty++;  // Skip the return value.
  // For non-static method, load "this" parameter, and increment args pointer.
  if (!isStatic) {
    simulator_->WriteWRegister(gpr_index++, *args++);
  }
  // Loop to fill registers.
  for (const char* s = shorty; *s != '\0'; s++) {
    switch (*s) {
      case 'D':
        simulator_->WriteDRegister(fpr_index++, *reinterpret_cast<double*>(args));
        args += 2;
        break;
      case 'J':
        simulator_->WriteXRegister(gpr_index++, *reinterpret_cast<int64_t*>(args));
        args += 2;
        break;
      case 'F':
        simulator_->WriteSRegister(fpr_index++, *reinterpret_cast<float*>(args));
        args++;
        break;
      default:
        // Everything else takes one vReg.
        simulator_->WriteWRegister(gpr_index++, *reinterpret_cast<int32_t*>(args));
        args++;
        break;
    }
    if (gpr_index > kRegisterIndexLimit || fpr_index < kRegisterIndexLimit) {
      // TODO: Handle register spill.
      UNREACHABLE();
    }
  }

  // REFRESH_MARKING_REGISTER
  if (kUseReadBarrier) {
    simulator_->WriteWRegister(kMR, self->GetIsGcMarking());
  }
}

void CodeSimulatorArm64::InitEntryPoints(QuickEntryPoints* qpoints) {
  simulator_->SetEntryPoints(qpoints);
}

bool CodeSimulatorArm64::CanSimulate(ArtMethod* method) REQUIRES_SHARED(Locks::mutator_lock_) {
  std::string name = method->PrettyMethod();

  // Make sure simulate methods with $simulate$ in their names.
  if (name.find("$simulate$") != std::string::npos) {
    return true;
  }
  // Simulation allow list mode, only simulate method on the allow list.
  if (kEnableSimulateMethodAllowList) {
    for (auto& s : simulate_method_allow_list) {
      if (name.find(s) != std::string::npos) {
        return true;
      }
    }
    return false;
  }
  // Avoid simulating following methods.
  for (auto& s : avoid_simulation_method_list) {
    if (name.find(s) != std::string::npos) {
      return false;
    }
  }

  // Try to simulate as much as we can.
  return true;
}

}  // namespace arm64
}  // namespace art
