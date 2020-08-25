/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include "java_lang_StringFactory.h"

#include "common_throws.h"
#include "handle_scope-inl.h"
#include "jni/jni_internal.h"
#include "mirror/object-inl.h"
#include "mirror/string-alloc-inl.h"
#include "native_util.h"
#include "nativehelper/jni_macros.h"
#include "nativehelper/scoped_local_ref.h"
#include "nativehelper/scoped_primitive_array.h"
#include "scoped_fast_native_object_access-inl.h"
#include "scoped_thread_state_change-inl.h"

namespace art {

static jstring StringFactory_newStringFromBytes(JNIEnv* env, jclass, jbyteArray java_data,
                                                jint high, jint offset, jint byte_count) {
  ScopedFastNativeObjectAccess soa(env);
  if (UNLIKELY(java_data == nullptr)) {
    ThrowNullPointerException("data == null");
    return nullptr;
  }
  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::ByteArray> byte_array(hs.NewHandle(soa.Decode<mirror::ByteArray>(java_data)));
  int32_t data_size = byte_array->GetLength();
  if ((offset | byte_count) < 0 || byte_count > data_size - offset) {
    soa.Self()->ThrowNewExceptionF("Ljava/lang/StringIndexOutOfBoundsException;",
                                   "length=%d; regionStart=%d; regionLength=%d", data_size,
                                   offset, byte_count);
    return nullptr;
  }
  gc::AllocatorType allocator_type = Runtime::Current()->GetHeap()->GetCurrentAllocator();
  ObjPtr<mirror::String> result = mirror::String::AllocFromByteArray(soa.Self(),
                                                                     byte_count,
                                                                     byte_array,
                                                                     offset,
                                                                     high,
                                                                     allocator_type);
  return soa.AddLocalReference<jstring>(result);
}

// The char array passed as `java_data` must not be a null reference.
static jstring StringFactory_newStringFromChars(JNIEnv* env, jclass, jint offset,
                                                jint char_count, jcharArray java_data) {
  DCHECK(java_data != nullptr);
  ScopedFastNativeObjectAccess soa(env);
  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::CharArray> char_array(hs.NewHandle(soa.Decode<mirror::CharArray>(java_data)));
  gc::AllocatorType allocator_type = Runtime::Current()->GetHeap()->GetCurrentAllocator();
  ObjPtr<mirror::String> result = mirror::String::AllocFromCharArray(soa.Self(),
                                                                     char_count,
                                                                     char_array,
                                                                     offset,
                                                                     allocator_type);
  return soa.AddLocalReference<jstring>(result);
}

static jstring StringFactory_newStringFromString(JNIEnv* env, jclass, jstring to_copy) {
  ScopedFastNativeObjectAccess soa(env);
  if (UNLIKELY(to_copy == nullptr)) {
    ThrowNullPointerException("toCopy == null");
    return nullptr;
  }
  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::String> string(hs.NewHandle(soa.Decode<mirror::String>(to_copy)));
  gc::AllocatorType allocator_type = Runtime::Current()->GetHeap()->GetCurrentAllocator();
  ObjPtr<mirror::String> result = mirror::String::AllocFromString(soa.Self(),
                                                                  string->GetLength(),
                                                                  string,
                                                                  /*offset=*/ 0,
                                                                  allocator_type);
  return soa.AddLocalReference<jstring>(result);
}

static jstring StringFactory_newStringFromUtf8Bytes(JNIEnv* env, jclass, jbyteArray java_data,
                                                    jint offset, jint byte_count) {
  // Local Define in here
  static const jchar REPLACEMENT_CHAR = 0xfffd;
  static const int DEFAULT_BUFFER_SIZE = 256;
  static const int TABLE_UTF8_NEEDED[] = {
    //      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0xc0 - 0xcf
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0xd0 - 0xdf
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // 0xe0 - 0xef
    3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0xf0 - 0xff
  };

  ScopedFastNativeObjectAccess soa(env);
  if (UNLIKELY(java_data == nullptr)) {
    ThrowNullPointerException("data == null");
    return nullptr;
  }

  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::ByteArray> byte_array(hs.NewHandle(soa.Decode<mirror::ByteArray>(java_data)));
  int32_t data_size = byte_array->GetLength();
  if ((offset | byte_count) < 0 || byte_count > data_size - offset) {
    soa.Self()->ThrowNewExceptionF("Ljava/lang/StringIndexOutOfBoundsException;",
        "length=%d; regionStart=%d; regionLength=%d", data_size,
        offset, byte_count);
    return nullptr;
  }

  jbyte *j_raw_array = byte_array->GetData();
  if (j_raw_array != nullptr) {
    // Initial value
    jchar temp_buffer[DEFAULT_BUFFER_SIZE];
    jbyte *d = j_raw_array;
    jchar *v;
    bool v_need_free = false;
    if (byte_count <= DEFAULT_BUFFER_SIZE) {
      v = temp_buffer;
    } else {
      v = new jchar[byte_count];
      v_need_free = true;
    }

    int idx = offset;
    int last = offset + byte_count;
    int s = 0;

    int code_point = 0;
    int utf8_bytes_seen = 0;
    int utf8_bytes_needed = 0;
    int lower_bound = 0x80;
    int upper_bound = 0xbf;
    while (idx < last) {
      int b = d[idx++] & 0xff;
      if (utf8_bytes_needed == 0) {
        if ((b & 0x80) == 0) {  // ASCII char. 0xxxxxxx
          v[s++] = (jchar) b;
          continue;
        }

        if ((b & 0x40) == 0) {  // 10xxxxxx is illegal as first byte
          v[s++] = REPLACEMENT_CHAR;
          continue;
        }

        // 11xxxxxx
        int tableLookupIndex = b & 0x3f;
        utf8_bytes_needed = TABLE_UTF8_NEEDED[tableLookupIndex];
        if (utf8_bytes_needed == 0) {
          v[s++] = REPLACEMENT_CHAR;
          continue;
        }

        // utf8_bytes_needed
        // 1: b & 0x1f
        // 2: b & 0x0f
        // 3: b & 0x07
        code_point = b & (0x3f >> utf8_bytes_needed);
        if (b == 0xe0) {
          lower_bound = 0xa0;
        } else if (b == 0xed) {
          upper_bound = 0x9f;
        } else if (b == 0xf0) {
          lower_bound = 0x90;
        } else if (b == 0xf4) {
          upper_bound = 0x8f;
        }
      } else {
        if (b < lower_bound || b > upper_bound) {
          // The bytes seen are ill-formed. Substitute them with U+FFFD
          v[s++] = REPLACEMENT_CHAR;
          code_point = 0;
          utf8_bytes_needed = 0;
          utf8_bytes_seen = 0;
          lower_bound = 0x80;
          upper_bound = 0xbf;
          /*
           * According to the Unicode Standard,
           * "a UTF-8 conversion process is required to never consume well-formed
           * subsequences as part of its error handling for ill-formed subsequences"
           * The current byte could be part of well-formed subsequences. Reduce the
           * index by 1 to parse it in next loop.
           */
          idx--;
          continue;
        }

        lower_bound = 0x80;
        upper_bound = 0xbf;
        code_point = (code_point << 6) | (b & 0x3f);
        utf8_bytes_seen++;
        if (utf8_bytes_needed != utf8_bytes_seen) {
          continue;
        }

        // Encode chars from U+10000 up as surrogate pairs
        if (code_point < 0x10000) {
          v[s++] = (jchar) code_point;
        } else {
          v[s++] = (jchar) ((code_point >> 10) + 0xd7c0);
          v[s++] = (jchar) ((code_point & 0x3ff) + 0xdc00);
        }

        utf8_bytes_seen = 0;
        utf8_bytes_needed = 0;
        code_point = 0;
      }
    }
    // The bytes seen are ill-formed. Substitute them by U+FFFD
    if (utf8_bytes_needed != 0) {
      v[s++] = REPLACEMENT_CHAR;
    }
    // Free the template char array.
    if (v_need_free) {
      delete[] v;
    }

    ObjPtr<mirror::String> result = mirror::String::AllocFromUtf16(soa.Self(), s, v);
    return soa.AddLocalReference<jstring>(result);
  }

  return nullptr;
}

static JNINativeMethod gMethods[] = {
  FAST_NATIVE_METHOD(StringFactory, newStringFromBytes, "([BIII)Ljava/lang/String;"),
  FAST_NATIVE_METHOD(StringFactory, newStringFromChars, "(II[C)Ljava/lang/String;"),
  FAST_NATIVE_METHOD(StringFactory, newStringFromString, "(Ljava/lang/String;)Ljava/lang/String;"),
  FAST_NATIVE_METHOD(StringFactory, newStringFromUtf8Bytes, "([BII)Ljava/lang/String;"),
};

void register_java_lang_StringFactory(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("java/lang/StringFactory");
}

}  // namespace art
