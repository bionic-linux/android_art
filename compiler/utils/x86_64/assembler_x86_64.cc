/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "assembler_x86_64.h"

#include "base/casts.h"
#include "base/memory_region.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "thread.h"

namespace art HIDDEN {
namespace x86_64 {

inline uint8_t GetEncodedVexLen(XmmRegister xmmReg) {
  return (xmmReg.IsYMM() ? SET_VEX_L_256 : SET_VEX_L_128);
}

std::ostream& operator<<(std::ostream& os, const CpuRegister& reg) {
  return os << reg.AsRegister();
}

std::ostream& operator<<(std::ostream& os, const XmmRegister& reg) {
  if (reg.IsYMM()) {
    return os << "ymm" << static_cast<int>(reg.AsFloatRegister());
  } else {
    return os << reg.AsFloatRegister();
  }
}

std::ostream& operator<<(std::ostream& os, const X87Register& reg) {
  return os << "ST" << static_cast<int>(reg);
}

std::ostream& operator<<(std::ostream& os, const Address& addr) {
  switch (addr.mod()) {
    case 0:
      if (addr.rm() != RSP || addr.cpu_index().AsRegister() == RSP) {
        return os << "(%" << addr.cpu_rm() << ")";
      } else if (addr.base() == RBP) {
        return os << static_cast<int>(addr.disp32()) << "(,%" << addr.cpu_index()
                  << "," << (1 << addr.scale()) << ")";
      }
      return os << "(%" << addr.cpu_base() << ",%"
                << addr.cpu_index() << "," << (1 << addr.scale()) << ")";
    case 1:
      if (addr.rm() != RSP || addr.cpu_index().AsRegister() == RSP) {
        return os << static_cast<int>(addr.disp8()) << "(%" << addr.cpu_rm() << ")";
      }
      return os << static_cast<int>(addr.disp8()) << "(%" << addr.cpu_base() << ",%"
                << addr.cpu_index() << "," << (1 << addr.scale()) << ")";
    case 2:
      if (addr.rm() != RSP || addr.cpu_index().AsRegister() == RSP) {
        return os << static_cast<int>(addr.disp32()) << "(%" << addr.cpu_rm() << ")";
      }
      return os << static_cast<int>(addr.disp32()) << "(%" << addr.cpu_base() << ",%"
                << addr.cpu_index() << "," << (1 << addr.scale()) << ")";
    default:
      return os << "<address?>";
  }
}

bool X86_64Assembler::CpuHasAVXFeatureFlag() { return has_AVX_; }
bool X86_64Assembler::CpuHasAVX2FeatureFlag() { return has_AVX2_; }

void X86_64Assembler::call(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg);
  EmitUint8(0xFF);
  EmitRegisterOperand(2, reg.LowBits());
}


void X86_64Assembler::call(const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(address);
  EmitUint8(0xFF);
  EmitOperand(2, address);
}


void X86_64Assembler::call(Label* label) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xE8);
  static const int kSize = 5;
  // Offset by one because we already have emitted the opcode.
  EmitLabel(label, kSize - 1);
}

void X86_64Assembler::pushq(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg);
  EmitUint8(0x50 + reg.LowBits());
}


void X86_64Assembler::pushq(const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(address);
  EmitUint8(0xFF);
  EmitOperand(6, address);
}


void X86_64Assembler::pushq(const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());  // pushq only supports 32b immediate.
  if (imm.is_int8()) {
    EmitUint8(0x6A);
    EmitUint8(imm.value() & 0xFF);
  } else {
    EmitUint8(0x68);
    EmitImmediate(imm);
  }
}


void X86_64Assembler::popq(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg);
  EmitUint8(0x58 + reg.LowBits());
}


void X86_64Assembler::popq(const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(address);
  EmitUint8(0x8F);
  EmitOperand(0, address);
}


void X86_64Assembler::movq(CpuRegister dst, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (imm.is_int32()) {
    // 32 bit. Note: sign-extends.
    EmitRex64(dst);
    EmitUint8(0xC7);
    EmitRegisterOperand(0, dst.LowBits());
    EmitInt32(static_cast<int32_t>(imm.value()));
  } else {
    EmitRex64(dst);
    EmitUint8(0xB8 + dst.LowBits());
    EmitInt64(imm.value());
  }
}


void X86_64Assembler::movl(CpuRegister dst, const Immediate& imm) {
  CHECK(imm.is_int32());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst);
  EmitUint8(0xB8 + dst.LowBits());
  EmitImmediate(imm);
}


void X86_64Assembler::movq(const Address& dst, const Immediate& imm) {
  CHECK(imm.is_int32());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst);
  EmitUint8(0xC7);
  EmitOperand(0, dst);
  EmitImmediate(imm);
}


void X86_64Assembler::movq(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  // 0x89 is movq r/m64 <- r64, with op1 in r/m and op2 in reg: so reverse EmitRex64
  EmitRex64(src, dst);
  EmitUint8(0x89);
  EmitRegisterOperand(src.LowBits(), dst.LowBits());
}


void X86_64Assembler::movl(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x8B);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::movq(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x8B);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::movl(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x8B);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::movq(const Address& dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(src, dst);
  EmitUint8(0x89);
  EmitOperand(src.LowBits(), dst);
}


void X86_64Assembler::movl(const Address& dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(src, dst);
  EmitUint8(0x89);
  EmitOperand(src.LowBits(), dst);
}

void X86_64Assembler::movl(const Address& dst, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst);
  EmitUint8(0xC7);
  EmitOperand(0, dst);
  EmitImmediate(imm);
}

void X86_64Assembler::movntl(const Address& dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(src, dst);
  EmitUint8(0x0F);
  EmitUint8(0xC3);
  EmitOperand(src.LowBits(), dst);
}

void X86_64Assembler::movntq(const Address& dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(src, dst);
  EmitUint8(0x0F);
  EmitUint8(0xC3);
  EmitOperand(src.LowBits(), dst);
}

void X86_64Assembler::cmov(Condition c, CpuRegister dst, CpuRegister src) {
  cmov(c, dst, src, true);
}

void X86_64Assembler::cmov(Condition c, CpuRegister dst, CpuRegister src, bool is64bit) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex(false, is64bit, dst.NeedsRex(), false, src.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x40 + c);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::cmov(Condition c, CpuRegister dst, const Address& src, bool is64bit) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (is64bit) {
    EmitRex64(dst, src);
  } else {
    EmitOptionalRex32(dst, src);
  }
  EmitUint8(0x0F);
  EmitUint8(0x40 + c);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::movzxb(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalByteRegNormalizingRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xB6);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::movzxb(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  // Byte register is only in the source register form, so we don't use
  // EmitOptionalByteRegNormalizingRex32(dst, src);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xB6);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::movsxb(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalByteRegNormalizingRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xBE);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::movsxb(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  // Byte register is only in the source register form, so we don't use
  // EmitOptionalByteRegNormalizingRex32(dst, src);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xBE);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::movb(CpuRegister /*dst*/, const Address& /*src*/) {
  LOG(FATAL) << "Use movzxb or movsxb instead.";
}


void X86_64Assembler::movb(const Address& dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalByteRegNormalizingRex32(src, dst);
  EmitUint8(0x88);
  EmitOperand(src.LowBits(), dst);
}


void X86_64Assembler::movb(const Address& dst, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst);
  EmitUint8(0xC6);
  EmitOperand(Register::RAX, dst);
  CHECK(imm.is_int8());
  EmitUint8(imm.value() & 0xFF);
}


void X86_64Assembler::movzxw(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xB7);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::movzxw(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xB7);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::movsxw(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xBF);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::movsxw(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xBF);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::movw(CpuRegister /*dst*/, const Address& /*src*/) {
  LOG(FATAL) << "Use movzxw or movsxw instead.";
}


void X86_64Assembler::movw(const Address& dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOperandSizeOverride();
  EmitOptionalRex32(src, dst);
  EmitUint8(0x89);
  EmitOperand(src.LowBits(), dst);
}


void X86_64Assembler::movw(const Address& dst, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOperandSizeOverride();
  EmitOptionalRex32(dst);
  EmitUint8(0xC7);
  EmitOperand(Register::RAX, dst);
  CHECK(imm.is_uint16() || imm.is_int16());
  EmitUint8(imm.value() & 0xFF);
  EmitUint8(imm.value() >> 8);
}


void X86_64Assembler::leaq(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x8D);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::leal(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x8D);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::movaps(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vmovaps(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x28);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::movups(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vmovups(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x10);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

/**VEX.128.0F.WIG 28 /r VMOVAPS xmm1, xmm2 */
void X86_64Assembler::vmovaps(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVXFeatureFlag());
  uint8_t byte_zero, byte_one, byte_two;
  bool is_twobyte_form = true;
  bool load = dst.NeedsRex();
  bool store = !load;
  uint8_t VEX_L = GetEncodedVexLen(dst);

  if (src.NeedsRex()&& dst.NeedsRex()) {
    is_twobyte_form = false;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  // Instruction VEX Prefix
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
  if (is_twobyte_form) {
    bool rex_bit = (load) ? dst.NeedsRex() : src.NeedsRex();
    byte_one = EmitVexPrefixByteOne(rex_bit, vvvv_reg, VEX_L, SET_VEX_PP_NONE);
  } else {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                    /*X=*/ false,
                                    src.NeedsRex(),
                                    SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_NONE);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }
  // Instruction Opcode
  if (is_twobyte_form && store) {
    EmitUint8(0x29);
  } else {
    EmitUint8(0x28);
  }
  // Instruction Operands
  if (is_twobyte_form && store) {
    EmitXmmRegisterOperand(src.LowBits(), dst);
  } else {
    EmitXmmRegisterOperand(dst.LowBits(), src);
  }
}

void X86_64Assembler::movaps(XmmRegister dst, const Address& src) {
  if (dst.IsYMM()) {
    vmovaps(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x28);
  EmitOperand(dst.LowBits(), src);
}

/**VEX.128.0F.WIG 28 /r VMOVAPS xmm1, m128 */
void X86_64Assembler::vmovaps(XmmRegister dst, const Address& src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t ByteZero, ByteOne, ByteTwo;
  bool is_twobyte_form = false;
  // Instruction VEX Prefix
  uint8_t VEX_L = GetEncodedVexLen(dst);
  uint8_t rex = src.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_b && !Rex_x) {
    is_twobyte_form = true;
  }
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_NONE);
  } else {
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(),
                                   Rex_x,
                                   Rex_b,
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_NONE);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  // Instruction Opcode
  EmitUint8(0x28);
  // Instruction Operands
  EmitOperand(dst.LowBits(), src);
}

void X86_64Assembler::movups(XmmRegister dst, const Address& src) {
  if (dst.IsYMM()) {
    vmovups(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x10);
  EmitOperand(dst.LowBits(), src);
}

/** VEX.128.0F.WIG 10 /r VMOVUPS xmm1, m128 */
void X86_64Assembler::vmovups(XmmRegister dst, const Address& src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t ByteZero, ByteOne, ByteTwo;
  bool is_twobyte_form = false;
  // Instruction VEX Prefix
  uint8_t VEX_L = GetEncodedVexLen(dst);
  uint8_t rex = src.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_x && !Rex_b) {
    is_twobyte_form = true;
  }
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_NONE);
  } else {
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(),
                                   Rex_x,
                                   Rex_b,
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_NONE);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  // Instruction Opcode
  EmitUint8(0x10);
  // Instruction Operands
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::movaps(const Address& dst, XmmRegister src) {
  if (src.IsYMM()) {
    vmovaps(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(src, dst);
  EmitUint8(0x0F);
  EmitUint8(0x29);
  EmitOperand(src.LowBits(), dst);
}

/** VEX.128.0F.WIG 29 /r VMOVAPS m128, xmm1 */
void X86_64Assembler::vmovaps(const Address& dst, XmmRegister src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t ByteZero, ByteOne, ByteTwo;
  bool is_twobyte_form = false;

  // Instruction VEX Prefix
  uint8_t VEX_L = GetEncodedVexLen(src);
  uint8_t rex = dst.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_b && !Rex_x) {
    is_twobyte_form = true;
  }
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    ByteOne = EmitVexPrefixByteOne(src.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_NONE);
  } else {
    ByteOne = EmitVexPrefixByteOne(src.NeedsRex(),
                                   Rex_x,
                                   Rex_b,
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_NONE);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  // Instruction Opcode
  EmitUint8(0x29);
  // Instruction Operands
  EmitOperand(src.LowBits(), dst);
}

void X86_64Assembler::movups(const Address& dst, XmmRegister src) {
  if (src.IsYMM()) {
    vmovups(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(src, dst);
  EmitUint8(0x0F);
  EmitUint8(0x11);
  EmitOperand(src.LowBits(), dst);
}

/** VEX.128.0F.WIG 11 /r VMOVUPS m128, xmm1 */
void X86_64Assembler::vmovups(const Address& dst, XmmRegister src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t ByteZero, ByteOne, ByteTwo;
  bool is_twobyte_form = false;

  // Instruction VEX Prefix
  uint8_t VEX_L = GetEncodedVexLen(src);
  uint8_t rex = dst.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_b && !Rex_x) {
    is_twobyte_form = true;
  }
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    ByteOne = EmitVexPrefixByteOne(src.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_NONE);
  } else {
    ByteOne = EmitVexPrefixByteOne(src.NeedsRex(),
                                   Rex_x,
                                   Rex_b,
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_NONE);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  // Instruction Opcode
  EmitUint8(0x11);
  // Instruction Operands
  EmitOperand(src.LowBits(), dst);
}

/** VEX.128.0F.WIG 10/11 /r VMOVUPS xmm1, xmm2
    VEX.256.0F.WIG 10/11 /r VMOVUPS ymm1, ymm2 */
void X86_64Assembler::vmovups(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t byte_zero, byte_one, byte_two;
  bool is_twobyte_form = true;
  bool is_load = src.NeedsRex() && !dst.NeedsRex();
  // Instruction VEX Prefix
  uint8_t VEX_L = GetEncodedVexLen(dst);
  if (dst.NeedsRex() && src.NeedsRex()) {
    is_twobyte_form = false;
  }
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    byte_one = EmitVexPrefixByteOne(
        is_load ? src.NeedsRex() : dst.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_NONE);
  } else {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                    /*X=*/false,
                                    src.NeedsRex(),
                                    SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_NONE);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }
  // Instruction Opcode
  if (is_load) {
    EmitUint8(0x11);
    EmitXmmRegisterOperand(src.LowBits(), dst);
  } else {
    EmitUint8(0x10);
    // Instruction Operands
    EmitXmmRegisterOperand(dst.LowBits(), src);
  }
}

void X86_64Assembler::movss(XmmRegister dst, const Address& src) {
  if (dst.IsYMM()) {
    vmovss(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x10);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::movss(const Address& dst, XmmRegister src) {
  if (src.IsYMM()) {
    vmovss(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(src, dst);
  EmitUint8(0x0F);
  EmitUint8(0x11);
  EmitOperand(src.LowBits(), dst);
}


void X86_64Assembler::movss(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vmovss(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(src, dst);  // Movss is MR encoding instead of the usual RM.
  EmitUint8(0x0F);
  EmitUint8(0x11);
  EmitXmmRegisterOperand(src.LowBits(), dst);
}

/* VEX.LIG.F3.0F.WIG 10 /r VMOVSS xmm1, m32
   Since LIG VEX.L = 0 and WIG => VEX.W = 0
*/
void X86_64Assembler::vmovss(XmmRegister dst, const Address& src) {
  DCHECK(CpuHasAVXFeatureFlag());
  uint8_t byte_zero, byte_one, byte_two;
  bool is_twobyte_form = false;
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);

  uint8_t rex = src.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_b && !Rex_x) {
    is_twobyte_form = true;
  }
  // Instruction VEX Prefix
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
  if (is_twobyte_form) {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, SET_VEX_L_128, SET_VEX_PP_F3);
  } else {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(), Rex_x, Rex_b, SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/false, SET_VEX_L_128, SET_VEX_PP_F3);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }
  // Instruction Opcode
  EmitUint8(0x10);

  // Instruction Operands
  EmitOperand(dst.LowBits(), src);
}

/* VEX.LIG.F3.0F.WIG 11 /r VMOVSS m32, xmm1 */
void X86_64Assembler::vmovss(const Address& dst, XmmRegister src) {
  DCHECK(CpuHasAVXFeatureFlag());
  uint8_t byte_zero, byte_one, byte_two;
  bool is_twobyte_form = false;
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);

  uint8_t rex = dst.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_b && !Rex_x) {
    is_twobyte_form = true;
  }
  // Instruction VEX Prefix
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
  if (is_twobyte_form) {
    byte_one = EmitVexPrefixByteOne(src.NeedsRex(), vvvv_reg, SET_VEX_L_128, SET_VEX_PP_F3);
  } else {
    byte_one = EmitVexPrefixByteOne(src.NeedsRex(), Rex_x, Rex_b, SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/false, SET_VEX_L_128, SET_VEX_PP_F3);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }
  // Instruction Opcode
  EmitUint8(0x11);

  // Instruction Operands
  EmitOperand(src.LowBits(), dst);
}

/* VEX.LIG.F3.0F.WIG 10/11 /r VMOVSS xmm1, xmm2, xmm3 */
void X86_64Assembler::vmovss(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVXFeatureFlag());
  uint8_t byte_zero, byte_one, byte_two;
  bool is_twobyte_form = true;
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);

  if (dst.NeedsRex() && src2.NeedsRex()) {
    is_twobyte_form = false;
  }
  // Instruction VEX Prefix
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg = X86_64ManagedRegister::FromXmmRegister(src1.AsFloatRegister());
  bool is_store = src2.NeedsRex() && !dst.NeedsRex();
  if (is_twobyte_form) {
    byte_one = EmitVexPrefixByteOne(
        is_store ? src2.NeedsRex() : dst.NeedsRex(), vvvv_reg, SET_VEX_L_128, SET_VEX_PP_F3);
  } else {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                    /*X=*/false,
                                    src2.NeedsRex(),
                                    SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/false, vvvv_reg, SET_VEX_L_128, SET_VEX_PP_F3);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }

  // Instruction Opcode
  if (is_store) {
    // Special opcode only when src2 needs rex
    EmitUint8(0x11);
    EmitXmmRegisterOperand(src2.LowBits(), dst);
  } else {
    EmitUint8(0x10);
    EmitXmmRegisterOperand(dst.LowBits(), src2);
  }
}

void X86_64Assembler::movsxd(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x63);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::movsxd(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x63);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::movd(XmmRegister dst, CpuRegister src) {
  movd(dst, src, true);
}

void X86_64Assembler::movd(CpuRegister dst, XmmRegister src) {
  movd(dst, src, true);
}

void X86_64Assembler::movd(XmmRegister dst, CpuRegister src, bool is64bit) {
  if (dst.IsYMM()) {
    vmovd(dst, src, is64bit);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, is64bit, dst.NeedsRex(), false, src.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x6E);
  EmitOperand(dst.LowBits(), Operand(src));
}

void X86_64Assembler::movd(CpuRegister dst, XmmRegister src, bool is64bit) {
  if (src.IsYMM()) {
    vmovd(dst, src, is64bit);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, is64bit, src.NeedsRex(), false, dst.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x7E);
  EmitOperand(src.LowBits(), Operand(dst));
}

void X86_64Assembler::vmovq(XmmRegister dst, CpuRegister src) { vmovd(dst, src, true); }

void X86_64Assembler::vmovq(CpuRegister dst, XmmRegister src) { vmovd(dst, src, true); }

void X86_64Assembler::vmovd(XmmRegister dst, CpuRegister src, bool is64bit) {
  DCHECK(CpuHasAVXFeatureFlag());
  uint8_t byte_zero, byte_one, byte_two;
  bool is_twobyte_form = !(is64bit || src.NeedsRex());

  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  // Instruction VEX Prefix
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
  if (is_twobyte_form) {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, SET_VEX_L_128, SET_VEX_PP_66);
  } else {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                    /*X=*/false,
                                    src.NeedsRex(),
                                    SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/is64bit, SET_VEX_L_128, SET_VEX_PP_66);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }
  // Instruction Opcode
  EmitUint8(0x6E);

  // Instruction Operands
  EmitOperand(dst.LowBits(), Operand(src));
}

void X86_64Assembler::vmovd(CpuRegister dst, XmmRegister src, bool is64bit) {
  DCHECK(CpuHasAVXFeatureFlag());
  uint8_t byte_zero, byte_one, byte_two;
  bool is_twobyte_form = !(is64bit || dst.NeedsRex());

  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  // Instruction VEX Prefix
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
  if (is_twobyte_form) {
    byte_one = EmitVexPrefixByteOne(src.NeedsRex(), vvvv_reg, SET_VEX_L_128, SET_VEX_PP_66);
  } else {
    byte_one = EmitVexPrefixByteOne(src.NeedsRex(),
                                    /*X=*/false,
                                    dst.NeedsRex(),
                                    SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/is64bit, SET_VEX_L_128, SET_VEX_PP_66);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }
  // Instruction Opcode
  EmitUint8(0x7E);

  // Instruction Operands
  EmitOperand(src.LowBits(), Operand(dst));
}

void X86_64Assembler::addss(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x58);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::addss(XmmRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x58);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::subss(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5C);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::subss(XmmRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5C);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::mulss(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x59);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::mulss(XmmRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x59);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::divss(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5E);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::divss(XmmRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5E);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::addps(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vaddps(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x58);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::subps(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vsubps(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5C);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vaddps(XmmRegister dst, XmmRegister add_left, XmmRegister add_right) {
  EmitVecArithAndLogicalOperation(
      dst, add_left, add_right, 0x58, SET_VEX_PP_NONE, /*is_commutative*/ true);
}

void X86_64Assembler::vsubps(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x5C, SET_VEX_PP_NONE);
}


void X86_64Assembler::mulps(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vmulps(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x59);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vmulps(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x59, SET_VEX_PP_NONE, /*is_commutative*/ true);
}

void X86_64Assembler::divps(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vdivps(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5E);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vdivps(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x5E, SET_VEX_PP_NONE);
}

void X86_64Assembler::vfmadd213ss(XmmRegister acc, XmmRegister left, XmmRegister right) {
  DCHECK(CpuHasAVX2FeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t ByteZero = 0x00, ByteOne = 0x00, ByteTwo = 0x00;
  ByteZero = EmitVexPrefixByteZero(/*is_twobyte_form=*/false);
  X86_64ManagedRegister vvvv_reg =
      X86_64ManagedRegister::FromXmmRegister(left.AsFloatRegister());
  ByteOne = EmitVexPrefixByteOne(acc.NeedsRex(),
                                 /*X=*/ false,
                                 right.NeedsRex(),
                                 SET_VEX_M_0F_38);
  ByteTwo = EmitVexPrefixByteTwo(/*W=*/ false, vvvv_reg, SET_VEX_L_128, SET_VEX_PP_66);
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  EmitUint8(ByteTwo);
  EmitUint8(0xA9);
  EmitXmmRegisterOperand(acc.LowBits(), right);
}

void X86_64Assembler::vfmadd213sd(XmmRegister acc, XmmRegister left, XmmRegister right) {
  DCHECK(CpuHasAVX2FeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t ByteZero = 0x00, ByteOne = 0x00, ByteTwo = 0x00;
  ByteZero = EmitVexPrefixByteZero(/*is_twobyte_form=*/ false);
  X86_64ManagedRegister vvvv_reg =
      X86_64ManagedRegister::FromXmmRegister(left.AsFloatRegister());
  ByteOne = EmitVexPrefixByteOne(acc.NeedsRex(),
                                 /*X=*/ false,
                                 right.NeedsRex(),
                                 SET_VEX_M_0F_38);
  ByteTwo = EmitVexPrefixByteTwo(/*W=*/ true, vvvv_reg, SET_VEX_L_128, SET_VEX_PP_66);
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  EmitUint8(ByteTwo);
  EmitUint8(0xA9);
  EmitXmmRegisterOperand(acc.LowBits(), right);
}
void X86_64Assembler::flds(const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitOperand(0, src);
}


void X86_64Assembler::fsts(const Address& dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitOperand(2, dst);
}


void X86_64Assembler::fstps(const Address& dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitOperand(3, dst);
}


void X86_64Assembler::movapd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vmovapd(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x28);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

/** VEX.128.66.0F.WIG 28 /r VMOVAPD xmm1, xmm2 */
void X86_64Assembler::vmovapd(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t ByteZero, ByteOne, ByteTwo;
  bool is_twobyte_form = true;
  uint8_t VEX_L = GetEncodedVexLen(dst);

  if (src.NeedsRex() && dst.NeedsRex()) {
    is_twobyte_form = false;
  }
  // Instruction VEX Prefix
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  bool load = dst.NeedsRex();
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    bool rex_bit = load ? dst.NeedsRex() : src.NeedsRex();
    ByteOne = EmitVexPrefixByteOne(rex_bit, vvvv_reg, VEX_L, SET_VEX_PP_66);
  } else {
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(),
                                   /*X=*/ false,
                                   src.NeedsRex(),
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_66);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  // Instruction Opcode
  if (is_twobyte_form && !load) {
    EmitUint8(0x29);
  } else {
    EmitUint8(0x28);
  }
  // Instruction Operands
  if (is_twobyte_form && !load) {
    EmitXmmRegisterOperand(src.LowBits(), dst);
  } else {
    EmitXmmRegisterOperand(dst.LowBits(), src);
  }
}

void X86_64Assembler::movapd(XmmRegister dst, const Address& src) {
  if (dst.IsYMM()) {
    vmovapd(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x28);
  EmitOperand(dst.LowBits(), src);
}

/** VEX.128.66.0F.WIG 28 /r VMOVAPD xmm1, m128 */
void X86_64Assembler::vmovapd(XmmRegister dst, const Address& src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t ByteZero, ByteOne, ByteTwo;
  bool is_twobyte_form = false;
  uint8_t VEX_L = GetEncodedVexLen(dst);

  // Instruction VEX Prefix
  uint8_t rex = src.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_b && !Rex_x) {
    is_twobyte_form = true;
  }
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_66);
  } else {
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(),
                                   Rex_x,
                                   Rex_b,
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_66);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  // Instruction Opcode
  EmitUint8(0x28);
  // Instruction Operands
  EmitOperand(dst.LowBits(), src);
}

void X86_64Assembler::movupd(XmmRegister dst, const Address& src) {
  if (dst.IsYMM()) {
    vmovupd(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x10);
  EmitOperand(dst.LowBits(), src);
}

/** VEX.128.66.0F.WIG 10 /r VMOVUPD xmm1, m128 */
void X86_64Assembler::vmovupd(XmmRegister dst, const Address& src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  bool is_twobyte_form = false;
  uint8_t ByteZero, ByteOne, ByteTwo;
  uint8_t VEX_L = GetEncodedVexLen(dst);

  // Instruction VEX Prefix
  uint8_t rex = src.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_b && !Rex_x) {
    is_twobyte_form = true;
  }
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_66);
  } else {
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(),
                                   Rex_x,
                                   Rex_b,
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_66);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form)
  EmitUint8(ByteTwo);
  // Instruction Opcode
  EmitUint8(0x10);
  // Instruction Operands
  EmitOperand(dst.LowBits(), src);
}

void X86_64Assembler::movapd(const Address& dst, XmmRegister src) {
  if (src.IsYMM()) {
    vmovapd(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(src, dst);
  EmitUint8(0x0F);
  EmitUint8(0x29);
  EmitOperand(src.LowBits(), dst);
}

/** VEX.128.66.0F.WIG 29 /r VMOVAPD m128, xmm1 */
void X86_64Assembler::vmovapd(const Address& dst, XmmRegister src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  bool is_twobyte_form = false;
  uint8_t ByteZero, ByteOne, ByteTwo;
  uint8_t VEX_L = GetEncodedVexLen(src);

  // Instruction VEX Prefix
  uint8_t rex = dst.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_x && !Rex_b) {
    is_twobyte_form = true;
  }
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    ByteOne = EmitVexPrefixByteOne(src.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_66);
  } else {
    ByteOne = EmitVexPrefixByteOne(src.NeedsRex(),
                                   Rex_x,
                                   Rex_b,
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_66);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  // Instruction Opcode
  EmitUint8(0x29);
  // Instruction Operands
  EmitOperand(src.LowBits(), dst);
}

void X86_64Assembler::movupd(const Address& dst, XmmRegister src) {
  if (src.IsYMM()) {
    vmovupd(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(src, dst);
  EmitUint8(0x0F);
  EmitUint8(0x11);
  EmitOperand(src.LowBits(), dst);
}

/** VEX.128.66.0F.WIG 11 /r VMOVUPD m128, xmm1 */
void X86_64Assembler::vmovupd(const Address& dst, XmmRegister src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  bool is_twobyte_form = false;
  uint8_t ByteZero, ByteOne, ByteTwo;
  uint8_t VEX_L = GetEncodedVexLen(src);

  // Instruction VEX Prefix
  uint8_t rex = dst.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_x && !Rex_b) {
    is_twobyte_form = true;
  }
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    ByteOne = EmitVexPrefixByteOne(src.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_66);
  } else {
    ByteOne = EmitVexPrefixByteOne(src.NeedsRex(),
                                   Rex_x,
                                   Rex_b,
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_66);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  // Instruction Opcode
  EmitUint8(0x11);
  // Instruction Operands
  EmitOperand(src.LowBits(), dst);
}


void X86_64Assembler::movsd(XmmRegister dst, const Address& src) {
  if (dst.IsYMM()) {
    vmovsd(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x10);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::movsd(const Address& dst, XmmRegister src) {
  if (src.IsYMM()) {
    vmovsd(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(src, dst);
  EmitUint8(0x0F);
  EmitUint8(0x11);
  EmitOperand(src.LowBits(), dst);
}


void X86_64Assembler::movsd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vmovsd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(src, dst);  // Movsd is MR encoding instead of the usual RM.
  EmitUint8(0x0F);
  EmitUint8(0x11);
  EmitXmmRegisterOperand(src.LowBits(), dst);
}

/* VEX.LIG.F2.0F.WIG 10 /r VMOVSD xmm1, m32 */
void X86_64Assembler::vmovsd(XmmRegister dst, const Address& src) {
  DCHECK(CpuHasAVXFeatureFlag());
  uint8_t byte_zero, byte_one, byte_two;
  bool is_twobyte_form = false;
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);

  uint8_t rex = src.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_b && !Rex_x) {
    is_twobyte_form = true;
  }
  // Instruction VEX Prefix
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
  if (is_twobyte_form) {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, SET_VEX_L_128, SET_VEX_PP_F2);
  } else {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(), Rex_x, Rex_b, SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/false, SET_VEX_L_128, SET_VEX_PP_F2);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }
  // Instruction Opcode
  EmitUint8(0x10);

  // Instruction Operands
  EmitOperand(dst.LowBits(), src);
}

/* VEX.LIG.F2.0F.WIG 11 /r VMOVSD m32, xmm1 */
void X86_64Assembler::vmovsd(const Address& dst, XmmRegister src) {
  DCHECK(CpuHasAVXFeatureFlag());
  uint8_t byte_zero, byte_one, byte_two;
  bool is_twobyte_form = false;
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);

  uint8_t rex = dst.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_b && !Rex_x) {
    is_twobyte_form = true;
  }
  // Instruction VEX Prefix
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
  if (is_twobyte_form) {
    byte_one = EmitVexPrefixByteOne(src.NeedsRex(), vvvv_reg, SET_VEX_L_128, SET_VEX_PP_F2);
  } else {
    byte_one = EmitVexPrefixByteOne(src.NeedsRex(), Rex_x, Rex_b, SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/false, SET_VEX_L_128, SET_VEX_PP_F2);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }
  // Instruction Opcode
  EmitUint8(0x11);

  // Instruction Operands
  EmitOperand(src.LowBits(), dst);
}

/* VEX.LIG.F2.0F.WIG 10/11 /r VMOVSD xmm1, xmm2, xmm3 */
void X86_64Assembler::vmovsd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVXFeatureFlag());
  uint8_t byte_zero, byte_one, byte_two;
  bool is_twobyte_form = true;
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);

  if (dst.NeedsRex() && src2.NeedsRex()) {
    is_twobyte_form = false;
  }
  // Instruction VEX Prefix
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg = X86_64ManagedRegister::FromXmmRegister(src1.AsFloatRegister());
  bool is_store = src2.NeedsRex() && !dst.NeedsRex();
  if (is_twobyte_form) {
    byte_one = EmitVexPrefixByteOne(
        is_store ? src2.NeedsRex() : dst.NeedsRex(), vvvv_reg, SET_VEX_L_128, SET_VEX_PP_F2);
  } else {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                    /*X=*/false,
                                    src2.NeedsRex(),
                                    SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/false, vvvv_reg, SET_VEX_L_128, SET_VEX_PP_F2);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }

  // Instruction Opcode && Instruction Operands
  if (is_store) {
    // Opcode only when src2 needs rex
    EmitUint8(0x11);
    EmitXmmRegisterOperand(src2.LowBits(), dst);
  } else {
    EmitUint8(0x10);
    EmitXmmRegisterOperand(dst.LowBits(), src2);
  }
}

void X86_64Assembler::addsd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x58);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::addsd(XmmRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x58);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::subsd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5C);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::subsd(XmmRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5C);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::mulsd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x59);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::mulsd(XmmRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x59);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::divsd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5E);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::divsd(XmmRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5E);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::addpd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vaddpd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x58);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::vaddpd(XmmRegister dst, XmmRegister add_left, XmmRegister add_right) {
  EmitVecArithAndLogicalOperation(
      dst, add_left, add_right, 0x58, SET_VEX_PP_66, /*is_commutative*/ true);
}


void X86_64Assembler::subpd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vsubpd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5C);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::vsubpd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x5C, SET_VEX_PP_66);
}


void X86_64Assembler::mulpd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vmulpd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x59);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vmulpd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x59, SET_VEX_PP_66, /*is_commutative*/ true);
}

void X86_64Assembler::divpd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vdivpd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5E);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::vdivpd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x5E, SET_VEX_PP_66);
}


void X86_64Assembler::movdqa(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vmovdqa(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x6F);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

/** VEX.128.66.0F.WIG 6F /r VMOVDQA xmm1, xmm2 */
void X86_64Assembler::vmovdqa(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t ByteZero, ByteOne, ByteTwo;
  bool is_twobyte_form = true;
  uint8_t VEX_L = GetEncodedVexLen(dst);

  // Instruction VEX Prefix
  if (src.NeedsRex() && dst.NeedsRex()) {
    is_twobyte_form = false;
  }
  bool load = dst.NeedsRex();
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    bool rex_bit = load ? dst.NeedsRex() : src.NeedsRex();
    ByteOne = EmitVexPrefixByteOne(rex_bit, vvvv_reg, VEX_L, SET_VEX_PP_66);
  } else {
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(),
                                   /*X=*/ false,
                                   src.NeedsRex(),
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_66);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  // Instruction Opcode
  if (is_twobyte_form && !load) {
    EmitUint8(0x7F);
  } else {
    EmitUint8(0x6F);
  }
  // Instruction Operands
  if (is_twobyte_form && !load) {
    EmitXmmRegisterOperand(src.LowBits(), dst);
  } else {
    EmitXmmRegisterOperand(dst.LowBits(), src);
  }
}

void X86_64Assembler::movdqa(XmmRegister dst, const Address& src) {
  if (dst.IsYMM()) {
    vmovdqa(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x6F);
  EmitOperand(dst.LowBits(), src);
}

/** VEX.128.66.0F.WIG 6F /r VMOVDQA xmm1, m128 */
void X86_64Assembler::vmovdqa(XmmRegister dst, const Address& src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t  ByteZero, ByteOne, ByteTwo;
  bool is_twobyte_form = false;
  uint8_t VEX_L = GetEncodedVexLen(dst);

  // Instruction VEX Prefix
  uint8_t rex = src.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_x && !Rex_b) {
    is_twobyte_form = true;
  }
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_66);
  } else {
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(),
                                   Rex_x,
                                   Rex_b,
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_66);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  // Instruction Opcode
  EmitUint8(0x6F);
  // Instruction Operands
  EmitOperand(dst.LowBits(), src);
}

void X86_64Assembler::movdqu(XmmRegister dst, const Address& src) {
  if (dst.IsYMM()) {
    vmovdqu(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x6F);
  EmitOperand(dst.LowBits(), src);
}

/** VEX.128.F3.0F.WIG 6F /r VMOVDQU xmm1, m128
Load Unaligned */
void X86_64Assembler::vmovdqu(XmmRegister dst, const Address& src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t ByteZero, ByteOne, ByteTwo;
  bool is_twobyte_form = false;
  uint8_t VEX_L = GetEncodedVexLen(dst);

  // Instruction VEX Prefix
  uint8_t rex = src.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_x && !Rex_b) {
    is_twobyte_form = true;
  }
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_F3);
  } else {
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(),
                                   Rex_x,
                                   Rex_b,
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_F3);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  // Instruction Opcode
  EmitUint8(0x6F);
  // Instruction Operands
  EmitOperand(dst.LowBits(), src);
}

void X86_64Assembler::movdqa(const Address& dst, XmmRegister src) {
  if (src.IsYMM()) {
    vmovdqa(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(src, dst);
  EmitUint8(0x0F);
  EmitUint8(0x7F);
  EmitOperand(src.LowBits(), dst);
}

/** VEX.128.66.0F.WIG 7F /r VMOVDQA m128, xmm1 */
void X86_64Assembler::vmovdqa(const Address& dst, XmmRegister src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  bool is_twobyte_form = false;
  uint8_t ByteZero, ByteOne, ByteTwo;
  uint8_t VEX_L = GetEncodedVexLen(src);

  // Instruction VEX Prefix
  uint8_t rex = dst.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_x && !Rex_b) {
    is_twobyte_form = true;
  }
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    ByteOne = EmitVexPrefixByteOne(src.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_66);
  } else {
    ByteOne = EmitVexPrefixByteOne(src.NeedsRex(),
                                   Rex_x,
                                   Rex_b,
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_66);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  // Instruction Opcode
  EmitUint8(0x7F);
  // Instruction Operands
  EmitOperand(src.LowBits(), dst);
}

void X86_64Assembler::movdqu(const Address& dst, XmmRegister src) {
  if (src.IsYMM()) {
    vmovdqu(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(src, dst);
  EmitUint8(0x0F);
  EmitUint8(0x7F);
  EmitOperand(src.LowBits(), dst);
}

/** VEX.128.F3.0F.WIG 7F /r VMOVDQU m128, xmm1 */
void X86_64Assembler::vmovdqu(const Address& dst, XmmRegister src) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t ByteZero, ByteOne, ByteTwo;
  bool is_twobyte_form = false;
  uint8_t VEX_L = GetEncodedVexLen(src);

  // Instruction VEX Prefix
  uint8_t rex = dst.rex();
  bool Rex_x = rex & GET_REX_X;
  bool Rex_b = rex & GET_REX_B;
  if (!Rex_b && !Rex_x) {
    is_twobyte_form = true;
  }
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
    ByteOne = EmitVexPrefixByteOne(src.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_F3);
  } else {
    ByteOne = EmitVexPrefixByteOne(src.NeedsRex(),
                                   Rex_x,
                                   Rex_b,
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_F3);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  // Instruction Opcode
  EmitUint8(0x7F);
  // Instruction Operands
  EmitOperand(src.LowBits(), dst);
}

void X86_64Assembler::paddb(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpaddb(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xFC);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::vpaddb(XmmRegister dst, XmmRegister add_left, XmmRegister add_right) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(
      dst, add_left, add_right, 0xFC, SET_VEX_PP_66, /*is_commutative*/ true);
}


void X86_64Assembler::psubb(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpsubb(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xF8);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::vpsubb(XmmRegister dst, XmmRegister add_left, XmmRegister add_right) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, add_left, add_right, 0xF8, SET_VEX_PP_66);
}

void X86_64Assembler::paddw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpaddw(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xFD);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vpaddw(XmmRegister dst, XmmRegister add_left, XmmRegister add_right) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(
      dst, add_left, add_right, 0xFD, SET_VEX_PP_66, /*is_commutative*/ true);
}

void X86_64Assembler::psubw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpsubw(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xF9);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vpsubw(XmmRegister dst, XmmRegister add_left, XmmRegister add_right) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, add_left, add_right, 0xF9, SET_VEX_PP_66);
}


void X86_64Assembler::pmullw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpmullw(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xD5);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vpmullw(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xD5, SET_VEX_PP_66, /*is_commutative*/ true);
}

void X86_64Assembler::paddd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpaddd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xFE);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vpaddd(XmmRegister dst, XmmRegister add_left, XmmRegister add_right) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(
      dst, add_left, add_right, 0xFE, SET_VEX_PP_66, /*is_commutative*/ true);
}

void X86_64Assembler::psubd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpsubd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xFA);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::pmulld(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpmulld(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x40);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vpmulld(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t ByteZero = 0x00, ByteOne = 0x00, ByteTwo = 0x00;
  uint8_t VEX_L = GetEncodedVexLen(dst);
  ByteZero = EmitVexPrefixByteZero(/*is_twobyte_form*/ false);
  X86_64ManagedRegister vvvv_reg =
      X86_64ManagedRegister::FromXmmRegister(src1.AsFloatRegister());
  ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(),
                                   /*X=*/ false,
                                   src2.NeedsRex(),
                                   SET_VEX_M_0F_38);
  ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, vvvv_reg, VEX_L, SET_VEX_PP_66);
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  EmitUint8(ByteTwo);
  EmitUint8(0x40);
  EmitXmmRegisterOperand(dst.LowBits(), src2);
}

void X86_64Assembler::paddq(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpaddq(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xD4);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::vpaddq(XmmRegister dst, XmmRegister add_left, XmmRegister add_right) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(
      dst, add_left, add_right, 0xD4, SET_VEX_PP_66, /*is_commutative*/ true);
}


void X86_64Assembler::psubq(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpsubq(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xFB);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vpsubq(XmmRegister dst, XmmRegister add_left, XmmRegister add_right) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, add_left, add_right, 0xFB, SET_VEX_PP_66);
}


void X86_64Assembler::paddusb(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpaddusb(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xDC);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::paddsb(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpaddsb(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xEC);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::paddusw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpaddusw(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xDD);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::paddsw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpaddsw(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xED);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::psubusb(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpsubusb(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xD8);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::psubsb(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpsubsb(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xE8);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::vpsubd(XmmRegister dst, XmmRegister add_left, XmmRegister add_right) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, add_left, add_right, 0xFA, SET_VEX_PP_66);
}


void X86_64Assembler::psubusw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpsubusw(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xD9);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::psubsw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpsubsw(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xE9);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

/*VEX.128.66.0F.WIG DC /r VPADDUSB xmm1, xmm2, xmm3/m128
  VEX.256.66.0F.WIG DC /r VPADDUSB ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpaddusb(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xDC, SET_VEX_PP_66, /*is_commutative*/ true);
}

/*VEX.128.66.0F.WIG EC /r VPADDSB xmm1, xmm2, xmm3/m128
  VEX.256.66.0F.WIG EC /r VPADDSB ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpaddsb(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xEC, SET_VEX_PP_66, /*is_commutative*/ true);
}

/*VEX.128.66.0F.WIG DD /r VPADDUSW xmm1, xmm2, xmm3/m128
  VEX.256.66.0F.WIG DD /r VPADDUSW ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpaddusw(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xDD, SET_VEX_PP_66, /*is_commutative*/ true);
}

/*VEX.128.66.0F.WIG ED /r VPADDSW xmm1, xmm2, xmm3/m128
  VEX.256.66.0F.WIG ED /r VPADDSW ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpaddsw(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xED, SET_VEX_PP_66, /*is_commutative*/ true);
}

/*VEX.128.66.0F.WIG D8 /r VPSUBUSB xmm1, xmm2, xmm3/m128
  VEX.256.66.0F.WIG D8 /r VPSUBUSB ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpsubusb(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xD8, SET_VEX_PP_66);
}

/*VEX.128.66.0F.WIG E8 /r VPSUBSB xmm1, xmm2, xmm3/m128
  VEX.256.66.0F.WIG E8 /r VPSUBSB ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpsubsb(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xE8, SET_VEX_PP_66);
}

/*VEX.128.66.0F.WIG D9 /r VPSUBUSW xmm1, xmm2, xmm3/m128
  VEX.256.66.0F.WIG D9 /r VPSUBUSW ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpsubusw(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xD9, SET_VEX_PP_66);
}

/*VEX.128.66.0F.WIG E9 /r VPSUBSW xmm1, xmm2, xmm3/m128
  VEX.256.66.0F.WIG E9 /r VPSUBSW ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpsubsw(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xE9, SET_VEX_PP_66);
}

void X86_64Assembler::cvtsi2ss(XmmRegister dst, CpuRegister src) {
  cvtsi2ss(dst, src, false);
}


void X86_64Assembler::cvtsi2ss(XmmRegister dst, CpuRegister src, bool is64bit) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  if (is64bit) {
    // Emit a REX.W prefix if the operand size is 64 bits.
    EmitRex64(dst, src);
  } else {
    EmitOptionalRex32(dst, src);
  }
  EmitUint8(0x0F);
  EmitUint8(0x2A);
  EmitOperand(dst.LowBits(), Operand(src));
}


void X86_64Assembler::cvtsi2ss(XmmRegister dst, const Address& src, bool is64bit) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  if (is64bit) {
    // Emit a REX.W prefix if the operand size is 64 bits.
    EmitRex64(dst, src);
  } else {
    EmitOptionalRex32(dst, src);
  }
  EmitUint8(0x0F);
  EmitUint8(0x2A);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::cvtsi2sd(XmmRegister dst, CpuRegister src) {
  cvtsi2sd(dst, src, false);
}


void X86_64Assembler::cvtsi2sd(XmmRegister dst, CpuRegister src, bool is64bit) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  if (is64bit) {
    // Emit a REX.W prefix if the operand size is 64 bits.
    EmitRex64(dst, src);
  } else {
    EmitOptionalRex32(dst, src);
  }
  EmitUint8(0x0F);
  EmitUint8(0x2A);
  EmitOperand(dst.LowBits(), Operand(src));
}


void X86_64Assembler::cvtsi2sd(XmmRegister dst, const Address& src, bool is64bit) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  if (is64bit) {
    // Emit a REX.W prefix if the operand size is 64 bits.
    EmitRex64(dst, src);
  } else {
    EmitOptionalRex32(dst, src);
  }
  EmitUint8(0x0F);
  EmitUint8(0x2A);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::cvtss2si(CpuRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x2D);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::cvtss2sd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5A);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::cvtss2sd(XmmRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5A);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::cvtsd2si(CpuRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x2D);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::cvttss2si(CpuRegister dst, XmmRegister src) {
  cvttss2si(dst, src, false);
}


void X86_64Assembler::cvttss2si(CpuRegister dst, XmmRegister src, bool is64bit) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  if (is64bit) {
    // Emit a REX.W prefix if the operand size is 64 bits.
    EmitRex64(dst, src);
  } else {
    EmitOptionalRex32(dst, src);
  }
  EmitUint8(0x0F);
  EmitUint8(0x2C);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::cvttsd2si(CpuRegister dst, XmmRegister src) {
  cvttsd2si(dst, src, false);
}


void X86_64Assembler::cvttsd2si(CpuRegister dst, XmmRegister src, bool is64bit) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  if (is64bit) {
    // Emit a REX.W prefix if the operand size is 64 bits.
    EmitRex64(dst, src);
  } else {
    EmitOptionalRex32(dst, src);
  }
  EmitUint8(0x0F);
  EmitUint8(0x2C);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::cvtsd2ss(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5A);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::cvtsd2ss(XmmRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5A);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::cvtdq2ps(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vcvtdq2ps(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5B);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vcvtdq2ps(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVXFeatureFlag());
  uint8_t VEX_L = GetEncodedVexLen(dst);
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  bool is_twobyte_form = false;
  uint8_t byte_zero = 0x00, byte_one = 0x00, byte_two = 0x00;
  if (!src.NeedsRex()) {
    is_twobyte_form = true;
  }
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
  if (is_twobyte_form) {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_NONE);
  } else {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                    /*X=*/false,
                                    src.NeedsRex(),
                                    SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_NONE);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }
  EmitUint8(0x5B);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::cvtdq2pd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xE6);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::comiss(XmmRegister a, XmmRegister b) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(a, b);
  EmitUint8(0x0F);
  EmitUint8(0x2F);
  EmitXmmRegisterOperand(a.LowBits(), b);
}


void X86_64Assembler::comiss(XmmRegister a, const Address& b) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(a, b);
  EmitUint8(0x0F);
  EmitUint8(0x2F);
  EmitOperand(a.LowBits(), b);
}


void X86_64Assembler::comisd(XmmRegister a, XmmRegister b) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(a, b);
  EmitUint8(0x0F);
  EmitUint8(0x2F);
  EmitXmmRegisterOperand(a.LowBits(), b);
}


void X86_64Assembler::comisd(XmmRegister a, const Address& b) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(a, b);
  EmitUint8(0x0F);
  EmitUint8(0x2F);
  EmitOperand(a.LowBits(), b);
}


void X86_64Assembler::ucomiss(XmmRegister a, XmmRegister b) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(a, b);
  EmitUint8(0x0F);
  EmitUint8(0x2E);
  EmitXmmRegisterOperand(a.LowBits(), b);
}


void X86_64Assembler::ucomiss(XmmRegister a, const Address& b) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(a, b);
  EmitUint8(0x0F);
  EmitUint8(0x2E);
  EmitOperand(a.LowBits(), b);
}


void X86_64Assembler::ucomisd(XmmRegister a, XmmRegister b) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(a, b);
  EmitUint8(0x0F);
  EmitUint8(0x2E);
  EmitXmmRegisterOperand(a.LowBits(), b);
}


void X86_64Assembler::ucomisd(XmmRegister a, const Address& b) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(a, b);
  EmitUint8(0x0F);
  EmitUint8(0x2E);
  EmitOperand(a.LowBits(), b);
}


void X86_64Assembler::roundsd(XmmRegister dst, XmmRegister src, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x3A);
  EmitUint8(0x0B);
  EmitXmmRegisterOperand(dst.LowBits(), src);
  EmitUint8(imm.value());
}


void X86_64Assembler::roundss(XmmRegister dst, XmmRegister src, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x3A);
  EmitUint8(0x0A);
  EmitXmmRegisterOperand(dst.LowBits(), src);
  EmitUint8(imm.value());
}


void X86_64Assembler::sqrtsd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x51);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::sqrtss(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x51);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::xorpd(XmmRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x57);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::xorpd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vxorpd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x57);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::xorps(XmmRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x57);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::xorps(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vxorps(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x57);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pxor(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpxor(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xEF);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

/* VEX.128.66.0F.WIG EF /r VPXOR xmm1, xmm2, xmm3/m128 */
void X86_64Assembler::vpxor(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xEF, SET_VEX_PP_66, /*is_commutative*/ true);
}

/* VEX.128.0F.WIG 57 /r VXORPS xmm1,xmm2, xmm3/m128 */
void X86_64Assembler::vxorps(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x57, SET_VEX_PP_NONE, /*is_commutative*/ true);
}

/* VEX.128.66.0F.WIG 57 /r VXORPD xmm1,xmm2, xmm3/m128 */
void X86_64Assembler::vxorpd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x57, SET_VEX_PP_66, /*is_commutative*/ true);
}

void X86_64Assembler::andpd(XmmRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x54);
  EmitOperand(dst.LowBits(), src);
}

void X86_64Assembler::andpd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vandpd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x54);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::andps(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vandps(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x54);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pand(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpand(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xDB);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

/* VEX.128.66.0F.WIG DB /r VPAND xmm1, xmm2, xmm3/m128 */
void X86_64Assembler::vpand(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xDB, SET_VEX_PP_66, /*is_commutative*/ true);
}

/* VEX.128.0F 54 /r VANDPS xmm1,xmm2, xmm3/m128 */
void X86_64Assembler::vandps(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x54, SET_VEX_PP_NONE, /*is_commutative*/ true);
}

/* VEX.128.66.0F 54 /r VANDPD xmm1, xmm2, xmm3/m128 */
void X86_64Assembler::vandpd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x54, SET_VEX_PP_66, /*is_commutative*/ true);
}

void X86_64Assembler::andn(CpuRegister dst, CpuRegister src1, CpuRegister src2) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t byte_zero = EmitVexPrefixByteZero(/*is_twobyte_form=*/ false);
  uint8_t byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                          /*X=*/ false,
                                          src2.NeedsRex(),
                                          SET_VEX_M_0F_38);
  uint8_t byte_two = EmitVexPrefixByteTwo(/*W=*/ true,
                                          X86_64ManagedRegister::FromCpuRegister(src1.AsRegister()),
                                          SET_VEX_L_128,
                                          SET_VEX_PP_NONE);
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  EmitUint8(byte_two);
  // Opcode field
  EmitUint8(0xF2);
  EmitRegisterOperand(dst.LowBits(), src2.LowBits());
}

void X86_64Assembler::andnpd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vandnpd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x55);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::andnps(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vandnps(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x55);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pandn(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpandn(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xDF);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

/* VEX.128.66.0F.WIG DF /r VPANDN xmm1, xmm2, xmm3/m128 */
void X86_64Assembler::vpandn(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xDF, SET_VEX_PP_66);
}

/* VEX.128.0F 55 /r VANDNPS xmm1, xmm2, xmm3/m128 */
void X86_64Assembler::vandnps(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x55, SET_VEX_PP_NONE);
}

/* VEX.128.66.0F 55 /r VANDNPD xmm1, xmm2, xmm3/m128 */
void X86_64Assembler::vandnpd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x55, SET_VEX_PP_66);
}

void X86_64Assembler::orpd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vorpd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x56);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::orps(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vorps(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x56);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::por(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpor(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xEB);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

/* VEX.128.66.0F.WIG EB /r VPOR xmm1, xmm2, xmm3/m128 */
void X86_64Assembler::vpor(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xEB, SET_VEX_PP_66, /*is_commutative*/ true);
}

/* VEX.128.0F 56 /r VORPS xmm1,xmm2, xmm3/m128 */
void X86_64Assembler::vorps(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x56, SET_VEX_PP_NONE, /*is_commutative*/ true);
}

/* VEX.128.66.0F 56 /r VORPD xmm1,xmm2, xmm3/m128 */
void X86_64Assembler::vorpd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x56, SET_VEX_PP_66, /*is_commutative*/ true);
}

void X86_64Assembler::pavgb(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpavgb(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xE0);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pavgw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpavgw(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xE3);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

/*VEX.128.66.0F.WIG E0 /r VPAVGB xmm1, xmm2, xmm3/m128
  VEX.256.66.0F.WIG E0 /r VPAVGB ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpavgb(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xE0, SET_VEX_PP_66, /*is_commutative*/ true);
}

/*VEX.128.66.0F.WIG E3 /r VPAVGW xmm1, xmm2, xmm3/m128
  VEX.256.66.0F.WIG E3 /r VPAVGW ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpavgw(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0xE3, SET_VEX_PP_66, /*is_commutative*/ true);
}

void X86_64Assembler::psadbw(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xF6);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pmaddwd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xF5);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vpmaddwd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  bool is_twobyte_form = false;
  uint8_t ByteZero = 0x00, ByteOne = 0x00, ByteTwo = 0x00;
  uint8_t VEX_L = GetEncodedVexLen(dst);
  if (!src2.NeedsRex()) {
    is_twobyte_form = true;
  } else if (!src1.NeedsRex()) {
    return vpmaddwd(dst, src2, src1);
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  ByteZero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg =
      X86_64ManagedRegister::FromXmmRegister(src1.AsFloatRegister());
  if (is_twobyte_form) {
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_66);
  } else {
    ByteOne = EmitVexPrefixByteOne(dst.NeedsRex(),
                                   /*X=*/ false,
                                   src2.NeedsRex(),
                                   SET_VEX_M_0F);
    ByteTwo = EmitVexPrefixByteTwo(/*W=*/false, vvvv_reg, VEX_L, SET_VEX_PP_66);
  }
  EmitUint8(ByteZero);
  EmitUint8(ByteOne);
  if (!is_twobyte_form) {
    EmitUint8(ByteTwo);
  }
  EmitUint8(0xF5);
  EmitXmmRegisterOperand(dst.LowBits(), src2);
}

void X86_64Assembler::phaddw(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x01);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::phaddd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x02);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

/*VEX.128.66.0F38.WIG 02 /r VPHADDD xmm1, xmm2, xmm3/m128
  VEX.256.66.0F38.WIG 02 /r VPHADDD ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vphaddd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  uint8_t byte_zero, byte_one, byte_two;
  uint8_t VEX_L = GetEncodedVexLen(dst);
  X86_64ManagedRegister vvvv_reg = X86_64ManagedRegister::FromXmmRegister(src1.AsFloatRegister());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);

  // Instruction VEX Prefix
  byte_zero = EmitVexPrefixByteZero(/*is_twobyte_form*/ false);
  byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                  /*X=*/false,
                                  src2.NeedsRex(),
                                  SET_VEX_M_0F_38);
  byte_two = EmitVexPrefixByteTwo(/*W=*/false, vvvv_reg, VEX_L, SET_VEX_PP_66);

  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  EmitUint8(byte_two);
  // Instruction Opcode
  EmitUint8(0x02);
  EmitXmmRegisterOperand(dst.LowBits(), src2);
}

void X86_64Assembler::haddps(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x7C);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::haddpd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x7C);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::phsubw(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x05);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::phsubd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x06);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::hsubps(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x7D);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::hsubpd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x7D);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pminsb(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpminsb(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x38);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pmaxsb(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpmaxsb(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x3C);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pminsw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpminsw(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xEA);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pmaxsw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpmaxsw(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xEE);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pminsd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpminsd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x39);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pmaxsd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpmaxsd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x3D);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pminub(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpminub(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xDA);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pmaxub(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpmaxub(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xDE);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pminuw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpminuw(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x3A);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pmaxuw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpmaxuw(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x3E);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pminud(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpminud(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x3B);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pmaxud(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpmaxud(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x3F);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::minps(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vminps(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5D);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::maxps(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vmaxps(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5F);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::minpd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vminpd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5D);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::maxpd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vmaxpd(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x5F);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pcmpeqb(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpcmpeqb(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x74);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pcmpeqw(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x75);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pcmpeqd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x76);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pcmpeqq(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x29);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vpcmpeqb(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecArithAndLogicalOperation(dst, src1, src2, 0x74, SET_VEX_PP_66, /*is_commutative*/ true);
}

void X86_64Assembler::pcmpgtb(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x64);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pcmpgtw(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x65);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pcmpgtd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x66);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::pcmpgtq(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpcmpgtq(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x37);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::vpcmpgtq(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecMinMaxOperation(dst, src1, src2, SET_VEX_PP_66, /*is_vex_3byte*/ true, /*opcode*/ 0x37);
}

void X86_64Assembler::shufpd(XmmRegister dst, XmmRegister src, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xC6);
  EmitXmmRegisterOperand(dst.LowBits(), src);
  EmitUint8(imm.value());
}


void X86_64Assembler::shufps(XmmRegister dst, XmmRegister src, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xC6);
  EmitXmmRegisterOperand(dst.LowBits(), src);
  EmitUint8(imm.value());
}


void X86_64Assembler::pshufd(XmmRegister dst, XmmRegister src, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x70);
  EmitXmmRegisterOperand(dst.LowBits(), src);
  EmitUint8(imm.value());
}


void X86_64Assembler::punpcklbw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpunpcklbw(dst, dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x60);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::punpcklwd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x61);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::punpckldq(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x62);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::punpcklqdq(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x6C);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::punpckhbw(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x68);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::punpckhwd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x69);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::punpckhdq(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x6A);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::punpckhqdq(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0x6D);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}


void X86_64Assembler::psllw(XmmRegister reg, const Immediate& shift_count) {
  DCHECK(shift_count.is_uint8());
  if (reg.IsYMM()) {
    vpsllw(reg, reg, shift_count);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, false, false, false, reg.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x71);
  EmitXmmRegisterOperand(6, reg);
  EmitUint8(shift_count.value());
}


void X86_64Assembler::pslld(XmmRegister reg, const Immediate& shift_count) {
  DCHECK(shift_count.is_uint8());
  if (reg.IsYMM()) {
    vpslld(reg, reg, shift_count);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, false, false, false, reg.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x72);
  EmitXmmRegisterOperand(6, reg);
  EmitUint8(shift_count.value());
}


void X86_64Assembler::psllq(XmmRegister reg, const Immediate& shift_count) {
  DCHECK(shift_count.is_uint8());
  if (reg.IsYMM()) {
    vpsllq(reg, reg, shift_count);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, false, false, false, reg.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x73);
  EmitXmmRegisterOperand(6, reg);
  EmitUint8(shift_count.value());
}

/*VEX.128.66.0F.WIG 71 /6 ib VPSLLW xmm1, xmm2, imm8
  VEX.256.66.0F.WIG 71 /6 ib VPSLLW ymm1, ymm2, imm8*/
void X86_64Assembler::vpsllw(XmmRegister dst, XmmRegister src, const Immediate& shift_count) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecShiftOperation(dst, src, shift_count, 0x71, 6);
}

/*VEX.128.66.0F.WIG 72 /6 ib VPSLLD xmm1, xmm2, imm8
  VEX.256.66.0F.WIG 72 /6 ib VPSLLD ymm1, ymm2, imm8*/
void X86_64Assembler::vpslld(XmmRegister dst, XmmRegister src, const Immediate& shift_count) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecShiftOperation(dst, src, shift_count, 0x72, 6);
}

/*VEX.128.66.0F.WIG 73 /6 ib VPSLLQ xmm1, xmm2, imm8
  VEX.256.66.0F.WIG 73 /6 ib VPSLLQ ymm1, ymm2, imm8*/
void X86_64Assembler::vpsllq(XmmRegister dst, XmmRegister src, const Immediate& shift_count) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecShiftOperation(dst, src, shift_count, 0x73, 6);
}

void X86_64Assembler::psraw(XmmRegister reg, const Immediate& shift_count) {
  DCHECK(shift_count.is_uint8());
  if (reg.IsYMM()) {
    vpsraw(reg, reg, shift_count);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, false, false, false, reg.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x71);
  EmitXmmRegisterOperand(4, reg);
  EmitUint8(shift_count.value());
}


void X86_64Assembler::psrad(XmmRegister reg, const Immediate& shift_count) {
  DCHECK(shift_count.is_uint8());
  if (reg.IsYMM()) {
    vpsrad(reg, reg, shift_count);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, false, false, false, reg.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x72);
  EmitXmmRegisterOperand(4, reg);
  EmitUint8(shift_count.value());
}

/*VEX.128.66.0F.WIG 71 /4 ib VPSRAW xmm1, xmm2, imm8
  VEX.256.66.0F.WIG 71 /4 ib VPSRAW ymm1, ymm2, imm8*/
void X86_64Assembler::vpsraw(XmmRegister dst, XmmRegister src, const Immediate& shift_count) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecShiftOperation(dst, src, shift_count, 0x71, 4);
}

/*VEX.128.66.0F.WIG 72 /4 ib VPSRAD xmm1, xmm2, imm8
  VEX.256.66.0F.WIG 72 /4 ib VPSRAD ymm1, ymm2, imm8*/
void X86_64Assembler::vpsrad(XmmRegister dst, XmmRegister src, const Immediate& shift_count) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecShiftOperation(dst, src, shift_count, 0x72, 4);
}

void X86_64Assembler::psrlw(XmmRegister reg, const Immediate& shift_count) {
  DCHECK(shift_count.is_uint8());
  if (reg.IsYMM()) {
    vpsrlw(reg, reg, shift_count);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, false, false, false, reg.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x71);
  EmitXmmRegisterOperand(2, reg);
  EmitUint8(shift_count.value());
}


void X86_64Assembler::psrld(XmmRegister reg, const Immediate& shift_count) {
  DCHECK(shift_count.is_uint8());
  if (reg.IsYMM()) {
    vpsrld(reg, reg, shift_count);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, false, false, false, reg.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x72);
  EmitXmmRegisterOperand(2, reg);
  EmitUint8(shift_count.value());
}


void X86_64Assembler::psrlq(XmmRegister reg, const Immediate& shift_count) {
  DCHECK(shift_count.is_uint8());
  if (reg.IsYMM()) {
    vpsrlq(reg, reg, shift_count);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, false, false, false, reg.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x73);
  EmitXmmRegisterOperand(2, reg);
  EmitUint8(shift_count.value());
}


void X86_64Assembler::psrldq(XmmRegister reg, const Immediate& shift_count) {
  DCHECK(shift_count.is_uint8());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, false, false, false, reg.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x73);
  EmitXmmRegisterOperand(3, reg);
  EmitUint8(shift_count.value());
}

/*VEX.128.66.0F.WIG 71 /2 ib VPSRLW xmm1, xmm2, imm8
  VEX.256.66.0F.WIG 71 /2 ib VPSRLW ymm1, ymm2, imm8*/
void X86_64Assembler::vpsrlw(XmmRegister dst, XmmRegister src, const Immediate& shift_count) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecShiftOperation(dst, src, shift_count, 0x71, 2);
}

/*VEX.128.66.0F.WIG 72 /2 ib VPSRLD xmm1, xmm2, imm8
  VEX.256.66.0F.WIG 72 /2 ib VPSRLD ymm1, ymm2, imm8*/
void X86_64Assembler::vpsrld(XmmRegister dst, XmmRegister src, const Immediate& shift_count) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecShiftOperation(dst, src, shift_count, 0x72, 2);
}

/*VEX.128.66.0F.WIG 73 /2 ib VPSRLQ xmm1, xmm2, imm8
  VEX.256.66.0F.WIG 73 /2 ib VPSRLQ ymm1, ymm2, imm8*/
void X86_64Assembler::vpsrlq(XmmRegister dst, XmmRegister src, const Immediate& shift_count) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecShiftOperation(dst, src, shift_count, 0x73, 2);
}

/*VEX.256.66.0F3A.W1 01 /r ib VPERMPD ymm1, ymm2/m256, imm8*/
void X86_64Assembler::vpermpd(XmmRegister dst, XmmRegister src, const Immediate& indices) {
  DCHECK(CpuHasAVX2FeatureFlag());
  DCHECK(dst.IsYMM());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t byte_zero = 0x00, byte_one = 0x00, byte_two = 0x00;
  uint8_t VEX_L = GetEncodedVexLen(dst);
  byte_zero = EmitVexPrefixByteZero(/*is_twobyte_form*/ false);
  byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                  /*X=*/false,
                                  src.NeedsRex(),
                                  SET_VEX_M_0F_3A);
  byte_two = EmitVexPrefixByteTwo(/*W=*/true, VEX_L, SET_VEX_PP_66);
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  EmitUint8(byte_two);
  EmitUint8(0x01);
  EmitXmmRegisterOperand(dst.LowBits(), src);
  EmitUint8(indices.value());
}

/*VEX.128.66.0F38 38 /r VPMINSB xmm1, xmm2, xmm3/m128
  VEX.256.66.0F38 38 /r VPMINSB ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpminsb(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecMinMaxOperation(dst, src1, src2, SET_VEX_PP_66, /*is_vex_3byte*/ true, /*opcode*/ 0x38);
}

/*VEX.128.66.0F38.WIG 3C /r VPMAXSB xmm1, xmm2, xmm3/m128
  VEX.256.66.0F38.WIG 3C /r VPMAXSB ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpmaxsb(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecMinMaxOperation(dst, src1, src2, SET_VEX_PP_66, /*is_vex_3byte*/ true, /*opcode*/ 0x3C);
}

/*VEX.128.66.0F EA /r VPMINSW xmm1, xmm2, xmm3/m128
  VEX.256.66.0F EA /r VPMINSW ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpminsw(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecMinMaxOperation(dst,
                         src1,
                         src2,
                         SET_VEX_PP_66,
                         /*is_vex_3byte*/ false,
                         /*opcode*/ 0xEA,
                         /*is_commutative*/ true);
}

/*VEX.128.66.0F.WIG EE /r VPMAXSW xmm1, xmm2, xmm3/m128
  VEX.256.66.0F.WIG EE /r VPMAXSW ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpmaxsw(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecMinMaxOperation(dst,
                         src1,
                         src2,
                         SET_VEX_PP_66,
                         /*is_vex_3byte*/ false,
                         /*opcode*/ 0xEE,
                         /*is_commutative*/ true);
}

/*VEX.128.66.0F38.WIG 39 /r VPMINSD xmm1, xmm2, xmm3/m128
  VEX.256.66.0F38.WIG 39 /r VPMINSD ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpminsd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecMinMaxOperation(dst, src1, src2, SET_VEX_PP_66, /*is_vex_3byte*/ true, /*opcode*/ 0x39);
}

/*VEX.128.66.0F38.WIG 3D /r VPMAXSD xmm1, xmm2, xmm3/m128
  VEX.256.66.0F38.WIG 3D /r VPMAXSD ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpmaxsd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecMinMaxOperation(dst, src1, src2, SET_VEX_PP_66, /*is_vex_3byte*/ true, /*opcode*/ 0x3D);
}

/*VEX.128.66.0F DA /r VPMINUB xmm1, xmm2, xmm3/m128
  VEX.256.66.0F DA /r VPMINUB ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpminub(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecMinMaxOperation(dst,
                         src1,
                         src2,
                         SET_VEX_PP_66,
                         /*is_vex_3byte*/ false,
                         /*opcode*/ 0xDA,
                         /*is_commutative*/ true);
}

/*VEX.128.66.0F DE /r VPMAXUB xmm1, xmm2, xmm3/m128
  VEX.256.66.0F DE /r VPMAXUB ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpmaxub(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecMinMaxOperation(dst,
                         src1,
                         src2,
                         SET_VEX_PP_66,
                         /*is_vex_3byte*/ false,
                         /*opcode*/ 0xDE,
                         /*is_commutative*/ true);
}

/*VEX.128.66.0F38 3A/r VPMINUW xmm1, xmm2, xmm3/m128
  VEX.256.66.0F38 3A/r VPMINUW ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpminuw(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecMinMaxOperation(dst, src1, src2, SET_VEX_PP_66, /*is_vex_3byte*/ true, /*opcode*/ 0x3A);
}

/*VEX.128.66.0F38 3E/r VPMAXUW xmm1, xmm2, xmm3/m128
  VEX.256.66.0F38 3E/r VPMAXUW ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpmaxuw(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecMinMaxOperation(dst, src1, src2, SET_VEX_PP_66, /*is_vex_3byte*/ true, /*opcode*/ 0x3E);
}

/*VEX.128.66.0F38.WIG 3B /r VPMINUD xmm1, xmm2, xmm3/m128
  VEX.256.66.0F38.WIG 3B /r VPMINUD ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpminud(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecMinMaxOperation(dst, src1, src2, SET_VEX_PP_66, /*is_vex_3byte*/ true, /*opcode*/ 0x3B);
}

/*VEX.128.66.0F38.WIG 3F /r VPMAXUD xmm1, xmm2, xmm3/m128
  VEX.256.66.0F38.WIG 3F /r VPMAXUD ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpmaxud(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecMinMaxOperation(dst, src1, src2, SET_VEX_PP_66, /*is_vex_3byte*/ true, /*opcode*/ 0x3F);
}

/*VEX.128.0F.WIG 5D /r VMINPS xmm1, xmm2, xmm3/m128
  VEX.256.0F.WIG 5D /r VMINPS ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vminps(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecMinMaxOperation(dst, src1, src2, SET_VEX_PP_NONE, /*is_vex_3byte*/ false, /*opcode*/ 0x5D);
}

/*VEX.128.0F.WIG 5F /r VMAXPS xmm1, xmm2, xmm3/m128
  VEX.256.0F.WIG 5F /r VMAXPS ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vmaxps(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecMinMaxOperation(dst, src1, src2, SET_VEX_PP_NONE, /*is_vex_3byte*/ false, /*opcode*/ 0x5F);
}

/*VEX.128.66.0F.WIG 5D /r VMINPD xmm1, xmm2, xmm3/m128
  VEX.256.66.0F.WIG 5D /r VMINPD ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vminpd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecMinMaxOperation(dst, src1, src2, SET_VEX_PP_66, /*is_vex_3byte*/ false, /*opcode*/ 0x5D);
}

/*VEX.128.66.0F.WIG 5F /r VMAXPD xmm1, xmm2, xmm3/m128
  VEX.256.66.0F.WIG 5F /r VMAXPD ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vmaxpd(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  EmitVecMinMaxOperation(dst, src1, src2, SET_VEX_PP_66, /*is_vex_3byte*/ false, /*opcode*/ 0x5F);
}

void X86_64Assembler::vbroadcastss(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVX2FeatureFlag());
  EmitVecBroadcastInstruction(dst, src, 0x18);
}

void X86_64Assembler::vbroadcastsd(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVX2FeatureFlag());
  EmitVecBroadcastInstruction(dst, src, 0x19);
}

void X86_64Assembler::vpbroadcastb(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVX2FeatureFlag());
  EmitVecBroadcastInstruction(dst, src, 0x78);
}

void X86_64Assembler::vpbroadcastw(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVX2FeatureFlag());
  EmitVecBroadcastInstruction(dst, src, 0x79);
}

void X86_64Assembler::vpbroadcastd(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVX2FeatureFlag());
  EmitVecBroadcastInstruction(dst, src, 0x58);
}

void X86_64Assembler::vpbroadcastq(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVX2FeatureFlag());
  EmitVecBroadcastInstruction(dst, src, 0x59);
}

void X86_64Assembler::pabsb(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpabsb(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, false, dst.NeedsRex(), false, src.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x1C);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}

void X86_64Assembler::pabsw(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpabsw(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, false, dst.NeedsRex(), false, src.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x1D);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}

void X86_64Assembler::pabsd(XmmRegister dst, XmmRegister src) {
  if (dst.IsYMM()) {
    vpabsd(dst, src);
    return;
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitOptionalRex(false, false, dst.NeedsRex(), false, src.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x1E);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}

void X86_64Assembler::vpabsb(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecBroadcastInstruction(dst, src, 0x1C);
}

void X86_64Assembler::vpabsw(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecBroadcastInstruction(dst, src, 0x1D);
}

void X86_64Assembler::vpabsd(XmmRegister dst, XmmRegister src) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  EmitVecBroadcastInstruction(dst, src, 0x1E);
}

/*VEX.128.66.0F.WIG 60/r VPUNPCKLBW xmm1,xmm2, xmm3/m128
  VEX.256.66.0F.WIG 60 /r VPUNPCKLBW ymm1, ymm2, ymm3/m256*/
void X86_64Assembler::vpunpcklbw(XmmRegister dst, XmmRegister src1, XmmRegister src2) {
  DCHECK(CpuHasAVX2FeatureFlag() || (!dst.IsYMM() && CpuHasAVXFeatureFlag()));
  uint8_t byte_zero, byte_one, byte_two;
  bool is_twobyte_form = true;
  uint8_t VEX_L = GetEncodedVexLen(dst);
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);

  if (src2.NeedsRex()) {
    is_twobyte_form = false;
  }
  // Instruction VEX Prefix
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg = X86_64ManagedRegister::FromXmmRegister(src1.AsFloatRegister());

  if (is_twobyte_form) {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_66);
  } else {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                    /*X=*/false,
                                    src2.NeedsRex(),
                                    SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/false, vvvv_reg, VEX_L, SET_VEX_PP_66);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }

  // Instruction Opcode
  EmitUint8(0x60);
  EmitXmmRegisterOperand(dst.LowBits(), src2);
}

void X86_64Assembler::fldl(const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDD);
  EmitOperand(0, src);
}


void X86_64Assembler::fstl(const Address& dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDD);
  EmitOperand(2, dst);
}


void X86_64Assembler::fstpl(const Address& dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDD);
  EmitOperand(3, dst);
}


void X86_64Assembler::fstsw() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x9B);
  EmitUint8(0xDF);
  EmitUint8(0xE0);
}


void X86_64Assembler::fnstcw(const Address& dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitOperand(7, dst);
}


void X86_64Assembler::fldcw(const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitOperand(5, src);
}


void X86_64Assembler::fistpl(const Address& dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDF);
  EmitOperand(7, dst);
}


void X86_64Assembler::fistps(const Address& dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDB);
  EmitOperand(3, dst);
}


void X86_64Assembler::fildl(const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDF);
  EmitOperand(5, src);
}


void X86_64Assembler::filds(const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDB);
  EmitOperand(0, src);
}


void X86_64Assembler::fincstp() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitUint8(0xF7);
}


void X86_64Assembler::ffree(const Immediate& index) {
  CHECK_LT(index.value(), 7);
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDD);
  EmitUint8(0xC0 + index.value());
}


void X86_64Assembler::fsin() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitUint8(0xFE);
}


void X86_64Assembler::fcos() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitUint8(0xFF);
}


void X86_64Assembler::fptan() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitUint8(0xF2);
}

void X86_64Assembler::fucompp() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDA);
  EmitUint8(0xE9);
}


void X86_64Assembler::fprem() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitUint8(0xF8);
}


bool X86_64Assembler::try_xchg_rax(CpuRegister dst,
                                   CpuRegister src,
                                   void (X86_64Assembler::*prefix_fn)(CpuRegister)) {
  Register src_reg = src.AsRegister();
  Register dst_reg = dst.AsRegister();
  if (src_reg != RAX && dst_reg != RAX) {
    return false;
  }
  if (dst_reg == RAX) {
    std::swap(src_reg, dst_reg);
  }
  if (dst_reg != RAX) {
    // Prefix is needed only if one of the registers is not RAX, otherwise it's a pure NOP.
    (this->*prefix_fn)(CpuRegister(dst_reg));
  }
  EmitUint8(0x90 + CpuRegister(dst_reg).LowBits());
  return true;
}


void X86_64Assembler::xchgb(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  // There is no short version for AL.
  EmitOptionalByteRegNormalizingRex32(dst, src, /*normalize_both=*/ true);
  EmitUint8(0x86);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::xchgb(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalByteRegNormalizingRex32(reg, address);
  EmitUint8(0x86);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::xchgw(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOperandSizeOverride();
  if (try_xchg_rax(dst, src, &X86_64Assembler::EmitOptionalRex32)) {
    // A short version for AX.
    return;
  }
  // General case.
  EmitOptionalRex32(dst, src);
  EmitUint8(0x87);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::xchgw(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOperandSizeOverride();
  EmitOptionalRex32(reg, address);
  EmitUint8(0x87);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::xchgl(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (try_xchg_rax(dst, src, &X86_64Assembler::EmitOptionalRex32)) {
    // A short version for EAX.
    return;
  }
  // General case.
  EmitOptionalRex32(dst, src);
  EmitUint8(0x87);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::xchgl(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg, address);
  EmitUint8(0x87);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::xchgq(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (try_xchg_rax(dst, src, &X86_64Assembler::EmitRex64)) {
    // A short version for RAX.
    return;
  }
  // General case.
  EmitRex64(dst, src);
  EmitUint8(0x87);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::xchgq(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg, address);
  EmitUint8(0x87);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::xaddb(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalByteRegNormalizingRex32(src, dst, /*normalize_both=*/ true);
  EmitUint8(0x0F);
  EmitUint8(0xC0);
  EmitRegisterOperand(src.LowBits(), dst.LowBits());
}


void X86_64Assembler::xaddb(const Address& address, CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalByteRegNormalizingRex32(reg, address);
  EmitUint8(0x0F);
  EmitUint8(0xC0);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::xaddw(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOperandSizeOverride();
  EmitOptionalRex32(src, dst);
  EmitUint8(0x0F);
  EmitUint8(0xC1);
  EmitRegisterOperand(src.LowBits(), dst.LowBits());
}


void X86_64Assembler::xaddw(const Address& address, CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOperandSizeOverride();
  EmitOptionalRex32(reg, address);
  EmitUint8(0x0F);
  EmitUint8(0xC1);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::xaddl(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(src, dst);
  EmitUint8(0x0F);
  EmitUint8(0xC1);
  EmitRegisterOperand(src.LowBits(), dst.LowBits());
}


void X86_64Assembler::xaddl(const Address& address, CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg, address);
  EmitUint8(0x0F);
  EmitUint8(0xC1);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::xaddq(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(src, dst);
  EmitUint8(0x0F);
  EmitUint8(0xC1);
  EmitRegisterOperand(src.LowBits(), dst.LowBits());
}


void X86_64Assembler::xaddq(const Address& address, CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg, address);
  EmitUint8(0x0F);
  EmitUint8(0xC1);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::cmpb(const Address& address, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());
  EmitOptionalRex32(address);
  EmitUint8(0x80);
  EmitOperand(7, address);
  EmitUint8(imm.value() & 0xFF);
}


void X86_64Assembler::cmpw(const Address& address, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());
  EmitOperandSizeOverride();
  EmitOptionalRex32(address);
  EmitComplex(7, address, imm, /* is_16_op= */ true);
}


void X86_64Assembler::cmpl(CpuRegister reg, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());
  EmitOptionalRex32(reg);
  EmitComplex(7, Operand(reg), imm);
}


void X86_64Assembler::cmpl(CpuRegister reg0, CpuRegister reg1) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg0, reg1);
  EmitUint8(0x3B);
  EmitOperand(reg0.LowBits(), Operand(reg1));
}


void X86_64Assembler::cmpl(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg, address);
  EmitUint8(0x3B);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::cmpl(const Address& address, CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg, address);
  EmitUint8(0x39);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::cmpl(const Address& address, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());
  EmitOptionalRex32(address);
  EmitComplex(7, address, imm);
}


void X86_64Assembler::cmpq(CpuRegister reg0, CpuRegister reg1) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg0, reg1);
  EmitUint8(0x3B);
  EmitOperand(reg0.LowBits(), Operand(reg1));
}


void X86_64Assembler::cmpq(CpuRegister reg, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());  // cmpq only supports 32b immediate.
  EmitRex64(reg);
  EmitComplex(7, Operand(reg), imm);
}


void X86_64Assembler::cmpq(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg, address);
  EmitUint8(0x3B);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::cmpq(const Address& address, const Immediate& imm) {
  CHECK(imm.is_int32());  // cmpq only supports 32b immediate.
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(address);
  EmitComplex(7, address, imm);
}


void X86_64Assembler::addl(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x03);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::addl(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg, address);
  EmitUint8(0x03);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::testl(CpuRegister reg1, CpuRegister reg2) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg1, reg2);
  EmitUint8(0x85);
  EmitRegisterOperand(reg1.LowBits(), reg2.LowBits());
}


void X86_64Assembler::testl(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg, address);
  EmitUint8(0x85);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::testl(CpuRegister reg, const Immediate& immediate) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  // For registers that have a byte variant (RAX, RBX, RCX, and RDX)
  // we only test the byte CpuRegister to keep the encoding short.
  if (immediate.is_uint8() && reg.AsRegister() < 4) {
    // Use zero-extended 8-bit immediate.
    if (reg.AsRegister() == RAX) {
      EmitUint8(0xA8);
    } else {
      EmitUint8(0xF6);
      EmitUint8(0xC0 + reg.AsRegister());
    }
    EmitUint8(immediate.value() & 0xFF);
  } else if (reg.AsRegister() == RAX) {
    // Use short form if the destination is RAX.
    EmitUint8(0xA9);
    EmitImmediate(immediate);
  } else {
    EmitOptionalRex32(reg);
    EmitUint8(0xF7);
    EmitOperand(0, Operand(reg));
    EmitImmediate(immediate);
  }
}


void X86_64Assembler::testq(CpuRegister reg1, CpuRegister reg2) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg1, reg2);
  EmitUint8(0x85);
  EmitRegisterOperand(reg1.LowBits(), reg2.LowBits());
}


void X86_64Assembler::testq(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg, address);
  EmitUint8(0x85);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::testb(const Address& dst, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst);
  EmitUint8(0xF6);
  EmitOperand(Register::RAX, dst);
  CHECK(imm.is_int8());
  EmitUint8(imm.value() & 0xFF);
}


void X86_64Assembler::testl(const Address& dst, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst);
  EmitUint8(0xF7);
  EmitOperand(0, dst);
  EmitImmediate(imm);
}


void X86_64Assembler::andl(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x23);
  EmitOperand(dst.LowBits(), Operand(src));
}


void X86_64Assembler::andl(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg, address);
  EmitUint8(0x23);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::andl(CpuRegister dst, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());  // andl only supports 32b immediate.
  EmitOptionalRex32(dst);
  EmitComplex(4, Operand(dst), imm);
}


void X86_64Assembler::andq(CpuRegister reg, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());  // andq only supports 32b immediate.
  EmitRex64(reg);
  EmitComplex(4, Operand(reg), imm);
}


void X86_64Assembler::andq(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x23);
  EmitOperand(dst.LowBits(), Operand(src));
}


void X86_64Assembler::andq(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x23);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::andw(const Address& address, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_uint16() || imm.is_int16()) << imm.value();
  EmitUint8(0x66);
  EmitOptionalRex32(address);
  EmitComplex(4, address, imm, /* is_16_op= */ true);
}


void X86_64Assembler::orl(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0B);
  EmitOperand(dst.LowBits(), Operand(src));
}


void X86_64Assembler::orl(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg, address);
  EmitUint8(0x0B);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::orl(CpuRegister dst, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst);
  EmitComplex(1, Operand(dst), imm);
}


void X86_64Assembler::orq(CpuRegister dst, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());  // orq only supports 32b immediate.
  EmitRex64(dst);
  EmitComplex(1, Operand(dst), imm);
}


void X86_64Assembler::orq(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x0B);
  EmitOperand(dst.LowBits(), Operand(src));
}


void X86_64Assembler::orq(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x0B);
  EmitOperand(dst.LowBits(), src);
}


void X86_64Assembler::xorl(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x33);
  EmitOperand(dst.LowBits(), Operand(src));
}


void X86_64Assembler::xorl(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg, address);
  EmitUint8(0x33);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::xorl(CpuRegister dst, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst);
  EmitComplex(6, Operand(dst), imm);
}


void X86_64Assembler::xorq(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x33);
  EmitOperand(dst.LowBits(), Operand(src));
}


void X86_64Assembler::xorq(CpuRegister dst, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());  // xorq only supports 32b immediate.
  EmitRex64(dst);
  EmitComplex(6, Operand(dst), imm);
}

void X86_64Assembler::xorq(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x33);
  EmitOperand(dst.LowBits(), src);
}


#if 0
void X86_64Assembler::rex(bool force, bool w, Register* r, Register* x, Register* b) {
  // REX.WRXB
  // W - 64-bit operand
  // R - MODRM.reg
  // X - SIB.index
  // B - MODRM.rm/SIB.base
  uint8_t rex = force ? 0x40 : 0;
  if (w) {
    rex |= 0x48;  // REX.W000
  }
  if (r != nullptr && *r >= Register::R8 && *r < Register::kNumberOfCpuRegisters) {
    rex |= 0x44;  // REX.0R00
    *r = static_cast<Register>(*r - 8);
  }
  if (x != nullptr && *x >= Register::R8 && *x < Register::kNumberOfCpuRegisters) {
    rex |= 0x42;  // REX.00X0
    *x = static_cast<Register>(*x - 8);
  }
  if (b != nullptr && *b >= Register::R8 && *b < Register::kNumberOfCpuRegisters) {
    rex |= 0x41;  // REX.000B
    *b = static_cast<Register>(*b - 8);
  }
  if (rex != 0) {
    EmitUint8(rex);
  }
}

void X86_64Assembler::rex_reg_mem(bool force, bool w, Register* dst, const Address& mem) {
  // REX.WRXB
  // W - 64-bit operand
  // R - MODRM.reg
  // X - SIB.index
  // B - MODRM.rm/SIB.base
  uint8_t rex = mem->rex();
  if (force) {
    rex |= 0x40;  // REX.0000
  }
  if (w) {
    rex |= 0x48;  // REX.W000
  }
  if (dst != nullptr && *dst >= Register::R8 && *dst < Register::kNumberOfCpuRegisters) {
    rex |= 0x44;  // REX.0R00
    *dst = static_cast<Register>(*dst - 8);
  }
  if (rex != 0) {
    EmitUint8(rex);
  }
}

void rex_mem_reg(bool force, bool w, Address* mem, Register* src);
#endif

void X86_64Assembler::addl(CpuRegister reg, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg);
  EmitComplex(0, Operand(reg), imm);
}


void X86_64Assembler::addw(CpuRegister reg, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_uint16() || imm.is_int16()) << imm.value();
  EmitUint8(0x66);
  EmitOptionalRex32(reg);
  EmitComplex(0, Operand(reg), imm, /* is_16_op= */ true);
}


void X86_64Assembler::addq(CpuRegister reg, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());  // addq only supports 32b immediate.
  EmitRex64(reg);
  EmitComplex(0, Operand(reg), imm);
}


void X86_64Assembler::addq(CpuRegister dst, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, address);
  EmitUint8(0x03);
  EmitOperand(dst.LowBits(), address);
}


void X86_64Assembler::addq(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  // 0x01 is addq r/m64 <- r/m64 + r64, with op1 in r/m and op2 in reg: so reverse EmitRex64
  EmitRex64(src, dst);
  EmitUint8(0x01);
  EmitRegisterOperand(src.LowBits(), dst.LowBits());
}


void X86_64Assembler::addl(const Address& address, CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg, address);
  EmitUint8(0x01);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::addl(const Address& address, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(address);
  EmitComplex(0, address, imm);
}


void X86_64Assembler::addw(const Address& address, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_uint16() || imm.is_int16()) << imm.value();
  EmitUint8(0x66);
  EmitOptionalRex32(address);
  EmitComplex(0, address, imm, /* is_16_op= */ true);
}


void X86_64Assembler::addw(const Address& address, CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOperandSizeOverride();
  EmitOptionalRex32(reg, address);
  EmitUint8(0x01);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::subl(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x2B);
  EmitOperand(dst.LowBits(), Operand(src));
}


void X86_64Assembler::subl(CpuRegister reg, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg);
  EmitComplex(5, Operand(reg), imm);
}


void X86_64Assembler::subq(CpuRegister reg, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());  // subq only supports 32b immediate.
  EmitRex64(reg);
  EmitComplex(5, Operand(reg), imm);
}


void X86_64Assembler::subq(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x2B);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::subq(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg, address);
  EmitUint8(0x2B);
  EmitOperand(reg.LowBits() & 7, address);
}


void X86_64Assembler::subl(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg, address);
  EmitUint8(0x2B);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::cdq() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x99);
}


void X86_64Assembler::cqo() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64();
  EmitUint8(0x99);
}


void X86_64Assembler::idivl(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg);
  EmitUint8(0xF7);
  EmitUint8(0xF8 | reg.LowBits());
}


void X86_64Assembler::idivq(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg);
  EmitUint8(0xF7);
  EmitUint8(0xF8 | reg.LowBits());
}


void X86_64Assembler::divl(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg);
  EmitUint8(0xF7);
  EmitUint8(0xF0 | reg.LowBits());
}


void X86_64Assembler::divq(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg);
  EmitUint8(0xF7);
  EmitUint8(0xF0 | reg.LowBits());
}


void X86_64Assembler::imull(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xAF);
  EmitOperand(dst.LowBits(), Operand(src));
}

void X86_64Assembler::imull(CpuRegister dst, CpuRegister src, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());  // imull only supports 32b immediate.

  EmitOptionalRex32(dst, src);

  // See whether imm can be represented as a sign-extended 8bit value.
  int32_t v32 = static_cast<int32_t>(imm.value());
  if (IsInt<8>(v32)) {
    // Sign-extension works.
    EmitUint8(0x6B);
    EmitOperand(dst.LowBits(), Operand(src));
    EmitUint8(static_cast<uint8_t>(v32 & 0xFF));
  } else {
    // Not representable, use full immediate.
    EmitUint8(0x69);
    EmitOperand(dst.LowBits(), Operand(src));
    EmitImmediate(imm);
  }
}


void X86_64Assembler::imull(CpuRegister reg, const Immediate& imm) {
  imull(reg, reg, imm);
}


void X86_64Assembler::imull(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg, address);
  EmitUint8(0x0F);
  EmitUint8(0xAF);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::imulq(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xAF);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}


void X86_64Assembler::imulq(CpuRegister reg, const Immediate& imm) {
  imulq(reg, reg, imm);
}

void X86_64Assembler::imulq(CpuRegister dst, CpuRegister reg, const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int32());  // imulq only supports 32b immediate.

  EmitRex64(dst, reg);

  // See whether imm can be represented as a sign-extended 8bit value.
  int64_t v64 = imm.value();
  if (IsInt<8>(v64)) {
    // Sign-extension works.
    EmitUint8(0x6B);
    EmitOperand(dst.LowBits(), Operand(reg));
    EmitUint8(static_cast<uint8_t>(v64 & 0xFF));
  } else {
    // Not representable, use full immediate.
    EmitUint8(0x69);
    EmitOperand(dst.LowBits(), Operand(reg));
    EmitImmediate(imm);
  }
}

void X86_64Assembler::imulq(CpuRegister reg, const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg, address);
  EmitUint8(0x0F);
  EmitUint8(0xAF);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::imull(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg);
  EmitUint8(0xF7);
  EmitOperand(5, Operand(reg));
}


void X86_64Assembler::imulq(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg);
  EmitUint8(0xF7);
  EmitOperand(5, Operand(reg));
}


void X86_64Assembler::imull(const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(address);
  EmitUint8(0xF7);
  EmitOperand(5, address);
}


void X86_64Assembler::mull(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg);
  EmitUint8(0xF7);
  EmitOperand(4, Operand(reg));
}


void X86_64Assembler::mull(const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(address);
  EmitUint8(0xF7);
  EmitOperand(4, address);
}


void X86_64Assembler::shll(CpuRegister reg, const Immediate& imm) {
  EmitGenericShift(false, 4, reg, imm);
}


void X86_64Assembler::shlq(CpuRegister reg, const Immediate& imm) {
  EmitGenericShift(true, 4, reg, imm);
}


void X86_64Assembler::shll(CpuRegister operand, CpuRegister shifter) {
  EmitGenericShift(false, 4, operand, shifter);
}


void X86_64Assembler::shlq(CpuRegister operand, CpuRegister shifter) {
  EmitGenericShift(true, 4, operand, shifter);
}


void X86_64Assembler::shrl(CpuRegister reg, const Immediate& imm) {
  EmitGenericShift(false, 5, reg, imm);
}


void X86_64Assembler::shrq(CpuRegister reg, const Immediate& imm) {
  EmitGenericShift(true, 5, reg, imm);
}


void X86_64Assembler::shrl(CpuRegister operand, CpuRegister shifter) {
  EmitGenericShift(false, 5, operand, shifter);
}


void X86_64Assembler::shrq(CpuRegister operand, CpuRegister shifter) {
  EmitGenericShift(true, 5, operand, shifter);
}


void X86_64Assembler::sarl(CpuRegister reg, const Immediate& imm) {
  EmitGenericShift(false, 7, reg, imm);
}


void X86_64Assembler::sarl(CpuRegister operand, CpuRegister shifter) {
  EmitGenericShift(false, 7, operand, shifter);
}


void X86_64Assembler::sarq(CpuRegister reg, const Immediate& imm) {
  EmitGenericShift(true, 7, reg, imm);
}


void X86_64Assembler::sarq(CpuRegister operand, CpuRegister shifter) {
  EmitGenericShift(true, 7, operand, shifter);
}


void X86_64Assembler::roll(CpuRegister reg, const Immediate& imm) {
  EmitGenericShift(false, 0, reg, imm);
}


void X86_64Assembler::roll(CpuRegister operand, CpuRegister shifter) {
  EmitGenericShift(false, 0, operand, shifter);
}


void X86_64Assembler::rorl(CpuRegister reg, const Immediate& imm) {
  EmitGenericShift(false, 1, reg, imm);
}


void X86_64Assembler::rorl(CpuRegister operand, CpuRegister shifter) {
  EmitGenericShift(false, 1, operand, shifter);
}


void X86_64Assembler::rolq(CpuRegister reg, const Immediate& imm) {
  EmitGenericShift(true, 0, reg, imm);
}


void X86_64Assembler::rolq(CpuRegister operand, CpuRegister shifter) {
  EmitGenericShift(true, 0, operand, shifter);
}


void X86_64Assembler::rorq(CpuRegister reg, const Immediate& imm) {
  EmitGenericShift(true, 1, reg, imm);
}


void X86_64Assembler::rorq(CpuRegister operand, CpuRegister shifter) {
  EmitGenericShift(true, 1, operand, shifter);
}


void X86_64Assembler::negl(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg);
  EmitUint8(0xF7);
  EmitOperand(3, Operand(reg));
}


void X86_64Assembler::negq(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg);
  EmitUint8(0xF7);
  EmitOperand(3, Operand(reg));
}


void X86_64Assembler::notl(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg);
  EmitUint8(0xF7);
  EmitUint8(0xD0 | reg.LowBits());
}


void X86_64Assembler::notq(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg);
  EmitUint8(0xF7);
  EmitOperand(2, Operand(reg));
}


void X86_64Assembler::enter(const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xC8);
  CHECK(imm.is_uint16()) << imm.value();
  EmitUint8(imm.value() & 0xFF);
  EmitUint8((imm.value() >> 8) & 0xFF);
  EmitUint8(0x00);
}


void X86_64Assembler::leave() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xC9);
}


void X86_64Assembler::ret() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xC3);
}


void X86_64Assembler::ret(const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xC2);
  CHECK(imm.is_uint16());
  EmitUint8(imm.value() & 0xFF);
  EmitUint8((imm.value() >> 8) & 0xFF);
}



void X86_64Assembler::nop() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x90);
}


void X86_64Assembler::int3() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xCC);
}


void X86_64Assembler::hlt() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF4);
}


void X86_64Assembler::j(Condition condition, Label* label) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (label->IsBound()) {
    static const int kShortSize = 2;
    static const int kLongSize = 6;
    int offset = label->Position() - buffer_.Size();
    CHECK_LE(offset, 0);
    if (IsInt<8>(offset - kShortSize)) {
      EmitUint8(0x70 + condition);
      EmitUint8((offset - kShortSize) & 0xFF);
    } else {
      EmitUint8(0x0F);
      EmitUint8(0x80 + condition);
      EmitInt32(offset - kLongSize);
    }
  } else {
    EmitUint8(0x0F);
    EmitUint8(0x80 + condition);
    EmitLabelLink(label);
  }
}


void X86_64Assembler::j(Condition condition, NearLabel* label) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (label->IsBound()) {
    static const int kShortSize = 2;
    int offset = label->Position() - buffer_.Size();
    CHECK_LE(offset, 0);
    CHECK(IsInt<8>(offset - kShortSize));
    EmitUint8(0x70 + condition);
    EmitUint8((offset - kShortSize) & 0xFF);
  } else {
    EmitUint8(0x70 + condition);
    EmitLabelLink(label);
  }
}


void X86_64Assembler::jrcxz(NearLabel* label) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (label->IsBound()) {
    static const int kShortSize = 2;
    int offset = label->Position() - buffer_.Size();
    CHECK_LE(offset, 0);
    CHECK(IsInt<8>(offset - kShortSize));
    EmitUint8(0xE3);
    EmitUint8((offset - kShortSize) & 0xFF);
  } else {
    EmitUint8(0xE3);
    EmitLabelLink(label);
  }
}


void X86_64Assembler::jmp(CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg);
  EmitUint8(0xFF);
  EmitRegisterOperand(4, reg.LowBits());
}

void X86_64Assembler::jmp(const Address& address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(address);
  EmitUint8(0xFF);
  EmitOperand(4, address);
}

void X86_64Assembler::jmp(Label* label) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (label->IsBound()) {
    static const int kShortSize = 2;
    static const int kLongSize = 5;
    int offset = label->Position() - buffer_.Size();
    CHECK_LE(offset, 0);
    if (IsInt<8>(offset - kShortSize)) {
      EmitUint8(0xEB);
      EmitUint8((offset - kShortSize) & 0xFF);
    } else {
      EmitUint8(0xE9);
      EmitInt32(offset - kLongSize);
    }
  } else {
    EmitUint8(0xE9);
    EmitLabelLink(label);
  }
}


void X86_64Assembler::jmp(NearLabel* label) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (label->IsBound()) {
    static const int kShortSize = 2;
    int offset = label->Position() - buffer_.Size();
    CHECK_LE(offset, 0);
    CHECK(IsInt<8>(offset - kShortSize));
    EmitUint8(0xEB);
    EmitUint8((offset - kShortSize) & 0xFF);
  } else {
    EmitUint8(0xEB);
    EmitLabelLink(label);
  }
}


void X86_64Assembler::rep_movsw() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0xF3);
  EmitUint8(0xA5);
}

void X86_64Assembler::rep_movsb() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitUint8(0xA4);
}

void X86_64Assembler::rep_movsl() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitUint8(0xA5);
}

X86_64Assembler* X86_64Assembler::lock() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF0);
  return this;
}


void X86_64Assembler::cmpxchgb(const Address& address, CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalByteRegNormalizingRex32(reg, address);
  EmitUint8(0x0F);
  EmitUint8(0xB0);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::cmpxchgw(const Address& address, CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOperandSizeOverride();
  EmitOptionalRex32(reg, address);
  EmitUint8(0x0F);
  EmitUint8(0xB1);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::cmpxchgl(const Address& address, CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(reg, address);
  EmitUint8(0x0F);
  EmitUint8(0xB1);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::cmpxchgq(const Address& address, CpuRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(reg, address);
  EmitUint8(0x0F);
  EmitUint8(0xB1);
  EmitOperand(reg.LowBits(), address);
}


void X86_64Assembler::mfence() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xAE);
  EmitUint8(0xF0);
}


X86_64Assembler* X86_64Assembler::gs() {
  // TODO: gs is a prefix and not an instruction
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x65);
  return this;
}


void X86_64Assembler::AddImmediate(CpuRegister reg, const Immediate& imm) {
  int value = imm.value();
  if (value != 0) {
    if (value > 0) {
      addl(reg, imm);
    } else {
      subl(reg, Immediate(value));
    }
  }
}


void X86_64Assembler::setcc(Condition condition, CpuRegister dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  // RSP, RBP, RDI, RSI need rex prefix (else the pattern encodes ah/bh/ch/dh).
  if (dst.NeedsRex() || dst.AsRegister() > 3) {
    EmitOptionalRex(true, false, false, false, dst.NeedsRex());
  }
  EmitUint8(0x0F);
  EmitUint8(0x90 + condition);
  EmitUint8(0xC0 + dst.LowBits());
}

void X86_64Assembler::blsi(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t byte_zero = EmitVexPrefixByteZero(/*is_twobyte_form=*/ false);
  uint8_t byte_one = EmitVexPrefixByteOne(/*R=*/ false,
                                          /*X=*/ false,
                                          src.NeedsRex(),
                                          SET_VEX_M_0F_38);
  uint8_t byte_two = EmitVexPrefixByteTwo(/*W=*/true,
                                          X86_64ManagedRegister::FromCpuRegister(dst.AsRegister()),
                                          SET_VEX_L_128,
                                          SET_VEX_PP_NONE);
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  EmitUint8(byte_two);
  EmitUint8(0xF3);
  EmitRegisterOperand(3, src.LowBits());
}

void X86_64Assembler::blsmsk(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t byte_zero = EmitVexPrefixByteZero(/*is_twobyte_form=*/ false);
  uint8_t byte_one = EmitVexPrefixByteOne(/*R=*/ false,
                                          /*X=*/ false,
                                          src.NeedsRex(),
                                          SET_VEX_M_0F_38);
  uint8_t byte_two = EmitVexPrefixByteTwo(/*W=*/ true,
                                          X86_64ManagedRegister::FromCpuRegister(dst.AsRegister()),
                                          SET_VEX_L_128,
                                          SET_VEX_PP_NONE);
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  EmitUint8(byte_two);
  EmitUint8(0xF3);
  EmitRegisterOperand(2, src.LowBits());
}

void X86_64Assembler::blsr(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t byte_zero = EmitVexPrefixByteZero(/*is_twobyte_form=*/false);
  uint8_t byte_one = EmitVexPrefixByteOne(/*R=*/ false,
                                          /*X=*/ false,
                                          src.NeedsRex(),
                                          SET_VEX_M_0F_38);
  uint8_t byte_two = EmitVexPrefixByteTwo(/*W=*/ true,
                                          X86_64ManagedRegister::FromCpuRegister(dst.AsRegister()),
                                          SET_VEX_L_128,
                                          SET_VEX_PP_NONE);
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  EmitUint8(byte_two);
  EmitUint8(0xF3);
  EmitRegisterOperand(1, src.LowBits());
}

void X86_64Assembler::bswapl(CpuRegister dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex(false, false, false, false, dst.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0xC8 + dst.LowBits());
}

void X86_64Assembler::bswapq(CpuRegister dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex(false, true, false, false, dst.NeedsRex());
  EmitUint8(0x0F);
  EmitUint8(0xC8 + dst.LowBits());
}

void X86_64Assembler::bsfl(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xBC);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}

void X86_64Assembler::bsfl(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xBC);
  EmitOperand(dst.LowBits(), src);
}

void X86_64Assembler::bsfq(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xBC);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}

void X86_64Assembler::bsfq(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xBC);
  EmitOperand(dst.LowBits(), src);
}

void X86_64Assembler::bsrl(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xBD);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}

void X86_64Assembler::bsrl(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xBD);
  EmitOperand(dst.LowBits(), src);
}

void X86_64Assembler::bsrq(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xBD);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}

void X86_64Assembler::bsrq(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitRex64(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xBD);
  EmitOperand(dst.LowBits(), src);
}

void X86_64Assembler::popcntl(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xB8);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}

void X86_64Assembler::popcntl(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitOptionalRex32(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xB8);
  EmitOperand(dst.LowBits(), src);
}

void X86_64Assembler::popcntq(CpuRegister dst, CpuRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitRex64(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xB8);
  EmitRegisterOperand(dst.LowBits(), src.LowBits());
}

void X86_64Assembler::popcntq(CpuRegister dst, const Address& src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitRex64(dst, src);
  EmitUint8(0x0F);
  EmitUint8(0xB8);
  EmitOperand(dst.LowBits(), src);
}

void X86_64Assembler::rdtsc() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x31);
}

void X86_64Assembler::repne_scasb() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitUint8(0xAE);
}

void X86_64Assembler::repne_scasw() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0xF2);
  EmitUint8(0xAF);
}

void X86_64Assembler::repe_cmpsw() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0xF3);
  EmitUint8(0xA7);
}


void X86_64Assembler::repe_cmpsl() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitUint8(0xA7);
}


void X86_64Assembler::repe_cmpsq() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitRex64();
  EmitUint8(0xA7);
}

void X86_64Assembler::ud2() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x0B);
}

void X86_64Assembler::vzeroupper() {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xC5);
  EmitUint8(0xF8);
  EmitUint8(0x77);
}

void X86_64Assembler::LoadDoubleConstant(XmmRegister dst, double value) {
  // TODO: Need to have a code constants table.
  int64_t constant = bit_cast<int64_t, double>(value);
  pushq(Immediate(High32Bits(constant)));
  pushq(Immediate(Low32Bits(constant)));
  movsd(dst, Address(CpuRegister(RSP), 0));
  addq(CpuRegister(RSP), Immediate(2 * sizeof(intptr_t)));
}


void X86_64Assembler::Align(int alignment, int offset) {
  CHECK(IsPowerOfTwo(alignment));
  // Emit nop instruction until the real position is aligned.
  while (((offset + buffer_.GetPosition()) & (alignment-1)) != 0) {
    nop();
  }
}


void X86_64Assembler::Bind(Label* label) {
  int bound = buffer_.Size();
  CHECK(!label->IsBound());  // Labels can only be bound once.
  while (label->IsLinked()) {
    int position = label->LinkPosition();
    int next = buffer_.Load<int32_t>(position);
    buffer_.Store<int32_t>(position, bound - (position + 4));
    label->position_ = next;
  }
  label->BindTo(bound);
}


void X86_64Assembler::Bind(NearLabel* label) {
  int bound = buffer_.Size();
  CHECK(!label->IsBound());  // Labels can only be bound once.
  while (label->IsLinked()) {
    int position = label->LinkPosition();
    uint8_t delta = buffer_.Load<uint8_t>(position);
    int offset = bound - (position + 1);
    CHECK(IsInt<8>(offset));
    buffer_.Store<int8_t>(position, offset);
    label->position_ = delta != 0u ? label->position_ - delta : 0;
  }
  label->BindTo(bound);
}


void X86_64Assembler::EmitOperand(uint8_t reg_or_opcode, const Operand& operand) {
  CHECK_GE(reg_or_opcode, 0);
  CHECK_LT(reg_or_opcode, 8);
  const int length = operand.length_;
  CHECK_GT(length, 0);
  // Emit the ModRM byte updated with the given reg value.
  CHECK_EQ(operand.encoding_[0] & 0x38, 0);
  EmitUint8(operand.encoding_[0] + (reg_or_opcode << 3));
  // Emit the rest of the encoded operand.
  for (int i = 1; i < length; i++) {
    EmitUint8(operand.encoding_[i]);
  }
  AssemblerFixup* fixup = operand.GetFixup();
  if (fixup != nullptr) {
    EmitFixup(fixup);
  }
}


void X86_64Assembler::EmitImmediate(const Immediate& imm, bool is_16_op) {
  if (is_16_op) {
    EmitUint8(imm.value() & 0xFF);
    EmitUint8(imm.value() >> 8);
  } else if (imm.is_int32()) {
    EmitInt32(static_cast<int32_t>(imm.value()));
  } else {
    EmitInt64(imm.value());
  }
}


void X86_64Assembler::EmitComplex(uint8_t reg_or_opcode,
                                  const Operand& operand,
                                  const Immediate& immediate,
                                  bool is_16_op) {
  CHECK_GE(reg_or_opcode, 0);
  CHECK_LT(reg_or_opcode, 8);
  if (immediate.is_int8()) {
    // Use sign-extended 8-bit immediate.
    EmitUint8(0x83);
    EmitOperand(reg_or_opcode, operand);
    EmitUint8(immediate.value() & 0xFF);
  } else if (operand.IsRegister(CpuRegister(RAX))) {
    // Use short form if the destination is eax.
    EmitUint8(0x05 + (reg_or_opcode << 3));
    EmitImmediate(immediate, is_16_op);
  } else {
    EmitUint8(0x81);
    EmitOperand(reg_or_opcode, operand);
    EmitImmediate(immediate, is_16_op);
  }
}


void X86_64Assembler::EmitLabel(Label* label, int instruction_size) {
  if (label->IsBound()) {
    int offset = label->Position() - buffer_.Size();
    CHECK_LE(offset, 0);
    EmitInt32(offset - instruction_size);
  } else {
    EmitLabelLink(label);
  }
}


void X86_64Assembler::EmitLabelLink(Label* label) {
  CHECK(!label->IsBound());
  int position = buffer_.Size();
  EmitInt32(label->position_);
  label->LinkTo(position);
}


void X86_64Assembler::EmitLabelLink(NearLabel* label) {
  CHECK(!label->IsBound());
  int position = buffer_.Size();
  if (label->IsLinked()) {
    // Save the delta in the byte that we have to play with.
    uint32_t delta = position - label->LinkPosition();
    CHECK(IsUint<8>(delta));
    EmitUint8(delta & 0xFF);
  } else {
    EmitUint8(0);
  }
  label->LinkTo(position);
}


void X86_64Assembler::EmitGenericShift(bool wide,
                                       int reg_or_opcode,
                                       CpuRegister reg,
                                       const Immediate& imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK(imm.is_int8());
  if (wide) {
    EmitRex64(reg);
  } else {
    EmitOptionalRex32(reg);
  }
  if (imm.value() == 1) {
    EmitUint8(0xD1);
    EmitOperand(reg_or_opcode, Operand(reg));
  } else {
    EmitUint8(0xC1);
    EmitOperand(reg_or_opcode, Operand(reg));
    EmitUint8(imm.value() & 0xFF);
  }
}


void X86_64Assembler::EmitGenericShift(bool wide,
                                       int reg_or_opcode,
                                       CpuRegister operand,
                                       CpuRegister shifter) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  CHECK_EQ(shifter.AsRegister(), RCX);
  if (wide) {
    EmitRex64(operand);
  } else {
    EmitOptionalRex32(operand);
  }
  EmitUint8(0xD3);
  EmitOperand(reg_or_opcode, Operand(operand));
}

void X86_64Assembler::EmitOptionalRex(bool force, bool w, bool r, bool x, bool b) {
  // REX.WRXB
  // W - 64-bit operand
  // R - MODRM.reg
  // X - SIB.index
  // B - MODRM.rm/SIB.base
  uint8_t rex = force ? 0x40 : 0;
  if (w) {
    rex |= 0x48;  // REX.W000
  }
  if (r) {
    rex |= 0x44;  // REX.0R00
  }
  if (x) {
    rex |= 0x42;  // REX.00X0
  }
  if (b) {
    rex |= 0x41;  // REX.000B
  }
  if (rex != 0) {
    EmitUint8(rex);
  }
}

void X86_64Assembler::EmitOptionalRex32(CpuRegister reg) {
  EmitOptionalRex(false, false, false, false, reg.NeedsRex());
}

void X86_64Assembler::EmitOptionalRex32(CpuRegister dst, CpuRegister src) {
  EmitOptionalRex(false, false, dst.NeedsRex(), false, src.NeedsRex());
}

void X86_64Assembler::EmitOptionalRex32(XmmRegister dst, XmmRegister src) {
  EmitOptionalRex(false, false, dst.NeedsRex(), false, src.NeedsRex());
}

void X86_64Assembler::EmitOptionalRex32(CpuRegister dst, XmmRegister src) {
  EmitOptionalRex(false, false, dst.NeedsRex(), false, src.NeedsRex());
}

void X86_64Assembler::EmitOptionalRex32(XmmRegister dst, CpuRegister src) {
  EmitOptionalRex(false, false, dst.NeedsRex(), false, src.NeedsRex());
}

void X86_64Assembler::EmitOptionalRex32(const Operand& operand) {
  uint8_t rex = operand.rex();
  if (rex != 0) {
    EmitUint8(rex);
  }
}

void X86_64Assembler::EmitOptionalRex32(CpuRegister dst, const Operand& operand) {
  uint8_t rex = operand.rex();
  if (dst.NeedsRex()) {
    rex |= 0x44;  // REX.0R00
  }
  if (rex != 0) {
    EmitUint8(rex);
  }
}

void X86_64Assembler::EmitOptionalRex32(XmmRegister dst, const Operand& operand) {
  uint8_t rex = operand.rex();
  if (dst.NeedsRex()) {
    rex |= 0x44;  // REX.0R00
  }
  if (rex != 0) {
    EmitUint8(rex);
  }
}

void X86_64Assembler::EmitRex64() {
  EmitOptionalRex(false, true, false, false, false);
}

void X86_64Assembler::EmitRex64(CpuRegister reg) {
  EmitOptionalRex(false, true, false, false, reg.NeedsRex());
}

void X86_64Assembler::EmitRex64(const Operand& operand) {
  uint8_t rex = operand.rex();
  rex |= 0x48;  // REX.W000
  EmitUint8(rex);
}

void X86_64Assembler::EmitRex64(CpuRegister dst, CpuRegister src) {
  EmitOptionalRex(false, true, dst.NeedsRex(), false, src.NeedsRex());
}

void X86_64Assembler::EmitRex64(XmmRegister dst, CpuRegister src) {
  EmitOptionalRex(false, true, dst.NeedsRex(), false, src.NeedsRex());
}

void X86_64Assembler::EmitRex64(CpuRegister dst, XmmRegister src) {
  EmitOptionalRex(false, true, dst.NeedsRex(), false, src.NeedsRex());
}

void X86_64Assembler::EmitRex64(XmmRegister dst, XmmRegister src) {
  EmitOptionalRex(false, true, dst.NeedsRex(), false, src.NeedsRex());
}

void X86_64Assembler::EmitRex64(CpuRegister dst, const Operand& operand) {
  uint8_t rex = 0x48 | operand.rex();  // REX.W000
  if (dst.NeedsRex()) {
    rex |= 0x44;  // REX.0R00
  }
  EmitUint8(rex);
}

void X86_64Assembler::EmitRex64(XmmRegister dst, const Operand& operand) {
  uint8_t rex = 0x48 | operand.rex();  // REX.W000
  if (dst.NeedsRex()) {
    rex |= 0x44;  // REX.0R00
  }
  EmitUint8(rex);
}

void X86_64Assembler::EmitOptionalByteRegNormalizingRex32(CpuRegister dst,
                                                          CpuRegister src,
                                                          bool normalize_both) {
  // SPL, BPL, SIL, DIL need the REX prefix.
  bool force = src.AsRegister() > 3;
  if (normalize_both) {
    // Some instructions take two byte registers, such as `xchg bpl, al`, so they need the REX
    // prefix if either `src` or `dst` needs it.
    force |= dst.AsRegister() > 3;
  } else {
    // Other instructions take one byte register and one full register, such as `movzxb rax, bpl`.
    // They need REX prefix only if `src` needs it, but not `dst`.
  }
  EmitOptionalRex(force, false, dst.NeedsRex(), false, src.NeedsRex());
}

void X86_64Assembler::EmitOptionalByteRegNormalizingRex32(CpuRegister dst, const Operand& operand) {
  uint8_t rex = operand.rex();
  // For dst, SPL, BPL, SIL, DIL need the rex prefix.
  bool force = dst.AsRegister() > 3;
  if (force) {
    rex |= 0x40;  // REX.0000
  }
  if (dst.NeedsRex()) {
    rex |= 0x44;  // REX.0R00
  }
  if (rex != 0) {
    EmitUint8(rex);
  }
}

void X86_64Assembler::AddConstantArea() {
  ArrayRef<const int32_t> area = constant_area_.GetBuffer();
  for (size_t i = 0, e = area.size(); i < e; i++) {
    AssemblerBuffer::EnsureCapacity ensured(&buffer_);
    EmitInt32(area[i]);
  }
}

size_t ConstantArea::AppendInt32(int32_t v) {
  size_t result = buffer_.size() * elem_size_;
  buffer_.push_back(v);
  return result;
}

size_t ConstantArea::AddInt32(int32_t v) {
  // Look for an existing match.
  for (size_t i = 0, e = buffer_.size(); i < e; i++) {
    if (v == buffer_[i]) {
      return i * elem_size_;
    }
  }

  // Didn't match anything.
  return AppendInt32(v);
}

size_t ConstantArea::AddInt64(int64_t v) {
  int32_t v_low = v;
  int32_t v_high = v >> 32;
  if (buffer_.size() > 1) {
    // Ensure we don't pass the end of the buffer.
    for (size_t i = 0, e = buffer_.size() - 1; i < e; i++) {
      if (v_low == buffer_[i] && v_high == buffer_[i + 1]) {
        return i * elem_size_;
      }
    }
  }

  // Didn't match anything.
  size_t result = buffer_.size() * elem_size_;
  buffer_.push_back(v_low);
  buffer_.push_back(v_high);
  return result;
}

size_t ConstantArea::AddDouble(double v) {
  // Treat the value as a 64-bit integer value.
  return AddInt64(bit_cast<int64_t, double>(v));
}

size_t ConstantArea::AddFloat(float v) {
  // Treat the value as a 32-bit integer value.
  return AddInt32(bit_cast<int32_t, float>(v));
}

uint8_t X86_64Assembler::EmitVexPrefixByteZero(bool is_twobyte_form) {
  // Vex Byte 0,
  // Bits [7:0] must contain the value 11000101b (0xC5) for 2-byte Vex
  // Bits [7:0] must contain the value 11000100b (0xC4) for 3-byte Vex
  uint8_t vex_prefix = 0xC0;
  if (is_twobyte_form) {
    vex_prefix |= TWO_BYTE_VEX;  // 2-Byte Vex
  } else {
    vex_prefix |= THREE_BYTE_VEX;  // 3-Byte Vex
  }
  return vex_prefix;
}

uint8_t X86_64Assembler::EmitVexPrefixByteOne(bool R, bool X, bool B, int SET_VEX_M) {
  // Vex Byte 1,
  uint8_t vex_prefix = VEX_INIT;
  /** Bit[7] This bit needs to be set to '1'
  otherwise the instruction is LES or LDS */
  if (!R) {
    // R .
    vex_prefix |= SET_VEX_R;
  }
  /** Bit[6] This bit needs to be set to '1'
  otherwise the instruction is LES or LDS */
  if (!X) {
    // X .
    vex_prefix |= SET_VEX_X;
  }
  /** Bit[5] This bit needs to be set to '1' */
  if (!B) {
    // B .
    vex_prefix |= SET_VEX_B;
  }
  /** Bits[4:0], Based on the instruction documentaion */
  vex_prefix |= SET_VEX_M;
  return vex_prefix;
}

void X86_64Assembler::EmitVecArithAndLogicalOperation(XmmRegister dst,
                                                      XmmRegister src1,
                                                      XmmRegister src2,
                                                      uint8_t opcode,
                                                      int vex_pp,
                                                      bool is_commutative) {
  DCHECK(CpuHasAVXFeatureFlag());
  bool is_twobyte_form = false;
  uint8_t byte_zero = 0x00, byte_one = 0x00, byte_two = 0x00;
  uint8_t VEX_L = GetEncodedVexLen(dst);
  if (!src2.NeedsRex()) {
    is_twobyte_form = true;
  } else if (is_commutative && !src1.NeedsRex()) {
    return EmitVecArithAndLogicalOperation(dst, src2, src1, opcode, vex_pp, is_commutative);
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);

  X86_64ManagedRegister vvvv_reg = X86_64ManagedRegister::FromXmmRegister(src1.AsFloatRegister());
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  if (is_twobyte_form) {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, VEX_L, vex_pp);
  } else {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                    /*X=*/false,
                                    src2.NeedsRex(),
                                    SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/false, vvvv_reg, VEX_L, vex_pp);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }
  EmitUint8(opcode);
  EmitXmmRegisterOperand(dst.LowBits(), src2);
}

void X86_64Assembler::EmitVecMinMaxOperation(XmmRegister dst,
                                             XmmRegister src1,
                                             XmmRegister src2,
                                             uint8_t vex_pp,
                                             bool is_vex_3byte,
                                             uint8_t opcode,
                                             bool is_commutative) {
  DCHECK(CpuHasAVXFeatureFlag());
  uint8_t byte_zero, byte_one, byte_two;
  bool is_twobyte_form = !is_vex_3byte;
  uint8_t VEX_L = GetEncodedVexLen(dst);

  if (is_twobyte_form) {
    if (is_commutative && src2.NeedsRex() && !src1.NeedsRex()) {
      return EmitVecMinMaxOperation(dst, src2, src1, vex_pp, is_vex_3byte, opcode, is_commutative);
    }
    is_twobyte_form = !src2.NeedsRex();
  }
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  // Instruction VEX Prefix
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg = X86_64ManagedRegister::FromXmmRegister(src1.AsFloatRegister());
  if (is_twobyte_form) {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, VEX_L, vex_pp);
  } else {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                    /*X=*/false,
                                    src2.NeedsRex(),
                                    is_vex_3byte ? SET_VEX_M_0F_38 : SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/false, vvvv_reg, VEX_L, vex_pp);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }
  // Instruction Opcode
  EmitUint8(opcode);

  // Instruction Operands
  EmitXmmRegisterOperand(dst.LowBits(), src2);
}

void X86_64Assembler::EmitVecBroadcastInstruction(XmmRegister dst,
                                                  XmmRegister src,
                                                  uint8_t opcode) {
  DCHECK(CpuHasAVXFeatureFlag());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  uint8_t byte_one = 0x00, byte_zero = 0x00, byte_two = 0x00;
  uint8_t VEX_L = GetEncodedVexLen(dst);
  byte_zero = EmitVexPrefixByteZero(false /*is_twobyte_form */);
  byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                  /*X=*/false,
                                  src.NeedsRex(),
                                  SET_VEX_M_0F_38);
  byte_two = EmitVexPrefixByteTwo(/*W=*/false, VEX_L, SET_VEX_PP_66);
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  EmitUint8(byte_two);
  EmitUint8(opcode);
  EmitXmmRegisterOperand(dst.LowBits(), src);
}

void X86_64Assembler::EmitVecShiftOperation(XmmRegister dst,
                                            XmmRegister src,
                                            const Immediate& shift_count,
                                            uint8_t opcode,
                                            uint8_t operand_byte) {
  DCHECK(CpuHasAVXFeatureFlag());
  uint8_t byte_zero, byte_one, byte_two;
  bool is_twobyte_form = true;
  uint8_t VEX_L = GetEncodedVexLen(dst);
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);

  if (src.NeedsRex()) {
    is_twobyte_form = false;
  }
  // Instruction VEX Prefix
  byte_zero = EmitVexPrefixByteZero(is_twobyte_form);
  X86_64ManagedRegister vvvv_reg = X86_64ManagedRegister::FromXmmRegister(dst.AsFloatRegister());
  if (is_twobyte_form) {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(), vvvv_reg, VEX_L, SET_VEX_PP_66);
  } else {
    byte_one = EmitVexPrefixByteOne(dst.NeedsRex(),
                                    /*X=*/false,
                                    src.NeedsRex(),
                                    SET_VEX_M_0F);
    byte_two = EmitVexPrefixByteTwo(/*W=*/false, vvvv_reg, VEX_L, SET_VEX_PP_66);
  }
  EmitUint8(byte_zero);
  EmitUint8(byte_one);
  if (!is_twobyte_form) {
    EmitUint8(byte_two);
  }
  // Instruction Opcode
  EmitUint8(opcode);

  // Instruction Operands
  EmitXmmRegisterOperand(operand_byte, src);
  EmitUint8(shift_count.value());
}

uint8_t X86_64Assembler::EmitVexPrefixByteOne(bool R,
                                              X86_64ManagedRegister operand,
                                              int SET_VEX_L,
                                              int SET_VEX_PP) {
  // Vex Byte 1,
  uint8_t vex_prefix = VEX_INIT;
  /** Bit[7] This bit needs to be set to '1'
  otherwise the instruction is LES or LDS */
  if (!R) {
    // R .
    vex_prefix |= SET_VEX_R;
  }
  /**Bits[6:3] - 'vvvv' the source or dest register specifier */
  if (operand.IsNoRegister()) {
    vex_prefix |= 0x78;
  } else if (operand.IsXmmRegister()) {
    XmmRegister vvvv = operand.AsXmmRegister();
    int inverted_reg = 15 - static_cast<int>(vvvv.AsFloatRegister());
    uint8_t reg = static_cast<uint8_t>(inverted_reg);
    vex_prefix |= ((reg & 0x0F) << 3);
  } else if (operand.IsCpuRegister()) {
    CpuRegister vvvv = operand.AsCpuRegister();
    int inverted_reg = 15 - static_cast<int>(vvvv.AsRegister());
    uint8_t reg = static_cast<uint8_t>(inverted_reg);
    vex_prefix |= ((reg & 0x0F) << 3);
  }
  /** Bit[2] - "L" If VEX.L = 1 indicates 256-bit vector operation,
  VEX.L = 0 indicates 128 bit vector operation */
  vex_prefix |= SET_VEX_L;
  // Bits[1:0] -  "pp"
  vex_prefix |= SET_VEX_PP;
  return vex_prefix;
}

uint8_t X86_64Assembler::EmitVexPrefixByteTwo(bool W,
                                              X86_64ManagedRegister operand,
                                              int SET_VEX_L,
                                              int SET_VEX_PP) {
  // Vex Byte 2,
  uint8_t vex_prefix = VEX_INIT;

  /** Bit[7] This bits needs to be set to '1' with default value.
  When using C4H form of VEX prefix, REX.W value is ignored */
  if (W) {
    vex_prefix |= SET_VEX_W;
  }
  // Bits[6:3] - 'vvvv' the source or dest register specifier
  if (operand.IsNoRegister()) {
    vex_prefix |= 0x78;
  } else if (operand.IsXmmRegister()) {
    XmmRegister vvvv = operand.AsXmmRegister();
    int inverted_reg = 15 - static_cast<int>(vvvv.AsFloatRegister());
    uint8_t reg = static_cast<uint8_t>(inverted_reg);
    vex_prefix |= ((reg & 0x0F) << 3);
  } else if (operand.IsCpuRegister()) {
    CpuRegister vvvv = operand.AsCpuRegister();
    int inverted_reg = 15 - static_cast<int>(vvvv.AsRegister());
    uint8_t reg = static_cast<uint8_t>(inverted_reg);
    vex_prefix |= ((reg & 0x0F) << 3);
  }
  /** Bit[2] - "L" If VEX.L = 1 indicates 256-bit vector operation,
  VEX.L = 0 indicates 128 bit vector operation */
  vex_prefix |= SET_VEX_L;
  // Bits[1:0] -  "pp"
  vex_prefix |= SET_VEX_PP;
  return vex_prefix;
}

uint8_t X86_64Assembler::EmitVexPrefixByteTwo(bool W,
                                              int SET_VEX_L,
                                              int SET_VEX_PP) {
  X86_64ManagedRegister vvvv_reg = ManagedRegister::NoRegister().AsX86_64();
  return EmitVexPrefixByteTwo(W, vvvv_reg, SET_VEX_L, SET_VEX_PP);
}

}  // namespace x86_64
}  // namespace art
