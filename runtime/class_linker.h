/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_RUNTIME_CLASS_LINKER_H_
#define ART_RUNTIME_CLASS_LINKER_H_

#include <string>
#include <utility>
#include <vector>

#include "base/allocator.h"
#include "base/hash_set.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "class_table.h"
#include "dex_file.h"
#include "gc_root.h"
#include "jni.h"
#include "oat_file.h"
#include "object_callbacks.h"

namespace art {

namespace gc {
namespace space {
  class ImageSpace;
}  // namespace space
}  // namespace gc
namespace mirror {
  class ClassLoader;
  class DexCache;
  class DexCachePointerArray;
  class DexCacheTest_Open_Test;
  class IfTable;
  template<class T> class ObjectArray;
  class StackTraceElement;
}  // namespace mirror

template<class T> class Handle;
template<class T> class MutableHandle;
class InternTable;
template<class T> class ObjectLock;
class Runtime;
class ScopedObjectAccessAlreadyRunnable;
template<size_t kNumReferences> class PACKED(4) StackHandleScope;

enum VisitRootFlags : uint8_t;

class ClassLoaderVisitor {
 public:
  virtual ~ClassLoaderVisitor() {}
  virtual void Visit(mirror::ClassLoader* class_loader)
      SHARED_REQUIRES(Locks::classlinker_classes_lock_, Locks::mutator_lock_) = 0;
};

class ClassLinker {
 public:
  // Well known mirror::Class roots accessed via GetClassRoot.
  enum ClassRoot {
    kJavaLangClass,
    kJavaLangObject,
    kClassArrayClass,
    kObjectArrayClass,
    kJavaLangString,
    kJavaLangDexCache,
    kJavaLangRefReference,
    kJavaLangReflectConstructor,
    kJavaLangReflectField,
    kJavaLangReflectMethod,
    kJavaLangReflectProxy,
    kJavaLangStringArrayClass,
    kJavaLangReflectConstructorArrayClass,
    kJavaLangReflectFieldArrayClass,
    kJavaLangReflectMethodArrayClass,
    kJavaLangClassLoader,
    kJavaLangThrowable,
    kJavaLangClassNotFoundException,
    kJavaLangStackTraceElement,
    kPrimitiveBoolean,
    kPrimitiveByte,
    kPrimitiveChar,
    kPrimitiveDouble,
    kPrimitiveFloat,
    kPrimitiveInt,
    kPrimitiveLong,
    kPrimitiveShort,
    kPrimitiveVoid,
    kBooleanArrayClass,
    kByteArrayClass,
    kCharArrayClass,
    kDoubleArrayClass,
    kFloatArrayClass,
    kIntArrayClass,
    kLongArrayClass,
    kShortArrayClass,
    kJavaLangStackTraceElementArrayClass,
    kClassRootsMax,
  };

  explicit ClassLinker(InternTable* intern_table);
  ~ClassLinker();

  // Initialize class linker by bootstraping from dex files.
  void InitWithoutImage(std::vector<std::unique_ptr<const DexFile>> boot_class_path)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);

  // Initialize class linker from one or more images.
  void InitFromImage() SHARED_REQUIRES(Locks::mutator_lock_) REQUIRES(!dex_lock_);

  // Finds a class by its descriptor, loading it if necessary.
  // If class_loader is null, searches boot_class_path_.
  mirror::Class* FindClass(Thread* self,
                           const char* descriptor,
                           Handle<mirror::ClassLoader> class_loader)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);

  // Finds a class in the path class loader, loading it if necessary without using JNI. Hash
  // function is supposed to be ComputeModifiedUtf8Hash(descriptor). Returns true if the
  // class-loader chain could be handled, false otherwise, i.e., a non-supported class-loader
  // was encountered while walking the parent chain (currently only BootClassLoader and
  // PathClassLoader are supported).
  bool FindClassInPathClassLoader(ScopedObjectAccessAlreadyRunnable& soa,
                                  Thread* self,
                                  const char* descriptor,
                                  size_t hash,
                                  Handle<mirror::ClassLoader> class_loader,
                                  mirror::Class** result)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);

  // Finds a class by its descriptor using the "system" class loader, ie by searching the
  // boot_class_path_.
  mirror::Class* FindSystemClass(Thread* self, const char* descriptor)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);

  // Finds the array class given for the element class.
  mirror::Class* FindArrayClass(Thread* self, mirror::Class** element_class)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);

  // Returns true if the class linker is initialized.
  bool IsInitialized() const {
    return init_done_;
  }

  // Define a new a class based on a ClassDef from a DexFile
  mirror::Class* DefineClass(Thread* self,
                             const char* descriptor,
                             size_t hash,
                             Handle<mirror::ClassLoader> class_loader,
                             const DexFile& dex_file,
                             const DexFile::ClassDef& dex_class_def)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);

  // Finds a class by its descriptor, returning null if it isn't wasn't loaded
  // by the given 'class_loader'.
  mirror::Class* LookupClass(Thread* self,
                             const char* descriptor,
                             size_t hash,
                             mirror::ClassLoader* class_loader)
      REQUIRES(!Locks::classlinker_classes_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Finds all the classes with the given descriptor, regardless of ClassLoader.
  void LookupClasses(const char* descriptor, std::vector<mirror::Class*>& classes)
      REQUIRES(!Locks::classlinker_classes_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  mirror::Class* FindPrimitiveClass(char type) SHARED_REQUIRES(Locks::mutator_lock_);

  // General class unloading is not supported, this is used to prune
  // unwanted classes during image writing.
  bool RemoveClass(const char* descriptor, mirror::ClassLoader* class_loader)
      REQUIRES(!Locks::classlinker_classes_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  void DumpAllClasses(int flags)
      REQUIRES(!Locks::classlinker_classes_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  void DumpForSigQuit(std::ostream& os) REQUIRES(!Locks::classlinker_classes_lock_);

  size_t NumLoadedClasses()
      REQUIRES(!Locks::classlinker_classes_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Resolve a String with the given index from the DexFile, storing the
  // result in the DexCache. The referrer is used to identify the
  // target DexCache and ClassLoader to use for resolution.
  mirror::String* ResolveString(uint32_t string_idx, ArtMethod* referrer)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Resolve a String with the given index from the DexFile, storing the
  // result in the DexCache.
  mirror::String* ResolveString(const DexFile& dex_file, uint32_t string_idx,
                                Handle<mirror::DexCache> dex_cache)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Resolve a Type with the given index from the DexFile, storing the
  // result in the DexCache. The referrer is used to identity the
  // target DexCache and ClassLoader to use for resolution.
  mirror::Class* ResolveType(const DexFile& dex_file, uint16_t type_idx, mirror::Class* referrer)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);

  // Resolve a Type with the given index from the DexFile, storing the
  // result in the DexCache. The referrer is used to identify the
  // target DexCache and ClassLoader to use for resolution.
  mirror::Class* ResolveType(uint16_t type_idx, ArtMethod* referrer)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);

  mirror::Class* ResolveType(uint16_t type_idx, ArtField* referrer)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);

  // Resolve a type with the given ID from the DexFile, storing the
  // result in DexCache. The ClassLoader is used to search for the
  // type, since it may be referenced from but not contained within
  // the given DexFile.
  mirror::Class* ResolveType(const DexFile& dex_file,
                             uint16_t type_idx,
                             Handle<mirror::DexCache> dex_cache,
                             Handle<mirror::ClassLoader> class_loader)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);

  // Resolve a method with a given ID from the DexFile, storing the
  // result in DexCache. The ClassLinker and ClassLoader are used as
  // in ResolveType. What is unique is the method type argument which
  // is used to determine if this method is a direct, static, or
  // virtual method.
  ArtMethod* ResolveMethod(const DexFile& dex_file,
                           uint32_t method_idx,
                           Handle<mirror::DexCache> dex_cache,
                           Handle<mirror::ClassLoader> class_loader,
                           ArtMethod* referrer,
                           InvokeType type)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);

  ArtMethod* GetResolvedMethod(uint32_t method_idx, ArtMethod* referrer)
      SHARED_REQUIRES(Locks::mutator_lock_);
  ArtMethod* ResolveMethod(Thread* self, uint32_t method_idx, ArtMethod* referrer, InvokeType type)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);
  ArtMethod* ResolveMethodWithoutInvokeType(const DexFile& dex_file,
                                            uint32_t method_idx,
                                            Handle<mirror::DexCache> dex_cache,
                                            Handle<mirror::ClassLoader> class_loader)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);

  ArtField* GetResolvedField(uint32_t field_idx, mirror::Class* field_declaring_class)
      SHARED_REQUIRES(Locks::mutator_lock_);
  ArtField* GetResolvedField(uint32_t field_idx, mirror::DexCache* dex_cache)
      SHARED_REQUIRES(Locks::mutator_lock_);
  ArtField* ResolveField(uint32_t field_idx, ArtMethod* referrer, bool is_static)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);

  // Resolve a field with a given ID from the DexFile, storing the
  // result in DexCache. The ClassLinker and ClassLoader are used as
  // in ResolveType. What is unique is the is_static argument which is
  // used to determine if we are resolving a static or non-static
  // field.
  ArtField* ResolveField(const DexFile& dex_file, uint32_t field_idx,
                         Handle<mirror::DexCache> dex_cache,
                         Handle<mirror::ClassLoader> class_loader, bool is_static)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);

  // Resolve a field with a given ID from the DexFile, storing the
  // result in DexCache. The ClassLinker and ClassLoader are used as
  // in ResolveType. No is_static argument is provided so that Java
  // field resolution semantics are followed.
  ArtField* ResolveFieldJLS(const DexFile& dex_file,
                            uint32_t field_idx,
                            Handle<mirror::DexCache> dex_cache,
                            Handle<mirror::ClassLoader> class_loader)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);

  // Get shorty from method index without resolution. Used to do handlerization.
  const char* MethodShorty(uint32_t method_idx, ArtMethod* referrer, uint32_t* length)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Returns true on success, false if there's an exception pending.
  // can_run_clinit=false allows the compiler to attempt to init a class,
  // given the restriction that no <clinit> execution is possible.
  bool EnsureInitialized(Thread* self,
                         Handle<mirror::Class> c,
                         bool can_init_fields,
                         bool can_init_parents)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);

  // Initializes classes that have instances in the image but that have
  // <clinit> methods so they could not be initialized by the compiler.
  void RunRootClinits()
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);

  mirror::DexCache* RegisterDexFile(const DexFile& dex_file)
      REQUIRES(!dex_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);
  void RegisterDexFile(const DexFile& dex_file, Handle<mirror::DexCache> dex_cache)
      REQUIRES(!dex_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  const std::vector<const DexFile*>& GetBootClassPath() {
    return boot_class_path_;
  }

  void VisitClasses(ClassVisitor* visitor)
      REQUIRES(!Locks::classlinker_classes_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Less efficient variant of VisitClasses that copies the class_table_ into secondary storage
  // so that it can visit individual classes without holding the doesn't hold the
  // Locks::classlinker_classes_lock_. As the Locks::classlinker_classes_lock_ isn't held this code
  // can race with insertion and deletion of classes while the visitor is being called.
  void VisitClassesWithoutClassesLock(ClassVisitor* visitor)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);

  void VisitClassRoots(RootVisitor* visitor, VisitRootFlags flags)
      REQUIRES(!Locks::classlinker_classes_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);
  void VisitRoots(RootVisitor* visitor, VisitRootFlags flags)
      REQUIRES(!dex_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  mirror::DexCache* FindDexCache(Thread* self,
                                 const DexFile& dex_file,
                                 bool allow_failure = false)
      REQUIRES(!dex_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);
  void FixupDexCaches(ArtMethod* resolution_method)
      REQUIRES(!dex_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Allocate an instance of a java.lang.Object.
  mirror::Object* AllocObject(Thread* self)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);

  // TODO: replace this with multiple methods that allocate the correct managed type.
  template <class T>
  mirror::ObjectArray<T>* AllocObjectArray(Thread* self, size_t length)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);

  mirror::ObjectArray<mirror::Class>* AllocClassArray(Thread* self, size_t length)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);

  mirror::ObjectArray<mirror::String>* AllocStringArray(Thread* self, size_t length)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);

  LengthPrefixedArray<ArtField>* AllocArtFieldArray(Thread* self,
                                                    LinearAlloc* allocator,
                                                    size_t length);

  LengthPrefixedArray<ArtMethod>* AllocArtMethodArray(Thread* self,
                                                      LinearAlloc* allocator,
                                                      size_t length);

  mirror::PointerArray* AllocPointerArray(Thread* self, size_t length)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);

  mirror::IfTable* AllocIfTable(Thread* self, size_t ifcount)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);

  mirror::ObjectArray<mirror::StackTraceElement>* AllocStackTraceElementArray(Thread* self,
                                                                              size_t length)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);

  void VerifyClass(Thread* self, Handle<mirror::Class> klass)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);
  bool VerifyClassUsingOatFile(const DexFile& dex_file,
                               mirror::Class* klass,
                               mirror::Class::Status& oat_file_class_status)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);
  void ResolveClassExceptionHandlerTypes(const DexFile& dex_file,
                                         Handle<mirror::Class> klass)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);
  void ResolveMethodExceptionHandlerTypes(const DexFile& dex_file, ArtMethod* klass)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);

  mirror::Class* CreateProxyClass(ScopedObjectAccessAlreadyRunnable& soa,
                                  jstring name,
                                  jobjectArray interfaces,
                                  jobject loader,
                                  jobjectArray methods,
                                  jobjectArray throws)
      SHARED_REQUIRES(Locks::mutator_lock_);
  std::string GetDescriptorForProxy(mirror::Class* proxy_class)
      SHARED_REQUIRES(Locks::mutator_lock_);
  ArtMethod* FindMethodForProxy(mirror::Class* proxy_class, ArtMethod* proxy_method)
      REQUIRES(!dex_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Get the oat code for a method when its class isn't yet initialized
  const void* GetQuickOatCodeFor(ArtMethod* method)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Get the oat code for a method from a method index.
  const void* GetQuickOatCodeFor(const DexFile& dex_file,
                                 uint16_t class_def_idx,
                                 uint32_t method_idx)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Get compiled code for a method, return null if no code
  // exists. This is unlike Get..OatCodeFor which will return a bridge
  // or interpreter entrypoint.
  const void* GetOatMethodQuickCodeFor(ArtMethod* method)
      SHARED_REQUIRES(Locks::mutator_lock_);

  const OatFile::OatMethod FindOatMethodFor(ArtMethod* method, bool* found)
      SHARED_REQUIRES(Locks::mutator_lock_);

  pid_t GetClassesLockOwner();  // For SignalCatcher.
  pid_t GetDexLockOwner();  // For SignalCatcher.

  mirror::Class* GetClassRoot(ClassRoot class_root) SHARED_REQUIRES(Locks::mutator_lock_);

  static const char* GetClassRootDescriptor(ClassRoot class_root);

  // Is the given entry point quick code to run the resolution stub?
  bool IsQuickResolutionStub(const void* entry_point) const;

  // Is the given entry point quick code to bridge into the interpreter?
  bool IsQuickToInterpreterBridge(const void* entry_point) const;

  // Is the given entry point quick code to run the generic JNI stub?
  bool IsQuickGenericJniStub(const void* entry_point) const;

  InternTable* GetInternTable() const {
    return intern_table_;
  }

  // Set the entrypoints up for method to the given code.
  void SetEntryPointsToCompiledCode(ArtMethod* method, const void* method_code) const
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Set the entrypoints up for method to the enter the interpreter.
  void SetEntryPointsToInterpreter(ArtMethod* method) const
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Attempts to insert a class into a class table.  Returns null if
  // the class was inserted, otherwise returns an existing class with
  // the same descriptor and ClassLoader.
  mirror::Class* InsertClass(const char* descriptor, mirror::Class* klass, size_t hash)
      REQUIRES(!Locks::classlinker_classes_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  mirror::ObjectArray<mirror::Class>* GetClassRoots() SHARED_REQUIRES(Locks::mutator_lock_) {
    mirror::ObjectArray<mirror::Class>* class_roots = class_roots_.Read();
    DCHECK(class_roots != nullptr);
    return class_roots;
  }

  // Move all of the image classes into the class table for faster lookups.
  void MoveImageClassesToClassTable()
      REQUIRES(!Locks::classlinker_classes_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);
  // Move the class table to the pre-zygote table to reduce memory usage. This works by ensuring
  // that no more classes are ever added to the pre zygote table which makes it that the pages
  // always remain shared dirty instead of private dirty.
  void MoveClassTableToPreZygote()
      REQUIRES(!Locks::classlinker_classes_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Returns true if the method can be called with its direct code pointer, false otherwise.
  bool MayBeCalledWithDirectCodePointer(ArtMethod* m)
      SHARED_REQUIRES(Locks::mutator_lock_) REQUIRES(!dex_lock_);

  // Creates a GlobalRef PathClassLoader that can be used to load classes from the given dex files.
  // Note: the objects are not completely set up. Do not use this outside of tests and the compiler.
  jobject CreatePathClassLoader(Thread* self, std::vector<const DexFile*>& dex_files)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);

  size_t GetImagePointerSize() const {
    DCHECK(ValidPointerSize(image_pointer_size_)) << image_pointer_size_;
    return image_pointer_size_;
  }

  // Used by image writer for checking.
  bool ClassInClassTable(mirror::Class* klass)
      REQUIRES(Locks::classlinker_classes_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  ArtMethod* CreateRuntimeMethod();

  // Clear the ArrayClass cache. This is necessary when cleaning up for the image, as the cache
  // entries are roots, but potentially not image classes.
  void DropFindArrayClassCache() SHARED_REQUIRES(Locks::mutator_lock_);

  // Clean up class loaders, this needs to happen after JNI weak globals are cleared.
  void CleanupClassLoaders()
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Locks::classlinker_classes_lock_);

  static LinearAlloc* GetAllocatorForClassLoader(mirror::ClassLoader* class_loader)
      SHARED_REQUIRES(Locks::mutator_lock_);

 private:
  struct ClassLoaderData {
    jweak weak_root;  // Weak root to enable class unloading.
    ClassTable* class_table;
    LinearAlloc* allocator;
  };

  void VisitClassLoaders(ClassLoaderVisitor* visitor) const
      SHARED_REQUIRES(Locks::classlinker_classes_lock_, Locks::mutator_lock_);

  void VisitClassesInternal(ClassVisitor* visitor)
      SHARED_REQUIRES(Locks::classlinker_classes_lock_, Locks::mutator_lock_);

  // Returns the number of zygote and image classes.
  size_t NumZygoteClasses() const
      REQUIRES(Locks::classlinker_classes_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Returns the number of non zygote nor image classes.
  size_t NumNonZygoteClasses() const
      REQUIRES(Locks::classlinker_classes_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  void FinishInit(Thread* self)
  SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);

  // For early bootstrapping by Init
  mirror::Class* AllocClass(Thread* self, mirror::Class* java_lang_Class, uint32_t class_size)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);

  // Alloc* convenience functions to avoid needing to pass in mirror::Class*
  // values that are known to the ClassLinker such as
  // kObjectArrayClass and kJavaLangString etc.
  mirror::Class* AllocClass(Thread* self, uint32_t class_size)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);
  mirror::DexCache* AllocDexCache(Thread* self, const DexFile& dex_file)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);

  mirror::Class* CreatePrimitiveClass(Thread* self, Primitive::Type type)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);
  mirror::Class* InitializePrimitiveClass(mirror::Class* primitive_class, Primitive::Type type)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);

  mirror::Class* CreateArrayClass(Thread* self,
                                  const char* descriptor,
                                  size_t hash,
                                  Handle<mirror::ClassLoader> class_loader)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_, !Roles::uninterruptible_);

  void AppendToBootClassPath(Thread* self, const DexFile& dex_file)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);
  void AppendToBootClassPath(const DexFile& dex_file, Handle<mirror::DexCache> dex_cache)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);

  // Precomputes size needed for Class, in the case of a non-temporary class this size must be
  // sufficient to hold all static fields.
  uint32_t SizeOfClassWithoutEmbeddedTables(const DexFile& dex_file,
                                            const DexFile::ClassDef& dex_class_def);

  // Setup the classloader, class def index, type idx so that we can insert this class in the class
  // table.
  void SetupClass(const DexFile& dex_file,
                  const DexFile::ClassDef& dex_class_def,
                  Handle<mirror::Class> klass,
                  mirror::ClassLoader* class_loader)
      SHARED_REQUIRES(Locks::mutator_lock_);

  void LoadClass(Thread* self,
                 const DexFile& dex_file,
                 const DexFile::ClassDef& dex_class_def,
                 Handle<mirror::Class> klass)
      SHARED_REQUIRES(Locks::mutator_lock_);
  void LoadClassMembers(Thread* self,
                        const DexFile& dex_file,
                        const uint8_t* class_data,
                        Handle<mirror::Class> klass,
                        const OatFile::OatClass* oat_class)
      SHARED_REQUIRES(Locks::mutator_lock_);

  void LoadField(const ClassDataItemIterator& it, Handle<mirror::Class> klass, ArtField* dst)
      SHARED_REQUIRES(Locks::mutator_lock_);

  void LoadMethod(Thread* self,
                  const DexFile& dex_file,
                  const ClassDataItemIterator& it,
                  Handle<mirror::Class> klass, ArtMethod* dst)
      SHARED_REQUIRES(Locks::mutator_lock_);

  void FixupStaticTrampolines(mirror::Class* klass) SHARED_REQUIRES(Locks::mutator_lock_);

  // Finds the associated oat class for a dex_file and descriptor. Returns an invalid OatClass on
  // error and sets found to false.
  OatFile::OatClass FindOatClass(const DexFile& dex_file, uint16_t class_def_idx, bool* found)
      SHARED_REQUIRES(Locks::mutator_lock_);

  void RegisterDexFileLocked(const DexFile& dex_file, Handle<mirror::DexCache> dex_cache)
      REQUIRES(dex_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);
  mirror::DexCache* FindDexCacheLocked(Thread* self, const DexFile& dex_file, bool allow_failure)
      REQUIRES(dex_lock_)
      SHARED_REQUIRES(Locks::mutator_lock_);

  bool InitializeClass(Thread* self,
                       Handle<mirror::Class> klass,
                       bool can_run_clinit,
                       bool can_init_parents)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);
  bool WaitForInitializeClass(Handle<mirror::Class> klass,
                              Thread* self,
                              ObjectLock<mirror::Class>& lock);
  bool ValidateSuperClassDescriptors(Handle<mirror::Class> klass)
      SHARED_REQUIRES(Locks::mutator_lock_);

  bool IsSameDescriptorInDifferentClassContexts(Thread* self,
                                                const char* descriptor,
                                                Handle<mirror::ClassLoader> class_loader1,
                                                Handle<mirror::ClassLoader> class_loader2)
      SHARED_REQUIRES(Locks::mutator_lock_);

  bool IsSameMethodSignatureInDifferentClassContexts(Thread* self,
                                                     ArtMethod* method,
                                                     mirror::Class* klass1,
                                                     mirror::Class* klass2)
      SHARED_REQUIRES(Locks::mutator_lock_);

  bool LinkClass(Thread* self,
                 const char* descriptor,
                 Handle<mirror::Class> klass,
                 Handle<mirror::ObjectArray<mirror::Class>> interfaces,
                 MutableHandle<mirror::Class>* h_new_class_out)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Locks::classlinker_classes_lock_);

  bool LinkSuperClass(Handle<mirror::Class> klass)
      SHARED_REQUIRES(Locks::mutator_lock_);

  bool LoadSuperAndInterfaces(Handle<mirror::Class> klass, const DexFile& dex_file)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);

  bool LinkMethods(Thread* self,
                   Handle<mirror::Class> klass,
                   Handle<mirror::ObjectArray<mirror::Class>> interfaces,
                   ArtMethod** out_imt)
      SHARED_REQUIRES(Locks::mutator_lock_);

  bool LinkVirtualMethods(Thread* self, Handle<mirror::Class> klass)
      SHARED_REQUIRES(Locks::mutator_lock_);

  bool LinkInterfaceMethods(Thread* self,
                            Handle<mirror::Class> klass,
                            Handle<mirror::ObjectArray<mirror::Class>> interfaces,
                            ArtMethod** out_imt)
      SHARED_REQUIRES(Locks::mutator_lock_);

  bool LinkStaticFields(Thread* self, Handle<mirror::Class> klass, size_t* class_size)
      SHARED_REQUIRES(Locks::mutator_lock_);
  bool LinkInstanceFields(Thread* self, Handle<mirror::Class> klass)
      SHARED_REQUIRES(Locks::mutator_lock_);
  bool LinkFields(Thread* self, Handle<mirror::Class> klass, bool is_static, size_t* class_size)
      SHARED_REQUIRES(Locks::mutator_lock_);
  void LinkCode(ArtMethod* method,
                const OatFile::OatClass* oat_class,
                uint32_t class_def_method_index)
      SHARED_REQUIRES(Locks::mutator_lock_);
  void CreateReferenceInstanceOffsets(Handle<mirror::Class> klass)
      SHARED_REQUIRES(Locks::mutator_lock_);

  void CheckProxyConstructor(ArtMethod* constructor) const
      SHARED_REQUIRES(Locks::mutator_lock_);
  void CheckProxyMethod(ArtMethod* method, ArtMethod* prototype) const
      SHARED_REQUIRES(Locks::mutator_lock_);

  // For use by ImageWriter to find DexCaches for its roots
  ReaderWriterMutex* DexLock()
      SHARED_REQUIRES(Locks::mutator_lock_)
      LOCK_RETURNED(dex_lock_) {
    return &dex_lock_;
  }
  size_t GetDexCacheCount() SHARED_REQUIRES(Locks::mutator_lock_, dex_lock_) {
    return dex_caches_.size();
  }
  const std::list<jweak>& GetDexCaches() SHARED_REQUIRES(Locks::mutator_lock_, dex_lock_) {
    return dex_caches_;
  }

  void CreateProxyConstructor(Handle<mirror::Class> klass, ArtMethod* out)
      SHARED_REQUIRES(Locks::mutator_lock_);
  void CreateProxyMethod(Handle<mirror::Class> klass, ArtMethod* prototype, ArtMethod* out)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Ensures that methods have the kAccPreverified bit set. We use the kAccPreverfied bit on the
  // class access flags to determine whether this has been done before.
  void EnsurePreverifiedMethods(Handle<mirror::Class> c)
      SHARED_REQUIRES(Locks::mutator_lock_);

  mirror::Class* LookupClassFromImage(const char* descriptor)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Returns null if not found.
  ClassTable* ClassTableForClassLoader(mirror::ClassLoader* class_loader)
      SHARED_REQUIRES(Locks::mutator_lock_, Locks::classlinker_classes_lock_);
  // Insert a new class table if not found.
  ClassTable* InsertClassTableForClassLoader(mirror::ClassLoader* class_loader)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(Locks::classlinker_classes_lock_);

  // EnsureResolved is called to make sure that a class in the class_table_ has been resolved
  // before returning it to the caller. Its the responsibility of the thread that placed the class
  // in the table to make it resolved. The thread doing resolution must notify on the class' lock
  // when resolution has occurred. This happens in mirror::Class::SetStatus. As resolution may
  // retire a class, the version of the class in the table is returned and this may differ from
  // the class passed in.
  mirror::Class* EnsureResolved(Thread* self, const char* descriptor, mirror::Class* klass)
      WARN_UNUSED
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);

  void FixupTemporaryDeclaringClass(mirror::Class* temp_class, mirror::Class* new_class)
      SHARED_REQUIRES(Locks::mutator_lock_);

  void SetClassRoot(ClassRoot class_root, mirror::Class* klass)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Return the quick generic JNI stub for testing.
  const void* GetRuntimeQuickGenericJniStub() const;

  // Throw the class initialization failure recorded when first trying to initialize the given
  // class.
  // Note: Currently we only store the descriptor, so we cannot throw the exact throwable, only
  //       a recreation with a custom string.
  void ThrowEarlierClassFailure(mirror::Class* c)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!dex_lock_);

  bool HasInitWithString(Thread* self, const char* descriptor)
      SHARED_REQUIRES(Locks::mutator_lock_) REQUIRES(!dex_lock_);

  bool CanWeInitializeClass(mirror::Class* klass, bool can_init_statics, bool can_init_parents)
      SHARED_REQUIRES(Locks::mutator_lock_);

  void UpdateClassVirtualMethods(mirror::Class* klass,
                                 LengthPrefixedArray<ArtMethod>* new_methods)
      SHARED_REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Locks::classlinker_classes_lock_);

  std::vector<const DexFile*> boot_class_path_;
  std::vector<std::unique_ptr<const DexFile>> opened_dex_files_;

  mutable ReaderWriterMutex dex_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  // JNI weak globals to allow dex caches to get unloaded. We lazily delete weak globals when we
  // register new dex files.
  std::list<jweak> dex_caches_ GUARDED_BY(dex_lock_);

  // This contains the class loaders which have class tables. It is populated by
  // InsertClassTableForClassLoader.
  std::list<ClassLoaderData> class_loaders_
      GUARDED_BY(Locks::classlinker_classes_lock_);

  // Boot class path table. Since the class loader for this is null.
  ClassTable boot_class_table_ GUARDED_BY(Locks::classlinker_classes_lock_);

  // New class roots, only used by CMS since the GC needs to mark these in the pause.
  std::vector<GcRoot<mirror::Class>> new_class_roots_ GUARDED_BY(Locks::classlinker_classes_lock_);

  // Do we need to search dex caches to find image classes?
  bool dex_cache_image_class_lookup_required_;
  // Number of times we've searched dex caches for a class. After a certain number of misses we move
  // the classes into the class_table_ to avoid dex cache based searches.
  Atomic<uint32_t> failed_dex_cache_class_lookups_;

  // Well known mirror::Class roots.
  GcRoot<mirror::ObjectArray<mirror::Class>> class_roots_;

  // The interface table used by all arrays.
  GcRoot<mirror::IfTable> array_iftable_;

  // A cache of the last FindArrayClass results. The cache serves to avoid creating array class
  // descriptors for the sake of performing FindClass.
  static constexpr size_t kFindArrayCacheSize = 16;
  GcRoot<mirror::Class> find_array_class_cache_[kFindArrayCacheSize];
  size_t find_array_class_cache_next_victim_;

  bool init_done_;
  bool log_new_class_table_roots_ GUARDED_BY(Locks::classlinker_classes_lock_);

  InternTable* intern_table_;

  // Trampolines within the image the bounce to runtime entrypoints. Done so that there is a single
  // patch point within the image. TODO: make these proper relocations.
  const void* quick_resolution_trampoline_;
  const void* quick_imt_conflict_trampoline_;
  const void* quick_generic_jni_trampoline_;
  const void* quick_to_interpreter_bridge_trampoline_;

  // Image pointer size.
  size_t image_pointer_size_;

  friend class ImageDumper;  // for DexLock
  friend class ImageWriter;  // for GetClassRoots
  friend class JniCompilerTest;  // for GetRuntimeQuickGenericJniStub
  friend class JniInternalTest;  // for GetRuntimeQuickGenericJniStub
  ART_FRIEND_TEST(mirror::DexCacheTest, Open);  // for AllocDexCache

  DISALLOW_COPY_AND_ASSIGN(ClassLinker);
};

}  // namespace art

#endif  // ART_RUNTIME_CLASS_LINKER_H_
