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

#ifndef ART_COMPILER_DEBUG_ELF_GNU_DEBUGDATA_WRITER_H_
#define ART_COMPILER_DEBUG_ELF_GNU_DEBUGDATA_WRITER_H_

#include <vector>

#include "arch/instruction_set.h"
#include "base/array_ref.h"
#include "dwarf/writer.h"
#include "linker/elf_builder.h"
#include "linker/vector_output_stream.h"
#include "base/leb128.h"

// liblzma.
#include "7zCrc.h"
#include "XzCrc64.h"
#include "XzEnc.h"

namespace art {
namespace debug {

constexpr size_t kChunkSize = kPageSize;

static void XzCompressChunk(const ArrayRef<uint8_t> src, std::vector<uint8_t>* dst) {
  // Configure the compression library.
  CrcGenerateTable();
  Crc64GenerateTable();
  CLzma2EncProps lzma2Props;
  Lzma2EncProps_Init(&lzma2Props);
  lzma2Props.lzmaProps.level = 1;  // Fast compression.
  Lzma2EncProps_Normalize(&lzma2Props);
  CXzProps props;
  XzProps_Init(&props);
  props.lzma2Props = lzma2Props;
  // Implement the required interface for communication (written in C so no virtual methods).
  struct XzCallbacks : public ISeqInStream, public ISeqOutStream, public ICompressProgress {
    static SRes ReadImpl(const ISeqInStream* p, void* buf, size_t* size) {
      auto* ctx = static_cast<XzCallbacks*>(const_cast<ISeqInStream*>(p));
      *size = std::min(*size, ctx->src_.size() - ctx->src_pos_);
      memcpy(buf, ctx->src_.data() + ctx->src_pos_, *size);
      ctx->src_pos_ += *size;
      return SZ_OK;
    }
    static size_t WriteImpl(const ISeqOutStream* p, const void* buf, size_t size) {
      auto* ctx = static_cast<const XzCallbacks*>(p);
      const uint8_t* buffer = reinterpret_cast<const uint8_t*>(buf);
      ctx->dst_->insert(ctx->dst_->end(), buffer, buffer + size);
      return size;
    }
    static SRes ProgressImpl(const ICompressProgress* , UInt64, UInt64) {
      return SZ_OK;
    }
    size_t src_pos_;
    ArrayRef<uint8_t> src_;
    std::vector<uint8_t>* dst_;
  };
  XzCallbacks callbacks;
  callbacks.Read = XzCallbacks::ReadImpl;
  callbacks.Write = XzCallbacks::WriteImpl;
  callbacks.Progress = XzCallbacks::ProgressImpl;
  callbacks.src_pos_ = 0;
  callbacks.src_ = src;
  callbacks.dst_ = dst;
  // Compress.
  SRes res = Xz_Encode(&callbacks, &callbacks, &props, &callbacks);
  CHECK_EQ(res, SZ_OK);
}

// Compress data while splitting it to smaller chunks to enable random-access reads.
// The XZ file format supports this well, but the compression library does not.
// Therefore compress the chunks separately and then glue them together manually.
//
// The XZ file format is described here: https://tukaani.org/xz/xz-file-format.txt
// In short, the file format is: [header] [compressed_block]* [index] [footer]
// Where [index] is: [num_records] ([compressed_size] [uncompressed_size])* [crc32]
//
static void XzCompress(std::vector<uint8_t>* src, std::vector<uint8_t>* dst) {
  uint8_t header[] = { 0xFD, '7', 'z', 'X', 'Z', 0, 0, 1, 0x69, 0x22, 0xDE, 0x36 };
  uint8_t footer[] = { 0, 1, 'Y', 'Z' };
  dst->insert(dst->end(), header, header + sizeof(header));
  std::vector<uint8_t> tmp;
  std::vector<uint32_t> index;
  for (size_t offset = 0; offset < src->size(); offset += kChunkSize) {
    size_t size = std::min(src->size() - offset, kChunkSize);
    tmp.clear();
    XzCompressChunk(ArrayRef<uint8_t>(src->data() + offset, size), &tmp);
    DCHECK_EQ(memcmp(tmp.data(), header, sizeof(header)), 0);
    DCHECK_EQ(memcmp(tmp.data() + tmp.size() - sizeof(footer), footer, sizeof(footer)), 0);
    uint32_t index_size = *reinterpret_cast<uint32_t*>(tmp.data() + tmp.size() - 8);
    size_t index_offset = tmp.size() - 16 - index_size * 4;
    const uint8_t* index_ptr = tmp.data() + index_offset;
    uint8_t index_indicator = *(index_ptr++);
    CHECK_EQ(index_indicator, 0);  // Mark the start of index (as opposed to compressed block).
    uint32_t num_records = DecodeUnsignedLeb128(&index_ptr);
    for (uint32_t i = 0; i < num_records; i++) {
      index.push_back(DecodeUnsignedLeb128(&index_ptr));  // Compressed size.
      index.push_back(DecodeUnsignedLeb128(&index_ptr));  // Uncompressed size.
    }
    // Copy the raw compressed block(s) located between the header and index.
    dst->insert(dst->end(), tmp.data() + sizeof(header), tmp.data() + index_offset);
  }

  // Write the index.
  uint32_t index_size_in_words;
  {
    tmp.clear();
    dwarf::Writer<> writer(&tmp);
    writer.PushUint8(0);  // Index indicator.
    writer.PushUleb128(static_cast<uint32_t>(index.size()) / 2);  // Record count.
    for (uint32_t i : index) {
      writer.PushUleb128(i);
    }
    writer.Pad(4);
    index_size_in_words = writer.size() / sizeof(uint32_t);
    writer.PushUint32(CrcCalc(tmp.data(), tmp.size()));
    dst->insert(dst->end(), tmp.data(), tmp.data() + tmp.size());
  }

  // Write the footer.
  {
    tmp.clear();
    dwarf::Writer<> writer(&tmp);
    writer.PushUint32(0);  // CRC placeholder.
    writer.PushUint32(index_size_in_words);
    writer.PushData(footer, sizeof(footer));
    writer.UpdateUint32(0, CrcCalc(tmp.data() + 4, 6));
    dst->insert(dst->end(), tmp.data(), tmp.data() + tmp.size());
  }
}

template <typename ElfTypes>
static std::vector<uint8_t> MakeMiniDebugInfoInternal(
    InstructionSet isa,
    const InstructionSetFeatures* features,
    typename ElfTypes::Addr text_section_address,
    size_t text_section_size,
    typename ElfTypes::Addr dex_section_address,
    size_t dex_section_size,
    const DebugInfo& debug_info) {
  std::vector<uint8_t> buffer;
  buffer.reserve(KB);
  linker::VectorOutputStream out("Mini-debug-info ELF file", &buffer);
  std::unique_ptr<linker::ElfBuilder<ElfTypes>> builder(
      new linker::ElfBuilder<ElfTypes>(isa, features, &out));
  builder->Start(false /* write_program_headers */);
  // Mirror ELF sections as NOBITS since the added symbols will reference them.
  builder->GetText()->AllocateVirtualMemory(text_section_address, text_section_size);
  if (dex_section_size != 0) {
    builder->GetDex()->AllocateVirtualMemory(dex_section_address, dex_section_size);
  }
  WriteDebugSymbols(builder.get(), true /* mini-debug-info */, debug_info);
  WriteCFISection(builder.get(),
                  debug_info.compiled_methods,
                  dwarf::DW_DEBUG_FRAME_FORMAT,
                  false /* write_oat_paches */);
  builder->End();
  CHECK(builder->Good());
  std::vector<uint8_t> compressed_buffer;
  compressed_buffer.reserve(buffer.size() / 4);
  XzCompress(&buffer, &compressed_buffer);
  return compressed_buffer;
}

}  // namespace debug
}  // namespace art

#endif  // ART_COMPILER_DEBUG_ELF_GNU_DEBUGDATA_WRITER_H_
