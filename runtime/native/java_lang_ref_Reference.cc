/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "java_lang_ref_Reference.h"

#include "nativehelper/jni_macros.h"

#include "gc/heap.h"
#include "gc/reference_processor.h"
#include "jni/jni_internal.h"
#include "mirror/object-inl.h"
#include "mirror/reference-inl.h"
#include "native_util.h"
#include "scoped_fast_native_object_access-inl.h"

namespace art {

static jobject Reference_getReferent(JNIEnv* env, jobject javaThis) {
  ScopedFastNativeObjectAccess soa(env);
  const ObjPtr<mirror::Reference> ref = soa.Decode<mirror::Reference>(javaThis);
  const ObjPtr<mirror::Object> referent =
      Runtime::Current()->GetHeap()->GetReferenceProcessor()->GetReferent(soa.Self(), ref);
  return soa.AddLocalReference<jobject>(referent);
}

static jboolean Reference_refersTo0(JNIEnv* env, jobject javaThis, jobject o) {
  ScopedFastNativeObjectAccess soa(env);
  const ObjPtr<mirror::Reference> ref = soa.Decode<mirror::Reference>(javaThis);
  const ObjPtr<mirror::Object> other = soa.Decode<mirror::Reference>(o);
  const ObjPtr<mirror::Object> referent = ref->template GetReferent<kWithReadBarrier>();
  return (jboolean)(referent == other);
  // It's very tempting to try to avoid copying the referent to to-space in the read barrier.
  // As it stands, if we call x.refersTo(y), where x's referent is dead, and hence not y,
  // we end up preserving x's referent for no useful reason. Unfortunately, it's hard to avoid
  // this, since Copy()'s memory ordering guarantee is too weak to ensure that when x's referent
  // is y, and thus has been marked, we can actually see the forwarding pointer. We could still
  // see a from-space address and a null forwarding pointer. We deem adding the fence to Copy()
  // after the forwarding address CAS is too expensive.
}

static void Reference_clearReferent(JNIEnv* env, jobject javaThis) {
  ScopedFastNativeObjectAccess soa(env);
  const ObjPtr<mirror::Reference> ref = soa.Decode<mirror::Reference>(javaThis);
  Runtime::Current()->GetHeap()->GetReferenceProcessor()->ClearReferent(ref);
}

static JNINativeMethod gMethods[] = {
  FAST_NATIVE_METHOD(Reference, getReferent, "()Ljava/lang/Object;"),
  FAST_NATIVE_METHOD(Reference, clearReferent, "()V"),
  FAST_NATIVE_METHOD(Reference, refersTo0, "(Ljava/lang/Object;)Z"),
};

void register_java_lang_ref_Reference(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("java/lang/ref/Reference");
}

}  // namespace art
