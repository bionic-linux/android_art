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

#include "code_generator_arm64.h"
#include "mirror/array-inl.h"

using namespace vixl::aarch64;  // NOLINT(build/namespaces)

namespace art {
namespace arm64 {

using helpers::DRegisterFrom;
using helpers::HeapOperand;
using helpers::InputRegisterAt;
using helpers::Int64ConstantFrom;
using helpers::XRegisterFrom;

#define __ GetVIXLAssembler()->

void LocationsBuilderARM64::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    case Primitive::kPrimFloat:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorARM64::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Dup(dst.V16B(), InputRegisterAt(instruction, 0));
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Dup(dst.V8H(), InputRegisterAt(instruction, 0));
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Dup(dst.V4S(), InputRegisterAt(instruction, 0));
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Dup(dst.V4S(), DRegisterFrom(locations->InAt(0)).V4S(), 0);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecSetScalars(HVecSetScalars* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorARM64::VisitVecSetScalars(HVecSetScalars* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARM64::VisitVecSumReduce(HVecSumReduce* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorARM64::VisitVecSumReduce(HVecSumReduce* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

// Helper to set up locations for vector unary operations.
static void CreateVecUnOpLocations(ArenaAllocator* arena, HVecUnaryOperation* instruction) {
  LocationSummary* locations = new (arena) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(),
                        instruction->IsVecNot() ? Location::kOutputOverlap
                                                : Location::kNoOutputOverlap);
      break;
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecCnv(HVecCnv* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecCnv(HVecCnv* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister src = DRegisterFrom(locations->InAt(0));
  FPRegister dst = DRegisterFrom(locations->Out());
  Primitive::Type from = instruction->GetInputType();
  Primitive::Type to = instruction->GetResultType();
  if (from == Primitive::kPrimInt && to == Primitive::kPrimFloat) {
    DCHECK_EQ(4u, instruction->GetVectorLength());
    __ Scvtf(dst.V4S(), src.V4S());
  } else {
    LOG(FATAL) << "Unsupported SIMD type";
  }
}

void LocationsBuilderARM64::VisitVecNeg(HVecNeg* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecNeg(HVecNeg* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister src = DRegisterFrom(locations->InAt(0));
  FPRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Neg(dst.V16B(), src.V16B());
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Neg(dst.V8H(), src.V8H());
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Neg(dst.V4S(), src.V4S());
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fneg(dst.V4S(), src.V4S());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecAbs(HVecAbs* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecAbs(HVecAbs* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister src = DRegisterFrom(locations->InAt(0));
  FPRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Abs(dst.V16B(), src.V16B());
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Abs(dst.V8H(), src.V8H());
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Abs(dst.V4S(), src.V4S());
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fabs(dst.V4S(), src.V4S());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
  }
}

void LocationsBuilderARM64::VisitVecNot(HVecNot* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecNot(HVecNot* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister src = DRegisterFrom(locations->InAt(0));
  FPRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:  // special case boolean-not
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Movi(dst.V16B(), 1);
      __ Eor(dst.V16B(), dst.V16B(), src.V16B());
      break;
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
      __ Not(dst.V16B(), src.V16B());  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

// Helper to set up locations for vector binary operations.
static void CreateVecBinOpLocations(ArenaAllocator* arena, HVecBinaryOperation* instruction) {
  LocationSummary* locations = new (arena) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecAdd(HVecAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecAdd(HVecAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister lhs = DRegisterFrom(locations->InAt(0));
  FPRegister rhs = DRegisterFrom(locations->InAt(1));
  FPRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Add(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Add(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Add(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fadd(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecSub(HVecSub* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecSub(HVecSub* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister lhs = DRegisterFrom(locations->InAt(0));
  FPRegister rhs = DRegisterFrom(locations->InAt(1));
  FPRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Sub(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Sub(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Sub(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fsub(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecMul(HVecMul* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecMul(HVecMul* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister lhs = DRegisterFrom(locations->InAt(0));
  FPRegister rhs = DRegisterFrom(locations->InAt(1));
  FPRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Mul(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Mul(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Mul(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fmul(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecDiv(HVecDiv* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecDiv(HVecDiv* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister lhs = DRegisterFrom(locations->InAt(0));
  FPRegister rhs = DRegisterFrom(locations->InAt(1));
  FPRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fdiv(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecAnd(HVecAnd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecAnd(HVecAnd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister lhs = DRegisterFrom(locations->InAt(0));
  FPRegister rhs = DRegisterFrom(locations->InAt(1));
  FPRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
      __ And(dst.V16B(), lhs.V16B(), rhs.V16B());  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecAndNot(HVecAndNot* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
}

void InstructionCodeGeneratorARM64::VisitVecAndNot(HVecAndNot* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
}

void LocationsBuilderARM64::VisitVecOr(HVecOr* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecOr(HVecOr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister lhs = DRegisterFrom(locations->InAt(0));
  FPRegister rhs = DRegisterFrom(locations->InAt(1));
  FPRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
      __ Orr(dst.V16B(), lhs.V16B(), rhs.V16B());  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecXor(HVecXor* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecXor(HVecXor* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister lhs = DRegisterFrom(locations->InAt(0));
  FPRegister rhs = DRegisterFrom(locations->InAt(1));
  FPRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
      __ Eor(dst.V16B(), lhs.V16B(), rhs.V16B());  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

// Helper to set up locations for vector shift operations.
static void CreateVecShiftLocations(ArenaAllocator* arena, HVecBinaryOperation* instruction) {
  LocationSummary* locations = new (arena) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::ConstantLocation(instruction->InputAt(1)->AsConstant()));
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecShl(HVecShl* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecShl(HVecShl* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister lhs = DRegisterFrom(locations->InAt(0));
  FPRegister dst = DRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Shl(dst.V16B(), lhs.V16B(), value);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Shl(dst.V8H(), lhs.V8H(), value);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Shl(dst.V4S(), lhs.V4S(), value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecShr(HVecShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecShr(HVecShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister lhs = DRegisterFrom(locations->InAt(0));
  FPRegister dst = DRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Sshr(dst.V16B(), lhs.V16B(), value);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Sshr(dst.V8H(), lhs.V8H(), value);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Sshr(dst.V4S(), lhs.V4S(), value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecUShr(HVecUShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecUShr(HVecUShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  FPRegister lhs = DRegisterFrom(locations->InAt(0));
  FPRegister dst = DRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Ushr(dst.V16B(), lhs.V16B(), value);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Ushr(dst.V8H(), lhs.V8H(), value);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Ushr(dst.V4S(), lhs.V4S(), value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

// Helper to set up locations for vector memory operations.
static void CreateVecMemLocations(ArenaAllocator* arena,
                                  HVecMemoryOperation* instruction,
                                  bool is_load) {
  LocationSummary* locations = new (arena) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(instruction->InputAt(1)));
      if (is_load) {
        locations->SetOut(Location::RequiresFpuRegister());
      } else {
        locations->SetInAt(2, Location::RequiresFpuRegister());
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

// Helper to set up registers and address for vector memory operations.
MemOperand InstructionCodeGeneratorARM64::CreateVecMemRegisters(
    HVecMemoryOperation* instruction,
    Location* reg_loc,
    bool is_load) {
  LocationSummary* locations = instruction->GetLocations();
  Register base = InputRegisterAt(instruction, 0);
  Location index = locations->InAt(1);
  *reg_loc = is_load ? locations->Out() : locations->InAt(2);

  Primitive::Type packed_type = instruction->GetPackedType();
  uint32_t offset = mirror::Array::DataOffset(Primitive::ComponentSize(packed_type)).Uint32Value();
  size_t shift = Primitive::ComponentSizeShift(packed_type);

  UseScratchRegisterScope temps(GetVIXLAssembler());
  Register temp = temps.AcquireSameSizeAs(base);
  if (index.IsConstant()) {
    offset += Int64ConstantFrom(index) << shift;
    __ Add(temp, base, offset);
  } else {
    if (instruction->InputAt(0)->IsIntermediateAddress()) {
      temp = base;
    } else {
      __ Add(temp, base, offset);
    }
    __ Add(temp.X(), temp.X(), Operand(XRegisterFrom(index), LSL, shift));
  }
  return HeapOperand(temp);
}

void LocationsBuilderARM64::VisitVecLoad(HVecLoad* instruction) {
  CreateVecMemLocations(GetGraph()->GetArena(), instruction, /*is_load*/ true);
}

void InstructionCodeGeneratorARM64::VisitVecLoad(HVecLoad* instruction) {
  Location reg_loc = Location::NoLocation();
  MemOperand mem = CreateVecMemRegisters(instruction, &reg_loc, /*is_load*/ true);
  FPRegister reg = DRegisterFrom(reg_loc);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Ld1(reg.V16B(), mem);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Ld1(reg.V8H(), mem);
      break;
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Ld1(reg.V4S(), mem);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecStore(HVecStore* instruction) {
  CreateVecMemLocations(GetGraph()->GetArena(), instruction, /*is_load*/ false);
}

void InstructionCodeGeneratorARM64::VisitVecStore(HVecStore* instruction) {
  Location reg_loc = Location::NoLocation();
  MemOperand mem = CreateVecMemRegisters(instruction, &reg_loc, /*is_load*/ false);
  FPRegister reg = DRegisterFrom(reg_loc);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ St1(reg.V16B(), mem);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ St1(reg.V8H(), mem);
      break;
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ St1(reg.V4S(), mem);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

#undef __

}  // namespace arm64
}  // namespace art
