/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "dexanalyze_bytecode.h"

#include <algorithm>
#include <iostream>

#include "dex/class_accessor-inl.h"
#include "dex/code_item_accessors-inl.h"
#include "dex/dex_instruction-inl.h"

namespace art {
namespace dexanalyze {

void NewRegisterInstructions::ProcessDexFiles(
    const std::vector<std::unique_ptr<const DexFile>>& dex_files) {
  using TypeMap = std::map<size_t, size_t>;
  std::set<std::vector<uint8_t>> deduped;
  for (const std::unique_ptr<const DexFile>& dex_file : dex_files) {
    std::set<const void*> visited;
    // Compute global index maps.
    std::map<size_t, size_t> field_idx_map;
    std::map<size_t, size_t> method_idx_map;
    // Per class type maps.
    std::map<dex::TypeIndex, TypeMap> type_maps;
    std::map<size_t, size_t> method_type_count;
    for (size_t i = 0; i < dex_file->NumMethodIds(); ++i) {
      const DexFile::MethodId& method = dex_file->GetMethodId(i);
      method_idx_map[i] = method_type_count[method.class_idx_.index_]++;
    }
    for (ClassAccessor accessor : dex_file->GetClasses()) {
      InstructionBuilder inst_builder(field_idx_map,
                                      method_idx_map,
                                      /*count_types*/ true,
                                      /*dump*/ false);
      size_t idx = 0u;
      for (const ClassAccessor::Method& method : accessor.GetMethods()) {
        inst_builder.Process(*dex_file, method.GetInstructionsAndData(), accessor.GetClassIdx());
      }
      idx = 0u;
      for (const ClassAccessor::Field& field : accessor.GetStaticFields()) {
        CHECK(field_idx_map.find(field.GetIndex()) == field_idx_map.end());
        field_idx_map[field.GetIndex()] = idx++;
      }
      idx = 0u;
      for (const ClassAccessor::Field& field : accessor.GetInstanceFields()) {
        CHECK(field_idx_map.find(field.GetIndex()) == field_idx_map.end());
        field_idx_map[field.GetIndex()] = idx++;
      }
      // Reorder types by most used.
      std::vector<std::pair<size_t, size_t>> usage;
      for (auto&& pair : inst_builder.type_use_counts_) {
        usage.emplace_back(pair.second, pair.first);
      }
      std::sort(usage.rbegin(), usage.rend());
      size_t current_index = 0u;
      TypeMap local_types;
      for (auto&& pair : usage) {
        local_types[pair.second] = current_index++;
      }
      type_maps[accessor.GetClassIdx()] = std::move(local_types);
    }
    // Visit classes and convert code items.
    for (ClassAccessor accessor : dex_file->GetClasses()) {
      InstructionBuilder inst_builder(field_idx_map,
                                      method_idx_map,
                                      /*count_types*/ false,
                                      dump_);
      inst_builder.local_types_ = type_maps[accessor.GetClassIdx()];
      for (const ClassAccessor::Method& method : accessor.GetMethods()) {
        if (method.GetCodeItem() == nullptr || !visited.insert(method.GetCodeItem()).second) {
          continue;
        }
        if (dump_) {
          std::cout << std::endl
                    << "Processing " << dex_file->PrettyMethod(method.GetIndex(), true);
        }
        CodeItemDataAccessor data = method.GetInstructionsAndData();
        inst_builder.Process(*dex_file, data, accessor.GetClassIdx());
        std::vector<uint8_t> buffer = std::move(inst_builder.buffer_);
        const size_t buffer_size = buffer.size();
        dex_code_bytes_ += data.InsnsSizeInBytes();
        output_size_ += buffer_size;
        // Add extra data at the end to have fair dedupe.
        EncodeUnsignedLeb128(&buffer, data.RegistersSize());
        EncodeUnsignedLeb128(&buffer, data.InsSize());
        EncodeUnsignedLeb128(&buffer, data.OutsSize());
        EncodeUnsignedLeb128(&buffer, data.TriesSize());
        EncodeUnsignedLeb128(&buffer, data.InsnsSizeInCodeUnits());
        if (deduped.insert(buffer).second) {
          deduped_size_ += buffer_size;
        }
      }
      missing_field_idx_count_ += inst_builder.missing_field_idx_count_;
      missing_method_idx_count_ += inst_builder.missing_method_idx_count_;
    }
  }
}

void NewRegisterInstructions::Dump(std::ostream& os, uint64_t total_size) const {
  os << "Total Dex code bytes: " << Percent(dex_code_bytes_, total_size) << "\n";
  os << "Total output code bytes: " << Percent(output_size_, total_size) << "\n";
  os << "Total deduped code bytes: " << Percent(deduped_size_, total_size) << "\n";
  os << "Missing field idx count: " << missing_field_idx_count_ << "\n";
  os << "Missing method idx count: " << missing_method_idx_count_ << "\n";
}

InstructionBuilder::InstructionBuilder(const std::map<size_t, size_t>& field_idx_map,
                                       const std::map<size_t, size_t>& method_idx_map,
                                       bool count_types,
                                       bool dump)
    : field_idx_map_(field_idx_map),
      method_idx_map_(method_idx_map),
      count_types_(count_types),
      dump_(dump) {}

void InstructionBuilder::Process(const DexFile& dex_file,
                                 const CodeItemDataAccessor& code_item,
                                 dex::TypeIndex current_class_type) {
  bool skip_next = false;
  for (auto inst = code_item.begin(); inst != code_item.end(); ++inst) {
    if (dump_) {
      std::cout << std::endl;
      std::cout << inst->DumpString(nullptr);
      if (skip_next) {
        std::cout << " (SKIPPED)";
      }
    }
    if (skip_next) {
      skip_next = false;
      continue;
    }
    const Instruction::Code opcode = inst->Opcode();
    switch (opcode) {
      case Instruction::IGET:
      case Instruction::IGET_WIDE:
      case Instruction::IGET_OBJECT:
      case Instruction::IGET_BOOLEAN:
      case Instruction::IGET_BYTE:
      case Instruction::IGET_CHAR:
      case Instruction::IGET_SHORT:
      case Instruction::IPUT:
      case Instruction::IPUT_WIDE:
      case Instruction::IPUT_OBJECT:
      case Instruction::IPUT_BOOLEAN:
      case Instruction::IPUT_BYTE:
      case Instruction::IPUT_CHAR:
      case Instruction::IPUT_SHORT: {
        const uint32_t dex_field_idx = inst->VRegC_22c();
        CHECK_LT(dex_field_idx, dex_file.NumFieldIds());
        dex::TypeIndex holder_type = dex_file.GetFieldId(dex_field_idx).class_idx_;
        if (count_types_) {
          ++type_use_counts_[holder_type.index_];
        } else {
          const uint32_t receiver = inst->VRegB_22c();
          const uint32_t first_arg_reg = code_item.RegistersSize() - code_item.InsSize();
          const uint32_t out_reg = inst->VRegA_22c();
          if (first_arg_reg == receiver && holder_type == current_class_type) {
            auto it = field_idx_map_.find(dex_field_idx);
            if (it == field_idx_map_.end()) {
              ++missing_field_idx_count_;
            } else if (InstNibbles(opcode, {out_reg, static_cast<uint32_t>(it->second)})) {
              continue;
            }
          }
        }
        break;
      }
      // Invoke cases.
      case Instruction::INVOKE_VIRTUAL:
      case Instruction::INVOKE_DIRECT:
      case Instruction::INVOKE_STATIC:
      case Instruction::INVOKE_INTERFACE:
      case Instruction::INVOKE_SUPER: {
        const uint32_t method_idx = DexMethodIndex(inst.Inst());
        const DexFile::MethodId& method = dex_file.GetMethodId(method_idx);
        const dex::TypeIndex receiver_type = method.class_idx_;
        if (count_types_) {
          ++type_use_counts_[receiver_type.index_];
        } else {
          uint32_t args[6] = {};
          uint32_t arg_count = inst->GetVarArgs(args);

          bool next_move_result = false;
          uint32_t dest_reg = 0;
          if (inst != code_item.end()) {
            auto next = std::next(inst);
            next_move_result =
                next->Opcode() == Instruction::MOVE_RESULT ||
                next->Opcode() == Instruction::MOVE_RESULT_WIDE ||
                next->Opcode() == Instruction::MOVE_RESULT_OBJECT;
            if (next_move_result) {
              dest_reg = next->VRegA_11x();
            }
          }

          bool result = false;
          constexpr bool use_16_bit_index = false;
          if (use_16_bit_index) {
            if (arg_count == 1) {
              result = InstNibblesAndIndex(opcode, method_idx, {dest_reg, args[0]});
            } else if (arg_count == 2) {
              result = InstNibblesAndIndex(opcode, method_idx, {dest_reg, args[0], args[1]});
            } else if (arg_count == 3) {
              result = InstNibblesAndIndex(opcode, method_idx, {dest_reg, args[0], args[1],
                                                                args[2]});
            }
          } else {
            CHECK(local_types_.find(receiver_type.index_) != local_types_.end());
            uint32_t type_idx = local_types_[receiver_type.index_];
            if (method_idx_map_.find(method_idx) == method_idx_map_.end()) {
              ++missing_method_idx_count_;
              break;
            }
            uint32_t local_idx = method_idx_map_.find(method_idx)->second;
            uint32_t local_idx1 = local_idx >> 4;
            uint32_t local_idx2 = local_idx & 0xF;
            if (arg_count == 0) {
              result = InstNibbles(opcode, {dest_reg, type_idx, local_idx1, local_idx2});
            } else if (arg_count == 1) {
              result = InstNibbles(opcode, {dest_reg, type_idx, local_idx1, local_idx2, args[0]});
            } else if (arg_count == 2) {
              result = InstNibbles(opcode, {dest_reg, type_idx, local_idx1, local_idx2, args[0],
                                            args[1]});
            } else if (arg_count == 3) {
              result = InstNibbles(opcode, {dest_reg, type_idx, local_idx1, local_idx2, args[0],
                                            args[1], args[2]});
            } else if (arg_count == 4) {
              result = InstNibbles(opcode, {dest_reg, type_idx, local_idx1, local_idx2, args[0],
                                            args[1], args[2], args[3]});
            }
          }

          if (result) {
            skip_next = next_move_result;
            continue;
          }
        }
        break;
      }
      case Instruction::IF_EQZ:
      case Instruction::IF_NEZ: {
        uint32_t reg = inst->VRegA_21t();
        int16_t offset = inst->VRegB_21t();
        if (!count_types_ && InstNibbles(opcode, {reg, static_cast<uint16_t>(offset)})) {
          continue;
        }
        break;
      }
      case Instruction::CONST_CLASS:
      case Instruction::CHECK_CAST:
      case Instruction::NEW_INSTANCE: {
        const uint32_t type_idx = inst->VRegB_21c();
        const uint32_t out_reg = inst->VRegA_21c();
        if (count_types_) {
          ++type_use_counts_[type_idx];
        } else {
          bool next_is_init = false;
          if (opcode == Instruction::NEW_INSTANCE && inst != code_item.end()) {
            auto next = std::next(inst);
            if (next->Opcode() == Instruction::INVOKE_DIRECT) {
              uint32_t args[6] = {};
              uint32_t arg_count = next->GetVarArgs(args);
              uint32_t method_idx = DexMethodIndex(next.Inst());
              if (arg_count == 1u && args[0] == out_reg &&
                  dex_file.GetMethodName(dex_file.GetMethodId(method_idx)) ==
                      std::string("<init>")) {
                next_is_init = true;
              }
            }
          }
          uint32_t local_type = static_cast<uint32_t>(local_types_.find(type_idx)->second);
          if (InstNibbles(opcode, {out_reg, local_type})) {
            skip_next = next_is_init;
            continue;
          }
        }
        break;
      }
      case Instruction::RETURN_VOID: {
        if (std::next(inst) == code_item.end()) {
          continue;
        }
        if (!count_types_ && InstNibbles(opcode, {})) {
          continue;
        }
        break;
      }
      case Instruction::INSTANCE_OF: {
        // ++type_use_counts_[inst->VRegC_22c()];
        break;
      }
      default:
        break;
    }
    if (!count_types_) {
      Add(inst.Inst());
    }
  }
  if (dump_) {
    std::cout << std::endl;
  }
}

void InstructionBuilder::Add(const Instruction& inst) {
  const uint8_t* start = reinterpret_cast<const uint8_t*>(&inst);
  buffer_.insert(buffer_.end(), start, start + 2 * inst.SizeInCodeUnits());
}

bool InstructionBuilder::InstNibblesAndIndex(uint8_t opcode,
                                             uint16_t idx,
                                             const std::vector<uint32_t>& args) {
  if (!InstNibbles(opcode, args)) {
    return false;
  }
  buffer_.push_back(static_cast<uint8_t>(idx >> 8));
  buffer_.push_back(static_cast<uint8_t>(idx));
  return true;
}

bool InstructionBuilder::InstNibbles(uint8_t opcode, const std::vector<uint32_t>& args) {
  if (dump_) {
    std::cout << " ==> " << Instruction::Name(static_cast<Instruction::Code>(opcode)) << " ";
    for (int v : args) {
      std::cout << v << ", ";
    }
  }
  for (int v : args) {
    if (v >= 16) {
      if (dump_) {
        std::cout << "(OUT_OF_RANGE)";
      }
      return false;
    }
  }
  buffer_.push_back(opcode);
  for (size_t i = 0; i < args.size(); i += 2) {
    buffer_.push_back(args[i] << 4);
    if (i + 1 < args.size()) {
      buffer_.back() |= args[i + 1];
    }
  }
  while (buffer_.size() % alignment_ != 0) {
    buffer_.push_back(0);
  }
  return true;
}

}  // namespace dexanalyze
}  // namespace art
