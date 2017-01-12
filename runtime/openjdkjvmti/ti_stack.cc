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

#include "ti_stack.h"

#include <list>
#include <unordered_map>
#include <vector>

#include "art_jvmti.h"
#include "art_method-inl.h"
#include "base/bit_utils.h"
#include "base/enums.h"
#include "base/mutex.h"
#include "dex_file.h"
#include "dex_file_annotations.h"
#include "jni_env_ext.h"
#include "jni_internal.h"
#include "mirror/class.h"
#include "mirror/dex_cache.h"
#include "scoped_thread_state_change-inl.h"
#include "ScopedLocalRef.h"
#include "stack.h"
#include "thread-inl.h"
#include "thread_list.h"
#include "thread_pool.h"

namespace openjdkjvmti {

struct GetStackTraceVisitor : public art::StackVisitor {
  GetStackTraceVisitor(art::Thread* thread_in,
                       size_t start_,
                       size_t stop_)
      : StackVisitor(thread_in, nullptr, StackVisitor::StackWalkKind::kIncludeInlinedFrames),
        start(start_),
        stop(stop_) {}

  bool VisitFrame() REQUIRES_SHARED(art::Locks::mutator_lock_) {
    art::ArtMethod* m = GetMethod();
    if (m->IsRuntimeMethod()) {
      return true;
    }

    if (start == 0) {
      m = m->GetInterfaceMethodIfProxy(art::kRuntimePointerSize);
      jmethodID id = art::jni::EncodeArtMethod(m);

      uint32_t dex_pc = GetDexPc(false);
      jlong dex_location = (dex_pc == art::DexFile::kDexNoIndex) ? -1 : static_cast<jlong>(dex_pc);

      jvmtiFrameInfo info = { id, dex_location };
      frames.push_back(info);

      if (stop == 1) {
        return false;  // We're done.
      } else if (stop > 0) {
        stop--;
      }
    } else {
      start--;
    }

    return true;
  }

  std::vector<jvmtiFrameInfo> frames;
  size_t start;
  size_t stop;
};

struct GetStackTraceClosure : public art::Closure {
 public:
  GetStackTraceClosure(size_t start, size_t stop)
      : start_input(start),
        stop_input(stop),
        start_result(0),
        stop_result(0) {}

  void Run(art::Thread* self) OVERRIDE REQUIRES_SHARED(art::Locks::mutator_lock_) {
    GetStackTraceVisitor visitor(self, start_input, stop_input);
    visitor.WalkStack(false);

    frames.swap(visitor.frames);
    start_result = visitor.start;
    stop_result = visitor.stop;
  }

  const size_t start_input;
  const size_t stop_input;

  std::vector<jvmtiFrameInfo> frames;
  size_t start_result;
  size_t stop_result;
};

static jvmtiError TranslateFrameVector(const std::vector<jvmtiFrameInfo>& frames,
                                       jint start_depth,
                                       size_t start_result,
                                       jint max_frame_count,
                                       jvmtiFrameInfo* frame_buffer,
                                       jint* count_ptr) {
  size_t collected_frames = frames.size();

  // Assume we're here having collected something.
  DCHECK_GT(max_frame_count, 0);

  // Frames from the top.
  if (start_depth >= 0) {
    if (start_result != 0) {
      // Not enough frames.
      return ERR(ILLEGAL_ARGUMENT);
    }
    DCHECK_LE(collected_frames, static_cast<size_t>(max_frame_count));
    if (frames.size() > 0) {
      memcpy(frame_buffer, frames.data(), collected_frames * sizeof(jvmtiFrameInfo));
    }
    *count_ptr = static_cast<jint>(frames.size());
    return ERR(NONE);
  }

  // Frames from the bottom.
  if (collected_frames < static_cast<size_t>(-start_depth)) {
    return ERR(ILLEGAL_ARGUMENT);
  }

  size_t count = std::min(static_cast<size_t>(-start_depth), static_cast<size_t>(max_frame_count));
  memcpy(frame_buffer,
         &frames.data()[collected_frames + start_depth],
         count * sizeof(jvmtiFrameInfo));
  *count_ptr = static_cast<jint>(count);
  return ERR(NONE);
}

jvmtiError StackUtil::GetStackTrace(jvmtiEnv* jvmti_env ATTRIBUTE_UNUSED,
                                    jthread java_thread,
                                    jint start_depth,
                                    jint max_frame_count,
                                    jvmtiFrameInfo* frame_buffer,
                                    jint* count_ptr) {
  if (java_thread == nullptr) {
    return ERR(INVALID_THREAD);
  }

  art::Thread* thread;
  {
    // TODO: Need non-aborting call here, to return JVMTI_ERROR_INVALID_THREAD.
    art::ScopedObjectAccess soa(art::Thread::Current());
    art::MutexLock mu(soa.Self(), *art::Locks::thread_list_lock_);
    thread = art::Thread::FromManagedThread(soa, java_thread);
    DCHECK(thread != nullptr);
  }

  art::ThreadState state = thread->GetState();
  if (state == art::ThreadState::kStarting ||
      state == art::ThreadState::kTerminated ||
      thread->IsStillStarting()) {
    return ERR(THREAD_NOT_ALIVE);
  }

  if (max_frame_count < 0) {
    return ERR(ILLEGAL_ARGUMENT);
  }
  if (frame_buffer == nullptr || count_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  if (max_frame_count == 0) {
    *count_ptr = 0;
    return ERR(NONE);
  }

  GetStackTraceClosure closure(start_depth >= 0 ? static_cast<size_t>(start_depth) : 0,
                               start_depth >= 0 ? static_cast<size_t>(max_frame_count) : 0);
  thread->RequestSynchronousCheckpoint(&closure);

  return TranslateFrameVector(closure.frames,
                              start_depth,
                              closure.start_result,
                              max_frame_count,
                              frame_buffer,
                              count_ptr);
}

struct GetAllStackTraceClosure : public art::Closure {
 public:
  explicit GetAllStackTraceClosure(size_t stop)
      : start_input(0),
        stop_input(stop),
        frames_lock("GetAllStackTraceGuard", art::LockLevel::kAbortLock),
        start_result(0),
        stop_result(0) {}

  void Run(art::Thread* self)
      OVERRIDE REQUIRES_SHARED(art::Locks::mutator_lock_) REQUIRES(!frames_lock) {
    // self should be live here (so it could be suspended). No need to filter.

    art::Thread* current = art::Thread::Current();
    std::vector<jvmtiFrameInfo> self_frames;

    GetStackTraceVisitor visitor(self, start_input, stop_input);
    visitor.WalkStack(false);

    self_frames.swap(visitor.frames);

    art::MutexLock mu(current, frames_lock);
    frames.emplace(self, self_frames);
  }

  const size_t start_input;
  const size_t stop_input;

  art::Mutex frames_lock;
  std::unordered_map<art::Thread*, std::vector<jvmtiFrameInfo>> frames GUARDED_BY(frames_lock);
  size_t start_result;
  size_t stop_result;
};



jvmtiError StackUtil::GetAllStackTraces(jvmtiEnv* env,
                                        jint max_frame_count,
                                        jvmtiStackInfo** stack_info_ptr,
                                        jint* thread_count_ptr) {
  if (max_frame_count < 0) {
    return ERR(ILLEGAL_ARGUMENT);
  }
  if (stack_info_ptr == nullptr || thread_count_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }


  art::Thread* current = art::Thread::Current();
  art::ScopedObjectAccess soa(current);      // Now we know we have the shared lock.
  art::ScopedThreadSuspension sts(current, art::kWaitingForDebuggerSuspension);
  art::ScopedSuspendAll ssa("GetAllStackTraces");

  std::vector<art::Thread*> threads;
  std::vector<std::vector<jvmtiFrameInfo>> frames;
  {
    std::list<art::Thread*> thread_list;
    {
      art::MutexLock mu(current, *art::Locks::thread_list_lock_);
      thread_list = art::Runtime::Current()->GetThreadList()->GetList();
    }

    for (art::Thread* thread : thread_list) {
      GetStackTraceClosure closure(0u, static_cast<size_t>(max_frame_count));
      thread->RequestSynchronousCheckpoint(&closure);

      threads.push_back(thread);
      frames.emplace_back();
      frames.back().swap(closure.frames);
    }
  }

  // Convert the data into our output format. Note: we need to keep the threads suspended,
  // as we need to access them for their peers.

  // Note: we use an array of jvmtiStackInfo for convenience. The spec says we need to
  //       allocate one big chunk for this and the actual frames, which means we need
  //       to either be conservative or rearrange things later (the latter is implemented).
  std::unique_ptr<jvmtiStackInfo[]> stack_info_array(new jvmtiStackInfo[frames.size()]);
  std::vector<std::unique_ptr<jvmtiFrameInfo[]>> frame_infos;
  frame_infos.reserve(frames.size());

  // Now run through and add data for each thread.
  size_t sum_frames = 0;
  for (size_t index = 0; index < frames.size(); ++index) {
    jvmtiStackInfo& stack_info = stack_info_array.get()[index];
    memset(&stack_info, 0, sizeof(jvmtiStackInfo));

    art::Thread* self = threads[index];
    const std::vector<jvmtiFrameInfo>& thread_frames = frames[index];

    // For the time being, set the thread to null. We don't have good ScopedLocalRef
    // infrastructure.
    DCHECK(self->GetPeer() != nullptr);
    stack_info.thread = nullptr;
    stack_info.state = JVMTI_THREAD_STATE_SUSPENDED;

    size_t collected_frames = thread_frames.size();
    if (max_frame_count == 0 || collected_frames == 0) {
      stack_info.frame_count = 0;
      stack_info.frame_buffer = nullptr;
      continue;
    }
    DCHECK_LE(collected_frames, static_cast<size_t>(max_frame_count));

    jvmtiFrameInfo* frame_info = new jvmtiFrameInfo[collected_frames];
    frame_infos.emplace_back(frame_info);

    jint count;
    jvmtiError translate_result = TranslateFrameVector(thread_frames,
                                                       0,
                                                       0,
                                                       static_cast<jint>(collected_frames),
                                                       frame_info,
                                                       &count);
    DCHECK(translate_result == JVMTI_ERROR_NONE);
    stack_info.frame_count = static_cast<jint>(collected_frames);
    stack_info.frame_buffer = frame_info;
    sum_frames += static_cast<size_t>(count);
  }

  // No errors, yet. Now put it all into an output buffer.
  size_t rounded_stack_info_size = art::RoundUp(sizeof(jvmtiStackInfo) * frames.size(),
                                                alignof(jvmtiFrameInfo));
  size_t chunk_size = rounded_stack_info_size + sum_frames * sizeof(jvmtiFrameInfo);
  unsigned char* chunk_data;
  jvmtiError alloc_result = env->Allocate(chunk_size, &chunk_data);
  if (alloc_result != ERR(NONE)) {
    return alloc_result;
  }

  jvmtiStackInfo* stack_info = reinterpret_cast<jvmtiStackInfo*>(chunk_data);
  // First copy in all the basic data.
  memcpy(stack_info, stack_info_array.get(), sizeof(jvmtiStackInfo) * frames.size());

  // Now copy the frames and fix up the pointers.
  jvmtiFrameInfo* frame_info = reinterpret_cast<jvmtiFrameInfo*>(
      chunk_data + rounded_stack_info_size);
  for (size_t i = 0; i < frames.size(); ++i) {
    jvmtiStackInfo& old_stack_info = stack_info_array.get()[i];
    jvmtiStackInfo& new_stack_info = stack_info[i];

    jthread thread_peer = current->GetJniEnv()->AddLocalReference<jthread>(threads[i]->GetPeer());
    new_stack_info.thread = thread_peer;

    if (old_stack_info.frame_count > 0) {
      // Only copy when there's data - leave the nullptr alone.
      size_t frames_size = static_cast<size_t>(old_stack_info.frame_count) * sizeof(jvmtiFrameInfo);
      memcpy(frame_info, old_stack_info.frame_buffer, frames_size);
      new_stack_info.frame_buffer = frame_info;
      frame_info += old_stack_info.frame_count;
    }
  }

  *stack_info_ptr = stack_info;
  *thread_count_ptr = static_cast<jint>(frames.size());

  return ERR(NONE);
}

}  // namespace openjdkjvmti
