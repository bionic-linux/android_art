/*
 * Copyright (C) 2021 The Android Open Source Project
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

namespace art {

static constexpr char ElfMagic32[] = { 0x7f, 'E', 'L', 'F', 1, 1, 1 };
static constexpr char ElfMagic64[] = { 0x7f, 'E', 'L', 'F', 2, 1, 1 };

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

static std::string DemangleCppName(const char* name, size_t size) {
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
static void WriteMinidebugInfo(std::vector<uint8_t>& input_elf, const std::string& filename) {
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
      Elf_Word name_idx = strtab->Write(DemangleCppName(name.data(), name.size()));
      symtab->Add(name_idx, text, sym.st_value, sym.st_size, STB_GLOBAL, STT_FUNC);
    }
  }
  strtab->End();
  symtab->WriteCachedSection();

  auto* debug_frame = builder->GetDebugFrame();
  debug_frame->Start();
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
      debug_frame->WriteFully(&new_header, sizeof(FDE));
      debug_frame->WriteFully(fde->data() + sizeof(FDE), fde->size() - sizeof(FDE));
    }
  }
  debug_frame->End();

  builder->End();
  CHECK(builder->Good());

  std::vector<uint8_t> compressed_output_elf;
  XzCompress(ArrayRef<const uint8_t>(output_elf), &compressed_output_elf, 9 /*size*/);
  std::unique_ptr<File> output_file(OS::CreateEmptyFile(filename.c_str()));
  if (!output_file->WriteFully(compressed_output_elf.data(), compressed_output_elf.size()) ||
      !output_file->FlushClose()) {
    LOG(FATAL) << "Failed to write " << filename;
  }
}

template<int N>
bool StartsWith(std::vector<uint8_t>& data, const char (&prefix)[N]) {
  return data.size() >= N && memcmp(data.data(), prefix, N) == 0;
}

static int Main(int argc, char** argv) {
  if (argc != 3) {
    printf("Usage: create_minidebuginfo ELF_FILE OUT_FILE\n");
    printf("  ELF_FILE\n");
    printf("    The path to an elf file.\n");
    printf("  OUT_FILE\n");
    printf("    The path for the generate mini-debug-info data (not an elf file).\n");
    return 1;
  }
  const char* input = argv[1];
  const char* output = argv[2];

  std::vector<uint8_t> elf = ReadElfFile(input);

  if (StartsWith(elf, ElfMagic32)) {
    WriteMinidebugInfo<art::ElfTypes32>(elf, output);
  } else if (StartsWith(elf, ElfMagic64)) {
    WriteMinidebugInfo<art::ElfTypes64>(elf, output);
  } else {
    LOG(FATAL) << "Invalid ELF file " << input;
  }
  return 0;
}

}  // namespace art

int main(int argc, char** argv) {
  return art::Main(argc, argv);
}

const char* itanium_demangle::parse_discriminator(const char*, const char* last) {
  return last;
}
