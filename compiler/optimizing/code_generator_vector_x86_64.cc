/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "code_generator_x86_64.h"

#include "mirror/array-inl.h"
#include "mirror/string.h"

namespace art HIDDEN {
namespace x86_64 {

// NOLINT on __ macro to suppress wrong warning/fix (misc-macro-parentheses) from clang-tidy.
#define __ down_cast<X86_64Assembler*>(GetAssembler())->  // NOLINT

static void CheckVectorization(CodeGeneratorX86_64* codegen,
                               HVecOperation* instruction,
                               XmmRegister& reg,
                               bool* uses_avx2 = nullptr) {
  DCHECK_EQ(instruction->GetVectorLength() * DataType::Size(instruction->GetPackedType()),
            codegen->GetSIMDRegisterWidth());
  DCHECK_EQ(codegen->GetInstructionSetFeatures().HasAVX2(), reg.IsYMM());
  if (uses_avx2 != nullptr) {
    *uses_avx2 = codegen->GetInstructionSetFeatures().HasAVX2();
  }
}

void LocationsBuilderX86_64::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  HInstruction* input = instruction->InputAt(0);
  bool is_zero = IsZeroBitPattern(input);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      locations->SetInAt(0, is_zero ? Location::ConstantLocation(input)
                                    : Location::RequiresRegister());
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, is_zero ? Location::ConstantLocation(input)
                                    : Location::RequiresFpuRegister());
      // This is a special instruction with scalar-in and vector-out
      // If we use same register for In and Out, we would wrongly consider it as vector-in
      //   during register allocation.
      // Any parallel moves generated, would have trouble as we wrongly marked
      // the in-reg as vector. Use a different register for in and out to avoid this.
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorX86_64::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();
  // Generic vectorization size check
  bool uses_avx2 = false;
  CheckVectorization(codegen_, instruction, dst, &uses_avx2);
  // Shorthand for any type of zero.
  if (IsZeroBitPattern(instruction->InputAt(0))) {
    uses_avx2 ? __ vxorps(dst, dst, dst) : __ xorps(dst, dst);
    return;
  }

  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ movd(dst, locations->InAt(0).AsRegister<CpuRegister>(), /*64-bit*/ false);
      if (!uses_avx2) {
        __ punpcklbw(dst, dst);
        __ punpcklwd(dst, dst);
        __ pshufd(dst, dst, Immediate(0));
      } else {
        __ vpbroadcastb(dst, dst);
      }
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ movd(dst, locations->InAt(0).AsRegister<CpuRegister>(), /*64-bit*/ false);
      if (!uses_avx2) {
        __ punpcklwd(dst, dst);
        __ pshufd(dst, dst, Immediate(0));
      } else {
        __ vpbroadcastw(dst, dst);
      }
      break;
    case DataType::Type::kInt32:
      __ movd(dst, locations->InAt(0).AsRegister<CpuRegister>(), /*64-bit*/ false);
      if (!uses_avx2) {
        __ pshufd(dst, dst, Immediate(0));
      } else {
        __ vpbroadcastd(dst, dst);
      }
      break;
    case DataType::Type::kInt64:
      __ movd(dst, locations->InAt(0).AsRegister<CpuRegister>(), /*64-bit*/ true);
      if (!uses_avx2) {
        __ punpcklqdq(dst, dst);
      } else {
        __ vpbroadcastq(dst, dst);
      }
      break;
    case DataType::Type::kFloat32:
      if (!uses_avx2) {
        XmmRegister src = locations->InAt(0).AsFpuRegister<XmmRegister>();
        __ movups(dst, src);
        __ shufps(dst, dst, Immediate(0));
      } else {
        XmmRegister src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
        __ vbroadcastss(dst, src);
      }
      break;
    case DataType::Type::kFloat64:
      if (!uses_avx2) {
        XmmRegister src = locations->InAt(0).AsFpuRegister<XmmRegister>();
        __ movups(dst, src);
        __ shufpd(dst, dst, Immediate(0));
      } else {
        XmmRegister src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
        __ vbroadcastsd(dst, src);
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecExtractScalar(HVecExtractScalar* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresRegister());
      break;
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      // This is a special instruction with scalar-out and vector-in
      // If we use same register for In and Out, we would consider it as vector-out
      //   during register allocation.
      // Eventually any users will see it as a vector register.
      // Using a different register for out, ensures it's not marked as vector
      locations->SetOut(Location::RequiresFpuRegister());

      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorX86_64::VisitVecExtractScalar(HVecExtractScalar* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, src);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:  // TODO: up to here, and?
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
    case DataType::Type::kInt32:
      __ movd(locations->Out().AsRegister<CpuRegister>(), src, /*64-bit*/ false);
      break;
    case DataType::Type::kInt64:
      __ movd(locations->Out().AsRegister<CpuRegister>(), src, /*64-bit*/ true);
      break;
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64: {
      XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
      __ movups(dst, src);
    }

    break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

// Helper to set up locations for vector unary operations.
static void CreateVecUnOpLocations(ArenaAllocator* allocator, HVecUnaryOperation* instruction) {
  LocationSummary* locations = new (allocator) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecReduce(HVecReduce* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
  // Long reduction or min/max require a temporary.
  if (instruction->GetPackedType() == DataType::Type::kInt64 ||
      instruction->GetReductionKind() == HVecReduce::kMin ||
      instruction->GetReductionKind() == HVecReduce::kMax) {
    instruction->GetLocations()->AddTemp(Location::RequiresFpuRegister());
  }
}

void InstructionCodeGeneratorX86_64::VisitVecReduce(HVecReduce* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  bool uses_avx2 = false;
  CheckVectorization(codegen_, instruction, dst, &uses_avx2);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kInt32:
      switch (instruction->GetReductionKind()) {
        case HVecReduce::kSum:
          if (!uses_avx2) {
            __ movaps(dst, src);
            __ phaddd(dst, dst);
            __ phaddd(dst, dst);
          } else {
            __ vmovaps(dst, src);
            __ vphaddd(dst, dst, dst);
            __ vpermpd(dst, dst, Immediate(0xd8));
            __ vphaddd(dst, dst, dst);
            __ vphaddd(dst, dst, dst);
          }
          break;
        case HVecReduce::kMin:
        case HVecReduce::kMax:
          // Historical note: We've had a broken implementation here. b/117863065
          // Do not draw on the old code if we ever want to bring MIN/MAX reduction back.
          LOG(FATAL) << "Unsupported reduction type.";
      }
      break;
    case DataType::Type::kInt64: {
      XmmRegister tmp = locations->GetTemp(0).AsFPVectorRegister<XmmRegister>();
      switch (instruction->GetReductionKind()) {
        case HVecReduce::kSum:
          if (!uses_avx2) {
            __ movaps(tmp, src);
            __ movaps(dst, src);
            __ punpckhqdq(tmp, tmp);
            __ paddq(dst, tmp);
          } else {
            __ vmovaps(tmp, src);
            __ vmovaps(dst, src);
            __ vpermpd(tmp, tmp, Immediate(0x4E));
            __ vpaddq(dst, dst, tmp);
            __ vmovaps(tmp, dst);
            __ vpermpd(tmp, tmp, Immediate(0xB1));
            __ vpaddq(dst, dst, tmp);
          }
          break;
        case HVecReduce::kMin:
        case HVecReduce::kMax:
          LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      }
      break;
    }
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecCnv(HVecCnv* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecCnv(HVecCnv* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();
  DataType::Type from = instruction->GetInputType();
  DataType::Type to = instruction->GetResultType();

  CheckVectorization(codegen_, instruction, dst);

  if (from == DataType::Type::kInt32 && to == DataType::Type::kFloat32) {
    __ cvtdq2ps(dst, src);
  } else {
    LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
  }
}

void LocationsBuilderX86_64::VisitVecNeg(HVecNeg* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecNeg(HVecNeg* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ pxor(dst, dst);
      __ psubb(dst, src);
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ pxor(dst, dst);
      __ psubw(dst, src);
      break;
    case DataType::Type::kInt32:
      __ pxor(dst, dst);
      __ psubd(dst, src);
      break;
    case DataType::Type::kInt64:
      __ pxor(dst, dst);
      __ psubq(dst, src);
      break;
    case DataType::Type::kFloat32:
      __ xorps(dst, dst);
      __ subps(dst, src);
      break;
    case DataType::Type::kFloat64:
      __ xorpd(dst, dst);
      __ subpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecAbs(HVecAbs* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
  // Integral-abs requires a temporary for the comparison.
  if (instruction->GetPackedType() == DataType::Type::kInt64) {
    instruction->GetLocations()->AddTemp(Location::RequiresFpuRegister());
  }
}

void InstructionCodeGeneratorX86_64::VisitVecAbs(HVecAbs* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kInt8:
      __ pabsb(dst, src);
      break;
    case DataType::Type::kInt16:
      __ pabsw(dst, src);
      break;
    case DataType::Type::kInt32: {
      __ pabsd(dst, src);
      break;
    }
    case DataType::Type::kInt64: {
      XmmRegister tmp = locations->GetTemp(0).AsFPVectorRegister<XmmRegister>();
      __ movaps(dst, src);
      __ pxor(tmp, tmp);
      __ pcmpgtq(tmp, dst);
      __ pxor(dst, tmp);
      __ psubq(dst, tmp);
    } break;
    case DataType::Type::kFloat32:
      __ pcmpeqb(dst, dst);  // all ones
      __ psrld(dst, Immediate(1));
      __ andps(dst, src);
      break;
    case DataType::Type::kFloat64:
      __ pcmpeqb(dst, dst);  // all ones
      __ psrlq(dst, Immediate(1));
      __ andpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecNot(HVecNot* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
  // Boolean-not requires a temporary to construct the 16 x one.
  if (instruction->GetPackedType() == DataType::Type::kBool) {
    instruction->GetLocations()->AddTemp(Location::RequiresFpuRegister());
  }
}

void InstructionCodeGeneratorX86_64::VisitVecNot(HVecNot* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool: {  // special case boolean-not
      XmmRegister tmp = locations->GetTemp(0).AsFPVectorRegister<XmmRegister>();
      __ pxor(dst, dst);
      __ pcmpeqb(tmp, tmp);  // all ones
      __ psubb(dst, tmp);    // 16 x one
      __ pxor(dst, src);
      break;
    }
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      __ pcmpeqb(dst, dst);  // all ones
      __ pxor(dst, src);
      break;
    case DataType::Type::kFloat32:
      __ pcmpeqb(dst, dst);  // all ones
      __ xorps(dst, src);
      break;
    case DataType::Type::kFloat64:
      __ pcmpeqb(dst, dst);  // all ones
      __ xorpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

// Helper to set up locations for vector binary operations.
static void CreateVecBinOpLocations(ArenaAllocator* allocator, HVecBinaryOperation* instruction) {
  LocationSummary* locations = new (allocator) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetOut(Location::SameAsFirstInput());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecAdd(HVecAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecAdd(HVecAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister other_src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);
  DCHECK(other_src == dst);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ paddb(dst, src);
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ paddw(dst, src);
      break;
    case DataType::Type::kInt32:
      __ paddd(dst, src);
      break;
    case DataType::Type::kInt64:
      __ paddq(dst, src);
      break;
    case DataType::Type::kFloat32:
      __ addps(dst, src);
      break;
    case DataType::Type::kFloat64:
      __ addpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecSaturationAdd(HVecSaturationAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecSaturationAdd(HVecSaturationAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister other_src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister src = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);
  DCHECK(other_src == dst);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
      __ paddusb(dst, src);
      break;
    case DataType::Type::kInt8:
      __ paddsb(dst, src);
      break;
    case DataType::Type::kUint16:
      __ paddusw(dst, src);
      break;
    case DataType::Type::kInt16:
      __ paddsw(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister other_src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister src = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);
  DCHECK(other_src == dst);
  DCHECK(instruction->IsRounded());

  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
      __ pavgb(dst, src);
      break;
    case DataType::Type::kUint16:
      __ pavgw(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecSub(HVecSub* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecSub(HVecSub* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister other_src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);
  DCHECK(other_src == dst);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ psubb(dst, src);
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ psubw(dst, src);
      break;
    case DataType::Type::kInt32:
      __ psubd(dst, src);
      break;
    case DataType::Type::kInt64:
      __ psubq(dst, src);
      break;
    case DataType::Type::kFloat32:
      __ subps(dst, src);
      break;
    case DataType::Type::kFloat64:
      __ subpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecSaturationSub(HVecSaturationSub* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecSaturationSub(HVecSaturationSub* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister other_src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister src = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);
  DCHECK(other_src == dst);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
      __ psubusb(dst, src);
      break;
    case DataType::Type::kInt8:
      __ psubsb(dst, src);
      break;
    case DataType::Type::kUint16:
      __ psubusw(dst, src);
      break;
    case DataType::Type::kInt16:
      __ psubsw(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecMul(HVecMul* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecMul(HVecMul* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister other_src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);
  DCHECK(other_src == dst);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ pmullw(dst, src);
      break;
    case DataType::Type::kInt32:
      __ pmulld(dst, src);
      break;
    case DataType::Type::kFloat32:
      __ mulps(dst, src);
      break;
    case DataType::Type::kFloat64:
      __ mulpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecDiv(HVecDiv* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecDiv(HVecDiv* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister other_src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);
  DCHECK(other_src == dst);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kFloat32:
      __ divps(dst, src);
      break;
    case DataType::Type::kFloat64:
      __ divpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecMin(HVecMin* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecMin(HVecMin* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  //   DCHECK(locations->InAt(0).Equals(locations->Out()));
  XmmRegister other_src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister src = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);
  DCHECK(other_src == dst);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
      __ pminub(dst, src);
      break;
    case DataType::Type::kInt8:
      __ pminsb(dst, src);
      break;
    case DataType::Type::kUint16:
      __ pminuw(dst, src);
      break;
    case DataType::Type::kInt16:
      __ pminsw(dst, src);
      break;
    case DataType::Type::kUint32:
      __ pminud(dst, src);
      break;
    case DataType::Type::kInt32:
      __ pminsd(dst, src);
      break;
    // Next cases are sloppy wrt 0.0 vs -0.0.
    case DataType::Type::kFloat32:
      __ minps(dst, src);
      break;
    case DataType::Type::kFloat64:
      __ minpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecMax(HVecMax* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecMax(HVecMax* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  //   DCHECK(locations->InAt(0).Equals(locations->Out()));
  XmmRegister other_src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister src = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);
  DCHECK(other_src == dst);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
      __ pmaxub(dst, src);
      break;
    case DataType::Type::kInt8:
      __ pmaxsb(dst, src);
      break;
    case DataType::Type::kUint16:
      __ pmaxuw(dst, src);
      break;
    case DataType::Type::kInt16:
      __ pmaxsw(dst, src);
      break;
    case DataType::Type::kUint32:
      __ pmaxud(dst, src);
      break;
    case DataType::Type::kInt32:
      __ pmaxsd(dst, src);
      break;
    // Next cases are sloppy wrt 0.0 vs -0.0.
    case DataType::Type::kFloat32:
      __ maxps(dst, src);
      break;
    case DataType::Type::kFloat64:
      __ maxpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecAnd(HVecAnd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecAnd(HVecAnd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister other_src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister src = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);
  DCHECK(other_src == dst);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      __ pand(dst, src);
      break;
    case DataType::Type::kFloat32:
      __ andps(dst, src);
      break;
    case DataType::Type::kFloat64:
      __ andpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecAndNot(HVecAndNot* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecAndNot(HVecAndNot* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister other_src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister src = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);
  DCHECK(other_src == dst);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      __ pandn(dst, src);
      break;
    case DataType::Type::kFloat32:
      __ andnps(dst, src);
      break;
    case DataType::Type::kFloat64:
      __ andnpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecOr(HVecOr* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecOr(HVecOr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister other_src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister src = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);
  DCHECK(other_src == dst);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      __ por(dst, src);
      break;
    case DataType::Type::kFloat32:
      __ orps(dst, src);
      break;
    case DataType::Type::kFloat64:
      __ orpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecXor(HVecXor* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecXor(HVecXor* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister other_src = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister src = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);
  DCHECK(other_src == dst);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      __ pxor(dst, src);
      break;
    case DataType::Type::kFloat32:
      __ xorps(dst, src);
      break;
    case DataType::Type::kFloat64:
      __ xorpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

// Helper to set up locations for vector shift operations.
static void CreateVecShiftLocations(ArenaAllocator* allocator, HVecBinaryOperation* instruction) {
  LocationSummary* locations = new (allocator) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::ConstantLocation(instruction->InputAt(1)));
      locations->SetOut(Location::SameAsFirstInput());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecShl(HVecShl* instruction) {
  CreateVecShiftLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecShl(HVecShl* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ psllw(dst, Immediate(static_cast<int8_t>(value)));
      break;
    case DataType::Type::kInt32:
      __ pslld(dst, Immediate(static_cast<int8_t>(value)));
      break;
    case DataType::Type::kInt64:
      __ psllq(dst, Immediate(static_cast<int8_t>(value)));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecShr(HVecShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecShr(HVecShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ psraw(dst, Immediate(static_cast<int8_t>(value)));
      break;
    case DataType::Type::kInt32:
      __ psrad(dst, Immediate(static_cast<int8_t>(value)));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecUShr(HVecUShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecUShr(HVecUShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, dst);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ psrlw(dst, Immediate(static_cast<int8_t>(value)));
      break;
    case DataType::Type::kInt32:
      __ psrld(dst, Immediate(static_cast<int8_t>(value)));
      break;
    case DataType::Type::kInt64:
      __ psrlq(dst, Immediate(static_cast<int8_t>(value)));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecSetScalars(HVecSetScalars* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);

  DCHECK_EQ(1u, instruction->InputCount());  // only one input currently implemented

  HInstruction* input = instruction->InputAt(0);
  bool is_zero = IsZeroBitPattern(input);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      locations->SetInAt(0, is_zero ? Location::ConstantLocation(input)
                                    : Location::RequiresRegister());
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, is_zero ? Location::ConstantLocation(input)
                                    : Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorX86_64::VisitVecSetScalars(HVecSetScalars* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister dst = locations->Out().AsFPVectorRegister<XmmRegister>();

  DCHECK_EQ(1u, instruction->InputCount());  // only one input currently implemented

  CheckVectorization(codegen_, instruction, dst);
  // Zero out all other elements first.
  __ xorps(dst, dst);

  // Shorthand for any type of zero.
  if (IsZeroBitPattern(instruction->InputAt(0))) {
    return;
  }

  // Set required elements.
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:  // TODO: up to here, and?
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
    case DataType::Type::kInt32:
      __ movd(dst, locations->InAt(0).AsRegister<CpuRegister>());
      break;
    case DataType::Type::kInt64:
      __ movd(dst, locations->InAt(0).AsRegister<CpuRegister>());  // is 64-bit
      break;
    case DataType::Type::kFloat32:
      __ movss(dst, locations->InAt(0).AsFPVectorRegister<XmmRegister>());
      break;
    case DataType::Type::kFloat64:
      __ movsd(dst, locations->InAt(0).AsFPVectorRegister<XmmRegister>());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

// Helper to set up locations for vector accumulations.
static void CreateVecAccumLocations(ArenaAllocator* allocator, HVecOperation* instruction) {
  LocationSummary* locations = new (allocator) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetInAt(2, Location::RequiresFpuRegister());
      locations->SetOut(Location::SameAsFirstInput());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instruction) {
  CreateVecAccumLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instruction) {
  // TODO: pmaddwd?
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderX86_64::VisitVecSADAccumulate(HVecSADAccumulate* instruction) {
  CreateVecAccumLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorX86_64::VisitVecSADAccumulate(HVecSADAccumulate* instruction) {
  // TODO: psadbw for unsigned?
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderX86_64::VisitVecDotProd(HVecDotProd* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetInAt(1, Location::RequiresFpuRegister());
  locations->SetInAt(2, Location::RequiresFpuRegister());
  locations->SetOut(Location::SameAsFirstInput());
  locations->AddTemp(Location::RequiresFpuRegister());
}

void InstructionCodeGeneratorX86_64::VisitVecDotProd(HVecDotProd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister acc = locations->InAt(0).AsFPVectorRegister<XmmRegister>();
  XmmRegister left = locations->InAt(1).AsFPVectorRegister<XmmRegister>();
  XmmRegister right = locations->InAt(2).AsFPVectorRegister<XmmRegister>();

  bool uses_avx2 = false;
  CheckVectorization(codegen_, instruction, acc, &uses_avx2);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kInt32: {
      XmmRegister tmp = locations->GetTemp(0).AsFPVectorRegister<XmmRegister>();
      if (!uses_avx2) {
        __ movaps(tmp, right);
        __ pmaddwd(tmp, left);
        __ paddd(acc, tmp);
      } else {
        __ vpmaddwd(tmp, left, right);
        __ vpaddd(acc, acc, tmp);
      }
      break;
    }
    default:
      LOG(FATAL) << "Unsupported SIMD Type" << instruction->GetPackedType();
      UNREACHABLE();
  }
}

// Helper to set up locations for vector memory operations.
static void CreateVecMemLocations(ArenaAllocator* allocator,
                                  HVecMemoryOperation* instruction,
                                  bool is_load) {
  LocationSummary* locations = new (allocator) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(instruction->InputAt(1)));
      if (is_load) {
        locations->SetOut(Location::RequiresFpuRegister());
      } else {
        locations->SetInAt(2, Location::RequiresFpuRegister());
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

// Helper to construct address for vector memory operations.
static Address VecAddress(LocationSummary* locations, size_t size, bool is_string_char_at) {
  Location base = locations->InAt(0);
  Location index = locations->InAt(1);
  ScaleFactor scale = TIMES_1;
  switch (size) {
    case 2: scale = TIMES_2; break;
    case 4: scale = TIMES_4; break;
    case 8: scale = TIMES_8; break;
    default: break;
  }
  // Incorporate the string or array offset in the address computation.
  uint32_t offset = is_string_char_at
      ? mirror::String::ValueOffset().Uint32Value()
      : mirror::Array::DataOffset(size).Uint32Value();
  return CodeGeneratorX86_64::ArrayAddress(base.AsRegister<CpuRegister>(), index, scale, offset);
}

void LocationsBuilderX86_64::VisitVecLoad(HVecLoad* instruction) {
  CreateVecMemLocations(GetGraph()->GetAllocator(), instruction, /*is_load*/ true);
  // String load requires a temporary for the compressed load.
  if (mirror::kUseStringCompression && instruction->IsStringCharAt()) {
    instruction->GetLocations()->AddTemp(Location::RequiresFpuRegister());
  }
}

void InstructionCodeGeneratorX86_64::VisitVecLoad(HVecLoad* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  size_t size = DataType::Size(instruction->GetPackedType());
  Address address = VecAddress(locations, size, instruction->IsStringCharAt());
  XmmRegister reg = locations->Out().AsFPVectorRegister<XmmRegister>();
  bool uses_avx2 = false;

  CheckVectorization(codegen_, instruction, reg, &uses_avx2);

  bool is_aligned = instruction->GetAlignment().IsAlignedAt(reg.IsYMM() ? 32 : 16);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kInt16:  // (short) s.charAt(.) can yield HVecLoad/Int16/StringCharAt.
    case DataType::Type::kUint16:
      // Special handling of compressed/uncompressed string load.
      if (mirror::kUseStringCompression && instruction->IsStringCharAt()) {
        NearLabel done, not_compressed;
        XmmRegister tmp = locations->GetTemp(0).AsFPVectorRegister<XmmRegister>();
        // Test compression bit.
        static_assert(static_cast<uint32_t>(mirror::StringCompressionFlag::kCompressed) == 0u,
                      "Expecting 0=compressed, 1=uncompressed");
        uint32_t count_offset = mirror::String::CountOffset().Uint32Value();
        __ testb(Address(locations->InAt(0).AsRegister<CpuRegister>(), count_offset), Immediate(1));
        __ j(kNotZero, &not_compressed);
        // Zero extend 8 compressed bytes into 8 chars.
        if (!uses_avx2) {
          __ movsd(reg, VecAddress(locations, 1, instruction->IsStringCharAt()));
        } else {
          __ movdqu(reg, VecAddress(locations, 1, instruction->IsStringCharAt()));
          // Permute to 0213, so that we can operate on the low quad words
          __ vpermpd(reg, reg, Immediate(0xd8));
        }
        __ pxor(tmp, tmp);
        __ punpcklbw(reg, tmp);
        __ jmp(&done);
        // Load 8 direct uncompressed chars.
        __ Bind(&not_compressed);
        is_aligned ? __ movdqa(reg, address) : __ movdqu(reg, address);
        __ Bind(&done);
        return;
      }
      FALLTHROUGH_INTENDED;
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      is_aligned ? __ movdqa(reg, address) : __ movdqu(reg, address);
      break;
    case DataType::Type::kFloat32:
      is_aligned ? __ movaps(reg, address) : __ movups(reg, address);
      break;
    case DataType::Type::kFloat64:
      is_aligned ? __ movapd(reg, address) : __ movupd(reg, address);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecStore(HVecStore* instruction) {
  CreateVecMemLocations(GetGraph()->GetAllocator(), instruction, /*is_load*/ false);
}

void InstructionCodeGeneratorX86_64::VisitVecStore(HVecStore* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  size_t size = DataType::Size(instruction->GetPackedType());
  Address address = VecAddress(locations, size, /*is_string_char_at*/ false);
  XmmRegister reg = locations->InAt(2).AsFPVectorRegister<XmmRegister>();

  CheckVectorization(codegen_, instruction, reg);

  bool is_aligned = instruction->GetAlignment().IsAlignedAt(reg.IsYMM() ? 32 : 16);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      is_aligned ? __ movdqa(address, reg) : __ movdqu(address, reg);
      break;
    case DataType::Type::kFloat32:
      is_aligned ? __ movaps(address, reg) : __ movups(address, reg);
      break;
    case DataType::Type::kFloat64:
      is_aligned ? __ movapd(address, reg) : __ movupd(address, reg);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderX86_64::VisitVecPredSetAll(HVecPredSetAll* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

void InstructionCodeGeneratorX86_64::VisitVecPredSetAll(HVecPredSetAll* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

void LocationsBuilderX86_64::VisitVecPredWhile(HVecPredWhile* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

void InstructionCodeGeneratorX86_64::VisitVecPredWhile(HVecPredWhile* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

void LocationsBuilderX86_64::VisitVecPredToBoolean(HVecPredToBoolean* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

void InstructionCodeGeneratorX86_64::VisitVecPredToBoolean(HVecPredToBoolean* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

void LocationsBuilderX86_64::VisitVecCondition(HVecCondition* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

void InstructionCodeGeneratorX86_64::VisitVecCondition(HVecCondition* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

void LocationsBuilderX86_64::VisitVecPredNot(HVecPredNot* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

void InstructionCodeGeneratorX86_64::VisitVecPredNot(HVecPredNot* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

void LocationsBuilderX86_64::VisitX86Clear(HX86Clear* clear) { clear->SetLocations(nullptr); }

void InstructionCodeGeneratorX86_64::VisitX86Clear(HX86Clear* clear ATTRIBUTE_UNUSED) {
  __ vzeroupper();
}

#undef __

}  // namespace x86_64
}  // namespace art
