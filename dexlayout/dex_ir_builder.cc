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
 *
 * Header file of an in-memory representation of DEX files.
 */

#include <stdint.h>
#include <vector>

#include "dex_ir_builder.h"

namespace art {
namespace dex_ir {

static void CheckAndSetRemainingOffsets(const IDexFile& dex_file, Collections* collections);

Header* DexIrBuilder(const IDexFile& dex_file) {
  const IDexFile::Header& disk_header = dex_file.GetHeader();
  Header* header = new Header(disk_header.magic_,
                              disk_header.checksum_,
                              disk_header.signature_,
                              disk_header.endian_tag_,
                              disk_header.file_size_,
                              disk_header.header_size_,
                              disk_header.link_size_,
                              disk_header.link_off_,
                              disk_header.data_size_,
                              disk_header.data_off_);
  Collections& collections = header->GetCollections();
  // Walk the rest of the header fields.
  // StringId table.
  collections.SetStringIdsOffset(disk_header.string_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumStringIds(); ++i) {
    collections.CreateStringId(dex_file, i);
  }
  // TypeId table.
  collections.SetTypeIdsOffset(disk_header.type_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumTypeIds(); ++i) {
    collections.CreateTypeId(dex_file, i);
  }
  // ProtoId table.
  collections.SetProtoIdsOffset(disk_header.proto_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumProtoIds(); ++i) {
    collections.CreateProtoId(dex_file, i);
  }
  // FieldId table.
  collections.SetFieldIdsOffset(disk_header.field_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumFieldIds(); ++i) {
    collections.CreateFieldId(dex_file, i);
  }
  // MethodId table.
  collections.SetMethodIdsOffset(disk_header.method_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumMethodIds(); ++i) {
    collections.CreateMethodId(dex_file, i);
  }
  // ClassDef table.
  collections.SetClassDefsOffset(disk_header.class_defs_off_);
  for (uint32_t i = 0; i < dex_file.NumClassDefs(); ++i) {
    collections.CreateClassDef(dex_file, i);
  }
  // MapItem.
  collections.SetMapListOffset(disk_header.map_off_);
  // CallSiteIds and MethodHandleItems.
  collections.CreateCallSitesAndMethodHandles(dex_file);

  CheckAndSetRemainingOffsets(dex_file, &collections);

  return header;
}

static void CheckAndSetRemainingOffsets(const IDexFile& dex_file, Collections* collections) {
  const IDexFile::Header& disk_header = dex_file.GetHeader();
  // Read MapItems and validate/set remaining offsets.
  const IDexFile::MapList* map =
      reinterpret_cast<const IDexFile::MapList*>(dex_file.Begin() + disk_header.map_off_);
  const uint32_t count = map->size_;
  for (uint32_t i = 0; i < count; ++i) {
    const IDexFile::MapItem* item = map->list_ + i;
    switch (item->type_) {
      case IDexFile::kDexTypeHeaderItem:
        CHECK_EQ(item->size_, 1u);
        CHECK_EQ(item->offset_, 0u);
        break;
      case IDexFile::kDexTypeStringIdItem:
        CHECK_EQ(item->size_, collections->StringIdsSize());
        CHECK_EQ(item->offset_, collections->StringIdsOffset());
        break;
      case IDexFile::kDexTypeTypeIdItem:
        CHECK_EQ(item->size_, collections->TypeIdsSize());
        CHECK_EQ(item->offset_, collections->TypeIdsOffset());
        break;
      case IDexFile::kDexTypeProtoIdItem:
        CHECK_EQ(item->size_, collections->ProtoIdsSize());
        CHECK_EQ(item->offset_, collections->ProtoIdsOffset());
        break;
      case IDexFile::kDexTypeFieldIdItem:
        CHECK_EQ(item->size_, collections->FieldIdsSize());
        CHECK_EQ(item->offset_, collections->FieldIdsOffset());
        break;
      case IDexFile::kDexTypeMethodIdItem:
        CHECK_EQ(item->size_, collections->MethodIdsSize());
        CHECK_EQ(item->offset_, collections->MethodIdsOffset());
        break;
      case IDexFile::kDexTypeClassDefItem:
        CHECK_EQ(item->size_, collections->ClassDefsSize());
        CHECK_EQ(item->offset_, collections->ClassDefsOffset());
        break;
      case IDexFile::kDexTypeCallSiteIdItem:
        CHECK_EQ(item->size_, collections->CallSiteIdsSize());
        CHECK_EQ(item->offset_, collections->CallSiteIdsOffset());
        break;
      case IDexFile::kDexTypeMethodHandleItem:
        CHECK_EQ(item->size_, collections->MethodHandleItemsSize());
        CHECK_EQ(item->offset_, collections->MethodHandleItemsOffset());
        break;
      case IDexFile::kDexTypeMapList:
        CHECK_EQ(item->size_, 1u);
        CHECK_EQ(item->offset_, disk_header.map_off_);
        break;
      case IDexFile::kDexTypeTypeList:
        collections->SetTypeListsOffset(item->offset_);
        break;
      case IDexFile::kDexTypeAnnotationSetRefList:
        collections->SetAnnotationSetRefListsOffset(item->offset_);
        break;
      case IDexFile::kDexTypeAnnotationSetItem:
        collections->SetAnnotationSetItemsOffset(item->offset_);
        break;
      case IDexFile::kDexTypeClassDataItem:
        collections->SetClassDatasOffset(item->offset_);
        break;
      case IDexFile::kDexTypeCodeItem:
        collections->SetCodeItemsOffset(item->offset_);
        break;
      case IDexFile::kDexTypeStringDataItem:
        collections->SetStringDatasOffset(item->offset_);
        break;
      case IDexFile::kDexTypeDebugInfoItem:
        collections->SetDebugInfoItemsOffset(item->offset_);
        break;
      case IDexFile::kDexTypeAnnotationItem:
        collections->SetAnnotationItemsOffset(item->offset_);
        break;
      case IDexFile::kDexTypeEncodedArrayItem:
        collections->SetEncodedArrayItemsOffset(item->offset_);
        break;
      case IDexFile::kDexTypeAnnotationsDirectoryItem:
        collections->SetAnnotationsDirectoryItemsOffset(item->offset_);
        break;
      default:
        LOG(ERROR) << "Unknown map list item type.";
    }
  }
}

}  // namespace dex_ir
}  // namespace art
