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

#include "disassembler_arm.h"

#include <string>

#include "base/bit_utils.h"

namespace art {
namespace arm {

CustomDisassembler::DisassemblerStream& CustomDisassembler::CustomDisassemblerStream::operator<<(
    const PrintLabel& label) {
  const LocationType type = label.GetLocationType();

  switch (type) {
    case kLoadByteLocation:
    case kLoadHalfWordLocation:
    case kLoadWordLocation:
    case kLoadDoubleWordLocation:
    case kLoadSignedByteLocation:
    case kLoadSignedHalfWordLocation:
    case kLoadSinglePrecisionLocation:
    case kLoadDoublePrecisionLocation:
    case kVld1Location:
    case kVld2Location:
    case kVld3Location:
    case kVld4Location: {
      const uintptr_t pc_delta = disasm_->IsT32()
          ? vixl::aarch32::kT32PcDelta
          : vixl::aarch32::kA32PcDelta;
      const int32_t offset = label.GetLabel()->GetLocation();

      os() << "[pc, #" << offset - pc_delta << "]";
      PrintLiteral(type, offset);
      return *this;
    }
    default:
      return DisassemblerStream::operator<<(label);
  }
}

CustomDisassembler::DisassemblerStream& CustomDisassembler::CustomDisassemblerStream::operator<<(
    const vixl::aarch32::Register reg) {
  if (reg.Is(tr)) {
    os() << "tr";
    return *this;
  } else {
    return DisassemblerStream::operator<<(reg);
  }
}

CustomDisassembler::DisassemblerStream& CustomDisassembler::CustomDisassemblerStream::operator<<(
    const vixl::aarch32::MemOperand& operand) {
  // VIXL must use a PrintLabel object whenever the base register is PC;
  // the following check verifies this invariant, and guards against bugs.
  DCHECK(!operand.GetBaseRegister().Is(vixl::aarch32::pc));
  DisassemblerStream::operator<<(operand);

  if (operand.GetBaseRegister().Is(tr) && operand.IsImmediate()) {
    os() << " ; ";
    options_->thread_offset_name_function_(os(), operand.GetOffsetImmediate());
  }

  return *this;
}

void CustomDisassembler::CustomDisassemblerStream::PrintLiteral(LocationType type,
                                                                int32_t offset) {
  // Literal offsets are not required to be aligned, so we may need unaligned access.
  typedef const int16_t unaligned_int16_t __attribute__ ((aligned (1)));
  typedef const uint16_t unaligned_uint16_t __attribute__ ((aligned (1)));
  typedef const int32_t unaligned_int32_t __attribute__ ((aligned (1)));
  typedef const int64_t unaligned_int64_t __attribute__ ((aligned (1)));
  typedef const float unaligned_float __attribute__ ((aligned (1)));
  typedef const double unaligned_double __attribute__ ((aligned (1)));

  const size_t literal_size[kVst4Location + 1] = {
      0, 0, 0, 0, sizeof(uint8_t), sizeof(unaligned_uint16_t), sizeof(unaligned_int32_t),
      sizeof(unaligned_int64_t), sizeof(int8_t), sizeof(unaligned_int16_t),
      sizeof(unaligned_float), sizeof(unaligned_double)};
  const uintptr_t begin = reinterpret_cast<uintptr_t>(options_->base_address_);
  const uintptr_t end = reinterpret_cast<uintptr_t>(options_->end_address_);
  uintptr_t literal_addr = RoundDown(disasm_->GetPc(), vixl::aarch32::kRegSizeInBytes) + offset;

  if (!options_->absolute_addresses_) {
    literal_addr += begin;
  }

  os() << "  ; ";

  // Bail out if not within expected buffer range to avoid trying to fetch invalid literals
  // (we can encounter them when interpreting raw data as instructions).
  if (literal_addr < begin || literal_addr > end - literal_size[type]) {
    os() << "(?)";
  } else {
    switch (type) {
      case kLoadByteLocation:
        os() << *reinterpret_cast<const uint8_t*>(literal_addr);
        break;
      case kLoadHalfWordLocation:
        os() << *reinterpret_cast<unaligned_uint16_t*>(literal_addr);
        break;
      case kLoadWordLocation: {
        const int32_t value = *reinterpret_cast<unaligned_int32_t*>(literal_addr);

        os() << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
        break;
      }
      case kLoadDoubleWordLocation: {
        const int64_t value = *reinterpret_cast<unaligned_int64_t*>(literal_addr);

        os() << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
        break;
      }
      case kLoadSignedByteLocation:
        os() << *reinterpret_cast<const int8_t*>(literal_addr);
        break;
      case kLoadSignedHalfWordLocation:
        os() << *reinterpret_cast<unaligned_int16_t*>(literal_addr);
        break;
      case kLoadSinglePrecisionLocation:
        os() << *reinterpret_cast<unaligned_float*>(literal_addr);
        break;
      case kLoadDoublePrecisionLocation:
        os() << *reinterpret_cast<unaligned_double*>(literal_addr);
        break;
      default:
        UNIMPLEMENTED(FATAL) << "Literal type: " << type;
    }
  }
}

size_t DisassemblerArm::Dump(std::ostream& os, const uint8_t* begin) {
  uintptr_t next;
  // Remove the Thumb specifier bit; no effect if begin does not point to T32 code.
  const uintptr_t instr_ptr = reinterpret_cast<uintptr_t>(begin) & ~1;

  disasm_.SetT32((reinterpret_cast<uintptr_t>(begin) & 1) != 0);
  disasm_.JumpToPc(GetPc(instr_ptr));

  if (disasm_.IsT32()) {
    const uint16_t* const ip = reinterpret_cast<const uint16_t*>(instr_ptr);

    next = reinterpret_cast<uintptr_t>(disasm_.DecodeT32At(ip));
  } else {
    const uint32_t* const ip = reinterpret_cast<const uint32_t*>(instr_ptr);

    next = reinterpret_cast<uintptr_t>(disasm_.DecodeA32At(ip));
  }

  os << output_.str();
  output_.str(std::string());
  return next - instr_ptr;
}

void DisassemblerArm::Dump(std::ostream& os, const uint8_t* begin, const uint8_t* end) {
  // Remove the Thumb specifier bit; no effect if begin does not point to T32 code.
  const uintptr_t base = reinterpret_cast<uintptr_t>(begin) & ~1;

  disasm_.SetT32((reinterpret_cast<uintptr_t>(begin) & 1) != 0);
  disasm_.JumpToPc(GetPc(base));

  if (disasm_.IsT32()) {
    // The Thumb specifier bits cancel each other.
    disasm_.DisassembleT32Buffer(reinterpret_cast<const uint16_t*>(base), end - begin);
  } else {
    disasm_.DisassembleA32Buffer(reinterpret_cast<const uint32_t*>(base), end - begin);
  }

  os << output_.str();
  output_.str(std::string());
}

}  // namespace arm
}  // namespace art
