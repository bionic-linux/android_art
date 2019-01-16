/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "arch/instruction_set.h"
#include "base/logging.h"
#include "disassembler.h"

#include <cstdio>
#include <cstring>
#include <limits>

namespace art {

Disassembler::Disassembler(InstructionSet insn_set) {
  ptr_ = nullptr;
  address_ = 0;
  insn_ = nullptr;
  // Instantiate a capstone handle/instance.
  switch (insn_set) {
  case InstructionSet::kX86:
    err_ = cs_open(capstone::CS_ARCH_X86, capstone::CS_MODE_32, &handle_);
    break;

  case InstructionSet::kX86_64:
    err_ = cs_open(capstone::CS_ARCH_X86, capstone::CS_MODE_64, &handle_);
    if (err_) {
       LOG(ERROR) << "Failed on cs_open() with error:" << cs_strerror(err_);
    }
    break;

  default:
    err_ = capstone::CS_ERR_ARCH;
    return;
  }

  if (err_ != capstone::CS_ERR_OK) {
    return;
  }

  err_ = cs_option(handle_, capstone::CS_OPT_DETAIL, capstone::CS_OPT_ON);

  if (err_ != capstone::CS_ERR_OK) {
    return;
  }

  // Allocate memory for the the iterator.
  insn_ = capstone::cs_malloc(handle_);

  if (insn_ == nullptr) {
    err_ = capstone::CS_ERR_MEM;
  }
}

Disassembler::~Disassembler() {
  if (insn_ != nullptr) {
    cs_free(insn_, 1);
  }

  capstone::cs_close(&handle_);
}

}  // namespace art
