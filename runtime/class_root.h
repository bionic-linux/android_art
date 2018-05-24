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

#ifndef ART_RUNTIME_CLASS_ROOT_H_
#define ART_RUNTIME_CLASS_ROOT_H_

#include "class_linker.h"
#include "mirror/class.h"
#include "mirror/object_array-inl.h"
#include "obj_ptr-inl.h"
#include "runtime.h"

namespace art {

namespace mirror {
class ArrayElementVarHandle;
class ByteArrayViewVarHandle;
class ByteBufferViewVarHandle;
class CallSite;
class ClassExt;
class ClassLoader;
class Constructor;
class DexCache;
class EmulatedStackFrame;
class Field;
class FieldVarHandle;
class Method;
class MethodHandleImpl;
class MethodHandlesLookup;
class MethodType;
class Object;
class Proxy;
template<typename T> class PrimitiveArray;
class Reference;
class StackTraceElement;
class String;
class Throwable;
class VarHandle;
}  // namespace mirror

#define CLASS_ROOT_LIST(M)                                                                \
  M(kJavaLangClass,                         "Ljava/lang/Class;")                          \
  M(kJavaLangObject,                        "Ljava/lang/Object;")                         \
  M(kClassArrayClass,                       "[Ljava/lang/Class;")                         \
  M(kObjectArrayClass,                      "[Ljava/lang/Object;")                        \
  M(kJavaLangString,                        "Ljava/lang/String;")                         \
  M(kJavaLangDexCache,                      "Ljava/lang/DexCache;")                       \
  M(kJavaLangRefReference,                  "Ljava/lang/ref/Reference;")                  \
  M(kJavaLangReflectConstructor,            "Ljava/lang/reflect/Constructor;")            \
  M(kJavaLangReflectField,                  "Ljava/lang/reflect/Field;")                  \
  M(kJavaLangReflectMethod,                 "Ljava/lang/reflect/Method;")                 \
  M(kJavaLangReflectProxy,                  "Ljava/lang/reflect/Proxy;")                  \
  M(kJavaLangStringArrayClass,              "[Ljava/lang/String;")                        \
  M(kJavaLangReflectConstructorArrayClass,  "[Ljava/lang/reflect/Constructor;")           \
  M(kJavaLangReflectFieldArrayClass,        "[Ljava/lang/reflect/Field;")                 \
  M(kJavaLangReflectMethodArrayClass,       "[Ljava/lang/reflect/Method;")                \
  M(kJavaLangInvokeCallSite,                "Ljava/lang/invoke/CallSite;")                \
  M(kJavaLangInvokeMethodHandle,            "Ljava/lang/invoke/MethodHandle;")            \
  M(kJavaLangInvokeMethodHandleImpl,        "Ljava/lang/invoke/MethodHandleImpl;")        \
  M(kJavaLangInvokeMethodHandlesLookup,     "Ljava/lang/invoke/MethodHandles$Lookup;")    \
  M(kJavaLangInvokeMethodType,              "Ljava/lang/invoke/MethodType;")              \
  M(kJavaLangInvokeVarHandle,               "Ljava/lang/invoke/VarHandle;")               \
  M(kJavaLangInvokeFieldVarHandle,          "Ljava/lang/invoke/FieldVarHandle;")          \
  M(kJavaLangInvokeArrayElementVarHandle,   "Ljava/lang/invoke/ArrayElementVarHandle;")   \
  M(kJavaLangInvokeByteArrayViewVarHandle,  "Ljava/lang/invoke/ByteArrayViewVarHandle;")  \
  M(kJavaLangInvokeByteBufferViewVarHandle, "Ljava/lang/invoke/ByteBufferViewVarHandle;") \
  M(kJavaLangClassLoader,                   "Ljava/lang/ClassLoader;")                    \
  M(kJavaLangThrowable,                     "Ljava/lang/Throwable;")                      \
  M(kJavaLangClassNotFoundException,        "Ljava/lang/ClassNotFoundException;")         \
  M(kJavaLangStackTraceElement,             "Ljava/lang/StackTraceElement;")              \
  M(kDalvikSystemEmulatedStackFrame,        "Ldalvik/system/EmulatedStackFrame;")         \
  M(kPrimitiveBoolean,                      "Z")                                          \
  M(kPrimitiveByte,                         "B")                                          \
  M(kPrimitiveChar,                         "C")                                          \
  M(kPrimitiveDouble,                       "D")                                          \
  M(kPrimitiveFloat,                        "F")                                          \
  M(kPrimitiveInt,                          "I")                                          \
  M(kPrimitiveLong,                         "J")                                          \
  M(kPrimitiveShort,                        "S")                                          \
  M(kPrimitiveVoid,                         "V")                                          \
  M(kBooleanArrayClass,                     "[Z")                                         \
  M(kByteArrayClass,                        "[B")                                         \
  M(kCharArrayClass,                        "[C")                                         \
  M(kDoubleArrayClass,                      "[D")                                         \
  M(kFloatArrayClass,                       "[F")                                         \
  M(kIntArrayClass,                         "[I")                                         \
  M(kLongArrayClass,                        "[J")                                         \
  M(kShortArrayClass,                       "[S")                                         \
  M(kJavaLangStackTraceElementArrayClass,   "[Ljava/lang/StackTraceElement;")             \
  M(kDalvikSystemClassExt,                  "Ldalvik/system/ClassExt;")

// Well known mirror::Class roots accessed via ClassLinker::GetClassRoots().
enum class ClassRoot : uint32_t {
#define CLASS_ROOT_ENUMERATOR(name, descriptor) name,
  CLASS_ROOT_LIST(CLASS_ROOT_ENUMERATOR)
#undef CLASS_ROOT_ENUMERATOR
  kMax,
};

const char* GetClassRootDescriptor(ClassRoot class_root);

template <ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
inline ObjPtr<mirror::Class> GetClassRoot(
    ClassRoot class_root,
    ObjPtr<mirror::ObjectArray<mirror::Class>> class_roots) REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(class_roots != nullptr);
  if (kReadBarrierOption == kWithReadBarrier) {
    // With read barrier all references must point to the to-space.
    // Without read barrier, this check could fail.
    DCHECK_EQ(class_roots, Runtime::Current()->GetClassLinker()->GetClassRoots());
  }
  DCHECK_LT(static_cast<uint32_t>(class_root), static_cast<uint32_t>(ClassRoot::kMax));
  int32_t index = static_cast<int32_t>(class_root);
  ObjPtr<mirror::Class> klass =
      class_roots->GetWithoutChecks<kDefaultVerifyFlags, kReadBarrierOption>(index);
  DCHECK(klass != nullptr);
  return klass.Ptr();
}

template <ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
inline ObjPtr<mirror::Class> GetClassRoot(ClassRoot class_root, ClassLinker* linker)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return GetClassRoot<kReadBarrierOption>(class_root, linker->GetClassRoots<kReadBarrierOption>());
}

template <ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
inline ObjPtr<mirror::Class> GetClassRoot(ClassRoot class_root)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return GetClassRoot<kReadBarrierOption>(class_root, Runtime::Current()->GetClassLinker());
}

namespace detail {

template <class MirrorType>
struct ClassRootSelector;  // No definition for unspecialized ClassRoot selector.

#define SPECIALIZE_CLASS_ROOT_SELECTOR(MirrorType, class_root)  \
  template <>                                                   \
  struct ClassRootSelector<MirrorType> {                        \
    static constexpr ClassRoot value = ClassRoot::class_root;   \
  }

SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::Class, kJavaLangClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::Object, kJavaLangObject);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::ObjectArray<mirror::Class>, kClassArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::ObjectArray<mirror::Object>, kObjectArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::String, kJavaLangString);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::DexCache, kJavaLangDexCache);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::Reference, kJavaLangRefReference);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::Constructor, kJavaLangReflectConstructor);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::Field, kJavaLangReflectField);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::Method, kJavaLangReflectMethod);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::Proxy, kJavaLangReflectProxy);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::ObjectArray<mirror::String>, kJavaLangStringArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::ObjectArray<mirror::Constructor>,
                               kJavaLangReflectConstructorArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::ObjectArray<mirror::Field>, kJavaLangReflectFieldArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::ObjectArray<mirror::Method>,
                               kJavaLangReflectMethodArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::CallSite, kJavaLangInvokeCallSite);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::MethodHandle, kJavaLangInvokeMethodHandle);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::MethodHandleImpl, kJavaLangInvokeMethodHandleImpl);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::MethodHandlesLookup, kJavaLangInvokeMethodHandlesLookup);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::MethodType, kJavaLangInvokeMethodType);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::VarHandle, kJavaLangInvokeVarHandle);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::FieldVarHandle, kJavaLangInvokeFieldVarHandle);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::ArrayElementVarHandle, kJavaLangInvokeArrayElementVarHandle);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::ByteArrayViewVarHandle,
                               kJavaLangInvokeByteArrayViewVarHandle);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::ByteBufferViewVarHandle,
                               kJavaLangInvokeByteBufferViewVarHandle);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::ClassLoader, kJavaLangClassLoader);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::Throwable, kJavaLangThrowable);
// No mirror class for kJavaLangClassNotFoundException.
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::StackTraceElement, kJavaLangStackTraceElement);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::EmulatedStackFrame, kDalvikSystemEmulatedStackFrame);
// No mirror class for kPrimitiveBoolean,
// No mirror class for kPrimitiveByte,
// No mirror class for kPrimitiveChar,
// No mirror class for kPrimitiveDouble,
// No mirror class for kPrimitiveFloat,
// No mirror class for kPrimitiveInt,
// No mirror class for kPrimitiveLong,
// No mirror class for kPrimitiveShort,
// No mirror class for kPrimitiveVoid,
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::PrimitiveArray<uint8_t>, kBooleanArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::PrimitiveArray<int8_t>, kByteArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::PrimitiveArray<uint16_t>, kCharArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::PrimitiveArray<double>, kDoubleArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::PrimitiveArray<float>, kFloatArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::PrimitiveArray<int32_t>, kIntArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::PrimitiveArray<int64_t>, kLongArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::PrimitiveArray<int16_t>, kShortArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::ObjectArray<mirror::StackTraceElement>,
                               kJavaLangStackTraceElementArrayClass);
SPECIALIZE_CLASS_ROOT_SELECTOR(mirror::ClassExt, kDalvikSystemClassExt);

#undef SPECIALIZE_CLASS_ROOT_SELECTOR

}  // namespace detail

template <class MirrorType, ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
inline ObjPtr<mirror::Class> GetClassRoot(ObjPtr<mirror::ObjectArray<mirror::Class>> class_roots)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return GetClassRoot<kWithReadBarrier>(detail::ClassRootSelector<MirrorType>::value, class_roots);
}

template <class MirrorType, ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
inline ObjPtr<mirror::Class> GetClassRoot(ClassLinker* linker)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return GetClassRoot<kWithReadBarrier>(detail::ClassRootSelector<MirrorType>::value, linker);
}

template <class MirrorType, ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
inline ObjPtr<mirror::Class> GetClassRoot() REQUIRES_SHARED(Locks::mutator_lock_) {
  return GetClassRoot<kWithReadBarrier>(detail::ClassRootSelector<MirrorType>::value);
}

}  // namespace art

#endif  // ART_RUNTIME_CLASS_ROOT_H_
