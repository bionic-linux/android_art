/* Copyright (C) 2016 The Android Open Source Project
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This file implements interfaces from the file jvmti.h. This implementation
 * is licensed under the same terms as the file jvmti.h.  The
 * copyright and license information for the file jvmti.h follows.
 *
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#ifndef ART_RUNTIME_OPENJDKJVMTI_TI_REDEFINE_H_
#define ART_RUNTIME_OPENJDKJVMTI_TI_REDEFINE_H_

#include <string>

#include <jni.h>

#include "art_jvmti.h"
#include "art_method.h"
#include "class_linker.h"
#include "dex_file.h"
#include "gc_root-inl.h"
#include "globals.h"
#include "jni_env_ext-inl.h"
#include "jvmti.h"
#include "linear_alloc.h"
#include "mem_map.h"
#include "mirror/array-inl.h"
#include "mirror/array.h"
#include "mirror/class-inl.h"
#include "mirror/class.h"
#include "mirror/class_loader-inl.h"
#include "mirror/string-inl.h"
#include "oat_file.h"
#include "obj_ptr.h"
#include "scoped_thread_state_change-inl.h"
#include "stack.h"
#include "thread_list.h"
#include "transform.h"
#include "utf.h"
#include "utils/dex_cache_arrays_layout-inl.h"

namespace openjdkjvmti {

// Class that can redefine a single class's methods.
// TODO We should really make this be driven by an outside class so we can do multiple classes at
// the same time and have less required cleanup.
class Redefiner {
 public:
  // Redefine the given class with the given dex data. Note this function does not take ownership of
  // the dex_data pointer. It is not used after this call however and may be freed if desired.
  // The caller is responsible for freeing it. The runtime makes its own copy of the data.
  static jvmtiError RedefineClass(ArtJvmTiEnv* env,
                                  art::Runtime* runtime,
                                  art::Thread* self,
                                  jclass klass,
                                  const std::string& original_dex_location,
                                  jint data_len,
                                  unsigned char* dex_data,
                                  std::string* error_msg);

 private:
  jvmtiError result_;
  art::Runtime* runtime_;
  art::Thread* self_;
  // Kept as a jclass since we have weird run-state changes that make keeping it around as a
  // mirror::Class difficult and confusing.
  jclass klass_;
  std::unique_ptr<const art::DexFile> dex_file_;
  std::string* error_msg_;
  char* class_sig_;

  // TODO Maybe change jclass to a mirror::Class
  Redefiner(art::Runtime* runtime,
            art::Thread* self,
            jclass klass,
            char* class_sig,
            std::unique_ptr<const art::DexFile>& redefined_dex_file,
            std::string* error_msg)
      : result_(ERR(INTERNAL)),
        runtime_(runtime),
        self_(self),
        klass_(klass),
        dex_file_(std::move(redefined_dex_file)),
        error_msg_(error_msg),
        class_sig_(class_sig) { }

  static std::unique_ptr<art::MemMap> MoveDataToMemMap(const std::string& original_location,
                                                       jint data_len,
                                                       unsigned char* dex_data,
                                                       std::string* error_msg);

  // TODO Put on all the lock qualifiers.
  jvmtiError Run() REQUIRES_SHARED(art::Locks::mutator_lock_);

  bool FinishRemainingAllocations(
        /*out*/art::MutableHandle<art::mirror::ClassLoader>* source_class_loader,
        /*out*/art::MutableHandle<art::mirror::Object>* source_dex_file_obj,
        /*out*/art::MutableHandle<art::mirror::LongArray>* new_dex_file_cookie,
        /*out*/art::MutableHandle<art::mirror::DexCache>* new_dex_cache)
      REQUIRES_SHARED(art::Locks::mutator_lock_);

  // Preallocates all needed allocations in klass so that we can pause execution safely.
  // TODO We should be able to free the arrays if they end up not being used. Investigate doing this
  // in the future. For now we will just take the memory hit.
  bool EnsureClassAllocationsFinished() REQUIRES_SHARED(art::Locks::mutator_lock_);

  // Ensure that obsolete methods are deoptimized. This is needed since optimized methods may have
  // pointers to their ArtMethods stashed in registers that they then use to attempt to hit the
  // DexCache.
  void EnsureObsoleteMethodsAreDeoptimized()
      REQUIRES(art::Locks::mutator_lock_)
      REQUIRES(!art::Locks::thread_list_lock_,
               !art::Locks::classlinker_classes_lock_);

  art::mirror::ClassLoader* GetClassLoader() REQUIRES_SHARED(art::Locks::mutator_lock_);

  // This finds the java.lang.DexFile we will add the native DexFile to as part of the classpath.
  // TODO Make sure the DexFile object returned is the one that the klass_ actually comes from.
  art::mirror::Object* FindSourceDexFileObject(art::Handle<art::mirror::ClassLoader> loader)
      REQUIRES_SHARED(art::Locks::mutator_lock_);

  art::mirror::Class* GetMirrorClass() REQUIRES_SHARED(art::Locks::mutator_lock_);

  // Allocates and fills the new DexFileCookie
  art::mirror::LongArray* AllocateDexFileCookie(art::Handle<art::mirror::Object> java_dex_file_obj)
      REQUIRES_SHARED(art::Locks::mutator_lock_);

  art::mirror::DexCache* CreateNewDexCache(art::Handle<art::mirror::ClassLoader> loader)
      REQUIRES_SHARED(art::Locks::mutator_lock_);

  void RecordFailure(jvmtiError result, const std::string& error_msg);

  // TODO Actually write this.
  // This will check that no constraints are violated (more than 1 class in dex file, any changes in
  // number/declaration of methods & fields, changes in access flags, etc.)
  bool EnsureRedefinitionIsValid() {
    LOG(WARNING) << "Redefinition is not checked for validity currently";
    return true;
  }

  bool UpdateJavaDexFile(art::ObjPtr<art::mirror::Object> java_dex_file,
                         art::ObjPtr<art::mirror::LongArray> new_cookie,
                         /*out*/art::ObjPtr<art::mirror::LongArray>* original_cookie)
      REQUIRES(art::Locks::mutator_lock_);

  void RestoreJavaDexFile(art::ObjPtr<art::mirror::Object> java_dex_file,
                          art::ObjPtr<art::mirror::LongArray> original_cookie)
      REQUIRES(art::Locks::mutator_lock_);

  bool UpdateFields(art::ObjPtr<art::mirror::Class> mclass)
      REQUIRES(art::Locks::mutator_lock_);

  bool UpdateMethods(art::ObjPtr<art::mirror::Class> mclass,
                     art::ObjPtr<art::mirror::DexCache> new_dex_cache,
                     const art::DexFile::ClassDef& class_def)
      REQUIRES(art::Locks::mutator_lock_);

  bool UpdateClass(art::ObjPtr<art::mirror::Class> mclass,
                   art::ObjPtr<art::mirror::DexCache> new_dex_cache)
      REQUIRES(art::Locks::mutator_lock_);

  bool FindAndAllocateObsoleteMethods(art::mirror::Class* art_klass)
      REQUIRES(art::Locks::mutator_lock_);

  void FillObsoleteMethodMap(art::mirror::Class* art_klass,
                             const std::unordered_map<art::ArtMethod*, art::ArtMethod*>& obsoletes)
      REQUIRES(art::Locks::mutator_lock_);
};

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_TI_REDEFINE_H_
