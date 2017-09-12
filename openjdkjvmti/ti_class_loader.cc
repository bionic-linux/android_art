/* Copyright (C) 2017 The Android Open Source Project
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

#include "ti_class_loader.h"

#include <limits>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include "art_field-inl.h"
#include "art_jvmti.h"
#include "dex/dex_file.h"
#include "dex/dex_file_types.h"
#include "events-inl.h"
#include "gc/allocation_listener.h"
#include "gc/heap.h"
#include "instrumentation.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "jni_env_ext-inl.h"
#include "jvmti_allocator.h"
#include "mirror/class.h"
#include "mirror/class_ext.h"
#include "mirror/object.h"
#include "native/dalvik_system_DexFile.h"
#include "nativehelper/scoped_local_ref.h"
#include "object_lock.h"
#include "runtime.h"
#include "transform.h"
#include "well_known_classes.h"

namespace openjdkjvmti {

bool ClassLoaderHelper::AddToClassLoader(art::Thread* self,
                                         art::Handle<art::mirror::ClassLoader> loader,
                                         const art::DexFile* dex_file) {
  art::ScopedObjectAccessUnchecked soa(self);
  art::StackHandleScope<3> hs(self);
  if (art::ClassLinker::IsBootClassLoader(soa, loader.Get())) {
    art::Runtime::Current()->GetClassLinker()->AppendToBootClassPath(self, *dex_file);
    return true;
  }
  art::Handle<art::mirror::Object> java_dex_file_obj(
      hs.NewHandle(FindSourceDexFileObject(self, loader)));
  if (java_dex_file_obj.IsNull()) {
    return false;
  }
  art::ScopedAssertNoThreadSuspension nts("Updating cookie field in j.l.DexFile object");
  return UpdateJavaDexFile(java_dex_file_obj.Get(), dex_file);
}

bool ClassLoaderHelper::UpdateJavaDexFile(art::ObjPtr<art::mirror::Object> java_dex_file,
                                          const art::DexFile* dex_file) {
  art::ArtField* cookie_field = art::jni::DecodeArtField(art::WellKnownClasses::dalvik_system_DexFile_cookie);
  CHECK(cookie_field != nullptr);
  art::DexFileCookie* cookie = art::DexFileCookieFromAddr(cookie_field->GetLong(java_dex_file));
  if (cookie == nullptr) {
    return false;
  }
  cookie->dex_files.emplace(cookie->dex_files.begin(), dex_file);
  return true;
}

// TODO This should return the actual source java.lang.DexFile object for the klass being loaded.
art::ObjPtr<art::mirror::Object> ClassLoaderHelper::FindSourceDexFileObject(
    art::Thread* self, art::Handle<art::mirror::ClassLoader> loader) {
  const char* dex_path_list_element_array_name = "[Ldalvik/system/DexPathList$Element;";
  const char* dex_path_list_element_name = "Ldalvik/system/DexPathList$Element;";
  const char* dex_file_name = "Ldalvik/system/DexFile;";
  const char* dex_path_list_name = "Ldalvik/system/DexPathList;";
  const char* dex_class_loader_name = "Ldalvik/system/BaseDexClassLoader;";

  CHECK(!self->IsExceptionPending());
  art::StackHandleScope<5> hs(self);
  art::ClassLinker* class_linker = art::Runtime::Current()->GetClassLinker();

  art::Handle<art::mirror::ClassLoader> null_loader(hs.NewHandle<art::mirror::ClassLoader>(
      nullptr));
  art::Handle<art::mirror::Class> base_dex_loader_class(hs.NewHandle(class_linker->FindClass(
      self, dex_class_loader_name, null_loader)));

  // Get all the ArtFields so we can look in the BaseDexClassLoader
  art::ArtField* path_list_field = base_dex_loader_class->FindDeclaredInstanceField(
      "pathList", dex_path_list_name);
  CHECK(path_list_field != nullptr);

  art::ArtField* dex_path_list_element_field =
      class_linker->FindClass(self, dex_path_list_name, null_loader)
        ->FindDeclaredInstanceField("dexElements", dex_path_list_element_array_name);
  CHECK(dex_path_list_element_field != nullptr);

  art::ArtField* element_dex_file_field =
      class_linker->FindClass(self, dex_path_list_element_name, null_loader)
        ->FindDeclaredInstanceField("dexFile", dex_file_name);
  CHECK(element_dex_file_field != nullptr);

  // Check if loader is a BaseDexClassLoader
  art::Handle<art::mirror::Class> loader_class(hs.NewHandle(loader->GetClass()));
  // Currently only base_dex_loader is allowed to actually define classes but if this changes in the
  // future we should make sure to support all class loader types.
  if (!loader_class->IsSubClass(base_dex_loader_class.Get())) {
    LOG(ERROR) << "The classloader is not a BaseDexClassLoader which is currently the only "
               << "supported class loader type!";
    return nullptr;
  }
  // Start navigating the fields of the loader (now known to be a BaseDexClassLoader derivative)
  art::Handle<art::mirror::Object> path_list(
      hs.NewHandle(path_list_field->GetObject(loader.Get())));
  CHECK(path_list != nullptr);
  CHECK(!self->IsExceptionPending());
  art::Handle<art::mirror::ObjectArray<art::mirror::Object>> dex_elements_list(hs.NewHandle(
      dex_path_list_element_field->GetObject(path_list.Get())->
      AsObjectArray<art::mirror::Object>()));
  CHECK(!self->IsExceptionPending());
  CHECK(dex_elements_list != nullptr);
  size_t num_elements = dex_elements_list->GetLength();
  // Iterate over the DexPathList$Element to find the right one
  for (size_t i = 0; i < num_elements; i++) {
    art::ObjPtr<art::mirror::Object> current_element = dex_elements_list->Get(i);
    CHECK(!current_element.IsNull());
    // TODO It would be cleaner to put the art::DexFile into the dalvik.system.DexFile the class
    // comes from but it is more annoying because we would need to find this class. It is not
    // necessary for proper function since we just need to be in front of the classes old dex file
    // in the path.
    art::ObjPtr<art::mirror::Object> first_dex_file(
        element_dex_file_field->GetObject(current_element));
    if (!first_dex_file.IsNull()) {
      return first_dex_file;
    }
  }
  return nullptr;
}

}  // namespace openjdkjvmti
