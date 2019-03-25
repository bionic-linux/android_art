/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "android-base/logging.h"

#include "ItaniumDemangle.h"
#include "base/os.h"
#include "base/unix_file/fd_file.h"
#include "elf/elf_builder.h"
#include "elf/elf_debug_reader.h"
#include "stream/file_output_stream.h"
#include "stream/vector_output_stream.h"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

const char* itanium_demangle::parse_discriminator(const char*, const char* last) {
  return last;
}

namespace art {

static constexpr char ElfMagic[] = { 0x7f, 'E', 'L', 'F' };
static constexpr char ElfMagic32[] = { 0x7f, 'E', 'L', 'F', 1, 1, 1 };
static constexpr char ElfMagic64[] = { 0x7f, 'E', 'L', 'F', 2, 1, 1 };

extern "C" char* __cxa_demangle(const char* mangled_name, char* buf, size_t* n, int* status);

template<int N>
bool StartsWith(std::vector<uint8_t>& data, const char (&prefix)[N]) {
  return data.size() >= N && memcmp(data.data(), prefix, N) == 0;
}

struct Options {
  bool Parse(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "-v") {
        verbose_ = true;
      } else if (!arg.empty() && arg[0] == '-') {
        return Usage();
      }
      filenames_.push_back(std::move(arg));
    }
    if (filenames_.size() == 2) {
      return true;
    }
    return Usage();
  }

  bool Usage() {
    LOG(ERROR) << "Usage: [-v] input_elf_file output_gnu_debugdata";
    return false;
  }

  bool verbose_;
  std::vector<std::string> filenames_;
};

std::vector<uint8_t> ReadElfFile(const std::string& filename) {
  std::unique_ptr<File> input(OS::OpenFileForReading(filename.c_str()));
  CHECK(input.get() != nullptr) << "Failed to open input file";
  std::vector<uint8_t> elf(input->GetLength());
  CHECK(input->ReadFully(elf.data(), elf.size())) << "Failed to read input file";
  return elf;
}

class NodeAllocator {
 public:
  using Node = itanium_demangle::Node;

  template<typename T, typename ...Args> T* makeNode(Args &&...args) {
    T* node = new T(std::forward<Args>(args)...);
    node_free_list.push_back(std::unique_ptr<Node>(node));
    return node;
  }

  void* allocateNodeArray(size_t count) {
    Node** nodes = new itanium_demangle::Node*[count];
    array_free_list.push_back(std::unique_ptr<Node*>(nodes));
    return nodes;
  }

 private:
  std::deque<std::unique_ptr<Node>> node_free_list;
  std::deque<std::unique_ptr<Node*>> array_free_list;
};

static std::string demangle(const char* name, size_t size) {
  itanium_demangle::ManglingParser<NodeAllocator> parser(name, name + size);
  itanium_demangle::Node* ast = parser.parse();
  if (ast != nullptr) {
    constexpr size_t kInitSize = 64;
    ::OutputStream out(new char[kInitSize], kInitSize);
    if (ast->getKind() == itanium_demangle::Node::Kind::KFunctionEncoding) {
      auto* function = reinterpret_cast<itanium_demangle::FunctionEncoding*>(ast);
      function->getName()->print(out);
    } else {
      ast->print(out);
    }
    std::string demangled(out.getBuffer(), out.getCurrentPosition());
    delete[] out.getBuffer();
    return demangled;
  }
  return name;
}

template<typename ElfTypes>
static InstructionSet GetIsa(const typename ElfTypes::Ehdr& header) {
  switch (header.e_machine) {
    case EM_ARM:
      return InstructionSet::kThumb2;
    case EM_AARCH64:
      return InstructionSet::kArm64;
    case EM_386:
      return InstructionSet::kX86;
    case EM_X86_64:
      return InstructionSet::kX86_64;
  }
  LOG(FATAL) << "Unknown architecture: " << header.e_machine;
  UNREACHABLE();
}

template<typename ElfTypes>
int GenerateMinidebugInfo(std::vector<uint8_t>& input_elf, const std::string& filename) {
  using Elf_Addr = typename ElfTypes::Addr;
  using Elf_Ehdr = typename ElfTypes::Ehdr;
  using Elf_Shdr = typename ElfTypes::Shdr;
  using Elf_Sym = typename ElfTypes::Sym;
  using Elf_Word = typename ElfTypes::Word;
  using CIE = typename ElfDebugReader<ElfTypes>::CIE;
  using FDE = typename ElfDebugReader<ElfTypes>::FDE;
  ElfDebugReader<ElfTypes> reader(input_elf);

  std::vector<uint8_t> output_elf;
  VectorOutputStream output_elf_stream("Output ELF", &output_elf);
  std::unique_ptr<ElfBuilder<ElfTypes>> builder(
      new ElfBuilder<ElfTypes>(GetIsa<ElfTypes>(*reader.GetHeader()), &output_elf_stream));
  builder->Start(/*write_program_headers=*/ false);

  // TODO: Check that the load address is zero as expected.
  auto* rodata = builder->GetRoData();
  auto* text = builder->GetText();
  const Elf_Shdr* original_text = reader.GetSection(".text");
  CHECK(original_text != nullptr);
  rodata->AllocateVirtualMemory(original_text->sh_addr - sizeof(Elf_Ehdr));
  text->AllocateVirtualMemory(original_text->sh_addr, original_text->sh_size);

  auto* strtab = builder->GetStrTab();
  auto* symtab = builder->GetSymTab();
  strtab->Start();
  {
    strtab->Write("");  // strtab should start with empty string.
    std::multimap<std::string_view, Elf_Sym> syms;
    reader.VisitFunctionSymbols([&](Elf_Sym sym, const char* name) {
      syms.emplace(name, sym);
    });
    reader.VisitDynamicSymbols([&](Elf_Sym sym, const char* name) {
      auto it = syms.find(name);
      if (it != syms.end() && it->second.st_value == sym.st_value) {
        syms.erase(it);
      }
    });
    for (auto& kvp : syms) {
      std::string_view name = kvp.first;
      const Elf_Sym& sym = kvp.second;
      Elf_Word name_idx = strtab->Write(demangle(name.data(), name.size()));
      symtab->Add(name_idx, text, sym.st_value, sym.st_size, STB_GLOBAL, STT_FUNC);
    }
  }
  strtab->End();
  symtab->WriteCachedSection();

  auto* debug_frame = builder->GetDebugFrame();
  debug_frame->Start();
  std::vector<std::pair<Elf_Addr, Elf_Addr>> binary_search_table;
  {
    std::map<std::basic_string_view<uint8_t>, Elf_Addr> cie_dedup;
    std::unordered_map<const CIE*, Elf_Addr> new_cie_offset;
    std::deque<std::pair<const FDE*, const CIE*>> entries;
    // Read, de-duplicate and write CIE entries.  Read FDE entries.
    reader.VisitDebugFrame(
        [&](const CIE* cie) {
          std::basic_string_view<uint8_t> key(cie->data(), cie->size());
          auto it = cie_dedup.emplace(key, debug_frame->GetPosition());
          if (/* inserted */ it.second) {
            debug_frame->WriteFully(cie->data(), cie->size());
          }
          new_cie_offset[cie] = it.first->second;
        },
        [&](const FDE* fde, const CIE* cie) {
          entries.emplace_back(std::make_pair(fde, cie));
        });
    // Sort FDE entries by opcodes to improve locality for compression (saves ~25%).
    std::stable_sort(entries.begin(), entries.end(), [](auto& lhs, auto& rhs) {
      constexpr size_t opcode_offset = sizeof(FDE);
      return std::lexicographical_compare(
          lhs.first->data() + opcode_offset, lhs.first->data() + lhs.first->size(),
          rhs.first->data() + opcode_offset, rhs.first->data() + rhs.first->size());
    });
    // Write all FDE entries while adjusting the CIE offsets to the new locations.
    for (auto entry : entries) {
      const FDE* fde = entry.first;
      const CIE* cie = entry.second;
      FDE new_header = *fde;
      new_header.cie_pointer = new_cie_offset[cie];
      binary_search_table.emplace_back(fde->sym_addr, debug_frame->GetPosition());
      debug_frame->WriteFully(&new_header, sizeof(FDE));
      debug_frame->WriteFully(fde->data() + sizeof(FDE), fde->size() - sizeof(FDE));
    }
  }
  debug_frame->End();

  auto* debug_frame_hdr = builder->GetDebugFrameHdr();
  debug_frame_hdr->Start();
  struct DebugFrameHdr {
    uint8_t version = 1;
    uint8_t eh_frame_ptr_enc = dwarf::DW_EH_PE_omit;
    uint8_t size_enc = dwarf::DW_EH_PE_udata4;
    uint8_t data_enc = (sizeof(Elf_Addr) == 8) ? dwarf::DW_EH_PE_udata8 : dwarf::DW_EH_PE_udata4;
  } binary_search_table_hdr;
  debug_frame_hdr->WriteFully(&binary_search_table_hdr, sizeof(binary_search_table_hdr));
  debug_frame_hdr->WriteFully(binary_search_table.data(), binary_search_table.size());
  debug_frame_hdr->End();

  builder->End();
  CHECK(builder->Good());

  std::vector<uint8_t> compressed_output_elf;
  XzCompress(ArrayRef<const uint8_t>(output_elf), &compressed_output_elf, 9 /*size*/);
  std::unique_ptr<File> output_file(OS::CreateEmptyFile(filename.c_str()));
  if (!output_file->WriteFully(compressed_output_elf.data(), compressed_output_elf.size())) {
    return 3;
  }
  return output_file->FlushClose();
}

int Run(int argc, char** argv) {
  Options opts;
  if (!opts.Parse(argc, argv)) {
    return 1;
  }

  std::vector<uint8_t> elf = ReadElfFile(opts.filenames_[0]);

  if (StartsWith(elf, ElfMagic32)) {
    return GenerateMinidebugInfo<ElfTypes32>(elf, opts.filenames_[1]);
  } else if (StartsWith(elf, ElfMagic64)) {
    return GenerateMinidebugInfo<ElfTypes64>(elf, opts.filenames_[1]);
  } else if (StartsWith(elf, ElfMagic)) {
    LOG(ERROR) << "Unsupported ELF file";
    return 2;
  } else {
    LOG(ERROR) << "The input is not an ELF file";
    return 2;
  }
}

}  // namespace art

int main(int argc, char** argv) {
  return art::Run(argc, argv);
}
