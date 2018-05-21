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

#ifndef ART_LIBDEXFILE_DEX_CLASS_ACCESSOR_H_
#define ART_LIBDEXFILE_DEX_CLASS_ACCESSOR_H_

#include "base/utils.h"
#include "dex_file.h"

namespace art {

// Classes to access Dex data.
class ClassAccessor {
 public:
  // Class method data.
  class Method {
   public:
    const uint8_t* Read(const uint8_t* ptr);

    uint32_t GetIndex() const {
      return method_idx_;
    }

    uint32_t GetAccessFlags() const {
      return access_flags_;
    }

    uint32_t GetCodeItemOffset() const {
      return code_off_;
    }

   private:
    // A decoded version of the method of a class_data_item
    uint32_t method_idx_ = 0u;
    uint32_t access_flags_ = 0u;
    uint32_t code_off_ = 0u;
  };

  // Class field data.
  class Field {
   public:
    const uint8_t* Read(const uint8_t* ptr);

   private:
    uint32_t field_idx_ = 0u;
    uint32_t access_flags_ = 0u;
  };

  ALWAYS_INLINE ClassAccessor(const DexFile& dex_file, const DexFile::ClassDef& class_def);

  // Return the code item for a method.
  const DexFile::CodeItem* GetCodeItem(const Method& method) const;

  // Iterator data is not very iterator friendly, use visitors to get around this.
  template <typename StaticFieldVisitor,
            typename InstanceFieldVisitor,
            typename DirectMethodVisitor,
            typename VirtualMethodVisitor>
  void VisitMethodsAndFields(const StaticFieldVisitor& static_field_visitor,
                             const InstanceFieldVisitor& instance_field_visitor,
                             const DirectMethodVisitor& direct_method_visitor,
                             const VirtualMethodVisitor& virtual_method_visitor);

  template <typename DirectMethodVisitor,
            typename VirtualMethodVisitor>
  void VisitMethods(const DirectMethodVisitor& direct_method_visitor,
                    const VirtualMethodVisitor& virtual_method_visitor) {
    VisitMethodsAndFields(VoidFunctor(),
                          VoidFunctor(),
                          direct_method_visitor,
                          virtual_method_visitor);
  }

  // Visit direct and virtual methods.
  template <typename MethodVisitor>
  void VisitMethods(const MethodVisitor& method_visitor) {
    VisitMethods(method_visitor, method_visitor);
  }

  uint32_t NumStaticFields() const {
    return num_static_fields_;
  }

  uint32_t NumInstanceFields() const {
    return num_instance_fields_;
  }

  uint32_t NumDirectMethods() const {
    return num_direct_methods_;
  }

  uint32_t NumVirtualMethods() const {
    return num_virtual_methods_;
  }

 protected:
  ALWAYS_INLINE ClassAccessor(const DexFile& dex_file, const uint8_t* class_data);

  const DexFile& dex_file_;
  const uint8_t* ptr_pos_ = nullptr;  // Pointer into stream of class_data_item.
  const uint32_t num_static_fields_ = 0u;
  const uint32_t num_instance_fields_ = 0u;
  const uint32_t num_direct_methods_ = 0u;
  const uint32_t num_virtual_methods_ = 0u;
  // Only cache descriptor.
  const void* class_def_ = nullptr;
  const void* class_data_ = nullptr;
};

}  // namespace art

#endif  // ART_LIBDEXFILE_DEX_CLASS_ACCESSOR_H_
