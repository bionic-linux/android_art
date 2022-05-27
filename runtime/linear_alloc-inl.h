/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef ART_RUNTIME_LINEAR_ALLOC_INL_H_
#define ART_RUNTIME_LINEAR_ALLOC_INL_H_

#include "linear_alloc.h"

#include "thread-current-inl.h"

namespace art {

template <bool kTrackAllocations>
void LinearAlloc<kTracAllocations>::SetFirstObject(void* begin, size_t bytes) const {
  CHECK(kTrackAllocations);
  void* end = static_cast<uint8_t*>(begin) + bytes;
  Arena* arena = allocator_.GetArenaHead();
  DCHECK_NE(head, nullptr);
  // The object would either be in the head arena or the next one.
  if (UNLIKELY(begin < arena->Begin() || begin >= arena->End())) {
    arena = arena->Next();
  }
  DCHECK(begin >= arena->Begin() && end <= arena->End());
  static_cast<TrackedArena*>(arena)->SetFirstObject(begin, end);
}

template <bool kTrackAllocations>
void* LinearAlloc<kTrackAllocations>::Realloc(Thread* self,
                                              void* ptr,
                                              size_t old_size,
                                              size_t new_size) {
  MutexLock mu(self, lock_);
  if (kTrackAllocations) {
    // Realloc cannot be called on 16-byte aligned as Realloc doesn't guarantee
    // that. So the header must be in the word immediately prior to ptr.
    TrackingHeader* header = reinterpret_cast<TrackingHeader*>(ptr) - 1;
    LinearAllocKind kind = header->Kind();
    old_size += sizeof(TrackingHeader);
    new_size += sizeof(TrackingHeader);
    CHECK_EQ(header->Size(), old_size);
    void* ret = allocator_.Realloc(header, old_size, new_size);
    header = new (ret) TrackingHeader(new_size, kind);
    SetFirstObject(ret, new_size);
    return header + 1;
  } else {
    return allocator_.Realloc(ptr, old_size, new_size);
  }
}

template <bool kTrackAllocations>
void* LinearAlloc<kTrackAllocations>::Alloc(Thread* self, size_t size, LinearAllocKind kind) {
  MutexLock mu(self, lock_);
  if (kTrackAllocations) {
    size += sizeof(TrackingHeader);
    TrackingHeader* storage = new (allocator_.Alloc(size)) TrackingHeader(size, kind);
    SetFirstObject(storage, size);
    return storage + 1;
  } else {
    return allocator_.Alloc(size);
  }
}

template <bool kTrackAllocations>
void* LinearAlloc<kTrackAllocations>::AllocAlign16(Thread* self, size_t size, LinearAllocKind kind) {
  MutexLock mu(self, lock_);
  if (kTrackAllocations) {
    // First allocate the header
    TrackingHeader* header = allocator_.Alloc(sizeof(TrackingHeader));
    // Aligned allocation
    uint8_t* storage = allocator_.AllocAlign16(size);
    // Handle the rare case where header and the actual allocation are on
    // different arenas.
    ptrdiff_t diff = storage - reinterpret_cast<uint8_t*>(header);
    if (UNLIKELY(diff > 16 || diff < 0)) {
      // Realloc will take care of the rare case wherein allocation was large
      // enough to require its own arena.
      size_t old_size = size;
      size += 16;
      storage = allocator_.Realloc(storage, old_size, size);
      storage = new (storage) TrackingHeader(size, kind);
      SetFirstObject(storage, size);
      storage += 16;
    } else {
      size += diff;
      new (header) TrackingHeader(size, kind);
      SetFirstObject(storage, size);
    }
    return storage;
  } else {
    return allocator_.AllocAlign16(size);
  }
}

template <bool kTrackAllocations>
size_t LinearAlloc<kTrackAllocations>::GetUsedMemory() const {
  MutexLock mu(Thread::Current(), lock_);
  return allocator_.BytesUsed();
}

template <bool kTrackAllocations>
ArenaPool* LinearAlloc<kTrackAllocations>::GetArenaPool() {
  MutexLock mu(Thread::Current(), lock_);
  return allocator_.GetArenaPool();
}

template <bool kTrackAllocations>
bool LinearAlloc<kTrackAllocations>::Contains(void* ptr) const {
  MutexLock mu(Thread::Current(), lock_);
  return allocator_.Contains(ptr);
}

}  // namespace art

#endif  // ART_RUNTIME_LINEAR_ALLOC_INL_H_
