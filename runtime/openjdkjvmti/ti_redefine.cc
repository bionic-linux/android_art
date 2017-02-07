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

#include "ti_redefine.h"

#include <limits>

#include "android-base/stringprintf.h"

#include "art_jvmti.h"
#include "base/array_slice.h"
#include "base/logging.h"
#include "dex_file.h"
#include "dex_file_types.h"
#include "events-inl.h"
#include "gc/allocation_listener.h"
#include "gc/heap.h"
#include "instrumentation.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "jni_env_ext-inl.h"
#include "jvmti_allocator.h"
#include "mirror/class-inl.h"
#include "mirror/class_ext.h"
#include "mirror/object.h"
#include "object_lock.h"
#include "runtime.h"
#include "ScopedLocalRef.h"
#include "ti_class_loader.h"
#include "transform.h"
#include "verifier/method_verifier.h"
#include "verifier/verifier_log_mode.h"

namespace openjdkjvmti {

using android::base::StringPrintf;

// This visitor walks thread stacks and allocates and sets up the obsolete methods. It also does
// some basic sanity checks that the obsolete method is sane.
class ObsoleteMethodStackVisitor : public art::StackVisitor {
 protected:
  ObsoleteMethodStackVisitor(
      art::Thread* thread,
      art::LinearAlloc* allocator,
      const std::unordered_set<art::ArtMethod*>& obsoleted_methods,
      /*out*/std::unordered_map<art::ArtMethod*, art::ArtMethod*>* obsolete_maps)
        : StackVisitor(thread,
                       /*context*/nullptr,
                       StackVisitor::StackWalkKind::kIncludeInlinedFrames),
          allocator_(allocator),
          obsoleted_methods_(obsoleted_methods),
          obsolete_maps_(obsolete_maps) { }

  ~ObsoleteMethodStackVisitor() OVERRIDE {}

 public:
  // Returns true if we successfully installed obsolete methods on this thread, filling
  // obsolete_maps_ with the translations if needed. Returns false and fills error_msg if we fail.
  // The stack is cleaned up when we fail.
  static void UpdateObsoleteFrames(
      art::Thread* thread,
      art::LinearAlloc* allocator,
      const std::unordered_set<art::ArtMethod*>& obsoleted_methods,
      /*out*/std::unordered_map<art::ArtMethod*, art::ArtMethod*>* obsolete_maps)
        REQUIRES(art::Locks::mutator_lock_) {
    ObsoleteMethodStackVisitor visitor(thread,
                                       allocator,
                                       obsoleted_methods,
                                       obsolete_maps);
    visitor.WalkStack();
  }

  bool VisitFrame() OVERRIDE REQUIRES(art::Locks::mutator_lock_) {
    art::ArtMethod* old_method = GetMethod();
    if (obsoleted_methods_.find(old_method) != obsoleted_methods_.end()) {
      // We cannot ensure that the right dex file is used in inlined frames so we don't support
      // redefining them.
      DCHECK(!IsInInlinedFrame()) << "Inlined frames are not supported when using redefinition";
      // TODO We should really support intrinsic obsolete methods.
      // TODO We should really support redefining intrinsics.
      // We don't support intrinsics so check for them here.
      DCHECK(!old_method->IsIntrinsic());
      art::ArtMethod* new_obsolete_method = nullptr;
      auto obsolete_method_pair = obsolete_maps_->find(old_method);
      if (obsolete_method_pair == obsolete_maps_->end()) {
        // Create a new Obsolete Method and put it in the list.
        art::Runtime* runtime = art::Runtime::Current();
        art::ClassLinker* cl = runtime->GetClassLinker();
        auto ptr_size = cl->GetImagePointerSize();
        const size_t method_size = art::ArtMethod::Size(ptr_size);
        auto* method_storage = allocator_->Alloc(GetThread(), method_size);
        CHECK(method_storage != nullptr) << "Unable to allocate storage for obsolete version of '"
                                         << old_method->PrettyMethod() << "'";
        new_obsolete_method = new (method_storage) art::ArtMethod();
        new_obsolete_method->CopyFrom(old_method, ptr_size);
        DCHECK_EQ(new_obsolete_method->GetDeclaringClass(), old_method->GetDeclaringClass());
        new_obsolete_method->SetIsObsolete();
        new_obsolete_method->SetDontCompile();
        obsolete_maps_->insert({old_method, new_obsolete_method});
        // Update JIT Data structures to point to the new method.
        art::jit::Jit* jit = art::Runtime::Current()->GetJit();
        if (jit != nullptr) {
          // Notify the JIT we are making this obsolete method. It will update the jit's internal
          // structures to keep track of the new obsolete method.
          jit->GetCodeCache()->MoveObsoleteMethod(old_method, new_obsolete_method);
        }
      } else {
        new_obsolete_method = obsolete_method_pair->second;
      }
      DCHECK(new_obsolete_method != nullptr);
      SetMethod(new_obsolete_method);
    }
    return true;
  }

 private:
  // The linear allocator we should use to make new methods.
  art::LinearAlloc* allocator_;
  // The set of all methods which could be obsoleted.
  const std::unordered_set<art::ArtMethod*>& obsoleted_methods_;
  // A map from the original to the newly allocated obsolete method for frames on this thread. The
  // values in this map must be added to the obsolete_methods_ (and obsolete_dex_caches_) fields of
  // the redefined classes ClassExt by the caller.
  std::unordered_map<art::ArtMethod*, art::ArtMethod*>* obsolete_maps_;
};

jvmtiError Redefiner::IsModifiableClass(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                        jclass klass,
                                        jboolean* is_redefinable) {
  // TODO Check for the appropriate feature flags once we have enabled them.
  art::Thread* self = art::Thread::Current();
  art::ScopedObjectAccess soa(self);
  art::StackHandleScope<1> hs(self);
  art::ObjPtr<art::mirror::Object> obj(self->DecodeJObject(klass));
  if (obj.IsNull()) {
    return ERR(INVALID_CLASS);
  }
  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(obj->AsClass()));
  std::string err_unused;
  *is_redefinable =
      Redefiner::GetClassRedefinitionError(h_klass, &err_unused) == OK ? JNI_TRUE : JNI_FALSE;
  return OK;
}

jvmtiError Redefiner::GetClassRedefinitionError(art::Handle<art::mirror::Class> klass,
                                                /*out*/std::string* error_msg) {
  if (klass->IsPrimitive()) {
    *error_msg = "Modification of primitive classes is not supported";
    return ERR(UNMODIFIABLE_CLASS);
  } else if (klass->IsInterface()) {
    *error_msg = "Modification of Interface classes is currently not supported";
    return ERR(UNMODIFIABLE_CLASS);
  } else if (klass->IsArrayClass()) {
    *error_msg = "Modification of Array classes is not supported";
    return ERR(UNMODIFIABLE_CLASS);
  } else if (klass->IsProxyClass()) {
    *error_msg = "Modification of proxy classes is not supported";
    return ERR(UNMODIFIABLE_CLASS);
  }

  // TODO We should check if the class has non-obsoletable methods on the stack
  LOG(WARNING) << "presence of non-obsoletable methods on stacks is not currently checked";
  return OK;
}

// Moves dex data to an anonymous, read-only mmap'd region.
std::unique_ptr<art::MemMap> Redefiner::MoveDataToMemMap(const std::string& original_location,
                                                         jint data_len,
                                                         const unsigned char* dex_data,
                                                         std::string* error_msg) {
  std::unique_ptr<art::MemMap> map(art::MemMap::MapAnonymous(
      StringPrintf("%s-transformed", original_location.c_str()).c_str(),
      nullptr,
      data_len,
      PROT_READ|PROT_WRITE,
      /*low_4gb*/false,
      /*reuse*/false,
      error_msg));
  if (map == nullptr) {
    return map;
  }
  memcpy(map->Begin(), dex_data, data_len);
  // Make the dex files mmap read only. This matches how other DexFiles are mmaped and prevents
  // programs from corrupting it.
  map->Protect(PROT_READ);
  return map;
}

Redefiner::ClassRedefinition::ClassRedefinition(
    Redefiner* driver,
    jclass klass,
    const art::DexFile* redefined_dex_file,
    const char* class_sig,
    art::ArraySlice<const unsigned char> orig_dex_file) :
      driver_(driver),
      klass_(klass),
      dex_file_(redefined_dex_file),
      class_sig_(class_sig),
      original_dex_file_(orig_dex_file) {
  GetMirrorClass()->MonitorEnter(driver_->self_);
}

Redefiner::ClassRedefinition::~ClassRedefinition() {
  if (driver_ != nullptr) {
    GetMirrorClass()->MonitorExit(driver_->self_);
  }
}

jvmtiError Redefiner::RedefineClasses(ArtJvmTiEnv* env,
                                      art::Runtime* runtime,
                                      art::Thread* self,
                                      jint class_count,
                                      const jvmtiClassDefinition* definitions,
                                      /*out*/std::string* error_msg) {
  if (env == nullptr) {
    *error_msg = "env was null!";
    return ERR(INVALID_ENVIRONMENT);
  } else if (class_count < 0) {
    *error_msg = "class_count was less then 0";
    return ERR(ILLEGAL_ARGUMENT);
  } else if (class_count == 0) {
    // We don't actually need to do anything. Just return OK.
    return OK;
  } else if (definitions == nullptr) {
    *error_msg = "null definitions!";
    return ERR(NULL_POINTER);
  }
  std::vector<ArtClassDefinition> def_vector;
  def_vector.reserve(class_count);
  for (jint i = 0; i < class_count; i++) {
    // We make a copy of the class_bytes to pass into the retransformation.
    // This makes cleanup easier (since we unambiguously own the bytes) and also is useful since we
    // will need to keep the original bytes around unaltered for subsequent RetransformClasses calls
    // to get the passed in bytes.
    // TODO Implement saving the original bytes.
    unsigned char* class_bytes_copy = nullptr;
    jvmtiError res = env->Allocate(definitions[i].class_byte_count, &class_bytes_copy);
    if (res != OK) {
      return res;
    }
    memcpy(class_bytes_copy, definitions[i].class_bytes, definitions[i].class_byte_count);

    ArtClassDefinition def;
    def.dex_len = definitions[i].class_byte_count;
    def.dex_data = MakeJvmtiUniquePtr(env, class_bytes_copy);
    // We are definitely modified.
    def.SetModified();
    def.original_dex_file = art::ArraySlice<const unsigned char>(definitions[i].class_bytes,
                                                                 definitions[i].class_byte_count);
    res = Transformer::FillInTransformationData(env, definitions[i].klass, &def);
    if (res != OK) {
      return res;
    }
    def_vector.push_back(std::move(def));
  }
  // Call all the transformation events.
  jvmtiError res = Transformer::RetransformClassesDirect(env,
                                                         self,
                                                         &def_vector);
  if (res != OK) {
    // Something went wrong with transformation!
    return res;
  }
  return RedefineClassesDirect(env, runtime, self, def_vector, error_msg);
}

jvmtiError Redefiner::RedefineClassesDirect(ArtJvmTiEnv* env,
                                            art::Runtime* runtime,
                                            art::Thread* self,
                                            const std::vector<ArtClassDefinition>& definitions,
                                            std::string* error_msg) {
  DCHECK(env != nullptr);
  if (definitions.size() == 0) {
    // We don't actually need to do anything. Just return OK.
    return OK;
  }
  // Stop JIT for the duration of this redefine since the JIT might concurrently compile a method we
  // are going to redefine.
  art::jit::ScopedJitSuspend suspend_jit;
  // Get shared mutator lock so we can lock all the classes.
  art::ScopedObjectAccess soa(self);
  Redefiner r(runtime, self, error_msg);
  for (const ArtClassDefinition& def : definitions) {
    // Only try to transform classes that have been modified.
    if (def.IsModified(self)) {
      jvmtiError res = r.AddRedefinition(env, def);
      if (res != OK) {
        return res;
      }
    }
  }
  return r.Run();
}

jvmtiError Redefiner::AddRedefinition(ArtJvmTiEnv* env, const ArtClassDefinition& def) {
  std::string original_dex_location;
  jvmtiError ret = OK;
  if ((ret = GetClassLocation(env, def.klass, &original_dex_location))) {
    *error_msg_ = "Unable to get original dex file location!";
    return ret;
  }
  char* generic_ptr_unused = nullptr;
  char* signature_ptr = nullptr;
  if ((ret = env->GetClassSignature(def.klass, &signature_ptr, &generic_ptr_unused)) != OK) {
    *error_msg_ = "Unable to get class signature!";
    return ret;
  }
  JvmtiUniquePtr generic_unique_ptr(MakeJvmtiUniquePtr(env, generic_ptr_unused));
  JvmtiUniquePtr signature_unique_ptr(MakeJvmtiUniquePtr(env, signature_ptr));
  std::unique_ptr<art::MemMap> map(MoveDataToMemMap(original_dex_location,
                                                    def.dex_len,
                                                    def.dex_data.get(),
                                                    error_msg_));
  std::ostringstream os;
  if (map.get() == nullptr) {
    os << "Failed to create anonymous mmap for modified dex file of class " << def.name
       << "in dex file " << original_dex_location << " because: " << *error_msg_;
    *error_msg_ = os.str();
    return ERR(OUT_OF_MEMORY);
  }
  if (map->Size() < sizeof(art::DexFile::Header)) {
    *error_msg_ = "Could not read dex file header because dex_data was too short";
    return ERR(INVALID_CLASS_FORMAT);
  }
  uint32_t checksum = reinterpret_cast<const art::DexFile::Header*>(map->Begin())->checksum_;
  std::unique_ptr<const art::DexFile> dex_file(art::DexFile::Open(map->GetName(),
                                                                  checksum,
                                                                  std::move(map),
                                                                  /*verify*/true,
                                                                  /*verify_checksum*/true,
                                                                  error_msg_));
  if (dex_file.get() == nullptr) {
    os << "Unable to load modified dex file for " << def.name << ": " << *error_msg_;
    *error_msg_ = os.str();
    return ERR(INVALID_CLASS_FORMAT);
  }
  redefinitions_.push_back(
      Redefiner::ClassRedefinition(this,
                                   def.klass,
                                   dex_file.release(),
                                   signature_ptr,
                                   def.original_dex_file));
  return OK;
}

art::mirror::Class* Redefiner::ClassRedefinition::GetMirrorClass() {
  return driver_->self_->DecodeJObject(klass_)->AsClass();
}

art::mirror::ClassLoader* Redefiner::ClassRedefinition::GetClassLoader() {
  return GetMirrorClass()->GetClassLoader();
}

art::mirror::DexCache* Redefiner::ClassRedefinition::CreateNewDexCache(
    art::Handle<art::mirror::ClassLoader> loader) {
  return driver_->runtime_->GetClassLinker()->RegisterDexFile(*dex_file_, loader.Get());
}

void Redefiner::RecordFailure(jvmtiError result,
                              const std::string& class_sig,
                              const std::string& error_msg) {
  *error_msg_ = StringPrintf("Unable to perform redefinition of '%s': %s",
                             class_sig.c_str(),
                             error_msg.c_str());
  result_ = result;
}

art::mirror::ByteArray* Redefiner::ClassRedefinition::AllocateOrGetOriginalDexFileBytes() {
  // If we have been specifically given a new set of bytes use that
  if (original_dex_file_.size() != 0) {
    return art::mirror::ByteArray::AllocateAndFill(
        driver_->self_,
        reinterpret_cast<const signed char*>(&original_dex_file_.At(0)),
        original_dex_file_.size());
  }

  // See if we already have one set.
  art::ObjPtr<art::mirror::ClassExt> ext(GetMirrorClass()->GetExtData());
  if (!ext.IsNull()) {
    art::ObjPtr<art::mirror::ByteArray> old_original_bytes(ext->GetOriginalDexFileBytes());
    if (!old_original_bytes.IsNull()) {
      // We do. Use it.
      return old_original_bytes.Ptr();
    }
  }

  // Copy the current dex_file
  const art::DexFile& current_dex_file = GetMirrorClass()->GetDexFile();
  // TODO Handle this or make it so it cannot happen.
  if (current_dex_file.NumClassDefs() != 1) {
    LOG(WARNING) << "Current dex file has more than one class in it. Calling RetransformClasses "
                 << "on this class might fail if no transformations are applied to it!";
  }
  return art::mirror::ByteArray::AllocateAndFill(
      driver_->self_,
      reinterpret_cast<const signed char*>(current_dex_file.Begin()),
      current_dex_file.Size());
}

struct CallbackCtx {
  art::LinearAlloc* allocator;
  std::unordered_map<art::ArtMethod*, art::ArtMethod*> obsolete_map;
  std::unordered_set<art::ArtMethod*> obsolete_methods;

  explicit CallbackCtx(art::LinearAlloc* alloc) : allocator(alloc) {}
};

void DoAllocateObsoleteMethodsCallback(art::Thread* t, void* vdata) NO_THREAD_SAFETY_ANALYSIS {
  CallbackCtx* data = reinterpret_cast<CallbackCtx*>(vdata);
  ObsoleteMethodStackVisitor::UpdateObsoleteFrames(t,
                                                   data->allocator,
                                                   data->obsolete_methods,
                                                   &data->obsolete_map);
}

// This creates any ArtMethod* structures needed for obsolete methods and ensures that the stack is
// updated so they will be run.
// TODO Rewrite so we can do this only once regardless of how many redefinitions there are.
void Redefiner::ClassRedefinition::FindAndAllocateObsoleteMethods(art::mirror::Class* art_klass) {
  art::ScopedAssertNoThreadSuspension ns("No thread suspension during thread stack walking");
  art::mirror::ClassExt* ext = art_klass->GetExtData();
  CHECK(ext->GetObsoleteMethods() != nullptr);
  art::ClassLinker* linker = driver_->runtime_->GetClassLinker();
  CallbackCtx ctx(linker->GetAllocatorForClassLoader(art_klass->GetClassLoader()));
  // Add all the declared methods to the map
  for (auto& m : art_klass->GetDeclaredMethods(art::kRuntimePointerSize)) {
    ctx.obsolete_methods.insert(&m);
    // TODO Allow this or check in IsModifiableClass.
    DCHECK(!m.IsIntrinsic());
  }
  {
    art::MutexLock mu(driver_->self_, *art::Locks::thread_list_lock_);
    art::ThreadList* list = art::Runtime::Current()->GetThreadList();
    list->ForEach(DoAllocateObsoleteMethodsCallback, static_cast<void*>(&ctx));
  }
  FillObsoleteMethodMap(art_klass, ctx.obsolete_map);
}

// Fills the obsolete method map in the art_klass's extData. This is so obsolete methods are able to
// figure out their DexCaches.
void Redefiner::ClassRedefinition::FillObsoleteMethodMap(
    art::mirror::Class* art_klass,
    const std::unordered_map<art::ArtMethod*, art::ArtMethod*>& obsoletes) {
  int32_t index = 0;
  art::mirror::ClassExt* ext_data = art_klass->GetExtData();
  art::mirror::PointerArray* obsolete_methods = ext_data->GetObsoleteMethods();
  art::mirror::ObjectArray<art::mirror::DexCache>* obsolete_dex_caches =
      ext_data->GetObsoleteDexCaches();
  int32_t num_method_slots = obsolete_methods->GetLength();
  // Find the first empty index.
  for (; index < num_method_slots; index++) {
    if (obsolete_methods->GetElementPtrSize<art::ArtMethod*>(
          index, art::kRuntimePointerSize) == nullptr) {
      break;
    }
  }
  // Make sure we have enough space.
  CHECK_GT(num_method_slots, static_cast<int32_t>(obsoletes.size() + index));
  CHECK(obsolete_dex_caches->Get(index) == nullptr);
  // Fill in the map.
  for (auto& obs : obsoletes) {
    obsolete_methods->SetElementPtrSize(index, obs.second, art::kRuntimePointerSize);
    obsolete_dex_caches->Set(index, art_klass->GetDexCache());
    index++;
  }
}

// Try and get the declared method. First try to get a virtual method then a direct method if that's
// not found.
static art::ArtMethod* FindMethod(art::Handle<art::mirror::Class> klass,
                                  const char* name,
                                  art::Signature sig) REQUIRES_SHARED(art::Locks::mutator_lock_) {
  art::ArtMethod* m = klass->FindDeclaredVirtualMethod(name, sig, art::kRuntimePointerSize);
  if (m == nullptr) {
    m = klass->FindDeclaredDirectMethod(name, sig, art::kRuntimePointerSize);
  }
  return m;
}

bool Redefiner::ClassRedefinition::CheckSameMethods() {
  art::StackHandleScope<1> hs(driver_->self_);
  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(GetMirrorClass()));
  DCHECK_EQ(dex_file_->NumClassDefs(), 1u);

  art::ClassDataItemIterator new_iter(*dex_file_,
                                      dex_file_->GetClassData(dex_file_->GetClassDef(0)));

  // Make sure we have the same number of methods.
  uint32_t num_new_method = new_iter.NumVirtualMethods() + new_iter.NumDirectMethods();
  uint32_t num_old_method = h_klass->GetDeclaredMethodsSlice(art::kRuntimePointerSize).size();
  if (num_new_method != num_old_method) {
    bool bigger = num_new_method > num_old_method;
    RecordFailure(bigger ? ERR(UNSUPPORTED_REDEFINITION_METHOD_ADDED)
                         : ERR(UNSUPPORTED_REDEFINITION_METHOD_DELETED),
                  StringPrintf("Total number of declared methods changed from %d to %d",
                               num_old_method, num_new_method));
    return false;
  }

  // Skip all of the fields. We should have already checked this.
  while (new_iter.HasNextStaticField() || new_iter.HasNextInstanceField()) {
    new_iter.Next();
  }
  // Check each of the methods. NB we don't need to specifically check for removals since the 2 dex
  // files have the same number of methods, which means there must be an equal amount of additions
  // and removals.
  for (; new_iter.HasNextVirtualMethod() || new_iter.HasNextDirectMethod(); new_iter.Next()) {
    // Get the data on the method we are searching for
    const art::DexFile::MethodId& new_method_id = dex_file_->GetMethodId(new_iter.GetMemberIndex());
    const char* new_method_name = dex_file_->GetMethodName(new_method_id);
    art::Signature new_method_signature = dex_file_->GetMethodSignature(new_method_id);
    art::ArtMethod* old_method = FindMethod(h_klass, new_method_name, new_method_signature);
    // If we got past the check for the same number of methods above that means there must be at
    // least one added and one removed method. We will return the ADDED failure message since it is
    // easier to get a useful error report for it.
    if (old_method == nullptr) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_METHOD_ADDED),
                    StringPrintf("Unknown method '%s' (sig: %s) was added!",
                                  new_method_name,
                                  new_method_signature.ToString().c_str()));
      return false;
    }
    // Since direct methods have different flags than virtual ones (specifically direct methods must
    // have kAccPrivate or kAccStatic or kAccConstructor flags) we can tell if a method changes from
    // virtual to direct.
    uint32_t new_flags = new_iter.GetMethodAccessFlags();
    if (new_flags != (old_method->GetAccessFlags() & art::kAccValidMethodFlags)) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED),
                    StringPrintf("method '%s' (sig: %s) had different access flags",
                                 new_method_name,
                                 new_method_signature.ToString().c_str()));
      return false;
    }
  }
  return true;
}

bool Redefiner::ClassRedefinition::CheckSameFields() {
  art::StackHandleScope<1> hs(driver_->self_);
  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(GetMirrorClass()));
  DCHECK_EQ(dex_file_->NumClassDefs(), 1u);
  art::ClassDataItemIterator new_iter(*dex_file_,
                                      dex_file_->GetClassData(dex_file_->GetClassDef(0)));
  const art::DexFile& old_dex_file = h_klass->GetDexFile();
  art::ClassDataItemIterator old_iter(old_dex_file,
                                      old_dex_file.GetClassData(*h_klass->GetClassDef()));
  // Instance and static fields can be differentiated by their flags so no need to check them
  // separately.
  while (new_iter.HasNextInstanceField() || new_iter.HasNextStaticField()) {
    // Get the data on the method we are searching for
    const art::DexFile::FieldId& new_field_id = dex_file_->GetFieldId(new_iter.GetMemberIndex());
    const char* new_field_name = dex_file_->GetFieldName(new_field_id);
    const char* new_field_type = dex_file_->GetFieldTypeDescriptor(new_field_id);

    if (!(old_iter.HasNextInstanceField() || old_iter.HasNextStaticField())) {
      // We are missing the old version of this method!
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED),
                    StringPrintf("Unknown field '%s' (type: %s) added!",
                                  new_field_name,
                                  new_field_type));
      return false;
    }

    const art::DexFile::FieldId& old_field_id = old_dex_file.GetFieldId(old_iter.GetMemberIndex());
    const char* old_field_name = old_dex_file.GetFieldName(old_field_id);
    const char* old_field_type = old_dex_file.GetFieldTypeDescriptor(old_field_id);

    // Check name and type.
    if (strcmp(old_field_name, new_field_name) != 0 ||
        strcmp(old_field_type, new_field_type) != 0) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED),
                    StringPrintf("Field changed from '%s' (sig: %s) to '%s' (sig: %s)!",
                                  old_field_name,
                                  old_field_type,
                                  new_field_name,
                                  new_field_type));
      return false;
    }

    // Since static fields have different flags than instance ones (specifically static fields must
    // have the kAccStatic flag) we can tell if a field changes from static to instance.
    if (new_iter.GetFieldAccessFlags() != old_iter.GetFieldAccessFlags()) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED),
                    StringPrintf("Field '%s' (sig: %s) had different access flags",
                                  new_field_name,
                                  new_field_type));
      return false;
    }

    new_iter.Next();
    old_iter.Next();
  }
  if (old_iter.HasNextInstanceField() || old_iter.HasNextStaticField()) {
    RecordFailure(ERR(UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED),
                  StringPrintf("field '%s' (sig: %s) is missing!",
                                old_dex_file.GetFieldName(old_dex_file.GetFieldId(
                                    old_iter.GetMemberIndex())),
                                old_dex_file.GetFieldTypeDescriptor(old_dex_file.GetFieldId(
                                    old_iter.GetMemberIndex()))));
    return false;
  }
  return true;
}

bool Redefiner::ClassRedefinition::CheckClass() {
  // TODO Might just want to put it in a ObjPtr and NoSuspend assert.
  art::StackHandleScope<1> hs(driver_->self_);
  // Easy check that only 1 class def is present.
  if (dex_file_->NumClassDefs() != 1) {
    RecordFailure(ERR(ILLEGAL_ARGUMENT),
                  StringPrintf("Expected 1 class def in dex file but found %d",
                               dex_file_->NumClassDefs()));
    return false;
  }
  // Get the ClassDef from the new DexFile.
  // Since the dex file has only a single class def the index is always 0.
  const art::DexFile::ClassDef& def = dex_file_->GetClassDef(0);
  // Get the class as it is now.
  art::Handle<art::mirror::Class> current_class(hs.NewHandle(GetMirrorClass()));

  // Check the access flags didn't change.
  if (def.GetJavaAccessFlags() != (current_class->GetAccessFlags() & art::kAccValidClassFlags)) {
    RecordFailure(ERR(UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED),
                  "Cannot change modifiers of class by redefinition");
    return false;
  }

  // Check class name.
  // These should have been checked by the dexfile verifier on load.
  DCHECK_NE(def.class_idx_, art::dex::TypeIndex::Invalid()) << "Invalid type index";
  const char* descriptor = dex_file_->StringByTypeIdx(def.class_idx_);
  DCHECK(descriptor != nullptr) << "Invalid dex file structure!";
  if (!current_class->DescriptorEquals(descriptor)) {
    std::string storage;
    RecordFailure(ERR(NAMES_DONT_MATCH),
                  StringPrintf("expected file to contain class called '%s' but found '%s'!",
                               current_class->GetDescriptor(&storage),
                               descriptor));
    return false;
  }
  if (current_class->IsObjectClass()) {
    if (def.superclass_idx_ != art::dex::TypeIndex::Invalid()) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED), "Superclass added!");
      return false;
    }
  } else {
    const char* super_descriptor = dex_file_->StringByTypeIdx(def.superclass_idx_);
    DCHECK(descriptor != nullptr) << "Invalid dex file structure!";
    if (!current_class->GetSuperClass()->DescriptorEquals(super_descriptor)) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED), "Superclass changed");
      return false;
    }
  }
  const art::DexFile::TypeList* interfaces = dex_file_->GetInterfacesList(def);
  if (interfaces == nullptr) {
    if (current_class->NumDirectInterfaces() != 0) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED), "Interfaces added");
      return false;
    }
  } else {
    DCHECK(!current_class->IsProxyClass());
    const art::DexFile::TypeList* current_interfaces = current_class->GetInterfaceTypeList();
    if (current_interfaces == nullptr || current_interfaces->Size() != interfaces->Size()) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED), "Interfaces added or removed");
      return false;
    }
    // The order of interfaces is (barely) meaningful so we error if it changes.
    const art::DexFile& orig_dex_file = current_class->GetDexFile();
    for (uint32_t i = 0; i < interfaces->Size(); i++) {
      if (strcmp(
            dex_file_->StringByTypeIdx(interfaces->GetTypeItem(i).type_idx_),
            orig_dex_file.StringByTypeIdx(current_interfaces->GetTypeItem(i).type_idx_)) != 0) {
        RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED),
                      "Interfaces changed or re-ordered");
        return false;
      }
    }
  }
  LOG(WARNING) << "No verification is done on annotations of redefined classes.";

  return true;
}

// TODO Move this to use IsRedefinable when that function is made.
bool Redefiner::ClassRedefinition::CheckRedefinable() {
  std::string err;
  art::StackHandleScope<1> hs(driver_->self_);

  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(GetMirrorClass()));
  jvmtiError res = Redefiner::GetClassRedefinitionError(h_klass, &err);
  if (res != OK) {
    RecordFailure(res, err);
    return false;
  } else {
    return true;
  }
}

bool Redefiner::ClassRedefinition::CheckRedefinitionIsValid() {
  return CheckRedefinable() &&
      CheckClass() &&
      CheckSameFields() &&
      CheckSameMethods();
}

// A wrapper that lets us hold onto the arbitrary sized data needed for redefinitions in a
// reasonably sane way. This adds no fields to the normal ObjectArray. By doing this we can avoid
// having to deal with the fact that we need to hold an arbitrary number of references live.
class RedefinitionDataHolder {
 public:
  enum DataSlot : int32_t {
    kSlotSourceClassLoader = 0,
    kSlotJavaDexFile = 1,
    kSlotNewDexFileCookie = 2,
    kSlotNewDexCache = 3,
    kSlotMirrorClass = 4,
    kSlotOrigDexFile = 5,

    // Must be last one.
    kNumSlots = 6,
  };

  // This needs to have a HandleScope passed in that is capable of creating a new Handle without
  // overflowing. Only one handle will be created. This object has a lifetime identical to that of
  // the passed in handle-scope.
  RedefinitionDataHolder(art::StackHandleScope<1>* hs,
                         art::Runtime* runtime,
                         art::Thread* self,
                         int32_t num_redefinitions) REQUIRES_SHARED(art::Locks::mutator_lock_) :
    arr_(
      hs->NewHandle(
        art::mirror::ObjectArray<art::mirror::Object>::Alloc(
            self,
            runtime->GetClassLinker()->GetClassRoot(art::ClassLinker::kObjectArrayClass),
            num_redefinitions * kNumSlots))) {}

  bool IsNull() const REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return arr_.IsNull();
  }

  // TODO Maybe make an iterable view type to simplify using this.
  art::mirror::ClassLoader* GetSourceClassLoader(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::down_cast<art::mirror::ClassLoader*>(GetSlot(klass_index, kSlotSourceClassLoader));
  }
  art::mirror::Object* GetJavaDexFile(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return GetSlot(klass_index, kSlotJavaDexFile);
  }
  art::mirror::LongArray* GetNewDexFileCookie(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::down_cast<art::mirror::LongArray*>(GetSlot(klass_index, kSlotNewDexFileCookie));
  }
  art::mirror::DexCache* GetNewDexCache(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::down_cast<art::mirror::DexCache*>(GetSlot(klass_index, kSlotNewDexCache));
  }
  art::mirror::Class* GetMirrorClass(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::down_cast<art::mirror::Class*>(GetSlot(klass_index, kSlotMirrorClass));
  }

  art::mirror::ByteArray* GetOriginalDexFileBytes(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::down_cast<art::mirror::ByteArray*>(GetSlot(klass_index, kSlotOrigDexFile));
  }

  void SetSourceClassLoader(jint klass_index, art::mirror::ClassLoader* loader)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotSourceClassLoader, loader);
  }
  void SetJavaDexFile(jint klass_index, art::mirror::Object* dexfile)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotJavaDexFile, dexfile);
  }
  void SetNewDexFileCookie(jint klass_index, art::mirror::LongArray* cookie)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotNewDexFileCookie, cookie);
  }
  void SetNewDexCache(jint klass_index, art::mirror::DexCache* cache)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotNewDexCache, cache);
  }
  void SetMirrorClass(jint klass_index, art::mirror::Class* klass)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotMirrorClass, klass);
  }
  void SetOriginalDexFileBytes(jint klass_index, art::mirror::ByteArray* bytes)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotOrigDexFile, bytes);
  }

  int32_t Length() const REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return arr_->GetLength() / kNumSlots;
  }

 private:
  mutable art::Handle<art::mirror::ObjectArray<art::mirror::Object>> arr_;

  art::mirror::Object* GetSlot(jint klass_index,
                               DataSlot slot) const REQUIRES_SHARED(art::Locks::mutator_lock_) {
    DCHECK_LT(klass_index, Length());
    return arr_->Get((kNumSlots * klass_index) + slot);
  }

  void SetSlot(jint klass_index,
               DataSlot slot,
               art::ObjPtr<art::mirror::Object> obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    DCHECK(!art::Runtime::Current()->IsActiveTransaction());
    DCHECK_LT(klass_index, Length());
    arr_->Set<false>((kNumSlots * klass_index) + slot, obj);
  }

  DISALLOW_COPY_AND_ASSIGN(RedefinitionDataHolder);
};

// TODO Stash and update soft failure state
bool Redefiner::ClassRedefinition::CheckVerification(int32_t klass_index,
                                                     const RedefinitionDataHolder& holder) {
  DCHECK_EQ(dex_file_->NumClassDefs(), 1u);
  art::StackHandleScope<2> hs(driver_->self_);
  std::string error;
  // TODO Make verification log level lower
  art::verifier::MethodVerifier::FailureKind failure =
      art::verifier::MethodVerifier::VerifyClass(driver_->self_,
                                                 dex_file_.get(),
                                                 hs.NewHandle(holder.GetNewDexCache(klass_index)),
                                                 hs.NewHandle(GetClassLoader()),
                                                 dex_file_->GetClassDef(0), /*class_def*/
                                                 nullptr, /*compiler_callbacks*/
                                                 false, /*allow_soft_failures*/
                                                 /*log_level*/
                                                 art::verifier::HardFailLogMode::kLogWarning,
                                                 &error);
  bool passes = failure == art::verifier::MethodVerifier::kNoFailure;
  if (!passes) {
    RecordFailure(ERR(FAILS_VERIFICATION), "Failed to verify class. Error was: " + error);
  }
  return passes;
}

// Looks through the previously allocated cookies to see if we need to update them with another new
// dexfile. This is so that even if multiple classes with the same classloader are redefined at
// once they are all added to the classloader.
bool Redefiner::ClassRedefinition::AllocateAndRememberNewDexFileCookie(
    int32_t klass_index,
    art::Handle<art::mirror::ClassLoader> source_class_loader,
    art::Handle<art::mirror::Object> dex_file_obj,
    /*out*/RedefinitionDataHolder* holder) {
  art::StackHandleScope<2> hs(driver_->self_);
  art::MutableHandle<art::mirror::LongArray> old_cookie(
      hs.NewHandle<art::mirror::LongArray>(nullptr));
  bool has_older_cookie = false;
  // See if we already have a cookie that a previous redefinition got from the same classloader.
  for (int32_t i = 0; i < klass_index; i++) {
    if (holder->GetSourceClassLoader(i) == source_class_loader.Get()) {
      // Since every instance of this classloader should have the same cookie associated with it we
      // can stop looking here.
      has_older_cookie = true;
      old_cookie.Assign(holder->GetNewDexFileCookie(i));
      break;
    }
  }
  if (old_cookie.IsNull()) {
    // No older cookie. Get it directly from the dex_file_obj
    // We should not have seen this classloader elsewhere.
    CHECK(!has_older_cookie);
    old_cookie.Assign(ClassLoaderHelper::GetDexFileCookie(dex_file_obj));
  }
  // Use the old cookie to generate the new one with the new DexFile* added in.
  art::Handle<art::mirror::LongArray>
      new_cookie(hs.NewHandle(ClassLoaderHelper::AllocateNewDexFileCookie(driver_->self_,
                                                                          old_cookie,
                                                                          dex_file_.get())));
  // Make sure the allocation worked.
  if (new_cookie.IsNull()) {
    return false;
  }

  // Save the cookie.
  holder->SetNewDexFileCookie(klass_index, new_cookie.Get());
  // If there are other copies of this same classloader we need to make sure that we all have the
  // same cookie.
  if (has_older_cookie) {
    for (int32_t i = 0; i < klass_index; i++) {
      // We will let the GC take care of the cookie we allocated for this one.
      if (holder->GetSourceClassLoader(i) == source_class_loader.Get()) {
        holder->SetNewDexFileCookie(i, new_cookie.Get());
      }
    }
  }

  return true;
}

bool Redefiner::ClassRedefinition::FinishRemainingAllocations(
    int32_t klass_index, /*out*/RedefinitionDataHolder* holder) {
  art::ScopedObjectAccessUnchecked soa(driver_->self_);
  art::StackHandleScope<2> hs(driver_->self_);
  holder->SetMirrorClass(klass_index, GetMirrorClass());
  // This shouldn't allocate
  art::Handle<art::mirror::ClassLoader> loader(hs.NewHandle(GetClassLoader()));
  // The bootclasspath is handled specially so it doesn't have a j.l.DexFile.
  if (!art::ClassLinker::IsBootClassLoader(soa, loader.Get())) {
    holder->SetSourceClassLoader(klass_index, loader.Get());
    art::Handle<art::mirror::Object> dex_file_obj(hs.NewHandle(
        ClassLoaderHelper::FindSourceDexFileObject(driver_->self_, loader)));
    holder->SetJavaDexFile(klass_index, dex_file_obj.Get());
    if (dex_file_obj.Get() == nullptr) {
      // TODO Better error msg.
      RecordFailure(ERR(INTERNAL), "Unable to find dex file!");
      return false;
    }
    // Allocate the new dex file cookie.
    if (!AllocateAndRememberNewDexFileCookie(klass_index, loader, dex_file_obj, holder)) {
      driver_->self_->AssertPendingOOMException();
      driver_->self_->ClearException();
      RecordFailure(ERR(OUT_OF_MEMORY), "Unable to allocate dex file array for class loader");
      return false;
    }
  }
  holder->SetNewDexCache(klass_index, CreateNewDexCache(loader));
  if (holder->GetNewDexCache(klass_index) == nullptr) {
    driver_->self_->AssertPendingOOMException();
    driver_->self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Unable to allocate DexCache");
    return false;
  }

  // We won't always need to set this field.
  holder->SetOriginalDexFileBytes(klass_index, AllocateOrGetOriginalDexFileBytes());
  if (holder->GetOriginalDexFileBytes(klass_index) == nullptr) {
    driver_->self_->AssertPendingOOMException();
    driver_->self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Unable to allocate array for original dex file");
    return false;
  }
  return true;
}

bool Redefiner::CheckAllRedefinitionAreValid() {
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    if (!redef.CheckRedefinitionIsValid()) {
      return false;
    }
  }
  return true;
}

bool Redefiner::EnsureAllClassAllocationsFinished() {
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    if (!redef.EnsureClassAllocationsFinished()) {
      return false;
    }
  }
  return true;
}

bool Redefiner::FinishAllRemainingAllocations(RedefinitionDataHolder& holder) {
  int32_t cnt = 0;
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    // Allocate the data this redefinition requires.
    if (!redef.FinishRemainingAllocations(cnt, &holder)) {
      return false;
    }
    cnt++;
  }
  return true;
}

void Redefiner::ClassRedefinition::ReleaseDexFile() {
  dex_file_.release();
}

void Redefiner::ReleaseAllDexFiles() {
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    redef.ReleaseDexFile();
  }
}

bool Redefiner::CheckAllClassesAreVerified(const RedefinitionDataHolder& holder) {
  int32_t cnt = 0;
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    if (!redef.CheckVerification(cnt, holder)) {
      return false;
    }
    cnt++;
  }
  return true;
}

jvmtiError Redefiner::Run() {
  art::StackHandleScope<1> hs(self_);
  // Allocate an array to hold onto all java temporary objects associated with this redefinition.
  // We will let this be collected after the end of this function.
  RedefinitionDataHolder holder(&hs, runtime_, self_, redefinitions_.size());
  if (holder.IsNull()) {
    self_->AssertPendingOOMException();
    self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Could not allocate storage for temporaries");
    return result_;
  }

  // First we just allocate the ClassExt and its fields that we need. These can be updated
  // atomically without any issues (since we allocate the map arrays as empty) so we don't bother
  // doing a try loop. The other allocations we need to ensure that nothing has changed in the time
  // between allocating them and pausing all threads before we can update them so we need to do a
  // try loop.
  if (!CheckAllRedefinitionAreValid() ||
      !EnsureAllClassAllocationsFinished() ||
      !FinishAllRemainingAllocations(holder) ||
      !CheckAllClassesAreVerified(holder)) {
    // TODO Null out the ClassExt fields we allocated (if possible, might be racing with another
    // redefineclass call which made it even bigger. Leak shouldn't be huge (2x array of size
    // declared_methods_.length) but would be good to get rid of. All other allocations should be
    // cleaned up by the GC eventually.
    return result_;
  }
  int32_t counter = 0;
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    if (holder.GetSourceClassLoader(counter) == nullptr) {
      runtime_->GetClassLinker()->AppendToBootClassPath(self_, redef.GetDexFile());
    }
    counter++;
  }
  // Disable GC and wait for it to be done if we are a moving GC.  This is fine since we are done
  // allocating so no deadlocks.
  art::gc::Heap* heap = runtime_->GetHeap();
  if (heap->IsGcConcurrentAndMoving()) {
    // GC moving objects can cause deadlocks as we are deoptimizing the stack.
    heap->IncrementDisableMovingGC(self_);
  }
  // Do transition to final suspension
  // TODO We might want to give this its own suspended state!
  // TODO This isn't right. We need to change state without any chance of suspend ideally!
  self_->TransitionFromRunnableToSuspended(art::ThreadState::kNative);
  runtime_->GetThreadList()->SuspendAll(
      "Final installation of redefined Classes!", /*long_suspend*/true);
  // TODO We need to invalidate all breakpoints in the redefined class with the debugger.
  // TODO We need to deal with any instrumentation/debugger deoptimized_methods_.
  // TODO We need to update all debugger MethodIDs so they note the method they point to is
  // obsolete or implement some other well defined semantics.
  // TODO We need to decide on & implement semantics for JNI jmethodids when we redefine methods.
  counter = 0;
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    art::ScopedAssertNoThreadSuspension nts("Updating runtime objects for redefinition");
    if (holder.GetSourceClassLoader(counter) != nullptr) {
      ClassLoaderHelper::UpdateJavaDexFile(holder.GetJavaDexFile(counter),
                                           holder.GetNewDexFileCookie(counter));
    }
    art::mirror::Class* klass = holder.GetMirrorClass(counter);
    // TODO Rewrite so we don't do a stack walk for each and every class.
    redef.FindAndAllocateObsoleteMethods(klass);
    redef.UpdateClass(klass, holder.GetNewDexCache(counter),
                      holder.GetOriginalDexFileBytes(counter));
    counter++;
  }
  // TODO Verify the new Class.
  // TODO Shrink the obsolete method maps if possible?
  // TODO find appropriate class loader.
  // TODO Put this into a scoped thing.
  runtime_->GetThreadList()->ResumeAll();
  // Get back shared mutator lock as expected for return.
  self_->TransitionFromSuspendedToRunnable();
  // TODO Do the dex_file release at a more reasonable place. This works but it muddles who really
  // owns the DexFile and when ownership is transferred.
  ReleaseAllDexFiles();
  if (heap->IsGcConcurrentAndMoving()) {
    heap->DecrementDisableMovingGC(self_);
  }
  return OK;
}

void Redefiner::ClassRedefinition::UpdateMethods(art::ObjPtr<art::mirror::Class> mclass,
                                                 art::ObjPtr<art::mirror::DexCache> new_dex_cache,
                                                 const art::DexFile::ClassDef& class_def) {
  art::ClassLinker* linker = driver_->runtime_->GetClassLinker();
  art::PointerSize image_pointer_size = linker->GetImagePointerSize();
  const art::DexFile::TypeId& declaring_class_id = dex_file_->GetTypeId(class_def.class_idx_);
  const art::DexFile& old_dex_file = mclass->GetDexFile();
  // Update methods.
  for (art::ArtMethod& method : mclass->GetMethods(image_pointer_size)) {
    const art::DexFile::StringId* new_name_id = dex_file_->FindStringId(method.GetName());
    art::dex::TypeIndex method_return_idx =
        dex_file_->GetIndexForTypeId(*dex_file_->FindTypeId(method.GetReturnTypeDescriptor()));
    const auto* old_type_list = method.GetParameterTypeList();
    std::vector<art::dex::TypeIndex> new_type_list;
    for (uint32_t i = 0; old_type_list != nullptr && i < old_type_list->Size(); i++) {
      new_type_list.push_back(
          dex_file_->GetIndexForTypeId(
              *dex_file_->FindTypeId(
                  old_dex_file.GetTypeDescriptor(
                      old_dex_file.GetTypeId(
                          old_type_list->GetTypeItem(i).type_idx_)))));
    }
    const art::DexFile::ProtoId* proto_id = dex_file_->FindProtoId(method_return_idx,
                                                                   new_type_list);
    // TODO Return false, cleanup.
    CHECK(proto_id != nullptr || old_type_list == nullptr);
    const art::DexFile::MethodId* method_id = dex_file_->FindMethodId(declaring_class_id,
                                                                      *new_name_id,
                                                                      *proto_id);
    // TODO Return false, cleanup.
    CHECK(method_id != nullptr);
    uint32_t dex_method_idx = dex_file_->GetIndexForMethodId(*method_id);
    method.SetDexMethodIndex(dex_method_idx);
    linker->SetEntryPointsToInterpreter(&method);
    method.SetCodeItemOffset(dex_file_->FindCodeItemOffset(class_def, dex_method_idx));
    method.SetDexCacheResolvedMethods(new_dex_cache->GetResolvedMethods(), image_pointer_size);
    // Notify the jit that this method is redefined.
    art::jit::Jit* jit = driver_->runtime_->GetJit();
    if (jit != nullptr) {
      jit->GetCodeCache()->NotifyMethodRedefined(&method);
    }
  }
}

void Redefiner::ClassRedefinition::UpdateFields(art::ObjPtr<art::mirror::Class> mclass) {
  // TODO The IFields & SFields pointers should be combined like the methods_ arrays were.
  for (auto fields_iter : {mclass->GetIFields(), mclass->GetSFields()}) {
    for (art::ArtField& field : fields_iter) {
      std::string declaring_class_name;
      const art::DexFile::TypeId* new_declaring_id =
          dex_file_->FindTypeId(field.GetDeclaringClass()->GetDescriptor(&declaring_class_name));
      const art::DexFile::StringId* new_name_id = dex_file_->FindStringId(field.GetName());
      const art::DexFile::TypeId* new_type_id = dex_file_->FindTypeId(field.GetTypeDescriptor());
      // TODO Handle error, cleanup.
      CHECK(new_name_id != nullptr && new_type_id != nullptr && new_declaring_id != nullptr);
      const art::DexFile::FieldId* new_field_id =
          dex_file_->FindFieldId(*new_declaring_id, *new_name_id, *new_type_id);
      CHECK(new_field_id != nullptr);
      // We only need to update the index since the other data in the ArtField cannot be updated.
      field.SetDexFieldIndex(dex_file_->GetIndexForFieldId(*new_field_id));
    }
  }
}

// Performs updates to class that will allow us to verify it.
void Redefiner::ClassRedefinition::UpdateClass(
    art::ObjPtr<art::mirror::Class> mclass,
    art::ObjPtr<art::mirror::DexCache> new_dex_cache,
    art::ObjPtr<art::mirror::ByteArray> original_dex_file) {
  DCHECK_EQ(dex_file_->NumClassDefs(), 1u);
  const art::DexFile::ClassDef& class_def = dex_file_->GetClassDef(0);
  UpdateMethods(mclass, new_dex_cache, class_def);
  UpdateFields(mclass);

  // Update the class fields.
  // Need to update class last since the ArtMethod gets its DexFile from the class (which is needed
  // to call GetReturnTypeDescriptor and GetParameterTypeList above).
  mclass->SetDexCache(new_dex_cache.Ptr());
  mclass->SetDexClassDefIndex(dex_file_->GetIndexForClassDef(class_def));
  mclass->SetDexTypeIndex(dex_file_->GetIndexForTypeId(*dex_file_->FindTypeId(class_sig_.c_str())));
  art::ObjPtr<art::mirror::ClassExt> ext(mclass->GetExtData());
  CHECK(!ext.IsNull());
  ext->SetOriginalDexFileBytes(original_dex_file);
}

// This function does all (java) allocations we need to do for the Class being redefined.
// TODO Change this name maybe?
bool Redefiner::ClassRedefinition::EnsureClassAllocationsFinished() {
  art::StackHandleScope<2> hs(driver_->self_);
  art::Handle<art::mirror::Class> klass(hs.NewHandle(
      driver_->self_->DecodeJObject(klass_)->AsClass()));
  if (klass.Get() == nullptr) {
    RecordFailure(ERR(INVALID_CLASS), "Unable to decode class argument!");
    return false;
  }
  // Allocate the classExt
  art::Handle<art::mirror::ClassExt> ext(hs.NewHandle(klass->EnsureExtDataPresent(driver_->self_)));
  if (ext.Get() == nullptr) {
    // No memory. Clear exception (it's not useful) and return error.
    // TODO This doesn't need to be fatal. We could just not support obsolete methods after hitting
    // this case.
    driver_->self_->AssertPendingOOMException();
    driver_->self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Could not allocate ClassExt");
    return false;
  }
  // Allocate the 2 arrays that make up the obsolete methods map.  Since the contents of the arrays
  // are only modified when all threads (other than the modifying one) are suspended we don't need
  // to worry about missing the unsyncronized writes to the array. We do synchronize when setting it
  // however, since that can happen at any time.
  // TODO Clear these after we walk the stacks in order to free them in the (likely?) event there
  // are no obsolete methods.
  {
    art::ObjectLock<art::mirror::ClassExt> lock(driver_->self_, ext);
    if (!ext->ExtendObsoleteArrays(
          driver_->self_, klass->GetDeclaredMethodsSlice(art::kRuntimePointerSize).size())) {
      // OOM. Clear exception and return error.
      driver_->self_->AssertPendingOOMException();
      driver_->self_->ClearException();
      RecordFailure(ERR(OUT_OF_MEMORY), "Unable to allocate/extend obsolete methods map");
      return false;
    }
  }
  return true;
}

}  // namespace openjdkjvmti
