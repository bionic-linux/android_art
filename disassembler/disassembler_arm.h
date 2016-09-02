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

#ifndef ART_DISASSEMBLER_DISASSEMBLER_ARM_H_
#define ART_DISASSEMBLER_DISASSEMBLER_ARM_H_

#include <sstream>

#include "arch/arm/registers_arm.h"
#include "disassembler.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "aarch32/instructions-aarch32.h"
#include "aarch32/disasm-aarch32.h"
#pragma GCC diagnostic pop

namespace art {
namespace arm {

static const vixl::aarch32::Register tr(TR);

class CustomDisassembler FINAL : public vixl::aarch32::PrintDisassembler {
  class CustomDisassemblerStream FINAL : public DisassemblerStream {
   public:
    explicit CustomDisassemblerStream(std::ostream& os,
                                      const CustomDisassembler* disasm,
                                      const DisassemblerOptions* options)
        : DisassemblerStream(os), disasm_(disasm), options_(options) {}

    DisassemblerStream& operator<<(const PrintLabel& label) OVERRIDE;
    DisassemblerStream& operator<<(const vixl::aarch32::Register reg) OVERRIDE;
    DisassemblerStream& operator<<(const vixl::aarch32::MemOperand& operand) OVERRIDE;

    DisassemblerStream& operator<<(const vixl::aarch32::AlignedMemOperand& operand) OVERRIDE {
      // VIXL must use a PrintLabel object whenever the base register is PC;
      // the following check verifies this invariant, and guards against bugs.
      DCHECK(!operand.GetBaseRegister().Is(vixl::aarch32::pc));
      return DisassemblerStream::operator<<(operand);
    }

   private:
    void PrintLiteral(LocationType type, int32_t offset);

    const CustomDisassembler* disasm_;
    const DisassemblerOptions* options_;
  };

 public:
  explicit CustomDisassembler(std::ostream& os, const DisassemblerOptions* options)
      : vixl::aarch32::PrintDisassembler(new CustomDisassemblerStream(os, this, options)) {}

  void PrintPc(uint32_t pc) OVERRIDE {
    os() << "0x" << std::hex << std::setw(8) << std::setfill('0') << pc << ": ";
  }

  bool IsT32(void) const {
    return is_t32_;
  }

  void SetT32(bool is_t32) {
    is_t32_ = is_t32;
  }

 private:
  bool is_t32_;
};

class DisassemblerArm FINAL : public Disassembler {
 public:
  explicit DisassemblerArm(DisassemblerOptions* options)
      : Disassembler(options), disasm_(output_, options) {}

  size_t Dump(std::ostream& os, const uint8_t* begin) OVERRIDE;
  void Dump(std::ostream& os, const uint8_t* begin, const uint8_t* end) OVERRIDE;

 private:
  uintptr_t GetPc(uintptr_t instr_ptr) const {
    return GetDisassemblerOptions()->absolute_addresses_
        ? instr_ptr
        : instr_ptr - reinterpret_cast<uintptr_t>(GetDisassemblerOptions()->base_address_);
  }

  std::ostringstream output_;
  CustomDisassembler disasm_;

  DISALLOW_COPY_AND_ASSIGN(DisassemblerArm);
};

}  // namespace arm
}  // namespace art

#endif  // ART_DISASSEMBLER_DISASSEMBLER_ARM_H_
