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

#ifndef ART_COMPILER_OPTIMIZING_NODES_VECTOR_H_
#define ART_COMPILER_OPTIMIZING_NODES_VECTOR_H_

// This #include should never be used by compilation, because this header file (nodes_vector.h)
// is included in the header file nodes.h itself. However it gives editing tools better context.
#include "nodes.h"

namespace art {

// Memory alignment, represented as an offset relative to a base, where 0 <= offset < base,
// and base is a power of two. For example, the value Alignment(16, 0) means memory is
// perfectly aligned at a 16-byte boundary, whereas the value Alignment(16, 4) means
// memory is always exactly 4 bytes above such a boundary.
class Alignment {
 public:
  Alignment(size_t base, size_t offset) : base_(base), offset_(offset) {
    DCHECK_LT(offset, base);
    DCHECK(IsPowerOfTwo(base));
  }

  // Returns true if memory is "at least" aligned at the given boundary.
  // Assumes requested base is power of two.
  bool IsAlignedAt(size_t base) const {
    DCHECK_NE(0u, base);
    DCHECK(IsPowerOfTwo(base));
    return ((offset_ | base_) & (base - 1u)) == 0;
  }

  std::string ToString() const {
    return "ALIGN(" + std::to_string(base_) + "," + std::to_string(offset_) + ")";
  }

 private:
  size_t base_;
  size_t offset_;
};

//
// Definitions of abstract vector operations in HIR.
//

// Abstraction of a vector operation, i.e., an operation that performs
// GetVectorLength() x GetPackedType() operations simultaneously.
class HVecOperation : public HVariableInputSizeInstruction {
 public:
  HVecOperation(ArenaAllocator* arena,
                Primitive::Type packed_type,
                SideEffects side_effects,
                size_t number_of_inputs,
                size_t vector_length,
                uint32_t dex_pc)
      : HVariableInputSizeInstruction(side_effects,
                                      dex_pc,
                                      arena,
                                      number_of_inputs,
                                      kArenaAllocVectorNode),
        vector_length_(vector_length) {
    SetPackedField<TypeField>(packed_type);
    DCHECK_LT(1u, vector_length);
  }

  // Returns the number of elements packed in a vector.
  size_t GetVectorLength() const {
    return vector_length_;
  }

  // Returns the number of bytes in a full vector.
  size_t GetVectorNumberOfBytes() const {
    return vector_length_ * Primitive::ComponentSize(GetPackedType());
  }

  // Returns the type of the vector operation: a SIMD operation looks like a FPU location.
  // TODO: we could introduce SIMD types in HIR.
  Primitive::Type GetType() const OVERRIDE {
    return Primitive::kPrimDouble;
  }

  // Returns the true component type packed in a vector.
  Primitive::Type GetPackedType() const {
    return GetPackedField<TypeField>();
  }

  DECLARE_ABSTRACT_INSTRUCTION(VecOperation);

 private:
  // Additional packed bits.
  static constexpr size_t kFieldType = HInstruction::kNumberOfGenericPackedBits;
  static constexpr size_t kFieldTypeSize =
      MinimumBitsToStore(static_cast<size_t>(Primitive::kPrimLast));
  static constexpr size_t kNumberOfVectorOpPackedBits = kFieldType + kFieldTypeSize;
  static_assert(kNumberOfVectorOpPackedBits <= kMaxNumberOfPackedBits, "Too many packed fields.");
  using TypeField = BitField<Primitive::Type, kFieldType, kFieldTypeSize>;

  const size_t vector_length_;

  DISALLOW_COPY_AND_ASSIGN(HVecOperation);
};

// Abstraction of a unary vector operation.
class HVecUnaryOperation : public HVecOperation {
 public:
  HVecUnaryOperation(ArenaAllocator* arena,
                     Primitive::Type packed_type,
                     size_t vector_length,
                     uint32_t dex_pc)
      : HVecOperation(arena,
                      packed_type,
                      SideEffects::None(),
                      /*number_of_inputs*/ 1,
                      vector_length,
                      dex_pc) { }
  DECLARE_ABSTRACT_INSTRUCTION(VecUnaryOperation);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecUnaryOperation);
};

// Abstraction of a binary vector operation.
class HVecBinaryOperation : public HVecOperation {
 public:
  HVecBinaryOperation(ArenaAllocator* arena,
                      Primitive::Type packed_type,
                      size_t vector_length,
                      uint32_t dex_pc)
      : HVecOperation(arena,
                      packed_type,
                      SideEffects::None(),
                      /*number_of_inputs*/ 2,
                      vector_length,
                      dex_pc) { }
  DECLARE_ABSTRACT_INSTRUCTION(VecBinaryOperation);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecBinaryOperation);
};

// Abstraction of a vector operation that references memory, with an alignment.
// The Android runtime guarantees at least "component size" alignment for array
// elements and, thus, vectors.
class HVecMemoryOperation : public HVecOperation {
 public:
  HVecMemoryOperation(ArenaAllocator* arena,
                      Primitive::Type packed_type,
                      SideEffects side_effects,
                      size_t number_of_inputs,
                      size_t vector_length,
                      uint32_t dex_pc)
      : HVecOperation(arena, packed_type, side_effects, number_of_inputs, vector_length, dex_pc),
        alignment_(Primitive::ComponentSize(packed_type), 0) { }

  void SetAlignment(Alignment alignment) { alignment_ = alignment; }

  Alignment GetAlignment() const { return alignment_; }

  DECLARE_ABSTRACT_INSTRUCTION(VecMemoryOperation);

 private:
  Alignment alignment_;

  DISALLOW_COPY_AND_ASSIGN(HVecMemoryOperation);
};

//
// Definitions of concrete vector operations in HIR.
//

// Replicates the given scalar into a vector,
// viz. replicate(x) = [ x, .. , x ].
class HVecReplicateScalar FINAL : public HVecUnaryOperation {
 public:
  HVecReplicateScalar(ArenaAllocator* arena,
                      HInstruction* scalar,
                      Primitive::Type packed_type,
                      size_t vector_length,
                      uint32_t dex_pc = kNoDexPc)
      : HVecUnaryOperation(arena, packed_type, vector_length, dex_pc) {
    SetRawInputAt(0, scalar);
  }
  DECLARE_INSTRUCTION(VecReplicateScalar);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecReplicateScalar);
};

// Assigns the given scalar elements to a vector,
// viz. set( array(x1, .., xn) ) = [ x1, .. , xn ].
class HVecSetScalars FINAL : public HVecUnaryOperation {
  HVecSetScalars(ArenaAllocator* arena,
                 HInstruction** scalars,  // array
                 Primitive::Type packed_type,
                 size_t vector_length,
                 uint32_t dex_pc = kNoDexPc)
      : HVecUnaryOperation(arena, packed_type, vector_length, dex_pc) {
    for (size_t i = 0; i < vector_length; i++) {
      SetRawInputAt(0, scalars[i]);
    }
  }
  DECLARE_INSTRUCTION(VecSetScalars);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecSetScalars);
};

// Sum-reduces the given vector into a shorter vector (m < n) or scalar (m = 1),
// viz. sum-reduce[ x1, .. , xn ] = [ y1, .., ym ], where yi = sum_j x_j.
class HVecSumReduce FINAL : public HVecUnaryOperation {
  HVecSumReduce(ArenaAllocator* arena,
                HInstruction* input,
                Primitive::Type packed_type,
                size_t vector_length,
                uint32_t dex_pc = kNoDexPc)
      : HVecUnaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(input->IsVecOperation());
    DCHECK_EQ(input->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, input);
  }

  // TODO: probably integral promotion
  Primitive::Type GetType() const OVERRIDE { return GetPackedType(); }

  DECLARE_INSTRUCTION(VecSumReduce);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecSumReduce);
};

// Converts every component in the vector,
// viz. cnv[ x1, .. , xn ]  = [ cnv(x1), .. , cnv(xn) ].
class HVecCnv FINAL : public HVecUnaryOperation {
 public:
  HVecCnv(ArenaAllocator* arena,
          HInstruction* input,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecUnaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(input->IsVecOperation());
    DCHECK_NE(input->AsVecOperation()->GetPackedType(), packed_type);  // actual convert
    SetRawInputAt(0, input);
  }

  Primitive::Type GetInputType() const { return InputAt(0)->AsVecOperation()->GetPackedType(); }
  Primitive::Type GetResultType() const { return GetPackedType(); }

  DECLARE_INSTRUCTION(VecCnv);

 private:
  DISALLOW_COPY_AND_ASSIGN(HVecCnv);
};

// Negates every component in the vector,
// viz. neg[ x1, .. , xn ]  = [ -x1, .. , -xn ].
class HVecNeg FINAL : public HVecUnaryOperation {
 public:
  HVecNeg(ArenaAllocator* arena,
          HInstruction* input,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecUnaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(input->IsVecOperation());
    DCHECK_EQ(input->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, input);
  }
  DECLARE_INSTRUCTION(VecNeg);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecNeg);
};

// Takes absolute value of every component in the vector,
// viz. abs[ x1, .. , xn ]  = [ |x1|, .. , |xn| ].
class HVecAbs FINAL : public HVecUnaryOperation {
 public:
  HVecAbs(ArenaAllocator* arena,
          HInstruction* input,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecUnaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(input->IsVecOperation());
    DCHECK_EQ(input->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, input);
  }
  DECLARE_INSTRUCTION(VecAbs);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecAbs);
};

// Bitwise- or boolean-nots every component in the vector,
// viz. not[ x1, .. , xn ]  = [ ~x1, .. , ~xn ], or
//      not[ x1, .. , xn ]  = [ !x1, .. , !xn ] for boolean.
class HVecNot FINAL : public HVecUnaryOperation {
 public:
  HVecNot(ArenaAllocator* arena,
          HInstruction* input,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecUnaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(input->IsVecOperation());
    SetRawInputAt(0, input);
  }
  DECLARE_INSTRUCTION(VecNot);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecNot);
};

// Adds every component in the two vectors,
// viz. [ x1, .. , xn ] + [ y1, .. , yn ] = [ x1 + y1, .. , xn + yn ].
class HVecAdd FINAL : public HVecBinaryOperation {
 public:
  HVecAdd(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
    DCHECK_EQ(left->AsVecOperation()->GetPackedType(), packed_type);
    DCHECK_EQ(right->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }
  DECLARE_INSTRUCTION(VecAdd);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecAdd);
};

// Performs halving add on every component in the two vectors, viz.
// rounded [ x1, .. , xn ] hradd [ y1, .. , yn ] = [ (x1 + y1 + 1) >> 1, .. , (xn + yn + 1) >> 1 ]
// or      [ x1, .. , xn ] hadd  [ y1, .. , yn ] = [ (x1 + y1)     >> 1, .. , (xn + yn )    >> 1 ]
// for signed operands x, y (sign extension) or unsigned operands x, y (zero extension).
class HVecHalvingAdd FINAL : public HVecBinaryOperation {
 public:
  HVecHalvingAdd(ArenaAllocator* arena,
                 HInstruction* left,
                 HInstruction* right,
                 Primitive::Type packed_type,
                 size_t vector_length,
                 bool is_unsigned,
                 bool is_rounded,
                 uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc),
        is_unsigned_(is_unsigned),
        is_rounded_(is_rounded) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
    DCHECK_EQ(left->AsVecOperation()->GetPackedType(), packed_type);
    DCHECK_EQ(right->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }

  bool IsUnsigned() const { return is_unsigned_; }
  bool IsRounded() const { return is_rounded_; }

  DECLARE_INSTRUCTION(VecHalvingAdd);

 private:
  bool is_unsigned_;
  bool is_rounded_;

  DISALLOW_COPY_AND_ASSIGN(HVecHalvingAdd);
};

// Subtracts every component in the two vectors,
// viz. [ x1, .. , xn ] - [ y1, .. , yn ] = [ x1 - y1, .. , xn - yn ].
class HVecSub FINAL : public HVecBinaryOperation {
 public:
  HVecSub(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
    DCHECK_EQ(left->AsVecOperation()->GetPackedType(), packed_type);
    DCHECK_EQ(right->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }
  DECLARE_INSTRUCTION(VecSub);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecSub);
};

// Multiplies every component in the two vectors,
// viz. [ x1, .. , xn ] * [ y1, .. , yn ] = [ x1 * y1, .. , xn * yn ].
class HVecMul FINAL : public HVecBinaryOperation {
 public:
  HVecMul(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
    DCHECK_EQ(left->AsVecOperation()->GetPackedType(), packed_type);
    DCHECK_EQ(right->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }
  DECLARE_INSTRUCTION(VecMul);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecMul);
};

// Divides every component in the two vectors,
// viz. [ x1, .. , xn ] / [ y1, .. , yn ] = [ x1 / y1, .. , xn / yn ].
class HVecDiv FINAL : public HVecBinaryOperation {
 public:
  HVecDiv(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
    DCHECK_EQ(left->AsVecOperation()->GetPackedType(), packed_type);
    DCHECK_EQ(right->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }
  DECLARE_INSTRUCTION(VecDiv);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecDiv);
};

// Takes minimum of every component in the two vectors,
// viz. MIN( [ x1, .. , xn ] , [ y1, .. , yn ]) = [ min(x1, y1), .. , min(xn, yn) ].
class HVecMin FINAL : public HVecBinaryOperation {
 public:
  HVecMin(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
    DCHECK_EQ(left->AsVecOperation()->GetPackedType(), packed_type);
    DCHECK_EQ(right->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }
  DECLARE_INSTRUCTION(VecMin);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecMin);
};

// Takes maximum of every component in the two vectors,
// viz. MAX( [ x1, .. , xn ] , [ y1, .. , yn ]) = [ max(x1, y1), .. , max(xn, yn) ].
class HVecMax FINAL : public HVecBinaryOperation {
 public:
  HVecMax(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
    DCHECK_EQ(left->AsVecOperation()->GetPackedType(), packed_type);
    DCHECK_EQ(right->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }
  DECLARE_INSTRUCTION(VecMax);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecMax);
};

// Bitwise-ands every component in the two vectors,
// viz. [ x1, .. , xn ] & [ y1, .. , yn ] = [ x1 & y1, .. , xn & yn ].
class HVecAnd FINAL : public HVecBinaryOperation {
 public:
  HVecAnd(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }
  DECLARE_INSTRUCTION(VecAnd);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecAnd);
};

// Bitwise-and-nots every component in the two vectors,
// viz. [ x1, .. , xn ] and-not [ y1, .. , yn ] = [ ~x1 & y1, .. , ~xn & yn ].
class HVecAndNot FINAL : public HVecBinaryOperation {
 public:
  HVecAndNot(ArenaAllocator* arena,
             HInstruction* left,
             HInstruction* right,
             Primitive::Type packed_type,
             size_t vector_length,
             uint32_t dex_pc = kNoDexPc)
         : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }
  DECLARE_INSTRUCTION(VecAndNot);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecAndNot);
};

// Bitwise-ors every component in the two vectors,
// viz. [ x1, .. , xn ] | [ y1, .. , yn ] = [ x1 | y1, .. , xn | yn ].
class HVecOr FINAL : public HVecBinaryOperation {
 public:
  HVecOr(ArenaAllocator* arena,
         HInstruction* left,
         HInstruction* right,
         Primitive::Type packed_type,
         size_t vector_length,
         uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }
  DECLARE_INSTRUCTION(VecOr);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecOr);
};

// Bitwise-xors every component in the two vectors,
// viz. [ x1, .. , xn ] ^ [ y1, .. , yn ] = [ x1 ^ y1, .. , xn ^ yn ].
class HVecXor FINAL : public HVecBinaryOperation {
 public:
  HVecXor(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }
  DECLARE_INSTRUCTION(VecXor);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecXor);
};

// Logically shifts every component in the vector left by the given distance,
// viz. [ x1, .. , xn ] << d = [ x1 << d, .. , xn << d ].
class HVecShl FINAL : public HVecBinaryOperation {
 public:
  HVecShl(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation());
    DCHECK_EQ(left->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }
  DECLARE_INSTRUCTION(VecShl);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecShl);
};

// Arithmetically shifts every component in the vector right by the given distance,
// viz. [ x1, .. , xn ] >> d = [ x1 >> d, .. , xn >> d ].
class HVecShr FINAL : public HVecBinaryOperation {
 public:
  HVecShr(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation());
    DCHECK_EQ(left->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }
  DECLARE_INSTRUCTION(VecShr);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecShr);
};

// Logically shifts every component in the vector right by the given distance,
// viz. [ x1, .. , xn ] >>> d = [ x1 >>> d, .. , xn >>> d ].
class HVecUShr FINAL : public HVecBinaryOperation {
 public:
  HVecUShr(ArenaAllocator* arena,
           HInstruction* left,
           HInstruction* right,
           Primitive::Type packed_type,
           size_t vector_length,
           uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation());
    DCHECK_EQ(left->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }
  DECLARE_INSTRUCTION(VecUShr);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecUShr);
};

// Loads a vector from memory, viz. load(mem, 1)
// yield the vector [ mem(1), .. , mem(n) ].
class HVecLoad FINAL : public HVecMemoryOperation {
 public:
  HVecLoad(ArenaAllocator* arena,
           HInstruction* base,
           HInstruction* index,
           Primitive::Type packed_type,
           size_t vector_length,
           uint32_t dex_pc = kNoDexPc)
      : HVecMemoryOperation(arena,
                            packed_type,
                            SideEffects::ArrayReadOfType(packed_type),
                            /*number_of_inputs*/ 2,
                            vector_length,
                            dex_pc) {
    SetRawInputAt(0, base);
    SetRawInputAt(1, index);
  }
  DECLARE_INSTRUCTION(VecLoad);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecLoad);
};

// Stores a vector to memory, viz. store(m, 1, [x1, .. , xn] )
// sets mem(1) = x1, .. , mem(n) = xn.
class HVecStore FINAL : public HVecMemoryOperation {
 public:
  HVecStore(ArenaAllocator* arena,
            HInstruction* base,
            HInstruction* index,
            HInstruction* value,
            Primitive::Type packed_type,
            size_t vector_length,
            uint32_t dex_pc = kNoDexPc)
      : HVecMemoryOperation(arena,
                            packed_type,
                            SideEffects::ArrayWriteOfType(packed_type),
                            /*number_of_inputs*/ 3,
                            vector_length,
                            dex_pc) {
    DCHECK(value->IsVecOperation());
    DCHECK_EQ(value->AsVecOperation()->GetPackedType(), packed_type);
    SetRawInputAt(0, base);
    SetRawInputAt(1, index);
    SetRawInputAt(2, value);
  }
  DECLARE_INSTRUCTION(VecStore);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecStore);
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_NODES_VECTOR_H_
