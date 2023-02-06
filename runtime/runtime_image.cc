/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "runtime_image.h"

#include <lz4.h>
#include <sstream>
#include <unistd.h>

#include "android-base/file.h"
#include "android-base/stringprintf.h"

#include "base/arena_allocator.h"
#include "base/arena_containers.h"
#include "base/bit_utils.h"
#include "base/file_utils.h"
#include "base/length_prefixed_array.h"
#include "base/stl_util.h"
#include "base/systrace.h"
#include "base/unix_file/fd_file.h"
#include "base/utils.h"
#include "class_loader_context.h"
#include "class_loader_utils.h"
#include "class_root-inl.h"
#include "gc/space/image_space.h"
#include "image.h"
#include "mirror/object-inl.h"
#include "mirror/object-refvisitor-inl.h"
#include "mirror/object_array-alloc-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/object_array.h"
#include "mirror/string-inl.h"
#include "oat.h"
#include "scoped_thread_state_change-inl.h"
#include "vdex_file.h"

namespace art {

using android::base::StringPrintf;

/**
 * The native data structures that we store in the image.
 */
enum class NativeRelocationKind {
  kArtFieldArray,
  kArtMethodArray,
  kArtMethod,
  kImTable,
};

/**
 * Helper class to generate an app image at runtime.
 */
class RuntimeImageHelper {
 public:
  explicit RuntimeImageHelper(gc::Heap* heap) :
    sections_(ImageHeader::kSectionCount),
    boot_image_begin_(heap->GetBootImagesStartAddress()),
    boot_image_size_(heap->GetBootImagesSize()),
    image_begin_(boot_image_begin_ + boot_image_size_),
    // Note: image relocation considers the image header in the bitmap.
    object_section_size_(sizeof(ImageHeader)),
    intern_table_(InternStringHash(this), InternStringEquals(this)),
    class_table_(ClassDescriptorHash(this), ClassDescriptorEquals()) {}


  bool Generate(std::string* error_msg) {
    if (!WriteObjects(error_msg)) {
      return false;
    }

    // Generate the sections information stored in the header.
    CreateImageSections();

    // Now that all sections have been created and we know their offset and
    // size, relocate native pointers inside classes and ImTables.
    RelocateNativePointers();

    // Generate the bitmap section, stored page aligned after the sections data
    // and of size `object_section_size_` page aligned.
    size_t sections_end = sections_[ImageHeader::kSectionMetadata].End();
    image_bitmap_ = gc::accounting::ContinuousSpaceBitmap::Create(
        "image bitmap",
        reinterpret_cast<uint8_t*>(image_begin_),
        RoundUp(object_section_size_, kPageSize));
    for (uint32_t offset : object_offsets_) {
      DCHECK(IsAligned<kObjectAlignment>(image_begin_ + sizeof(ImageHeader) + offset));
      image_bitmap_.Set(
          reinterpret_cast<mirror::Object*>(image_begin_ + sizeof(ImageHeader) + offset));
    }
    const size_t bitmap_bytes = image_bitmap_.Size();
    auto* bitmap_section = &sections_[ImageHeader::kSectionImageBitmap];
    *bitmap_section = ImageSection(RoundUp(sections_end, kPageSize),
                                   RoundUp(bitmap_bytes, kPageSize));

    // Compute boot image checksum and boot image components, to be stored in
    // the header.
    gc::Heap* const heap = Runtime::Current()->GetHeap();
    uint32_t boot_image_components = 0u;
    uint32_t boot_image_checksums = 0u;
    const std::vector<gc::space::ImageSpace*>& image_spaces = heap->GetBootImageSpaces();
    for (size_t i = 0u, size = image_spaces.size(); i != size; ) {
      const ImageHeader& header = image_spaces[i]->GetImageHeader();
      boot_image_components += header.GetComponentCount();
      boot_image_checksums ^= header.GetImageChecksum();
      DCHECK_LE(header.GetImageSpaceCount(), size - i);
      i += header.GetImageSpaceCount();
    }

    header_ = ImageHeader(
        /* image_reservation_size= */ RoundUp(sections_end, kPageSize),
        /* component_count= */ 1,
        image_begin_,
        sections_end,
        sections_.data(),
        /* image_roots= */ image_begin_ + sizeof(ImageHeader),
        /* oat_checksum= */ 0,
        /* oat_file_begin= */ 0,
        /* oat_data_begin= */ 0,
        /* oat_data_end= */ 0,
        /* oat_file_end= */ 0,
        heap->GetBootImagesStartAddress(),
        heap->GetBootImagesSize(),
        boot_image_components,
        boot_image_checksums,
        static_cast<uint32_t>(kRuntimePointerSize));

    // Data size includes everything except the bitmap.
    header_.data_size_ = sections_end;

    // Write image methods - needs to happen after creation of the header.
    WriteImageMethods();

    return true;
  }

  const std::vector<uint8_t>& GetObjects() const {
    return objects_;
  }

  const std::vector<uint8_t>& GetArtMethods() const {
    return art_methods_;
  }

  const std::vector<uint8_t>& GetArtFields() const {
    return art_fields_;
  }

  const std::vector<uint8_t>& GetImTables() const {
    return im_tables_;
  }

  const ImageHeader& GetHeader() const {
    return header_;
  }

  const gc::accounting::ContinuousSpaceBitmap& GetImageBitmap() const {
    return image_bitmap_;
  }

  const std::string& GetDexLocation() const {
    return dex_location_;
  }

  void GenerateInternData(std::vector<uint8_t>& data) const {
    intern_table_.WriteToMemory(data.data());
  }

  void GenerateClassTableData(std::vector<uint8_t>& data) const {
    class_table_.WriteToMemory(data.data());
  }

 private:
  bool IsInBootImage(const void* obj) const {
    return reinterpret_cast<uintptr_t>(obj) - boot_image_begin_ < boot_image_size_;
  }

  // Returns a pointer that can be stored in `objects_`:
  // - The pointer itself for boot image objects,
  // - The offset in the image for all other objects.
  mirror::Object* GetOrComputeImageAddress(ObjPtr<mirror::Object> object)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (object == nullptr || IsInBootImage(object.Ptr())) {
      DCHECK(object == nullptr || Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(object));
      return object.Ptr();
    }

    if (object->IsClassLoader()) {
      // DexCache and Class point to class loaders. For runtime-generated app
      // images, we don't encode the class loader. It will be set when the
      // runtime is loading the image.
      return nullptr;
    }

    if (object->GetClass() == GetClassRoot<mirror::ClassExt>()) {
      // No need to encode `ClassExt`. If needed, it will be reconstructed at
      // runtime.
      return nullptr;
    }

    uint32_t offset = 0u;
    if (object->IsClass()) {
      offset = CopyClass(object->AsClass());
    } else if (object->IsDexCache()) {
      offset = CopyDexCache(object->AsDexCache());
    } else {
      offset = CopyObject(object);
    }
    return reinterpret_cast<mirror::Object*>(image_begin_ + sizeof(ImageHeader) + offset);
  }

  void CreateImageSections() {
    sections_[ImageHeader::kSectionObjects] = ImageSection(0u, object_section_size_);
    sections_[ImageHeader::kSectionArtFields] =
        ImageSection(sections_[ImageHeader::kSectionObjects].End(), art_fields_.size());

    // Round up to the alignment for ArtMethod.
    static_assert(IsAligned<sizeof(void*)>(ArtMethod::Size(kRuntimePointerSize)));
    size_t cur_pos = RoundUp(sections_[ImageHeader::kSectionArtFields].End(), sizeof(void*));
    sections_[ImageHeader::kSectionArtMethods] = ImageSection(cur_pos, art_methods_.size());

    // Round up to the alignment for ImTables.
    cur_pos = RoundUp(sections_[ImageHeader::kSectionArtMethods].End(), sizeof(void*));
    sections_[ImageHeader::kSectionImTables] = ImageSection(cur_pos, im_tables_.size());

    // Round up to the alignment for conflict tables.
    cur_pos = RoundUp(sections_[ImageHeader::kSectionImTables].End(), sizeof(void*));
    sections_[ImageHeader::kSectionIMTConflictTables] = ImageSection(cur_pos, 0u);

    sections_[ImageHeader::kSectionRuntimeMethods] =
        ImageSection(sections_[ImageHeader::kSectionIMTConflictTables].End(), 0u);

    // Round up to the alignment the string table expects. See HashSet::WriteToMemory.
    cur_pos = RoundUp(sections_[ImageHeader::kSectionRuntimeMethods].End(), sizeof(uint64_t));

    size_t intern_table_bytes = intern_table_.WriteToMemory(nullptr);
    sections_[ImageHeader::kSectionInternedStrings] = ImageSection(cur_pos, intern_table_bytes);

    // Obtain the new position and round it up to the appropriate alignment.
    cur_pos = RoundUp(sections_[ImageHeader::kSectionInternedStrings].End(), sizeof(uint64_t));

    size_t class_table_bytes = class_table_.WriteToMemory(nullptr);
    sections_[ImageHeader::kSectionClassTable] = ImageSection(cur_pos, class_table_bytes);

    // Round up to the alignment of the offsets we are going to store.
    cur_pos = RoundUp(sections_[ImageHeader::kSectionClassTable].End(), sizeof(uint32_t));
    sections_[ImageHeader::kSectionStringReferenceOffsets] = ImageSection(cur_pos, 0u);

    // Round up to the alignment of the offsets we are going to store.
    cur_pos =
        RoundUp(sections_[ImageHeader::kSectionStringReferenceOffsets].End(), sizeof(uint32_t));

    sections_[ImageHeader::kSectionMetadata] = ImageSection(cur_pos, 0u);
  }

  // Returns the copied mirror Object if in the image, or the object directly if
  // in the boot image. For the copy, this is really its content, it should not
  // be returned as an `ObjPtr` (as it's not a GC object), nor stored anywhere.
  template<typename T> T* FromImageOffsetToRuntimeContent(uint32_t offset) {
    if (offset == 0u || IsInBootImage(reinterpret_cast<const void*>(offset))) {
      return reinterpret_cast<T*>(offset);
    }
    uint32_t vector_data_offset = FromImageOffsetToVectorOffset(offset);
    return reinterpret_cast<T*>(objects_.data() + vector_data_offset);
  }

  uint32_t FromImageOffsetToVectorOffset(uint32_t offset) const {
    DCHECK(!IsInBootImage(reinterpret_cast<const void*>(offset)));
    return offset - sizeof(ImageHeader) - image_begin_;
  }

  class InternStringHash {
   public:
    explicit InternStringHash(RuntimeImageHelper* helper) : helper_(helper) {}

    // NO_THREAD_SAFETY_ANALYSIS as these helpers get passed to `HashSet`.
    size_t operator()(mirror::String* str) const NO_THREAD_SAFETY_ANALYSIS {
      int32_t hash = str->GetStoredHashCode();
      DCHECK_EQ(hash, str->ComputeHashCode());
      // An additional cast to prevent undesired sign extension.
      return static_cast<uint32_t>(hash);
    }

    size_t operator()(uint32_t entry) const NO_THREAD_SAFETY_ANALYSIS {
      return (*this)(helper_->FromImageOffsetToRuntimeContent<mirror::String>(entry));
    }

   private:
    RuntimeImageHelper* helper_;
  };

  class InternStringEquals {
   public:
    explicit InternStringEquals(RuntimeImageHelper* helper) : helper_(helper) {}

    // NO_THREAD_SAFETY_ANALYSIS as these helpers get passed to `HashSet`.
    bool operator()(uint32_t entry, mirror::String* other) const NO_THREAD_SAFETY_ANALYSIS {
      if (kIsDebugBuild) {
        Locks::mutator_lock_->AssertSharedHeld(Thread::Current());
      }
      return other->Equals(helper_->FromImageOffsetToRuntimeContent<mirror::String>(entry));
    }

    bool operator()(uint32_t entry, uint32_t other) const NO_THREAD_SAFETY_ANALYSIS {
      return (*this)(entry, helper_->FromImageOffsetToRuntimeContent<mirror::String>(other));
    }

   private:
    RuntimeImageHelper* helper_;
  };

  using InternTableSet =
        HashSet<uint32_t, DefaultEmptyFn<uint32_t>, InternStringHash, InternStringEquals>;

  class ClassDescriptorHash {
   public:
    explicit ClassDescriptorHash(RuntimeImageHelper* helper) : helper_(helper) {}

    uint32_t operator()(const ClassTable::TableSlot& slot) const NO_THREAD_SAFETY_ANALYSIS {
      uint32_t ptr = slot.NonHashData();
      if (helper_->IsInBootImage(reinterpret_cast32<const void*>(ptr))) {
        return reinterpret_cast32<mirror::Class*>(ptr)->DescriptorHash();
      }
      return helper_->class_hashes_[helper_->FromImageOffsetToVectorOffset(ptr)];
    }

   private:
    RuntimeImageHelper* helper_;
  };

  class ClassDescriptorEquals {
   public:
    ClassDescriptorEquals() {}

    bool operator()(const ClassTable::TableSlot& a, const ClassTable::TableSlot& b)
        const NO_THREAD_SAFETY_ANALYSIS {
      // No need to fetch the descriptor: we know the classes we are inserting
      // in the ClassTable are unique.
      return a.Data() == b.Data();
    }
  };

  using ClassTableSet = HashSet<ClassTable::TableSlot,
                                ClassTable::TableSlotEmptyFn,
                                ClassDescriptorHash,
                                ClassDescriptorEquals>;

  void VisitDexCache(ObjPtr<mirror::DexCache> dex_cache) REQUIRES_SHARED(Locks::mutator_lock_) {
    const DexFile& dex_file = *dex_cache->GetDexFile();
    // Currently only copy string objects into the image. Populate the intern
    // table with these strings.
    for (uint32_t i = 0; i < dex_file.NumStringIds(); ++i) {
      ObjPtr<mirror::String> str = dex_cache->GetResolvedString(dex::StringIndex(i));
      if (str != nullptr && !IsInBootImage(str.Ptr())) {
        uint32_t hash = static_cast<uint32_t>(str->GetStoredHashCode());
        DCHECK_EQ(hash, static_cast<uint32_t>(str->ComputeHashCode()))
            << "Dex cache strings should be interned";
        if (intern_table_.FindWithHash(str.Ptr(), hash) == intern_table_.end()) {
          uint32_t offset = CopyObject(str);
          intern_table_.InsertWithHash(image_begin_ + offset + sizeof(ImageHeader), hash);
        }
      }
    }
  }

  // Helper class to collect classes that we will generate in the image.
  class ClassTableVisitor {
   public:
    ClassTableVisitor(Handle<mirror::ClassLoader> loader, VariableSizedHandleScope& handles)
        : loader_(loader), handles_(handles) {}

    bool operator()(ObjPtr<mirror::Class> klass) REQUIRES_SHARED(Locks::mutator_lock_) {
      // Record app classes and boot classpath classes: app classes will be
      // generated in the image and put in the class table, boot classpath
      // classes will be put in the class table.
      ObjPtr<mirror::ClassLoader> class_loader = klass->GetClassLoader();
      if (class_loader == loader_.Get() || class_loader == nullptr) {
        handles_.NewHandle(klass);
      }
      return true;
    }

   private:
    Handle<mirror::ClassLoader> loader_;
    VariableSizedHandleScope& handles_;
  };

  // Helper class visitor to filter out classes we cannot emit.
  class PruneVisitor {
   public:
    PruneVisitor(Thread* self,
                 RuntimeImageHelper* helper,
                 const ArenaSet<const DexFile*>& dex_files,
                 ArenaVector<Handle<mirror::Class>>& classes,
                 ArenaAllocator& allocator)
        : self_(self),
          helper_(helper),
          dex_files_(dex_files),
          visited_(allocator.Adapter()),
          classes_to_write_(classes) {}

    bool CanEmitHelper(Handle<mirror::Class> cls) REQUIRES_SHARED(Locks::mutator_lock_) {
      // Only emit classes that are resolved and not erroneous.
      if (!cls->IsResolved() || cls->IsErroneous()) {
        return false;
      }

      // Classes in the boot image can be trivially encoded directly.
      if (helper_->IsInBootImage(cls.Get())) {
        return true;
      }

      // If the class comes from a dex file which is not part of the primary
      // APK, don't encode it.
      if (!ContainsElement(dex_files_, &cls->GetDexFile())) {
        return false;
      }

      // Ensure pointers to classes in `cls` can also be emitted.
      StackHandleScope<1> hs(self_);
      MutableHandle<mirror::Class> other_class = hs.NewHandle(cls->GetSuperClass());
      if (!CanEmit(other_class)) {
        return false;
      }

      other_class.Assign(cls->GetComponentType());
      if (!CanEmit(other_class)) {
        return false;
      }

      for (size_t i = 0, num_interfaces = cls->NumDirectInterfaces(); i < num_interfaces; ++i) {
        other_class.Assign(cls->GetDirectInterface(i));
        if (!CanEmit(other_class)) {
          return false;
        }
      }
      return true;
    }

    bool CanEmit(Handle<mirror::Class> cls) REQUIRES_SHARED(Locks::mutator_lock_) {
      if (cls == nullptr) {
        return true;
      }
      const dex::ClassDef* class_def = cls->GetClassDef();
      if (class_def == nullptr) {
        // Covers array classes and proxy classes.
        // TODO: Handle these differently.
        return false;
      }
      auto existing = visited_.find(class_def);
      if (existing != visited_.end()) {
        // Already processed;
        return existing->second == VisitState::kCanEmit;
      }

      visited_.Put(class_def, VisitState::kVisiting);
      if (CanEmitHelper(cls)) {
        visited_.Overwrite(class_def, VisitState::kCanEmit);
        return true;
      } else {
        visited_.Overwrite(class_def, VisitState::kCannotEmit);
        return false;
      }
    }

    void Visit(Handle<mirror::Object> obj) REQUIRES_SHARED(Locks::mutator_lock_) {
      MutableHandle<mirror::Class> cls(obj.GetReference());
      if (CanEmit(cls)) {
        if (cls->IsBootStrapClassLoaded()) {
          DCHECK(helper_->IsInBootImage(cls.Get()));
          // Insert the bootclasspath class in the class table.
          uint32_t hash = cls->DescriptorHash();
          helper_->class_table_.InsertWithHash(ClassTable::TableSlot(cls.Get(), hash), hash);
        } else {
          classes_to_write_.push_back(cls);
        }
      }
    }

   private:
    enum class VisitState {
      kVisiting,
      kCanEmit,
      kCannotEmit,
    };

    Thread* const self_;
    RuntimeImageHelper* const helper_;
    const ArenaSet<const DexFile*>& dex_files_;
    ArenaSafeMap<const dex::ClassDef*, VisitState> visited_;
    ArenaVector<Handle<mirror::Class>>& classes_to_write_;
  };

  void EmitStringsAndClasses(Thread* self,
                             Handle<mirror::ObjectArray<mirror::Object>> dex_cache_array)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    ArenaAllocator allocator(Runtime::Current()->GetArenaPool());
    ArenaSet<const DexFile*> dex_files(allocator.Adapter());
    for (int32_t i = 0; i < dex_cache_array->GetLength(); ++i) {
      dex_files.insert(dex_cache_array->Get(i)->AsDexCache()->GetDexFile());
      VisitDexCache(ObjPtr<mirror::DexCache>::DownCast((dex_cache_array->Get(i))));
    }

    StackHandleScope<1> hs(self);
    Handle<mirror::ClassLoader> loader = hs.NewHandle(
        dex_cache_array->Get(0)->AsDexCache()->GetClassLoader());
    ClassTable* const class_table = loader->GetClassTable();
    if (class_table == nullptr) {
      return;
    }

    VariableSizedHandleScope handles(self);
    {
      ClassTableVisitor class_table_visitor(loader, handles);
      class_table->Visit(class_table_visitor);
    }

    ArenaVector<Handle<mirror::Class>> classes_to_write(allocator.Adapter());
    classes_to_write.reserve(class_table->Size());
    {
      PruneVisitor prune_visitor(self, this, dex_files, classes_to_write, allocator);
      handles.VisitHandles(prune_visitor);
    }

    for (Handle<mirror::Class> cls : classes_to_write) {
      ScopedAssertNoThreadSuspension sants("Writing class");
      CopyClass(cls.Get());
    }
  }

  // Helper visitor returning the location of a native pointer in the image.
  class NativePointerVisitor {
   public:
    explicit NativePointerVisitor(RuntimeImageHelper* helper) : helper_(helper) {}

    template <typename T>
    T* operator()(T* ptr, void** dest_addr ATTRIBUTE_UNUSED) const {
      return helper_->NativeLocationInImage(ptr);
    }

    template <typename T> T* operator()(T* ptr) const {
      return helper_->NativeLocationInImage(ptr);
    }

   private:
    RuntimeImageHelper* helper_;
  };

  template <typename T> T* NativeLocationInImage(T* ptr) const {
    if (ptr == nullptr || IsInBootImage(ptr)) {
      return ptr;
    }

    auto it = native_relocations_.find(ptr);
    DCHECK(it != native_relocations_.end());
    switch (it->second.first) {
      case NativeRelocationKind::kArtMethod:
      case NativeRelocationKind::kArtMethodArray: {
        uint32_t offset = sections_[ImageHeader::kSectionArtMethods].Offset();
        return reinterpret_cast<T*>(image_begin_ + offset + it->second.second);
      }
      case NativeRelocationKind::kArtFieldArray: {
        uint32_t offset = sections_[ImageHeader::kSectionArtFields].Offset();
        return reinterpret_cast<T*>(image_begin_ + offset + it->second.second);
      }
      case NativeRelocationKind::kImTable: {
        uint32_t offset = sections_[ImageHeader::kSectionImTables].Offset();
        return reinterpret_cast<T*>(image_begin_ + offset + it->second.second);
      }
    }
  }

  template <typename Visitor>
  void RelocateMethodPointerArrays(mirror::Class* klass, const Visitor& visitor)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    // A bit of magic here: we cast contents from our buffer to mirror::Class,
    // and do pointer comparison between 1) these classes, and 2) boot image objects.
    // Both kinds do not move.

    // See if we need to fixup the vtable field.
    mirror::Class* super = FromImageOffsetToRuntimeContent<mirror::Class>(
        reinterpret_cast32<uint32_t>(
            klass->GetSuperClass<kVerifyNone, kWithoutReadBarrier>().Ptr()));
    DCHECK(super != nullptr) << "j.l.Object should never be in an app runtime image";
    mirror::PointerArray* vtable = FromImageOffsetToRuntimeContent<mirror::PointerArray>(
        reinterpret_cast32<uint32_t>(klass->GetVTable<kVerifyNone, kWithoutReadBarrier>().Ptr()));
    mirror::PointerArray* super_vtable = FromImageOffsetToRuntimeContent<mirror::PointerArray>(
        reinterpret_cast32<uint32_t>(super->GetVTable<kVerifyNone, kWithoutReadBarrier>().Ptr()));
    if (vtable != nullptr && vtable != super_vtable) {
      DCHECK(!IsInBootImage(vtable));
      vtable->Fixup(vtable, kRuntimePointerSize, visitor);
    }

    // See if we need to fixup entries in the IfTable.
    mirror::IfTable* iftable = FromImageOffsetToRuntimeContent<mirror::IfTable>(
        reinterpret_cast32<uint32_t>(
            klass->GetIfTable<kVerifyNone, kWithoutReadBarrier>().Ptr()));
    mirror::IfTable* super_iftable = FromImageOffsetToRuntimeContent<mirror::IfTable>(
        reinterpret_cast32<uint32_t>(
            super->GetIfTable<kVerifyNone, kWithoutReadBarrier>().Ptr()));
    int32_t iftable_count = iftable->Count();
    int32_t super_iftable_count = super_iftable->Count();
    for (int32_t i = 0; i < iftable_count; ++i) {
      mirror::PointerArray* methods = FromImageOffsetToRuntimeContent<mirror::PointerArray>(
          reinterpret_cast32<uint32_t>(
              iftable->GetMethodArrayOrNull<kVerifyNone, kWithoutReadBarrier>(i).Ptr()));
      mirror::PointerArray* super_methods = (i < super_iftable_count)
          ? FromImageOffsetToRuntimeContent<mirror::PointerArray>(
                reinterpret_cast32<uint32_t>(
                    super_iftable->GetMethodArrayOrNull<kVerifyNone, kWithoutReadBarrier>(i).Ptr()))
          : nullptr;
      if (methods != super_methods) {
        DCHECK(!IsInBootImage(methods));
        methods->Fixup(methods, kRuntimePointerSize, visitor);
      }
    }
  }

  void RelocateNativePointers() {
    ScopedObjectAccess soa(Thread::Current());
    NativePointerVisitor visitor(this);
    for (auto it : classes_) {
      mirror::Class* cls = reinterpret_cast<mirror::Class*>(&objects_[it.second]);
      cls->FixupNativePointers(cls, kRuntimePointerSize, visitor);
      RelocateMethodPointerArrays(cls, visitor);
    }
    for (auto it : native_relocations_) {
      if (it.second.first == NativeRelocationKind::kImTable) {
        ImTable* im_table = reinterpret_cast<ImTable*>(im_tables_.data() + it.second.second);
        RelocateImTable(im_table, visitor);
      }
    }
  }

  void RelocateImTable(ImTable* im_table, const NativePointerVisitor& visitor) {
    for (size_t i = 0; i < ImTable::kSize; ++i) {
      ArtMethod* method = im_table->Get(i, kRuntimePointerSize);
      ArtMethod* new_method = nullptr;
      if (method->IsRuntimeMethod() && !IsInBootImage(method)) {
        // New IMT conflict method: just use the boot image version.
        // TODO: Consider copying the new IMT conflict method.
        new_method = Runtime::Current()->GetImtConflictMethod();
        DCHECK(IsInBootImage(new_method));
      } else {
        new_method = visitor(method);
      }
      if (method != new_method) {
        im_table->Set(i, new_method, kRuntimePointerSize);
      }
    }
  }

  void CopyFieldArrays(ObjPtr<mirror::Class> cls, uint32_t class_image_address)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    LengthPrefixedArray<ArtField>* fields[] = {
        cls->GetSFieldsPtr(), cls->GetIFieldsPtr(),
    };
    for (LengthPrefixedArray<ArtField>* cur_fields : fields) {
      if (cur_fields != nullptr) {
        // Copy the array.
        size_t number_of_fields = cur_fields->size();
        size_t size = LengthPrefixedArray<ArtField>::ComputeSize(number_of_fields);
        size_t offset = art_fields_.size();
        art_fields_.resize(offset + size);
        auto* dest_array =
            reinterpret_cast<LengthPrefixedArray<ArtField>*>(art_fields_.data() + offset);
        memcpy(dest_array, cur_fields, size);
        native_relocations_[cur_fields] =
            std::make_pair(NativeRelocationKind::kArtFieldArray, offset);

        // Update the class pointer of individual fields.
        for (size_t i = 0; i != number_of_fields; ++i) {
          dest_array->At(i).GetDeclaringClassAddressWithoutBarrier()->Assign(
              reinterpret_cast<mirror::Class*>(class_image_address));
        }
      }
    }
  }

  void CopyMethodArrays(ObjPtr<mirror::Class> cls, uint32_t class_image_address)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    size_t number_of_methods = cls->NumMethods();
    if (number_of_methods == 0) {
      return;
    }

    size_t size = LengthPrefixedArray<ArtMethod>::ComputeSize(number_of_methods);
    size_t offset = art_methods_.size();
    art_methods_.resize(offset + size);
    auto* dest_array =
        reinterpret_cast<LengthPrefixedArray<ArtMethod>*>(art_methods_.data() + offset);
    memcpy(dest_array, cls->GetMethodsPtr(), size);
    native_relocations_[cls->GetMethodsPtr()] =
        std::make_pair(NativeRelocationKind::kArtMethodArray, offset);

    for (size_t i = 0; i != number_of_methods; ++i) {
      ArtMethod* method = &cls->GetMethodsPtr()->At(i);
      ArtMethod* copy = &dest_array->At(i);

      // Update the class pointer.
      ObjPtr<mirror::Class> declaring_class = method->GetDeclaringClass();
      if (declaring_class == cls) {
        copy->GetDeclaringClassAddressWithoutBarrier()->Assign(
            reinterpret_cast<mirror::Class*>(class_image_address));
      } else {
        DCHECK(method->IsCopied());
        if (!IsInBootImage(declaring_class.Ptr())) {
          DCHECK(classes_.find(declaring_class->GetClassDef()) != classes_.end());
          copy->GetDeclaringClassAddressWithoutBarrier()->Assign(
              reinterpret_cast<mirror::Class*>(
                  image_begin_ + sizeof(ImageHeader) + classes_[declaring_class->GetClassDef()]));
        }
      }

      // Record the native relocation of the method.
      uintptr_t copy_offset =
          reinterpret_cast<uintptr_t>(copy) - reinterpret_cast<uintptr_t>(art_methods_.data());
      native_relocations_[method] = std::make_pair(NativeRelocationKind::kArtMethod, copy_offset);

      // Ignore the single-implementation info for abstract method.
      if (method->IsAbstract()) {
        copy->SetHasSingleImplementation(false);
        copy->SetSingleImplementation(nullptr, kRuntimePointerSize);
      }

      // Set the entrypoint and data pointer of the method.
      StubType stub;
      if (method->IsNative()) {
        stub = StubType::kQuickGenericJNITrampoline;
      } else if (!cls->IsVerified()) {
        stub = StubType::kQuickToInterpreterBridge;
      } else if (method->NeedsClinitCheckBeforeCall()) {
        stub = StubType::kQuickResolutionTrampoline;
      } else {
        stub = StubType::kNterpTrampoline;
      }
      const std::vector<gc::space::ImageSpace*>& image_spaces =
          Runtime::Current()->GetHeap()->GetBootImageSpaces();
      DCHECK(!image_spaces.empty());
      const OatFile* oat_file = image_spaces[0]->GetOatFile();
      DCHECK(oat_file != nullptr);
      const OatHeader& header = oat_file->GetOatHeader();
      copy->SetEntryPointFromQuickCompiledCode(header.GetOatAddress(stub));

      if (method->IsNative()) {
        StubType stub_type = method->IsCriticalNative()
            ? StubType::kJNIDlsymLookupCriticalTrampoline
            : StubType::kJNIDlsymLookupTrampoline;
        copy->SetEntryPointFromJni(header.GetOatAddress(stub_type));
      } else if (method->IsInvokable()) {
        DCHECK(method->HasCodeItem()) << method->PrettyMethod();
        ptrdiff_t code_item_offset = reinterpret_cast<const uint8_t*>(method->GetCodeItem()) -
                method->GetDexFile()->DataBegin();
        copy->SetDataPtrSize(
            reinterpret_cast<const void*>(code_item_offset), kRuntimePointerSize);
      }
    }
  }

  void CopyImTable(ObjPtr<mirror::Class> cls) REQUIRES_SHARED(Locks::mutator_lock_) {
    ImTable* table = cls->GetImt(kRuntimePointerSize);

    // If the table is null or shared and/or already emitted, we can skip.
    if (table == nullptr || IsInBootImage(table) || HasNativeRelocation(table)) {
      return;
    }
    const size_t size = ImTable::SizeInBytes(kRuntimePointerSize);
    size_t offset = im_tables_.size();
    im_tables_.resize(offset + size);
    uint8_t* dest = im_tables_.data() + offset;
    memcpy(dest, table, size);
    native_relocations_[table] = std::make_pair(NativeRelocationKind::kImTable, offset);
  }

  bool HasNativeRelocation(void* ptr) const {
    return native_relocations_.find(ptr) != native_relocations_.end();
  }

  bool WriteObjects(std::string* error_msg) {
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    ScopedObjectAccess soa(Thread::Current());
    VariableSizedHandleScope handles(soa.Self());

    Handle<mirror::Class> object_array_class = handles.NewHandle(
        GetClassRoot<mirror::ObjectArray<mirror::Object>>(class_linker));

    Handle<mirror::ObjectArray<mirror::Object>> image_roots = handles.NewHandle(
        mirror::ObjectArray<mirror::Object>::Alloc(
            soa.Self(), object_array_class.Get(), ImageHeader::kImageRootsMax));

    if (image_roots == nullptr) {
      DCHECK(soa.Self()->IsExceptionPending());
      soa.Self()->ClearException();
      *error_msg = "Out of memory when trying to generate a runtime app image";
      return false;
    }

    // Find the dex files that will be used for generating the app image.
    dchecked_vector<Handle<mirror::DexCache>> dex_caches;
    FindDexCaches(soa.Self(), dex_caches, handles);

    if (dex_caches.size() == 0) {
      *error_msg = "Did not find dex caches to generate an app image";
      return false;
    }
    const OatDexFile* oat_dex_file = dex_caches[0]->GetDexFile()->GetOatDexFile();
    VdexFile* vdex_file = oat_dex_file->GetOatFile()->GetVdexFile();
    // The first entry in `dex_caches` contains the location of the primary APK.
    dex_location_ = oat_dex_file->GetDexFileLocation();

    size_t number_of_dex_files = vdex_file->GetNumberOfDexFiles();
    if (number_of_dex_files != dex_caches.size()) {
      // This means some dex files haven't been executed. For simplicity, just
      // register them and recollect dex caches.
      Handle<mirror::ClassLoader> loader = handles.NewHandle(dex_caches[0]->GetClassLoader());
      VisitClassLoaderDexFiles(soa.Self(), loader, [&](const art::DexFile* dex_file)
          REQUIRES_SHARED(Locks::mutator_lock_) {
        class_linker->RegisterDexFile(*dex_file, dex_caches[0]->GetClassLoader());
        return true;  // Continue with other dex files.
      });
      dex_caches.clear();
      FindDexCaches(soa.Self(), dex_caches, handles);
      if (number_of_dex_files != dex_caches.size()) {
        *error_msg = "Number of dex caches does not match number of dex files in the primary APK";
        return false;
      }
    }

    // Create and populate the checksums aray.
    Handle<mirror::IntArray> checksums_array = handles.NewHandle(
        mirror::IntArray::Alloc(soa.Self(), number_of_dex_files));

    if (checksums_array == nullptr) {
      DCHECK(soa.Self()->IsExceptionPending());
      soa.Self()->ClearException();
      *error_msg = "Out of memory when trying to generate a runtime app image";
      return false;
    }

    const VdexFile::VdexChecksum* checksums = vdex_file->GetDexChecksumsArray();
    static_assert(sizeof(VdexFile::VdexChecksum) == sizeof(int32_t));
    for (uint32_t i = 0; i < number_of_dex_files; ++i) {
      checksums_array->Set(i, checksums[i]);
    }

    // Create and populate the dex caches aray.
    Handle<mirror::ObjectArray<mirror::Object>> dex_cache_array = handles.NewHandle(
        mirror::ObjectArray<mirror::Object>::Alloc(
            soa.Self(), object_array_class.Get(), dex_caches.size()));

    if (dex_cache_array == nullptr) {
      DCHECK(soa.Self()->IsExceptionPending());
      soa.Self()->ClearException();
      *error_msg = "Out of memory when trying to generate a runtime app image";
      return false;
    }

    for (uint32_t i = 0; i < dex_caches.size(); ++i) {
      dex_cache_array->Set(i, dex_caches[i].Get());
    }

    // Create the special roots array.
    Handle<mirror::ObjectArray<mirror::Object>> special_array = handles.NewHandle(
        mirror::ObjectArray<mirror::Object>::Alloc(soa.Self(), object_array_class.Get(), 2));

    ObjPtr<mirror::String> str = mirror::String::AllocFromModifiedUtf8(
        soa.Self(), oat_dex_file->GetOatFile()->GetClassLoaderContext().c_str());
    if (str == nullptr) {
      DCHECK(soa.Self()->IsExceptionPending());
      soa.Self()->ClearException();
      *error_msg = "Out of memory when trying to generate a runtime app image";
      return false;
    }
    special_array->Set(0, str);
    special_array->Set(1, checksums_array.Get());

    image_roots->Set(ImageHeader::kDexCaches, dex_cache_array.Get());
    image_roots->Set(ImageHeader::kClassRoots, class_linker->GetClassRoots());
    image_roots->Set(ImageHeader::kAppImageContextAndDexChecksums, special_array.Get());

    {
      // Now that we have created all objects needed for the `image_roots`, copy
      // it into the buffer. Note that this will recursively copy all objects
      // contained in `image_roots`. That's acceptable as we don't have cycles,
      // nor a deep graph.
      ScopedAssertNoThreadSuspension sants("Writing runtime app image");
      CopyObject(image_roots.Get());
    }

    // Emit string referenced in dex caches, and classes defined in the app class loader.
    EmitStringsAndClasses(soa.Self(), dex_cache_array);

    return true;
  }

  class FixupVisitor {
   public:
    FixupVisitor(RuntimeImageHelper* image, size_t copy_offset)
        : image_(image), copy_offset_(copy_offset) {}

    // We do not visit native roots. These are handled with other logic.
    void VisitRootIfNonNull(mirror::CompressedReference<mirror::Object>* root ATTRIBUTE_UNUSED)
        const {
      LOG(FATAL) << "UNREACHABLE";
    }
    void VisitRoot(mirror::CompressedReference<mirror::Object>* root ATTRIBUTE_UNUSED) const {
      LOG(FATAL) << "UNREACHABLE";
    }

    void operator()(ObjPtr<mirror::Object> obj,
                    MemberOffset offset,
                    bool is_static) const
        REQUIRES_SHARED(Locks::mutator_lock_) {
      // We don't copy static fields, instead classes will be marked as resolved
      // and initialized at runtime.
      ObjPtr<mirror::Object> ref =
          is_static ? nullptr : obj->GetFieldObject<mirror::Object>(offset);
      mirror::Object* address = image_->GetOrComputeImageAddress(ref.Ptr());
      mirror::Object* copy =
          reinterpret_cast<mirror::Object*>(image_->objects_.data() + copy_offset_);
      copy->GetFieldObjectReferenceAddr<kVerifyNone>(offset)->Assign(address);
    }

    // java.lang.ref.Reference visitor.
    void operator()(ObjPtr<mirror::Class> klass ATTRIBUTE_UNUSED,
                    ObjPtr<mirror::Reference> ref) const
        REQUIRES_SHARED(Locks::mutator_lock_) {
      operator()(ref, mirror::Reference::ReferentOffset(), /* is_static */ false);
    }

   private:
    RuntimeImageHelper* image_;
    size_t copy_offset_;
  };

  uint32_t CopyDexCache(ObjPtr<mirror::DexCache> cache) REQUIRES_SHARED(Locks::mutator_lock_) {
    auto it = dex_caches_.find(cache->GetDexFile());
    if (it != dex_caches_.end()) {
      return it->second;
    }
    uint32_t offset = CopyObject(cache);
    dex_caches_[cache->GetDexFile()] = offset;
    // For dex caches, clear pointers to data that will be set at runtime.
    mirror::Object* copy = reinterpret_cast<mirror::Object*>(objects_.data() + offset);
    reinterpret_cast<mirror::DexCache*>(copy)->ResetNativeArrays();
    reinterpret_cast<mirror::DexCache*>(copy)->SetDexFile(nullptr);
    return offset;
  }

  uint32_t CopyClass(ObjPtr<mirror::Class> cls) REQUIRES_SHARED(Locks::mutator_lock_) {
    const dex::ClassDef* class_def = cls->GetClassDef();
    auto it = classes_.find(class_def);
    if (it != classes_.end()) {
      return it->second;
    }
    uint32_t offset = CopyObject(cls);
    classes_[class_def] = offset;

    uint32_t hash = cls->DescriptorHash();
    // Save the hash, the `HashSet` implementation requires to find it.
    class_hashes_[offset] = hash;
    uint32_t class_image_address = image_begin_ + sizeof(ImageHeader) + offset;
    bool inserted =
        class_table_.InsertWithHash(ClassTable::TableSlot(class_image_address, hash), hash).second;
    DCHECK(inserted) << "Class " << cls->PrettyDescriptor()
                     << " (" << cls.Ptr() << ") already inserted";

    // Clear internal state.
    mirror::Class* copy = reinterpret_cast<mirror::Class*>(objects_.data() + offset);
    copy->SetClinitThreadId(static_cast<pid_t>(0u));
    copy->SetStatusInternal(cls->IsVerified() ? ClassStatus::kVerified : ClassStatus::kResolved);
    copy->SetObjectSizeAllocFastPath(std::numeric_limits<uint32_t>::max());
    copy->SetAccessFlags(copy->GetAccessFlags() & ~kAccRecursivelyInitialized);

    // Clear static field values.
    MemberOffset static_offset = cls->GetFirstReferenceStaticFieldOffset(kRuntimePointerSize);
    memset(objects_.data() + offset + static_offset.Uint32Value(),
           0,
           cls->GetClassSize() - static_offset.Uint32Value());

    CopyFieldArrays(cls, class_image_address);
    CopyMethodArrays(cls, class_image_address);
    if (cls->ShouldHaveImt()) {
      CopyImTable(cls);
    }

    return offset;
  }

  // Copy `obj` in `objects_` and relocate references. Returns the offset
  // within our buffer.
  uint32_t CopyObject(ObjPtr<mirror::Object> obj) REQUIRES_SHARED(Locks::mutator_lock_) {
    // Copy the object in `objects_`.
    size_t object_size = obj->SizeOf();
    size_t offset = objects_.size();
    DCHECK(IsAligned<kObjectAlignment>(offset));
    object_offsets_.push_back(offset);
    objects_.resize(RoundUp(offset + object_size, kObjectAlignment));
    memcpy(objects_.data() + offset, obj.Ptr(), object_size);
    object_section_size_ += RoundUp(object_size, kObjectAlignment);

    // Fixup reference pointers.
    FixupVisitor visitor(this, offset);
    obj->VisitReferences</*kVisitNativeRoots=*/ false>(visitor, visitor);

    mirror::Object* copy = reinterpret_cast<mirror::Object*>(objects_.data() + offset);

    // Clear any lockword data.
    copy->SetLockWord(LockWord::Default(), /* as_volatile= */ false);

    if (obj->IsString()) {
      // Ensure a string always has a hashcode stored. This is checked at
      // runtime because boot images don't want strings dirtied due to hashcode.
      reinterpret_cast<mirror::String*>(copy)->GetHashCode();
    }
    return offset;
  }

  class CollectDexCacheVisitor : public DexCacheVisitor {
   public:
    explicit CollectDexCacheVisitor(VariableSizedHandleScope& handles) : handles_(handles) {}

    void Visit(ObjPtr<mirror::DexCache> dex_cache)
        REQUIRES_SHARED(Locks::dex_lock_, Locks::mutator_lock_) override {
      dex_caches_.push_back(handles_.NewHandle(dex_cache));
    }
    const std::vector<Handle<mirror::DexCache>>& GetDexCaches() const {
      return dex_caches_;
    }
   private:
    VariableSizedHandleScope& handles_;
    std::vector<Handle<mirror::DexCache>> dex_caches_;
  };

  // Find dex caches corresponding to the primary APK.
  void FindDexCaches(Thread* self,
                     dchecked_vector<Handle<mirror::DexCache>>& dex_caches,
                     VariableSizedHandleScope& handles)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(dex_caches.empty());
    // Collect all dex caches.
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    CollectDexCacheVisitor visitor(handles);
    {
      ReaderMutexLock mu(self, *Locks::dex_lock_);
      class_linker->VisitDexCaches(&visitor);
    }

    // Find the primary APK.
    AppInfo* app_info = Runtime::Current()->GetAppInfo();
    for (Handle<mirror::DexCache> cache : visitor.GetDexCaches()) {
      if (app_info->GetRegisteredCodeType(cache->GetDexFile()->GetLocation()) ==
              AppInfo::CodeType::kPrimaryApk) {
        dex_caches.push_back(handles.NewHandle(cache.Get()));
        break;
      }
    }

    if (dex_caches.empty()) {
      return;
    }

    const OatDexFile* oat_dex_file = dex_caches[0]->GetDexFile()->GetOatDexFile();
    if (oat_dex_file == nullptr) {
      // We need a .oat file for loading an app image;
      dex_caches.clear();
      return;
    }
    const OatFile* oat_file = oat_dex_file->GetOatFile();
    for (Handle<mirror::DexCache> cache : visitor.GetDexCaches()) {
      if (cache.Get() != dex_caches[0].Get()) {
        const OatDexFile* other_oat_dex_file = cache->GetDexFile()->GetOatDexFile();
        if (other_oat_dex_file != nullptr && other_oat_dex_file->GetOatFile() == oat_file) {
          dex_caches.push_back(handles.NewHandle(cache.Get()));
        }
      }
    }
  }

  static uint64_t PointerToUint64(void* ptr) {
    return reinterpret_cast64<uint64_t>(ptr);
  }

  void WriteImageMethods() {
    ScopedObjectAccess soa(Thread::Current());
    // We can just use plain runtime pointers.
    Runtime* runtime = Runtime::Current();
    header_.image_methods_[ImageHeader::kResolutionMethod] =
        PointerToUint64(runtime->GetResolutionMethod());
    header_.image_methods_[ImageHeader::kImtConflictMethod] =
        PointerToUint64(runtime->GetImtConflictMethod());
    header_.image_methods_[ImageHeader::kImtUnimplementedMethod] =
        PointerToUint64(runtime->GetImtUnimplementedMethod());
    header_.image_methods_[ImageHeader::kSaveAllCalleeSavesMethod] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveAllCalleeSaves));
    header_.image_methods_[ImageHeader::kSaveRefsOnlyMethod] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveRefsOnly));
    header_.image_methods_[ImageHeader::kSaveRefsAndArgsMethod] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveRefsAndArgs));
    header_.image_methods_[ImageHeader::kSaveEverythingMethod] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveEverything));
    header_.image_methods_[ImageHeader::kSaveEverythingMethodForClinit] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveEverythingForClinit));
    header_.image_methods_[ImageHeader::kSaveEverythingMethodForSuspendCheck] =
        PointerToUint64(
            runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveEverythingForSuspendCheck));
  }

  // Header for the image, created at the end once we know the size of all
  // sections.
  ImageHeader header_;

  // Contents of the various sections.
  std::vector<uint8_t> objects_;
  std::vector<uint8_t> art_fields_;
  std::vector<uint8_t> art_methods_;
  std::vector<uint8_t> im_tables_;

  // Bitmap of live objects in `objects_`. Populated from `object_offsets_`
  // once we know `object_section_size`.
  gc::accounting::ContinuousSpaceBitmap image_bitmap_;

  // Sections stored in the header.
  dchecked_vector<ImageSection> sections_;

  // A list of offsets in `objects_` where objects begin.
  std::vector<uint32_t> object_offsets_;

  std::map<const dex::ClassDef*, uint32_t> classes_;
  std::map<const DexFile*, uint32_t> dex_caches_;
  std::map<uint32_t, uint32_t> class_hashes_;

  std::map<void*, std::pair<NativeRelocationKind, uint32_t>> native_relocations_;

  // Cached values of boot image information.
  const uint32_t boot_image_begin_;
  const uint32_t boot_image_size_;

  // Where the image begins: just after the boot image.
  const uint32_t image_begin_;

  // Size of the `kSectionObjects` section.
  size_t object_section_size_;

  // The location of the primary APK / dex file.
  std::string dex_location_;

  // The intern table for strings that we will write to disk.
  InternTableSet intern_table_;

  // The class table holding classes that we will write to disk.
  ClassTableSet class_table_;

  friend class ClassDescriptorHash;
  friend class PruneVisitor;
  friend class NativePointerVisitor;
};

static const char* GetImageExtension() {
  return kRuntimePointerSize == PointerSize::k32 ? "art32" : "art64";
}

std::string RuntimeImage::GetRuntimeImagePath(const std::string& dex_location) {
  const std::string& data_dir = Runtime::Current()->GetProcessDataDirectory();

  std::string new_location = ReplaceFileExtension(dex_location, GetImageExtension());

  if (data_dir.empty()) {
    // The data ditectory is empty for tests.
    return new_location;
  } else {
    std::replace(new_location.begin(), new_location.end(), '/', '@');
    return data_dir + "/oat/" + new_location;
  }
}

static bool EnsureDirectoryExists(const std::string& path, std::string* error_msg) {
  size_t last_slash_pos = path.find_last_of('/');
  CHECK_NE(last_slash_pos, std::string::npos) << "Invalid path: " << path;
  std::string directory = path.substr(0, last_slash_pos);
  if (!OS::DirectoryExists(directory.c_str())) {
    static constexpr mode_t kDirectoryMode = S_IRWXU | S_IRGRP | S_IXGRP| S_IROTH | S_IXOTH;
    if (mkdir(directory.c_str(), kDirectoryMode) != 0) {
      *error_msg =
          StringPrintf("Could not create directory %s: %s", directory.c_str(), strerror(errno));
      return false;
    }
  }
  return true;
}

bool RuntimeImage::WriteImageToDisk(std::string* error_msg) {
  gc::Heap* heap = Runtime::Current()->GetHeap();
  if (!heap->HasBootImageSpace()) {
    *error_msg = "Cannot generate an app image without a boot image";
    return false;
  }
  ScopedTrace generate_image_trace("Generating runtime image");
  RuntimeImageHelper image(heap);
  if (!image.Generate(error_msg)) {
    return false;
  }

  ScopedTrace write_image_trace("Writing runtime image to disk");
  const std::string path = GetRuntimeImagePath(image.GetDexLocation());
  if (!EnsureDirectoryExists(path, error_msg)) {
    return false;
  }
  // We first generate the app image in a temporary file, which we will then
  // move to `path`.
  const std::string temp_path =
      ReplaceFileExtension(path, std::to_string(getpid()) + GetImageExtension());
  std::unique_ptr<File> out(OS::CreateEmptyFileWriteOnly(temp_path.c_str()));
  if (out == nullptr) {
    *error_msg = "Could not open " + temp_path + " for writing";
    return false;
  }

  // Write objects. The header is written at the end in case we get killed.
  if (out->Write(reinterpret_cast<const char*>(image.GetObjects().data()),
                 image.GetObjects().size(),
                 sizeof(ImageHeader)) != static_cast<int64_t>(image.GetObjects().size())) {
    *error_msg = "Could not write image data to " + temp_path;
    out->Erase(/*unlink=*/true);
    return false;
  }

  {
    // Write fields.
    auto fields_section = image.GetHeader().GetImageSection(ImageHeader::kSectionArtFields);
    if (out->Write(reinterpret_cast<const char*>(image.GetArtFields().data()),
                   fields_section.Size(),
                   fields_section.Offset()) != fields_section.Size()) {
      *error_msg = "Could not write fields section " + temp_path;
      out->Erase(/*unlink=*/true);
      return false;
    }
  }

  {
    // Write methods.
    auto methods_section = image.GetHeader().GetImageSection(ImageHeader::kSectionArtMethods);
    if (out->Write(reinterpret_cast<const char*>(image.GetArtMethods().data()),
                   methods_section.Size(),
                   methods_section.Offset()) != methods_section.Size()) {
      *error_msg = "Could not write methods section " + temp_path;
      out->Erase(/*unlink=*/true);
      return false;
    }
  }

  {
    // Write im tables.
    auto im_tables_section = image.GetHeader().GetImageSection(ImageHeader::kSectionImTables);
    if (out->Write(reinterpret_cast<const char*>(image.GetImTables().data()),
                   im_tables_section.Size(),
                   im_tables_section.Offset()) != im_tables_section.Size()) {
      *error_msg = "Could not write ImTable section " + temp_path;
      out->Erase(/*unlink=*/true);
      return false;
    }
  }

  {
    // Write intern string set.
    auto intern_section = image.GetHeader().GetImageSection(ImageHeader::kSectionInternedStrings);
    std::vector<uint8_t> intern_data(intern_section.Size());
    image.GenerateInternData(intern_data);
    if (out->Write(reinterpret_cast<const char*>(intern_data.data()),
                   intern_section.Size(),
                   intern_section.Offset()) != intern_section.Size()) {
      *error_msg = "Could not write intern section " + temp_path;
      out->Erase(/*unlink=*/true);
      return false;
    }
  }

  {
    // Write class table.
    auto class_table_section = image.GetHeader().GetImageSection(ImageHeader::kSectionClassTable);
    std::vector<uint8_t> class_table_data(class_table_section.Size());
    image.GenerateClassTableData(class_table_data);
    if (out->Write(reinterpret_cast<const char*>(class_table_data.data()),
                   class_table_section.Size(),
                   class_table_section.Offset()) != class_table_section.Size()) {
      *error_msg = "Could not write class table section " + temp_path;
      out->Erase(/*unlink=*/true);
      return false;
    }
  }

  // Write bitmap.
  auto bitmap_section = image.GetHeader().GetImageSection(ImageHeader::kSectionImageBitmap);
  if (out->Write(reinterpret_cast<const char*>(image.GetImageBitmap().Begin()),
                 bitmap_section.Size(),
                 bitmap_section.Offset()) != bitmap_section.Size()) {
    *error_msg = "Could not write image bitmap " + temp_path;
    out->Erase(/*unlink=*/true);
    return false;
  }

  // Now write header.
  if (out->Write(reinterpret_cast<const char*>(&image.GetHeader()), sizeof(ImageHeader), 0u) !=
          sizeof(ImageHeader)) {
    *error_msg = "Could not write image header to " + temp_path;
    out->Erase(/*unlink=*/true);
    return false;
  }

  if (out->FlushClose() != 0) {
    *error_msg = "Could not flush and close " + temp_path;
    // Unlink directly: we cannot use `out` as we may have closed it.
    unlink(temp_path.c_str());
    return false;
  }

  if (rename(temp_path.c_str(), path.c_str()) != 0) {
    *error_msg =
        "Failed to move runtime app image to " + path + ": " + std::string(strerror(errno));
    // Unlink directly: we cannot use `out` as we have closed it.
    unlink(temp_path.c_str());
    return false;
  }

  return true;
}

}  // namespace art
