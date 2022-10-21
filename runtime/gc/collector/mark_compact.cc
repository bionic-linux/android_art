/*
 * Copyright 2021 The Android Open Source Project
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

#include "mark_compact-inl.h"

#include "base/memfd.h"
#include "base/quasi_atomic.h"
#include "base/systrace.h"
#include "base/utils.h"
#include "gc/accounting/mod_union_table-inl.h"
#include "gc/reference_processor.h"
#include "gc/space/bump_pointer_space.h"
#include "gc/task_processor.h"
#include "gc/verification-inl.h"
#include "jit/jit_code_cache.h"
#include "mirror/object-refvisitor-inl.h"
#include "read_barrier_config.h"
#include "scoped_thread_state_change-inl.h"
#include "sigchain.h"
#include "thread_list.h"

#include <linux/userfaultfd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fstream>
#include <numeric>

#ifndef __BIONIC__
#ifndef MREMAP_DONTUNMAP
#define MREMAP_DONTUNMAP 4
#endif
#ifndef __NR_userfaultfd
#if defined(__x86_64__)
#define __NR_userfaultfd 323
#elif defined(__i386__)
#define __NR_userfaultfd 374
#elif defined(__aarch64__)
#define __NR_userfaultfd 282
#elif defined(__arm__)
#define __NR_userfaultfd 388
#else
#error "__NR_userfaultfd undefined"
#endif
#endif  // __NR_userfaultfd
#endif  // __BIONIC__

namespace art {

// We require MREMAP_DONTUNMAP functionality of the mremap syscall, which was
// introduced in 5.13 kernel version. Check for that on host. Checking
// on Android is not required as MREMAP_DONTUNMAP and userfaultfd were enabled
// together.
// Concurrent compaction termination logic depends on the kernel having
// the fault-retry feature (allowing repeated faults on the same page), which was
// introduced in 5.7. On Android this feature is backported on all the kernels where
// userfaultfd is enabled.
static const bool gHaveMremapDontunmap = kIsTargetAndroid || IsKernelVersionAtLeast(5, 13);
static const bool gKernelHasFaultRetry = kIsTargetAndroid || IsKernelVersionAtLeast(5, 7);

#ifndef ART_FORCE_USE_READ_BARRIER
static bool ShouldUseUserfaultfd() {
#if !defined(__linux__)
  return false;
#endif
  int fd = syscall(__NR_userfaultfd, O_CLOEXEC | UFFD_USER_MODE_ONLY);
  // On non-android devices we may not have the kernel patches that restrict
  // userfaultfd to user mode. But that is not a security concern as we are
  // on host. Therefore, attempt one more time without UFFD_USER_MODE_ONLY.
  if (!kIsTargetAndroid && fd == -1 && errno == EINVAL) {
    fd = syscall(__NR_userfaultfd, O_CLOEXEC);
  }
  if (fd >= 0) {
    close(fd);
    return true;
  } else {
    return false;
  }
}
#endif

// The other cases are defined as a constexpr in runtime/read_barrier_config.h
#ifndef ART_FORCE_USE_READ_BARRIER
const bool gUseReadBarrier = (kUseBakerReadBarrier || kUseTableLookupReadBarrier)
                             && !ShouldUseUserfaultfd();
#ifdef ART_DEFAULT_GC_TYPE_IS_CMC
const bool gUseUserfaultfd = !gUseReadBarrier;
#endif
#endif

namespace gc {
namespace collector {

// Turn off kCheckLocks when profiling the GC as it slows down the GC
// significantly.
static constexpr bool kCheckLocks = kDebugLocking;
static constexpr bool kVerifyRootsMarked = kIsDebugBuild;
static constexpr size_t kNumUffdWorkers = kIsTargetAndroid ? 4 : 2;

bool MarkCompact::CreateUserfaultfd(bool post_fork) {
  if (post_fork || uffd_ == -1) {
    // Don't use O_NONBLOCK as we rely on read waiting on uffd_ if there isn't
    // any read event available. We don't use poll.
    if (gKernelHasFaultRetry) {
      uffd_ = syscall(__NR_userfaultfd, O_CLOEXEC | UFFD_USER_MODE_ONLY);
      // On non-android devices we may not have the kernel patches that restrict
      // userfaultfd to user mode. But that is not a security concern as we are
      // on host. Therefore, attempt one more time without UFFD_USER_MODE_ONLY.
      if (!kIsTargetAndroid && UNLIKELY(uffd_ == -1 && errno == EINVAL)) {
        uffd_ = syscall(__NR_userfaultfd, O_CLOEXEC);
      }
      if (UNLIKELY(uffd_ == -1)) {
        uffd_ = kFallbackMode;
        LOG(WARNING) << "Userfaultfd isn't supported (reason: " << strerror(errno)
                     << ") and therefore falling back to stop-the-world compaction.";
      } else {
        DCHECK_GE(uffd_, 0);
        // Get/update the features that we want in userfaultfd
        struct uffdio_api api = {.api = UFFD_API,
                                 .features = UFFD_FEATURE_MISSING_SHMEM
                                             | UFFD_FEATURE_MINOR_SHMEM};
        CHECK_EQ(ioctl(uffd_, UFFDIO_API, &api), 0)
              << "ioctl_userfaultfd: API: " << strerror(errno);
        // Missing userfaults on shmem should always be available.
        DCHECK_NE(api.features & UFFD_FEATURE_MISSING_SHMEM, 0u);
        uffd_minor_fault_supported_ = (api.features & UFFD_FEATURE_MINOR_SHMEM) != 0;
        // TODO: Assert that minor-fault support isn't available only on 32-bit
        // kernel.
      }
    } else {
      // Without fault-retry feature in the kernel we can't terminate concurrent
      // compaction. So fallback to stop-the-world compaction.
      uffd_ = kFallbackMode;
    }
  }
  uffd_initialized_ = !post_fork || uffd_ == kFallbackMode;
  return uffd_ >= 0;
}

template <size_t kAlignment>
MarkCompact::LiveWordsBitmap<kAlignment>* MarkCompact::LiveWordsBitmap<kAlignment>::Create(
    uintptr_t begin, uintptr_t end) {
  return static_cast<LiveWordsBitmap<kAlignment>*>(
          MemRangeBitmap::Create("Concurrent Mark Compact live words bitmap", begin, end));
}

MarkCompact::MarkCompact(Heap* heap)
        : GarbageCollector(heap, "concurrent mark compact"),
          gc_barrier_(0),
          mark_stack_lock_("mark compact mark stack lock", kMarkSweepMarkStackLock),
          bump_pointer_space_(heap->GetBumpPointerSpace()),
          uffd_(-1),
          thread_pool_counter_(0),
          compacting_(false),
          uffd_initialized_(false),
          uffd_minor_fault_supported_(false),
          minor_fault_initialized_(false) {
  // TODO: Depending on how the bump-pointer space move is implemented. If we
  // switch between two virtual memories each time, then we will have to
  // initialize live_words_bitmap_ accordingly.
  live_words_bitmap_.reset(LiveWordsBitmap<kAlignment>::Create(
          reinterpret_cast<uintptr_t>(bump_pointer_space_->Begin()),
          reinterpret_cast<uintptr_t>(bump_pointer_space_->Limit())));

  // Create one MemMap for all the data structures
  size_t chunk_info_vec_size = bump_pointer_space_->Capacity() / kOffsetChunkSize;
  size_t nr_moving_pages = bump_pointer_space_->Capacity() / kPageSize;
  size_t nr_non_moving_pages = heap->GetNonMovingSpace()->Capacity() / kPageSize;

  std::string err_msg;
  info_map_ = MemMap::MapAnonymous("Concurrent mark-compact chunk-info vector",
                                   chunk_info_vec_size * sizeof(uint32_t)
                                   + nr_non_moving_pages * sizeof(ObjReference)
                                   + nr_moving_pages * sizeof(ObjReference)
                                   + nr_moving_pages * sizeof(uint32_t),
                                   PROT_READ | PROT_WRITE,
                                   /*low_4gb=*/ false,
                                   &err_msg);
  if (UNLIKELY(!info_map_.IsValid())) {
    LOG(FATAL) << "Failed to allocate concurrent mark-compact chunk-info vector: " << err_msg;
  } else {
    uint8_t* p = info_map_.Begin();
    chunk_info_vec_ = reinterpret_cast<uint32_t*>(p);
    vector_length_ = chunk_info_vec_size;

    p += chunk_info_vec_size * sizeof(uint32_t);
    first_objs_non_moving_space_ = reinterpret_cast<ObjReference*>(p);

    p += nr_non_moving_pages * sizeof(ObjReference);
    first_objs_moving_space_ = reinterpret_cast<ObjReference*>(p);

    p += nr_moving_pages * sizeof(ObjReference);
    pre_compact_offset_moving_space_ = reinterpret_cast<uint32_t*>(p);
  }

  from_space_map_ = MemMap::MapAnonymous("Concurrent mark-compact from-space",
                                         bump_pointer_space_->Capacity(),
                                         PROT_NONE,
                                         /*low_4gb=*/ kObjPtrPoisoning,
                                         &err_msg);
  if (UNLIKELY(!from_space_map_.IsValid())) {
    LOG(FATAL) << "Failed to allocate concurrent mark-compact from-space" << err_msg;
  } else {
    from_space_begin_ = from_space_map_.Begin();
  }

  // poisoning requires 32-bit pointers and therefore compaction buffers on
  // the stack can't be used. We also use the first page-sized buffer for the
  // purpose of terminating concurrent compaction.
  const size_t num_pages = 1 + kNumUffdWorkers;
  compaction_buffers_map_ = MemMap::MapAnonymous("Concurrent mark-compact compaction buffers",
                                                 kPageSize * (kObjPtrPoisoning ? num_pages : 1),
                                                 PROT_READ | PROT_WRITE,
                                                 /*low_4gb=*/ kObjPtrPoisoning,
                                                 &err_msg);
  if (UNLIKELY(!compaction_buffers_map_.IsValid())) {
    LOG(ERROR) << "Failed to allocate concurrent mark-compact compaction buffers" << err_msg;
  }
  conc_compaction_termination_page_ = compaction_buffers_map_.Begin();
  if (kObjPtrPoisoning) {
    // Touch the page deliberately to avoid userfaults on it. We madvise it in
    // CompactionPhase() before using it to terminate concurrent compaction.
    CHECK_EQ(*conc_compaction_termination_page_, 0);
  }
  // In most of the cases, we don't expect more than one LinearAlloc space.
  linear_alloc_spaces_data_.reserve(1);
}

void MarkCompact::AddLinearAllocSpaceData(uint8_t* begin, size_t len, int fd) {
  DCHECK_ALIGNED(begin, kPageSize);
  DCHECK_ALIGNED(len, kPageSize);
  std::string err_msg;
  MemMap shadow(MemMap::MapAnonymous("linear-alloc shadow map",
                                     len,
                                     PROT_NONE,
                                     /*low_4gb=*/false,
                                     &err_msg));
  if (!shadow.IsValid()) {
    LOG(FATAL) << "Failed to allocate linear-alloc shadow map: " << err_msg;
    UNREACHABLE();
  }

  MemMap page_status_map(MemMap::MapAnonymous("linear-alloc page-status map",
                                              len / kPageSize,
                                              PROT_NONE,
                                              /*low_4gb=*/false,
                                              &err_msg));
  if (!page_status_map.IsValid()) {
    LOG(FATAL) << "Failed to allocate linear-alloc page-status shadow map: " << err_msg;
    UNREACHABLE();
  }
  // We need to retain the file descriptor if MREMAP_DONTUNMAP is not available
  // as then we have to mmap the map again after mremap.
  if (gHaveMremapDontunmap && fd >= 0) {
    close(fd);
    fd = -2;
  }
  linear_alloc_spaces_data_.emplace_back(std::forward<MemMap>(shadow),
                                         std::forward<MemMap>(page_status_map),
                                         begin,
                                         begin + len,
                                         fd);
}

void MarkCompact::BindAndResetBitmaps() {
  // TODO: We need to hold heap_bitmap_lock_ only for populating immune_spaces.
  // The card-table and mod-union-table processing can be done without it. So
  // change the logic below. Note that the bitmap clearing would require the
  // lock.
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  accounting::CardTable* const card_table = heap_->GetCardTable();
  // Mark all of the spaces we never collect as immune.
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->GetGcRetentionPolicy() == space::kGcRetentionPolicyNeverCollect ||
        space->GetGcRetentionPolicy() == space::kGcRetentionPolicyFullCollect) {
      CHECK(space->IsZygoteSpace() || space->IsImageSpace());
      immune_spaces_.AddSpace(space);
      accounting::ModUnionTable* table = heap_->FindModUnionTableFromSpace(space);
      if (table != nullptr) {
        table->ProcessCards();
      } else {
        // Keep cards aged if we don't have a mod-union table since we may need
        // to scan them in future GCs. This case is for app images.
        // TODO: We could probably scan the objects right here to avoid doing
        // another scan through the card-table.
        card_table->ModifyCardsAtomic(
            space->Begin(),
            space->End(),
            [](uint8_t card) {
              return (card == gc::accounting::CardTable::kCardClean)
                  ? card
                  : gc::accounting::CardTable::kCardAged;
            },
            /* card modified visitor */ VoidFunctor());
      }
    } else {
      CHECK(!space->IsZygoteSpace());
      CHECK(!space->IsImageSpace());
      // The card-table corresponding to bump-pointer and non-moving space can
      // be cleared, because we are going to traverse all the reachable objects
      // in these spaces. This card-table will eventually be used to track
      // mutations while concurrent marking is going on.
      card_table->ClearCardRange(space->Begin(), space->Limit());
      if (space == bump_pointer_space_) {
        // It is OK to clear the bitmap with mutators running since the only
        // place it is read is VisitObjects which has exclusion with this GC.
        moving_space_bitmap_ = bump_pointer_space_->GetMarkBitmap();
        moving_space_bitmap_->Clear();
      } else {
        CHECK(space == heap_->GetNonMovingSpace());
        non_moving_space_ = space;
        non_moving_space_bitmap_ = space->GetMarkBitmap();
      }
    }
  }
}

void MarkCompact::InitializePhase() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  mark_stack_ = heap_->GetMarkStack();
  CHECK(mark_stack_->IsEmpty());
  immune_spaces_.Reset();
  moving_first_objs_count_ = 0;
  non_moving_first_objs_count_ = 0;
  black_page_count_ = 0;
  freed_objects_ = 0;
  from_space_slide_diff_ = from_space_begin_ - bump_pointer_space_->Begin();
  black_allocations_begin_ = bump_pointer_space_->Limit();
  compacting_ = false;
  // TODO: Would it suffice to read it once in the constructor, which is called
  // in zygote process?
  pointer_size_ = Runtime::Current()->GetClassLinker()->GetImagePointerSize();
}

void MarkCompact::RunPhases() {
  Thread* self = Thread::Current();
  thread_running_gc_ = self;
  InitializePhase();
  GetHeap()->PreGcVerification(this);
  {
    ReaderMutexLock mu(self, *Locks::mutator_lock_);
    MarkingPhase();
  }
  {
    ScopedPause pause(this);
    MarkingPause();
    if (kIsDebugBuild) {
      bump_pointer_space_->AssertAllThreadLocalBuffersAreRevoked();
    }
  }
  // To increase likelihood of black allocations. For testing purposes only.
  if (kIsDebugBuild && heap_->GetTaskProcessor()->GetRunningThread() == thread_running_gc_) {
    usleep(500'000);
  }
  {
    ReaderMutexLock mu(self, *Locks::mutator_lock_);
    ReclaimPhase();
    PrepareForCompaction();
  }
  if (uffd_ != kFallbackMode) {
    heap_->GetThreadPool()->WaitForWorkersToBeCreated();
  }
  {
    heap_->ThreadFlipBegin(self);
    {
      ScopedPause pause(this);
      PreCompactionPhase();
    }
    heap_->ThreadFlipEnd(self);
  }

  if (uffd_ >= 0) {
    ReaderMutexLock mu(self, *Locks::mutator_lock_);
    CompactionPhase();
  }

  FinishPhase();
  thread_running_gc_ = nullptr;
  GetHeap()->PostGcVerification(this);
}

void MarkCompact::InitMovingSpaceFirstObjects(const size_t vec_len) {
  // Find the first live word first.
  size_t to_space_page_idx = 0;
  uint32_t offset_in_chunk_word;
  uint32_t offset;
  mirror::Object* obj;
  const uintptr_t heap_begin = moving_space_bitmap_->HeapBegin();

  size_t chunk_idx;
  // Find the first live word in the space
  for (chunk_idx = 0; chunk_info_vec_[chunk_idx] == 0; chunk_idx++) {
    if (chunk_idx > vec_len) {
      // We don't have any live data on the moving-space.
      return;
    }
  }
  // Use live-words bitmap to find the first word
  offset_in_chunk_word = live_words_bitmap_->FindNthLiveWordOffset(chunk_idx, /*n*/ 0);
  offset = chunk_idx * kBitsPerVectorWord + offset_in_chunk_word;
  DCHECK(live_words_bitmap_->Test(offset)) << "offset=" << offset
                                           << " chunk_idx=" << chunk_idx
                                           << " N=0"
                                           << " offset_in_word=" << offset_in_chunk_word
                                           << " word=" << std::hex
                                           << live_words_bitmap_->GetWord(chunk_idx);
  // The first object doesn't require using FindPrecedingObject().
  obj = reinterpret_cast<mirror::Object*>(heap_begin + offset * kAlignment);
  // TODO: add a check to validate the object.

  pre_compact_offset_moving_space_[to_space_page_idx] = offset;
  first_objs_moving_space_[to_space_page_idx].Assign(obj);
  to_space_page_idx++;

  uint32_t page_live_bytes = 0;
  while (true) {
    for (; page_live_bytes <= kPageSize; chunk_idx++) {
      if (chunk_idx > vec_len) {
        moving_first_objs_count_ = to_space_page_idx;
        return;
      }
      page_live_bytes += chunk_info_vec_[chunk_idx];
    }
    chunk_idx--;
    page_live_bytes -= kPageSize;
    DCHECK_LE(page_live_bytes, kOffsetChunkSize);
    DCHECK_LE(page_live_bytes, chunk_info_vec_[chunk_idx])
        << " chunk_idx=" << chunk_idx
        << " to_space_page_idx=" << to_space_page_idx
        << " vec_len=" << vec_len;
    DCHECK(IsAligned<kAlignment>(chunk_info_vec_[chunk_idx] - page_live_bytes));
    offset_in_chunk_word =
            live_words_bitmap_->FindNthLiveWordOffset(
                chunk_idx, (chunk_info_vec_[chunk_idx] - page_live_bytes) / kAlignment);
    offset = chunk_idx * kBitsPerVectorWord + offset_in_chunk_word;
    DCHECK(live_words_bitmap_->Test(offset))
        << "offset=" << offset
        << " chunk_idx=" << chunk_idx
        << " N=" << ((chunk_info_vec_[chunk_idx] - page_live_bytes) / kAlignment)
        << " offset_in_word=" << offset_in_chunk_word
        << " word=" << std::hex << live_words_bitmap_->GetWord(chunk_idx);
    // TODO: Can we optimize this for large objects? If we are continuing a
    // large object that spans multiple pages, then we may be able to do without
    // calling FindPrecedingObject().
    //
    // Find the object which encapsulates offset in it, which could be
    // starting at offset itself.
    obj = moving_space_bitmap_->FindPrecedingObject(heap_begin + offset * kAlignment);
    // TODO: add a check to validate the object.
    pre_compact_offset_moving_space_[to_space_page_idx] = offset;
    first_objs_moving_space_[to_space_page_idx].Assign(obj);
    to_space_page_idx++;
    chunk_idx++;
  }
}

void MarkCompact::InitNonMovingSpaceFirstObjects() {
  accounting::ContinuousSpaceBitmap* bitmap = non_moving_space_->GetLiveBitmap();
  uintptr_t begin = reinterpret_cast<uintptr_t>(non_moving_space_->Begin());
  const uintptr_t end = reinterpret_cast<uintptr_t>(non_moving_space_->End());
  mirror::Object* prev_obj;
  size_t page_idx;
  {
    // Find first live object
    mirror::Object* obj = nullptr;
    bitmap->VisitMarkedRange</*kVisitOnce*/ true>(begin,
                                                  end,
                                                  [&obj] (mirror::Object* o) {
                                                    obj = o;
                                                  });
    if (obj == nullptr) {
      // There are no live objects in the non-moving space
      return;
    }
    page_idx = (reinterpret_cast<uintptr_t>(obj) - begin) / kPageSize;
    first_objs_non_moving_space_[page_idx++].Assign(obj);
    prev_obj = obj;
  }
  // TODO: check obj is valid
  uintptr_t prev_obj_end = reinterpret_cast<uintptr_t>(prev_obj)
                           + RoundUp(prev_obj->SizeOf<kDefaultVerifyFlags>(), kAlignment);
  // For every page find the object starting from which we need to call
  // VisitReferences. It could either be an object that started on some
  // preceding page, or some object starting within this page.
  begin = RoundDown(reinterpret_cast<uintptr_t>(prev_obj) + kPageSize, kPageSize);
  while (begin < end) {
    // Utilize, if any, large object that started in some preceding page, but
    // overlaps with this page as well.
    if (prev_obj != nullptr && prev_obj_end > begin) {
      DCHECK_LT(prev_obj, reinterpret_cast<mirror::Object*>(begin));
      first_objs_non_moving_space_[page_idx].Assign(prev_obj);
      mirror::Class* klass = prev_obj->GetClass<kVerifyNone, kWithoutReadBarrier>();
      if (bump_pointer_space_->HasAddress(klass)) {
        LOG(WARNING) << "found inter-page object " << prev_obj
                     << " in non-moving space with klass " << klass
                     << " in moving space";
      }
    } else {
      prev_obj_end = 0;
      // It's sufficient to only search for previous object in the preceding page.
      // If no live object started in that page and some object had started in
      // the page preceding to that page, which was big enough to overlap with
      // the current page, then we wouldn't be in the else part.
      prev_obj = bitmap->FindPrecedingObject(begin, begin - kPageSize);
      if (prev_obj != nullptr) {
        prev_obj_end = reinterpret_cast<uintptr_t>(prev_obj)
                        + RoundUp(prev_obj->SizeOf<kDefaultVerifyFlags>(), kAlignment);
      }
      if (prev_obj_end > begin) {
        mirror::Class* klass = prev_obj->GetClass<kVerifyNone, kWithoutReadBarrier>();
        if (bump_pointer_space_->HasAddress(klass)) {
          LOG(WARNING) << "found inter-page object " << prev_obj
                       << " in non-moving space with klass " << klass
                       << " in moving space";
        }
        first_objs_non_moving_space_[page_idx].Assign(prev_obj);
      } else {
        // Find the first live object in this page
        bitmap->VisitMarkedRange</*kVisitOnce*/ true>(
                begin,
                begin + kPageSize,
                [this, page_idx] (mirror::Object* obj) {
                  first_objs_non_moving_space_[page_idx].Assign(obj);
                });
      }
      // An empty entry indicates that the page has no live objects and hence
      // can be skipped.
    }
    begin += kPageSize;
    page_idx++;
  }
  non_moving_first_objs_count_ = page_idx;
}

class MarkCompact::ConcurrentCompactionGcTask : public SelfDeletingTask {
 public:
  explicit ConcurrentCompactionGcTask(MarkCompact* collector, size_t idx)
      : collector_(collector), index_(idx) {}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wframe-larger-than="
  void Run(Thread* self ATTRIBUTE_UNUSED) override REQUIRES_SHARED(Locks::mutator_lock_) {
    if (collector_->minor_fault_initialized_) {
      collector_->ConcurrentCompaction<MarkCompact::kMinorFaultMode>(/*buf=*/nullptr);
    } else if (kObjPtrPoisoning) {
      // The passed page/buf to ConcurrentCompaction is used by the thread as a
      // kPageSize buffer for compacting and updating objects into and then
      // passing the buf to uffd ioctls.
      uint8_t* buf = collector_->compaction_buffers_map_.Begin() + index_ * kPageSize;
      collector_->ConcurrentCompaction<MarkCompact::kCopyMode>(buf);
    } else {
      uint8_t buf[kPageSize];
      collector_->ConcurrentCompaction<MarkCompact::kCopyMode>(buf);
    }
  }
#pragma clang diagnostic pop

 private:
  MarkCompact* const collector_;
  size_t index_;
};

void MarkCompact::PrepareForCompaction() {
  uint8_t* space_begin = bump_pointer_space_->Begin();
  size_t vector_len = (black_allocations_begin_ - space_begin) / kOffsetChunkSize;
  DCHECK_LE(vector_len, vector_length_);
  for (size_t i = 0; i < vector_len; i++) {
    DCHECK_LE(chunk_info_vec_[i], kOffsetChunkSize);
    DCHECK_EQ(chunk_info_vec_[i], live_words_bitmap_->LiveBytesInBitmapWord(i));
  }
  InitMovingSpaceFirstObjects(vector_len);
  InitNonMovingSpaceFirstObjects();

  // TODO: We can do a lot of neat tricks with this offset vector to tune the
  // compaction as we wish. Originally, the compaction algorithm slides all
  // live objects towards the beginning of the heap. This is nice because it
  // keeps the spatial locality of objects intact.
  // However, sometimes it's desired to compact objects in certain portions
  // of the heap. For instance, it is expected that, over time,
  // objects towards the beginning of the heap are long lived and are always
  // densely packed. In this case, it makes sense to only update references in
  // there and not try to compact it.
  // Furthermore, we might have some large objects and may not want to move such
  // objects.
  // We can adjust, without too much effort, the values in the chunk_info_vec_ such
  // that the objects in the dense beginning area aren't moved. OTOH, large
  // objects, which could be anywhere in the heap, could also be kept from
  // moving by using a similar trick. The only issue is that by doing this we will
  // leave an unused hole in the middle of the heap which can't be used for
  // allocations until we do a *full* compaction.
  //
  // At this point every element in the chunk_info_vec_ contains the live-bytes
  // of the corresponding chunk. For old-to-new address computation we need
  // every element to reflect total live-bytes till the corresponding chunk.

  // Live-bytes count is required to compute post_compact_end_ below.
  uint32_t total;
  // Update the vector one past the heap usage as it is required for black
  // allocated objects' post-compact address computation.
  if (vector_len < vector_length_) {
    vector_len++;
    total = 0;
  } else {
    // Fetch the value stored in the last element before it gets overwritten by
    // std::exclusive_scan().
    total = chunk_info_vec_[vector_len - 1];
  }
  std::exclusive_scan(chunk_info_vec_, chunk_info_vec_ + vector_len, chunk_info_vec_, 0);
  total += chunk_info_vec_[vector_len - 1];

  for (size_t i = vector_len; i < vector_length_; i++) {
    DCHECK_EQ(chunk_info_vec_[i], 0u);
  }
  post_compact_end_ = AlignUp(space_begin + total, kPageSize);
  CHECK_EQ(post_compact_end_, space_begin + moving_first_objs_count_ * kPageSize);
  black_objs_slide_diff_ = black_allocations_begin_ - post_compact_end_;
  // How do we handle compaction of heap portion used for allocations after the
  // marking-pause?
  // All allocations after the marking-pause are considered black (reachable)
  // for this GC cycle. However, they need not be allocated contiguously as
  // different mutators use TLABs. So we will compact the heap till the point
  // where allocations took place before the marking-pause. And everything after
  // that will be slid with TLAB holes, and then TLAB info in TLS will be
  // appropriately updated in the pre-compaction pause.
  // The chunk-info vector entries for the post marking-pause allocations will be
  // also updated in the pre-compaction pause.

  if (!uffd_initialized_ && CreateUserfaultfd(/*post_fork*/false)) {
    // Register the buffer that we use for terminating concurrent compaction
    struct uffdio_register uffd_register;
    uffd_register.range.start = reinterpret_cast<uintptr_t>(conc_compaction_termination_page_);
    uffd_register.range.len = kPageSize;
    uffd_register.mode = UFFDIO_REGISTER_MODE_MISSING;
    CHECK_EQ(ioctl(uffd_, UFFDIO_REGISTER, &uffd_register), 0)
          << "ioctl_userfaultfd: register compaction termination page: " << strerror(errno);
  }
  // For zygote we create the thread pool each time before starting compaction,
  // and get rid of it when finished. This is expected to happen rarely as
  // zygote spends most of the time in native fork loop.
  if (uffd_ != kFallbackMode) {
    ThreadPool* pool = heap_->GetThreadPool();
    if (UNLIKELY(pool == nullptr)) {
      heap_->CreateThreadPool(kNumUffdWorkers);
      pool = heap_->GetThreadPool();
    }
    const size_t num_threads = pool->GetThreadCount();
    DCHECK_EQ(kNumUffdWorkers, num_threads);
    thread_pool_counter_ = num_threads;
    for (size_t i = 0; i < num_threads; i++) {
      pool->AddTask(thread_running_gc_, new ConcurrentCompactionGcTask(this, i + 1));
    }
    CHECK_EQ(pool->GetTaskCount(thread_running_gc_), num_threads);
    if (minor_fault_initialized_) {
      DCHECK(!Runtime::Current()->IsZygote());
      int truncate = ftruncate(moving_space_fds_[0], shadow_to_space_map_.Size());
      DCHECK_EQ(truncate, 0) << "ftruncate for moving-space failed:" << strerror(errno);
      void* ret = mmap(shadow_to_space_map_.Begin(),
                       shadow_to_space_map_.Size(),
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_FIXED,
                       moving_space_fds_[0],
                       0);
      DCHECK_NE(ret, MAP_FAILED) << "mmap for moving-space shadow failed:" << strerror(errno);
    } else if (!Runtime::Current()->IsZygote() && uffd_minor_fault_supported_) {
      // First GC after zygote-fork. We will still use uffd's copy mode but will
      // use it to move objects to memfd (to prepare for subsequent GCs, which
      // will use uffd's  minor-fault feature).
      DCHECK_EQ(moving_space_fds_[0], -1);
      DCHECK_EQ(moving_space_fds_[1], -1);
      size_t size = bump_pointer_space_->Capacity();
      const char* name = bump_pointer_space_->GetName();
      moving_space_fds_[0] = memfd_create(name, MFD_CLOEXEC);
      CHECK_NE(moving_space_fds_[0], -1)
          << "memfd_create: failed for " << name << ": " << strerror(errno);
      moving_space_fds_[1] = memfd_create(name, MFD_CLOEXEC);
      CHECK_NE(moving_space_fds_[1], -1)
          << "memfd_create: failed for " << name << ": " << strerror(errno);

      std::string err_msg;
      // This map doesn't have to be aligned to 2MB as we don't mremap on it.
      shadow_to_space_map_ = MemMap::MapAnonymous("Concurrent mark-compact moving-space shadow",
                                                  size,
                                                  PROT_NONE,
                                                  /*low_4gb=*/ kObjPtrPoisoning,
                                                  &err_msg);
      if (UNLIKELY(!shadow_to_space_map_.IsValid())) {
        LOG(FATAL) << "Failed to allocate concurrent mark-compact moving-space shadow" << err_msg;
      }

      // moving-space will map this fd so that we compact objects into it.
      int ret = ftruncate(moving_space_fds_[0], size);
      CHECK_EQ(ret, 0) << "ftruncate failed for moving-space:" << strerror(errno);

      // ftruncate linear-alloc spaces, if required.
      for (auto& data : linear_alloc_spaces_data_) {
        if (data.fd_ == -1) {
          data.fd_ = memfd_create("linear-alloc space", MFD_CLOEXEC);
          CHECK_NE(data.fd_, -1)
              << "memfd_create: failed for linear-alloc space: " << strerror(errno);
          ret = ftruncate(data.fd_, data.end_ - data.begin_);
          CHECK_EQ(ret, 0) << "ftruncate failed for linear-alloc space: " << strerror(errno);
        }
      }
    }
  }
}

class MarkCompact::VerifyRootMarkedVisitor : public SingleRootVisitor {
 public:
  explicit VerifyRootMarkedVisitor(MarkCompact* collector) : collector_(collector) { }

  void VisitRoot(mirror::Object* root, const RootInfo& info) override
      REQUIRES_SHARED(Locks::mutator_lock_, Locks::heap_bitmap_lock_) {
    CHECK(collector_->IsMarked(root) != nullptr) << info.ToString();
  }

 private:
  MarkCompact* const collector_;
};

void MarkCompact::ReMarkRoots(Runtime* runtime) {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  DCHECK_EQ(thread_running_gc_, Thread::Current());
  Locks::mutator_lock_->AssertExclusiveHeld(thread_running_gc_);
  MarkNonThreadRoots(runtime);
  MarkConcurrentRoots(static_cast<VisitRootFlags>(kVisitRootFlagNewRoots
                                                  | kVisitRootFlagStopLoggingNewRoots
                                                  | kVisitRootFlagClearRootLog),
                      runtime);

  if (kVerifyRootsMarked) {
    TimingLogger::ScopedTiming t2("(Paused)VerifyRoots", GetTimings());
    VerifyRootMarkedVisitor visitor(this);
    runtime->VisitRoots(&visitor);
  }
}

void MarkCompact::MarkingPause() {
  TimingLogger::ScopedTiming t("(Paused)MarkingPause", GetTimings());
  Runtime* runtime = Runtime::Current();
  Locks::mutator_lock_->AssertExclusiveHeld(thread_running_gc_);
  {
    // Handle the dirty objects as we are a concurrent GC
    WriterMutexLock mu(thread_running_gc_, *Locks::heap_bitmap_lock_);
    {
      MutexLock mu2(thread_running_gc_, *Locks::runtime_shutdown_lock_);
      MutexLock mu3(thread_running_gc_, *Locks::thread_list_lock_);
      std::list<Thread*> thread_list = runtime->GetThreadList()->GetList();
      for (Thread* thread : thread_list) {
        thread->VisitRoots(this, static_cast<VisitRootFlags>(0));
        // Need to revoke all the thread-local allocation stacks since we will
        // swap the allocation stacks (below) and don't want anybody to allocate
        // into the live stack.
        thread->RevokeThreadLocalAllocationStack();
        bump_pointer_space_->RevokeThreadLocalBuffers(thread);
      }
    }
    // Re-mark root set. Doesn't include thread-roots as they are already marked
    // above.
    ReMarkRoots(runtime);
    // Scan dirty objects.
    RecursiveMarkDirtyObjects(/*paused*/ true, accounting::CardTable::kCardDirty);
    {
      TimingLogger::ScopedTiming t2("SwapStacks", GetTimings());
      heap_->SwapStacks();
      live_stack_freeze_size_ = heap_->GetLiveStack()->Size();
    }
  }
  // Fetch only the accumulated objects-allocated count as it is guaranteed to
  // be up-to-date after the TLAB revocation above.
  freed_objects_ += bump_pointer_space_->GetAccumulatedObjectsAllocated();
  // TODO: For PreSweepingGcVerification(), find correct strategy to visit/walk
  // objects in bump-pointer space when we have a mark-bitmap to indicate live
  // objects. At the same time we also need to be able to visit black allocations,
  // even though they are not marked in the bitmap. Without both of these we fail
  // pre-sweeping verification. As well as we leave windows open wherein a
  // VisitObjects/Walk on the space would either miss some objects or visit
  // unreachable ones. These windows are when we are switching from shared
  // mutator-lock to exclusive and vice-versa starting from here till compaction pause.
  // heap_->PreSweepingGcVerification(this);

  // Disallow new system weaks to prevent a race which occurs when someone adds
  // a new system weak before we sweep them. Since this new system weak may not
  // be marked, the GC may incorrectly sweep it. This also fixes a race where
  // interning may attempt to return a strong reference to a string that is
  // about to be swept.
  runtime->DisallowNewSystemWeaks();
  // Enable the reference processing slow path, needs to be done with mutators
  // paused since there is no lock in the GetReferent fast path.
  heap_->GetReferenceProcessor()->EnableSlowPath();

  // Capture 'end' of moving-space at this point. Every allocation beyond this
  // point will be considered as black.
  // Align-up to page boundary so that black allocations happen from next page
  // onwards.
  black_allocations_begin_ = bump_pointer_space_->AlignEnd(thread_running_gc_, kPageSize);
  DCHECK(IsAligned<kAlignment>(black_allocations_begin_));
  black_allocations_begin_ = AlignUp(black_allocations_begin_, kPageSize);
}

void MarkCompact::SweepSystemWeaks(Thread* self, Runtime* runtime, const bool paused) {
  TimingLogger::ScopedTiming t(paused ? "(Paused)SweepSystemWeaks" : "SweepSystemWeaks",
                               GetTimings());
  ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
  runtime->SweepSystemWeaks(this);
}

void MarkCompact::ProcessReferences(Thread* self) {
  WriterMutexLock mu(self, *Locks::heap_bitmap_lock_);
  GetHeap()->GetReferenceProcessor()->ProcessReferences(self, GetTimings());
}

void MarkCompact::Sweep(bool swap_bitmaps) {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  // Ensure that nobody inserted objects in the live stack after we swapped the
  // stacks.
  CHECK_GE(live_stack_freeze_size_, GetHeap()->GetLiveStack()->Size());
  {
    TimingLogger::ScopedTiming t2("MarkAllocStackAsLive", GetTimings());
    // Mark everything allocated since the last GC as live so that we can sweep
    // concurrently, knowing that new allocations won't be marked as live.
    accounting::ObjectStack* live_stack = heap_->GetLiveStack();
    heap_->MarkAllocStackAsLive(live_stack);
    live_stack->Reset();
    DCHECK(mark_stack_->IsEmpty());
  }
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->IsContinuousMemMapAllocSpace() && space != bump_pointer_space_) {
      space::ContinuousMemMapAllocSpace* alloc_space = space->AsContinuousMemMapAllocSpace();
      TimingLogger::ScopedTiming split(
          alloc_space->IsZygoteSpace() ? "SweepZygoteSpace" : "SweepMallocSpace",
          GetTimings());
      RecordFree(alloc_space->Sweep(swap_bitmaps));
    }
  }
  SweepLargeObjects(swap_bitmaps);
}

void MarkCompact::SweepLargeObjects(bool swap_bitmaps) {
  space::LargeObjectSpace* los = heap_->GetLargeObjectsSpace();
  if (los != nullptr) {
    TimingLogger::ScopedTiming split(__FUNCTION__, GetTimings());
    RecordFreeLOS(los->Sweep(swap_bitmaps));
  }
}

void MarkCompact::ReclaimPhase() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  DCHECK(thread_running_gc_ == Thread::Current());
  Runtime* const runtime = Runtime::Current();
  // Process the references concurrently.
  ProcessReferences(thread_running_gc_);
  // TODO: Try to merge this system-weak sweeping with the one while updating
  // references during the compaction pause.
  SweepSystemWeaks(thread_running_gc_, runtime, /*paused*/ false);
  runtime->AllowNewSystemWeaks();
  // Clean up class loaders after system weaks are swept since that is how we know if class
  // unloading occurred.
  runtime->GetClassLinker()->CleanupClassLoaders();
  {
    WriterMutexLock mu(thread_running_gc_, *Locks::heap_bitmap_lock_);
    // Reclaim unmarked objects.
    Sweep(false);
    // Swap the live and mark bitmaps for each space which we modified space. This is an
    // optimization that enables us to not clear live bits inside of the sweep. Only swaps unbound
    // bitmaps.
    SwapBitmaps();
    // Unbind the live and mark bitmaps.
    GetHeap()->UnBindBitmaps();
  }
}

// We want to avoid checking for every reference if it's within the page or
// not. This can be done if we know where in the page the holder object lies.
// If it doesn't overlap either boundaries then we can skip the checks.
template <bool kCheckBegin, bool kCheckEnd>
class MarkCompact::RefsUpdateVisitor {
 public:
  explicit RefsUpdateVisitor(MarkCompact* collector,
                             mirror::Object* obj,
                             uint8_t* begin,
                             uint8_t* end)
      : collector_(collector), obj_(obj), begin_(begin), end_(end) {
    DCHECK(!kCheckBegin || begin != nullptr);
    DCHECK(!kCheckEnd || end != nullptr);
  }

  void operator()(mirror::Object* old ATTRIBUTE_UNUSED, MemberOffset offset, bool /* is_static */)
      const ALWAYS_INLINE REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES_SHARED(Locks::heap_bitmap_lock_) {
    bool update = true;
    if (kCheckBegin || kCheckEnd) {
      uint8_t* ref = reinterpret_cast<uint8_t*>(obj_) + offset.Int32Value();
      update = (!kCheckBegin || ref >= begin_) && (!kCheckEnd || ref < end_);
    }
    if (update) {
      collector_->UpdateRef(obj_, offset);
    }
  }

  // For object arrays we don't need to check boundaries here as it's done in
  // VisitReferenes().
  // TODO: Optimize reference updating using SIMD instructions. Object arrays
  // are perfect as all references are tightly packed.
  void operator()(mirror::Object* old ATTRIBUTE_UNUSED,
                  MemberOffset offset,
                  bool /*is_static*/,
                  bool /*is_obj_array*/)
      const ALWAYS_INLINE REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES_SHARED(Locks::heap_bitmap_lock_) {
    collector_->UpdateRef(obj_, offset);
  }

  void VisitRootIfNonNull(mirror::CompressedReference<mirror::Object>* root) const
      ALWAYS_INLINE
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (!root->IsNull()) {
      VisitRoot(root);
    }
  }

  void VisitRoot(mirror::CompressedReference<mirror::Object>* root) const
      ALWAYS_INLINE
      REQUIRES_SHARED(Locks::mutator_lock_) {
    collector_->UpdateRoot(root);
  }

 private:
  MarkCompact* const collector_;
  mirror::Object* const obj_;
  uint8_t* const begin_;
  uint8_t* const end_;
};

bool MarkCompact::IsValidObject(mirror::Object* obj) const {
  mirror::Class* klass = obj->GetClass<kVerifyNone, kWithoutReadBarrier>();
  if (!heap_->GetVerification()->IsValidHeapObjectAddress(klass)) {
    return false;
  }
  return heap_->GetVerification()->IsValidClassUnchecked<kWithFromSpaceBarrier>(
          obj->GetClass<kVerifyNone, kWithFromSpaceBarrier>());
}

template <typename Callback>
void MarkCompact::VerifyObject(mirror::Object* ref, Callback& callback) const {
  if (kIsDebugBuild) {
    mirror::Class* klass = ref->GetClass<kVerifyNone, kWithFromSpaceBarrier>();
    mirror::Class* pre_compact_klass = ref->GetClass<kVerifyNone, kWithoutReadBarrier>();
    mirror::Class* klass_klass = klass->GetClass<kVerifyNone, kWithFromSpaceBarrier>();
    mirror::Class* klass_klass_klass = klass_klass->GetClass<kVerifyNone, kWithFromSpaceBarrier>();
    if (bump_pointer_space_->HasAddress(pre_compact_klass) &&
        reinterpret_cast<uint8_t*>(pre_compact_klass) < black_allocations_begin_) {
      CHECK(moving_space_bitmap_->Test(pre_compact_klass))
          << "ref=" << ref
          << " post_compact_end=" << static_cast<void*>(post_compact_end_)
          << " pre_compact_klass=" << pre_compact_klass
          << " black_allocations_begin=" << static_cast<void*>(black_allocations_begin_);
      CHECK(live_words_bitmap_->Test(pre_compact_klass));
    }
    if (!IsValidObject(ref)) {
      std::ostringstream oss;
      oss << "Invalid object: "
          << "ref=" << ref
          << " klass=" << klass
          << " klass_klass=" << klass_klass
          << " klass_klass_klass=" << klass_klass_klass
          << " pre_compact_klass=" << pre_compact_klass
          << " from_space_begin=" << static_cast<void*>(from_space_begin_)
          << " pre_compact_begin=" << static_cast<void*>(bump_pointer_space_->Begin())
          << " post_compact_end=" << static_cast<void*>(post_compact_end_)
          << " black_allocations_begin=" << static_cast<void*>(black_allocations_begin_);

      // Call callback before dumping larger data like RAM and space dumps.
      callback(oss);

      oss << " \nobject="
          << heap_->GetVerification()->DumpRAMAroundAddress(reinterpret_cast<uintptr_t>(ref), 128)
          << " \nklass(from)="
          << heap_->GetVerification()->DumpRAMAroundAddress(reinterpret_cast<uintptr_t>(klass), 128)
          << "spaces:\n";
      heap_->DumpSpaces(oss);
      LOG(FATAL) << oss.str();
    }
  }
}

template <bool kNeedMemsetZero>
void MarkCompact::CompactPage(mirror::Object* obj, uint32_t offset, uint8_t* addr) {
  DCHECK(moving_space_bitmap_->Test(obj)
         && live_words_bitmap_->Test(obj));
  DCHECK(live_words_bitmap_->Test(offset)) << "obj=" << obj
                                           << " offset=" << offset
                                           << " addr=" << static_cast<void*>(addr)
                                           << " black_allocs_begin="
                                           << static_cast<void*>(black_allocations_begin_)
                                           << " post_compact_addr="
                                           << static_cast<void*>(post_compact_end_);
  uint8_t* const start_addr = addr;
  // How many distinct live-strides do we have.
  size_t stride_count = 0;
  uint8_t* last_stride = addr;
  uint32_t last_stride_begin = 0;
  auto verify_obj_callback = [&] (std::ostream& os) {
                               os << " stride_count=" << stride_count
                                  << " last_stride=" << static_cast<void*>(last_stride)
                                  << " offset=" << offset
                                  << " start_addr=" << static_cast<void*>(start_addr);
                             };
  obj = GetFromSpaceAddr(obj);
  live_words_bitmap_->VisitLiveStrides(offset,
                                       black_allocations_begin_,
                                       kPageSize,
                                       [&addr,
                                        &last_stride,
                                        &stride_count,
                                        &last_stride_begin,
                                        verify_obj_callback,
                                        this] (uint32_t stride_begin,
                                               size_t stride_size,
                                               bool /*is_last*/)
                                        REQUIRES_SHARED(Locks::mutator_lock_) {
                                         const size_t stride_in_bytes = stride_size * kAlignment;
                                         DCHECK_LE(stride_in_bytes, kPageSize);
                                         last_stride_begin = stride_begin;
                                         DCHECK(IsAligned<kAlignment>(addr));
                                         memcpy(addr,
                                                from_space_begin_ + stride_begin * kAlignment,
                                                stride_in_bytes);
                                         if (kIsDebugBuild) {
                                           uint8_t* space_begin = bump_pointer_space_->Begin();
                                           // We can interpret the first word of the stride as an
                                           // obj only from second stride onwards, as the first
                                           // stride's first-object may have started on previous
                                           // page. The only exception is the first page of the
                                           // moving space.
                                           if (stride_count > 0
                                               || stride_begin * kAlignment < kPageSize) {
                                             mirror::Object* o =
                                                reinterpret_cast<mirror::Object*>(space_begin
                                                                                  + stride_begin
                                                                                  * kAlignment);
                                             CHECK(live_words_bitmap_->Test(o)) << "ref=" << o;
                                             CHECK(moving_space_bitmap_->Test(o))
                                                 << "ref=" << o
                                                 << " bitmap: "
                                                 << moving_space_bitmap_->DumpMemAround(o);
                                             VerifyObject(reinterpret_cast<mirror::Object*>(addr),
                                                          verify_obj_callback);
                                           }
                                         }
                                         last_stride = addr;
                                         addr += stride_in_bytes;
                                         stride_count++;
                                       });
  DCHECK_LT(last_stride, start_addr + kPageSize);
  DCHECK_GT(stride_count, 0u);
  size_t obj_size = 0;
  uint32_t offset_within_obj = offset * kAlignment
                               - (reinterpret_cast<uint8_t*>(obj) - from_space_begin_);
  // First object
  if (offset_within_obj > 0) {
    mirror::Object* to_ref = reinterpret_cast<mirror::Object*>(start_addr - offset_within_obj);
    if (stride_count > 1) {
      RefsUpdateVisitor</*kCheckBegin*/true, /*kCheckEnd*/false> visitor(this,
                                                                         to_ref,
                                                                         start_addr,
                                                                         nullptr);
      obj_size = obj->VisitRefsForCompaction</*kFetchObjSize*/true, /*kVisitNativeRoots*/false>(
              visitor, MemberOffset(offset_within_obj), MemberOffset(-1));
    } else {
      RefsUpdateVisitor</*kCheckBegin*/true, /*kCheckEnd*/true> visitor(this,
                                                                        to_ref,
                                                                        start_addr,
                                                                        start_addr + kPageSize);
      obj_size = obj->VisitRefsForCompaction</*kFetchObjSize*/true, /*kVisitNativeRoots*/false>(
              visitor, MemberOffset(offset_within_obj), MemberOffset(offset_within_obj
                                                                     + kPageSize));
    }
    obj_size = RoundUp(obj_size, kAlignment);
    DCHECK_GT(obj_size, offset_within_obj);
    obj_size -= offset_within_obj;
    // If there is only one stride, then adjust last_stride_begin to the
    // end of the first object.
    if (stride_count == 1) {
      last_stride_begin += obj_size / kAlignment;
    }
  }

  // Except for the last page being compacted, the pages will have addr ==
  // start_addr + kPageSize.
  uint8_t* const end_addr = addr;
  addr = start_addr;
  size_t bytes_done = obj_size;
  // All strides except the last one can be updated without any boundary
  // checks.
  DCHECK_LE(addr, last_stride);
  size_t bytes_to_visit = last_stride - addr;
  DCHECK_LE(bytes_to_visit, kPageSize);
  while (bytes_to_visit > bytes_done) {
    mirror::Object* ref = reinterpret_cast<mirror::Object*>(addr + bytes_done);
    VerifyObject(ref, verify_obj_callback);
    RefsUpdateVisitor</*kCheckBegin*/false, /*kCheckEnd*/false>
            visitor(this, ref, nullptr, nullptr);
    obj_size = ref->VisitRefsForCompaction(visitor, MemberOffset(0), MemberOffset(-1));
    obj_size = RoundUp(obj_size, kAlignment);
    bytes_done += obj_size;
  }
  // Last stride may have multiple objects in it and we don't know where the
  // last object which crosses the page boundary starts, therefore check
  // page-end in all of these objects. Also, we need to call
  // VisitRefsForCompaction() with from-space object as we fetch object size,
  // which in case of klass requires 'class_size_'.
  uint8_t* from_addr = from_space_begin_ + last_stride_begin * kAlignment;
  bytes_to_visit = end_addr - addr;
  DCHECK_LE(bytes_to_visit, kPageSize);
  while (bytes_to_visit > bytes_done) {
    mirror::Object* ref = reinterpret_cast<mirror::Object*>(addr + bytes_done);
    obj = reinterpret_cast<mirror::Object*>(from_addr);
    VerifyObject(ref, verify_obj_callback);
    RefsUpdateVisitor</*kCheckBegin*/false, /*kCheckEnd*/true>
            visitor(this, ref, nullptr, start_addr + kPageSize);
    obj_size = obj->VisitRefsForCompaction(visitor,
                                           MemberOffset(0),
                                           MemberOffset(end_addr - (addr + bytes_done)));
    obj_size = RoundUp(obj_size, kAlignment);
    from_addr += obj_size;
    bytes_done += obj_size;
  }
  // The last page that we compact may have some bytes left untouched in the
  // end, we should zero them as the kernel copies at page granularity.
  if (kNeedMemsetZero && UNLIKELY(bytes_done < kPageSize)) {
    std::memset(addr + bytes_done, 0x0, kPageSize - bytes_done);
  }
}

// We store the starting point (pre_compact_page - first_obj) and first-chunk's
// size. If more TLAB(s) started in this page, then those chunks are identified
// using mark bitmap. All this info is prepared in UpdateMovingSpaceBlackAllocations().
// If we find a set bit in the bitmap, then we copy the remaining page and then
// use the bitmap to visit each object for updating references.
template <bool kNeedMemsetZero>
void MarkCompact::SlideBlackPage(mirror::Object* first_obj,
                                 const size_t page_idx,
                                 uint8_t* const pre_compact_page,
                                 uint8_t* dest) {
  DCHECK(IsAligned<kPageSize>(pre_compact_page));
  size_t bytes_copied;
  const uint32_t first_chunk_size = black_alloc_pages_first_chunk_size_[page_idx];
  mirror::Object* next_page_first_obj = first_objs_moving_space_[page_idx + 1].AsMirrorPtr();
  uint8_t* src_addr = reinterpret_cast<uint8_t*>(GetFromSpaceAddr(first_obj));
  uint8_t* pre_compact_addr = reinterpret_cast<uint8_t*>(first_obj);
  uint8_t* const pre_compact_page_end = pre_compact_page + kPageSize;
  uint8_t* const dest_page_end = dest + kPageSize;

  auto verify_obj_callback = [&] (std::ostream& os) {
                               os << " first_obj=" << first_obj
                                  << " next_page_first_obj=" << next_page_first_obj
                                  << " first_chunk_sie=" << first_chunk_size
                                  << " dest=" << static_cast<void*>(dest)
                                  << " pre_compact_page="
                                  << static_cast<void* const>(pre_compact_page);
                             };
  // We have empty portion at the beginning of the page. Zero it.
  if (pre_compact_addr > pre_compact_page) {
    bytes_copied = pre_compact_addr - pre_compact_page;
    DCHECK_LT(bytes_copied, kPageSize);
    if (kNeedMemsetZero) {
      std::memset(dest, 0x0, bytes_copied);
    }
    dest += bytes_copied;
  } else {
    bytes_copied = 0;
    size_t offset = pre_compact_page - pre_compact_addr;
    pre_compact_addr = pre_compact_page;
    src_addr += offset;
    DCHECK(IsAligned<kPageSize>(src_addr));
  }
  // Copy the first chunk of live words
  std::memcpy(dest, src_addr, first_chunk_size);
  // Update references in the first chunk. Use object size to find next object.
  {
    size_t bytes_to_visit = first_chunk_size;
    size_t obj_size;
    // The first object started in some previous page. So we need to check the
    // beginning.
    DCHECK_LE(reinterpret_cast<uint8_t*>(first_obj), pre_compact_addr);
    size_t offset = pre_compact_addr - reinterpret_cast<uint8_t*>(first_obj);
    if (bytes_copied == 0 && offset > 0) {
      mirror::Object* to_obj = reinterpret_cast<mirror::Object*>(dest - offset);
      mirror::Object* from_obj = reinterpret_cast<mirror::Object*>(src_addr - offset);
      // If the next page's first-obj is in this page or nullptr, then we don't
      // need to check end boundary
      if (next_page_first_obj == nullptr
          || (first_obj != next_page_first_obj
              && reinterpret_cast<uint8_t*>(next_page_first_obj) <= pre_compact_page_end)) {
        RefsUpdateVisitor</*kCheckBegin*/true, /*kCheckEnd*/false> visitor(this,
                                                                           to_obj,
                                                                           dest,
                                                                           nullptr);
        obj_size = from_obj->VisitRefsForCompaction<
                /*kFetchObjSize*/true, /*kVisitNativeRoots*/false>(visitor,
                                                                   MemberOffset(offset),
                                                                   MemberOffset(-1));
      } else {
        RefsUpdateVisitor</*kCheckBegin*/true, /*kCheckEnd*/true> visitor(this,
                                                                          to_obj,
                                                                          dest,
                                                                          dest_page_end);
        from_obj->VisitRefsForCompaction<
                /*kFetchObjSize*/false, /*kVisitNativeRoots*/false>(visitor,
                                                                    MemberOffset(offset),
                                                                    MemberOffset(offset
                                                                                 + kPageSize));
        return;
      }
      obj_size = RoundUp(obj_size, kAlignment);
      obj_size -= offset;
      dest += obj_size;
      bytes_to_visit -= obj_size;
    }
    bytes_copied += first_chunk_size;
    // If the last object in this page is next_page_first_obj, then we need to check end boundary
    bool check_last_obj = false;
    if (next_page_first_obj != nullptr
        && reinterpret_cast<uint8_t*>(next_page_first_obj) < pre_compact_page_end
        && bytes_copied == kPageSize) {
      size_t diff = pre_compact_page_end - reinterpret_cast<uint8_t*>(next_page_first_obj);
      DCHECK_LE(diff, kPageSize);
      DCHECK_LE(diff, bytes_to_visit);
      bytes_to_visit -= diff;
      check_last_obj = true;
    }
    while (bytes_to_visit > 0) {
      mirror::Object* dest_obj = reinterpret_cast<mirror::Object*>(dest);
      VerifyObject(dest_obj, verify_obj_callback);
      RefsUpdateVisitor</*kCheckBegin*/false, /*kCheckEnd*/false> visitor(this,
                                                                          dest_obj,
                                                                          nullptr,
                                                                          nullptr);
      obj_size = dest_obj->VisitRefsForCompaction(visitor, MemberOffset(0), MemberOffset(-1));
      obj_size = RoundUp(obj_size, kAlignment);
      bytes_to_visit -= obj_size;
      dest += obj_size;
    }
    DCHECK_EQ(bytes_to_visit, 0u);
    if (check_last_obj) {
      mirror::Object* dest_obj = reinterpret_cast<mirror::Object*>(dest);
      VerifyObject(dest_obj, verify_obj_callback);
      RefsUpdateVisitor</*kCheckBegin*/false, /*kCheckEnd*/true> visitor(this,
                                                                         dest_obj,
                                                                         nullptr,
                                                                         dest_page_end);
      mirror::Object* obj = GetFromSpaceAddr(next_page_first_obj);
      obj->VisitRefsForCompaction</*kFetchObjSize*/false>(visitor,
                                                          MemberOffset(0),
                                                          MemberOffset(dest_page_end - dest));
      return;
    }
  }

  // Probably a TLAB finished on this page and/or a new TLAB started as well.
  if (bytes_copied < kPageSize) {
    src_addr += first_chunk_size;
    pre_compact_addr += first_chunk_size;
    // Use mark-bitmap to identify where objects are. First call
    // VisitMarkedRange for only the first marked bit. If found, zero all bytes
    // until that object and then call memcpy on the rest of the page.
    // Then call VisitMarkedRange for all marked bits *after* the one found in
    // this invocation. This time to visit references.
    uintptr_t start_visit = reinterpret_cast<uintptr_t>(pre_compact_addr);
    uintptr_t page_end = reinterpret_cast<uintptr_t>(pre_compact_page_end);
    mirror::Object* found_obj = nullptr;
    moving_space_bitmap_->VisitMarkedRange</*kVisitOnce*/true>(start_visit,
                                                                page_end,
                                                                [&found_obj](mirror::Object* obj) {
                                                                  found_obj = obj;
                                                                });
    size_t remaining_bytes = kPageSize - bytes_copied;
    if (found_obj == nullptr) {
      if (kNeedMemsetZero) {
        // No more black objects in this page. Zero the remaining bytes and return.
        std::memset(dest, 0x0, remaining_bytes);
      }
      return;
    }
    // Copy everything in this page, which includes any zeroed regions
    // in-between.
    std::memcpy(dest, src_addr, remaining_bytes);
    DCHECK_LT(reinterpret_cast<uintptr_t>(found_obj), page_end);
    moving_space_bitmap_->VisitMarkedRange(
            reinterpret_cast<uintptr_t>(found_obj) + mirror::kObjectHeaderSize,
            page_end,
            [&found_obj, pre_compact_addr, dest, this, verify_obj_callback] (mirror::Object* obj)
            REQUIRES_SHARED(Locks::mutator_lock_) {
              ptrdiff_t diff = reinterpret_cast<uint8_t*>(found_obj) - pre_compact_addr;
              mirror::Object* ref = reinterpret_cast<mirror::Object*>(dest + diff);
              VerifyObject(ref, verify_obj_callback);
              RefsUpdateVisitor</*kCheckBegin*/false, /*kCheckEnd*/false>
                      visitor(this, ref, nullptr, nullptr);
              ref->VisitRefsForCompaction</*kFetchObjSize*/false>(visitor,
                                                                  MemberOffset(0),
                                                                  MemberOffset(-1));
              // Remember for next round.
              found_obj = obj;
            });
    // found_obj may have been updated in VisitMarkedRange. Visit the last found
    // object.
    DCHECK_GT(reinterpret_cast<uint8_t*>(found_obj), pre_compact_addr);
    DCHECK_LT(reinterpret_cast<uintptr_t>(found_obj), page_end);
    ptrdiff_t diff = reinterpret_cast<uint8_t*>(found_obj) - pre_compact_addr;
    mirror::Object* ref = reinterpret_cast<mirror::Object*>(dest + diff);
    VerifyObject(ref, verify_obj_callback);
    RefsUpdateVisitor</*kCheckBegin*/false, /*kCheckEnd*/true> visitor(this,
                                                                       ref,
                                                                       nullptr,
                                                                       dest_page_end);
    ref->VisitRefsForCompaction</*kFetchObjSize*/false>(
            visitor, MemberOffset(0), MemberOffset(page_end -
                                                   reinterpret_cast<uintptr_t>(found_obj)));
  }
}

template <bool kFirstPageMapping>
void MarkCompact::MapProcessedPages(uint8_t* to_space_start,
                                    Atomic<PageState>* state_arr,
                                    size_t arr_idx,
                                    size_t arr_len) {
  DCHECK(minor_fault_initialized_);
  DCHECK_LT(arr_idx, arr_len);
  DCHECK_ALIGNED(to_space_start, kPageSize);
  auto map_ioctl = [this] (void* dst, size_t len) {
                      DCHECK_ALIGNED(len, kPageSize);
                      struct uffdio_continue uffd_continue;
                      uffd_continue.range.start = reinterpret_cast<uintptr_t>(dst);
                      uffd_continue.range.len = len;
                      uffd_continue.mode = 0;
                      int ret = ioctl(uffd_, UFFDIO_CONTINUE, &uffd_continue);
                      // We may receive ENOENT if gc-thread unregisters the
                      // range behind our back, which is fine because that
                      // happens only when it knows compaction is done.
                      CHECK(ret == 0 || errno == ENOENT)
                            << "ioctl: continue failed: " << strerror(errno);
                      if (ret == 0) {
                        DCHECK_EQ(uffd_continue.mapped, static_cast<ssize_t>(len));
                      }
                    };
  // Claim all the contiguous pages, which are ready to be mapped, and then do
  // so in a single ioctl. This helps avoid the overhead of invoking syscall
  // several times and also maps the already-processed pages, avoiding
  // unnecessary faults on them.
  size_t length = kFirstPageMapping ? kPageSize : 0;
  if (kFirstPageMapping) {
    arr_idx++;
  }
  PageState expected_state = PageState::kProcessed;
  // TODO: Are these CAS operations
  // automatically ordered w.r.t. each other? We need to guarantee that we don't
  // end up sucsessfully marking a later page 'mapping' and then fail to mark an
  // earlier page. Can we guarantee that with 'relaxed' order?
  for ( ; arr_idx < arr_len; arr_idx++, length += kPageSize) {
    if (!state_arr[arr_idx].compare_exchange_strong(expected_state,
                                                    PageState::kProcessedAndMapping,
                                                    std::memory_order_acq_rel)) {
      DCHECK_GT(static_cast<uint8_t>(expected_state), static_cast<uint8_t>(PageState::kProcessed));
      break;
    }
  }
  if (length > 0) {
    // Note: to_space_start must be the first page because the ioctl can bail
    // out in the middle, not attempting to map the subsequent pages. We need
    // the first page to be attempted (to map) no matter what.
    map_ioctl(to_space_start, length);
  }
}

template <int kMode>
void MarkCompact::CompactMovingSpace(uint8_t* page) {
  // For every page we have a starting object, which may have started in some
  // preceding page, and an offset within that object from where we must start
  // copying.
  // Consult the live-words bitmap to copy all contiguously live words at a
  // time. These words may constitute multiple objects. To avoid the need for
  // consulting mark-bitmap to find where does the next live object start, we
  // use the object-size returned by VisitRefsForCompaction.
  //
  // We do the compaction in reverse direction so that the pages containing
  // TLAB and latest allocations are processed first.
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  auto copy_ioctl = [this] (void* dst, void* buffer) {
                      struct uffdio_copy uffd_copy;
                      uffd_copy.src = reinterpret_cast<uintptr_t>(buffer);
                      uffd_copy.dst = reinterpret_cast<uintptr_t>(dst);
                      uffd_copy.len = kPageSize;
                      uffd_copy.mode = 0;
                      CHECK_EQ(ioctl(uffd_, UFFDIO_COPY, &uffd_copy), 0)
                            << "ioctl: copy " << strerror(errno);
                      DCHECK_EQ(uffd_copy.copy, static_cast<ssize_t>(kPageSize));
                    };
  uint8_t* to_space_end = bump_pointer_space_->Begin()
                          + (moving_first_objs_count_ + black_page_count_) * kPageSize;

  uint8_t* shadow_space_end = nullptr;
  if (kMode == kMinorFaultMode) {
    shadow_space_end = shadow_to_space_map_.Begin()
                       + (moving_first_objs_count_ + black_page_count_) * kPageSize;
  }
  // Allocated-black pages
  size_t page_status_arr_len = moving_first_objs_count_ + black_page_count_;
  size_t idx = page_status_arr_len;
  uint8_t* pre_compact_page = black_allocations_begin_ + (black_page_count_ * kPageSize);
  DCHECK(IsAligned<kPageSize>(pre_compact_page));
  while (idx > moving_first_objs_count_) {
    idx--;
    pre_compact_page -= kPageSize;
    to_space_end -= kPageSize;
    if (kMode == kMinorFaultMode) {
      shadow_space_end -= kPageSize;
      page = shadow_space_end;
    } else if (kMode == kFallbackMode) {
      page = to_space_end;
    }

    mirror::Object* first_obj = first_objs_moving_space_[idx].AsMirrorPtr();
    if (first_obj != nullptr) {
      PageState expected_state = PageState::kUnprocessed;
      PageState desired_state = kMode == kCopyMode
                                ? PageState::kProcessingAndMapping
                                : PageState::kProcessing;
      // Relaxed memory-order is used in copy mode as the subsequent ioctl syscall acts as a fence.
      // In the concurrent case (kMode != kFallbackMode) we need to ensure that the update
      // to moving_spaces_status_[idx] is released before the contents of the page.
      //
      // Acquire order is required to ensure that we don't start writing to
      // shadow space, which is shared, before successfully claiming that we are
      // working on it.
      std::memory_order order = kMode == kCopyMode
                                ? std::memory_order_relaxed
                                : std::memory_order_acquire;
      if (kMode == kFallbackMode
          || moving_pages_status_[idx].compare_exchange_strong(expected_state,
                                                               desired_state,
                                                               order)) {
        DCHECK_GT(black_alloc_pages_first_chunk_size_[idx], 0u);
        SlideBlackPage<kMode == kCopyMode>(first_obj, idx, pre_compact_page, page);
        if (kMode == kCopyMode) {
          copy_ioctl(to_space_end, page);
        } else if (kMode == kMinorFaultMode) {
          expected_state = PageState::kProcessing;
          desired_state = PageState::kProcessed;
          // the CAS needs to be with release order to ensure that stores to the
          // page makes it to memory *before* other threads observe that it's
          // ready to be mapped.
          if (!moving_pages_status_[idx].compare_exchange_strong(expected_state,
                                                                 desired_state,
                                                                 std::memory_order_release)) {
            // Some mutator has requested to map the page after processing it.
            DCHECK_EQ(expected_state, PageState::kProcessingAndMapping);
            MapProcessedPages</*kFirstPageMapping=*/true>(to_space_end,
                                                          moving_pages_status_,
                                                          idx,
                                                          page_status_arr_len);
          }
        }
      } else {
        DCHECK_GT(static_cast<uint8_t>(expected_state),
                  static_cast<uint8_t>(PageState::kProcessed));
      }
    }
  }
  DCHECK_EQ(pre_compact_page, black_allocations_begin_);

  while (idx > 0) {
    idx--;
    to_space_end -= kPageSize;
    if (kMode == kMinorFaultMode) {
      shadow_space_end -= kPageSize;
      page = shadow_space_end;
    } else if (kMode == kFallbackMode) {
      page = to_space_end;
    }
    PageState expected_state = PageState::kUnprocessed;
    PageState desired_state = kMode == kCopyMode
                              ? PageState::kProcessingAndMapping
                              : PageState::kProcessing;
    std::memory_order order = kMode == kCopyMode
                              ? std::memory_order_relaxed
                              : std::memory_order_acquire;
    if (kMode == kFallbackMode
        || moving_pages_status_[idx].compare_exchange_strong(expected_state,
                                                             desired_state,
                                                             order)) {
      CompactPage<kMode == kCopyMode>(first_objs_moving_space_[idx].AsMirrorPtr(),
                                      pre_compact_offset_moving_space_[idx],
                                      page);
      if (kMode == kCopyMode) {
        copy_ioctl(to_space_end, page);
      } else if (kMode == kMinorFaultMode) {
        expected_state = PageState::kProcessing;
        desired_state = PageState::kProcessed;
        // the CAS needs to be with release order to ensure that stores to the
        // page makes it to memory *before* other threads observe that it's
        // ready to be mapped.
        if (!moving_pages_status_[idx].compare_exchange_strong(expected_state,
                                                               desired_state,
                                                               std::memory_order_release)) {
          // Some mutator has requested to map the page after processing it.
          DCHECK_EQ(expected_state, PageState::kProcessingAndMapping);
          MapProcessedPages</*kFirstPageMapping=*/true>(to_space_end,
                                                        moving_pages_status_,
                                                        idx,
                                                        page_status_arr_len);
        }
      }
    }
  }
  DCHECK_EQ(to_space_end, bump_pointer_space_->Begin());
}

void MarkCompact::UpdateNonMovingPage(mirror::Object* first, uint8_t* page) {
  DCHECK_LT(reinterpret_cast<uint8_t*>(first), page + kPageSize);
  // For every object found in the page, visit the previous object. This ensures
  // that we can visit without checking page-end boundary.
  // Call VisitRefsForCompaction with from-space read-barrier as the klass object and
  // super-class loads require it.
  // TODO: Set kVisitNativeRoots to false once we implement concurrent
  // compaction
  mirror::Object* curr_obj = first;
  non_moving_space_bitmap_->VisitMarkedRange(
          reinterpret_cast<uintptr_t>(first) + mirror::kObjectHeaderSize,
          reinterpret_cast<uintptr_t>(page + kPageSize),
          [&](mirror::Object* next_obj) {
            // TODO: Once non-moving space update becomes concurrent, we'll
            // require fetching the from-space address of 'curr_obj' and then call
            // visitor on that.
            if (reinterpret_cast<uint8_t*>(curr_obj) < page) {
              RefsUpdateVisitor</*kCheckBegin*/true, /*kCheckEnd*/false>
                      visitor(this, curr_obj, page, page + kPageSize);
              MemberOffset begin_offset(page - reinterpret_cast<uint8_t*>(curr_obj));
              // Native roots shouldn't be visited as they are done when this
              // object's beginning was visited in the preceding page.
              curr_obj->VisitRefsForCompaction</*kFetchObjSize*/false, /*kVisitNativeRoots*/false>(
                      visitor, begin_offset, MemberOffset(-1));
            } else {
              RefsUpdateVisitor</*kCheckBegin*/false, /*kCheckEnd*/false>
                      visitor(this, curr_obj, page, page + kPageSize);
              curr_obj->VisitRefsForCompaction</*kFetchObjSize*/false>(visitor,
                                                                       MemberOffset(0),
                                                                       MemberOffset(-1));
            }
            curr_obj = next_obj;
          });

  MemberOffset end_offset(page + kPageSize - reinterpret_cast<uint8_t*>(curr_obj));
  if (reinterpret_cast<uint8_t*>(curr_obj) < page) {
    RefsUpdateVisitor</*kCheckBegin*/true, /*kCheckEnd*/true>
            visitor(this, curr_obj, page, page + kPageSize);
    curr_obj->VisitRefsForCompaction</*kFetchObjSize*/false, /*kVisitNativeRoots*/false>(
            visitor, MemberOffset(page - reinterpret_cast<uint8_t*>(curr_obj)), end_offset);
  } else {
    RefsUpdateVisitor</*kCheckBegin*/false, /*kCheckEnd*/true>
            visitor(this, curr_obj, page, page + kPageSize);
    curr_obj->VisitRefsForCompaction</*kFetchObjSize*/false>(visitor, MemberOffset(0), end_offset);
  }
}

void MarkCompact::UpdateNonMovingSpace() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  uint8_t* page = non_moving_space_->Begin();
  for (size_t i = 0; i < non_moving_first_objs_count_; i++) {
    mirror::Object* obj = first_objs_non_moving_space_[i].AsMirrorPtr();
    // null means there are no objects on the page to update references.
    if (obj != nullptr) {
      UpdateNonMovingPage(obj, page);
    }
    page += kPageSize;
  }
}

void MarkCompact::UpdateMovingSpaceBlackAllocations() {
  // For sliding black pages, we need the first-object, which overlaps with the
  // first byte of the page. Additionally, we compute the size of first chunk of
  // black objects. This will suffice for most black pages. Unlike, compaction
  // pages, here we don't need to pre-compute the offset within first-obj from
  // where sliding has to start. That can be calculated using the pre-compact
  // address of the page. Therefore, to save space, we store the first chunk's
  // size in black_alloc_pages_first_chunk_size_ array.
  // For the pages which may have holes after the first chunk, which could happen
  // if a new TLAB starts in the middle of the page, we mark the objects in
  // the mark-bitmap. So, if the first-chunk size is smaller than kPageSize,
  // then we use the mark-bitmap for the remainder of the page.
  uint8_t* const begin = bump_pointer_space_->Begin();
  uint8_t* black_allocs = black_allocations_begin_;
  DCHECK_LE(begin, black_allocs);
  size_t consumed_blocks_count = 0;
  size_t first_block_size;
  // Get the list of all blocks allocated in the bump-pointer space.
  std::vector<size_t>* block_sizes = bump_pointer_space_->GetBlockSizes(thread_running_gc_,
                                                                        &first_block_size);
  DCHECK_LE(first_block_size, (size_t)(black_allocs - begin));
  if (block_sizes != nullptr) {
    size_t black_page_idx = moving_first_objs_count_;
    uint8_t* block_end = begin + first_block_size;
    uint32_t remaining_chunk_size = 0;
    uint32_t first_chunk_size = 0;
    mirror::Object* first_obj = nullptr;
    for (size_t block_size : *block_sizes) {
      block_end += block_size;
      // Skip the blocks that are prior to the black allocations. These will be
      // merged with the main-block later.
      if (black_allocs >= block_end) {
        consumed_blocks_count++;
        continue;
      }
      mirror::Object* obj = reinterpret_cast<mirror::Object*>(black_allocs);
      bool set_mark_bit = remaining_chunk_size > 0;
      // We don't know how many objects are allocated in the current block. When we hit
      // a null assume it's the end. This works as every block is expected to
      // have objects allocated linearly using bump-pointer.
      // BumpPointerSpace::Walk() also works similarly.
      while (black_allocs < block_end
             && obj->GetClass<kDefaultVerifyFlags, kWithoutReadBarrier>() != nullptr) {
        if (first_obj == nullptr) {
          first_obj = obj;
        }
        // We only need the mark-bitmap in the pages wherein a new TLAB starts in
        // the middle of the page.
        if (set_mark_bit) {
          moving_space_bitmap_->Set(obj);
        }
        size_t obj_size = RoundUp(obj->SizeOf(), kAlignment);
        // Handle objects which cross page boundary, including objects larger
        // than page size.
        if (remaining_chunk_size + obj_size >= kPageSize) {
          set_mark_bit = false;
          first_chunk_size += kPageSize - remaining_chunk_size;
          remaining_chunk_size += obj_size;
          // We should not store first-object and remaining_chunk_size if there were
          // unused bytes before this TLAB, in which case we must have already
          // stored the values (below).
          if (black_alloc_pages_first_chunk_size_[black_page_idx] == 0) {
            black_alloc_pages_first_chunk_size_[black_page_idx] = first_chunk_size;
            first_objs_moving_space_[black_page_idx].Assign(first_obj);
          }
          black_page_idx++;
          remaining_chunk_size -= kPageSize;
          // Consume an object larger than page size.
          while (remaining_chunk_size >= kPageSize) {
            black_alloc_pages_first_chunk_size_[black_page_idx] = kPageSize;
            first_objs_moving_space_[black_page_idx].Assign(obj);
            black_page_idx++;
            remaining_chunk_size -= kPageSize;
          }
          first_obj = remaining_chunk_size > 0 ? obj : nullptr;
          first_chunk_size = remaining_chunk_size;
        } else {
          DCHECK_LE(first_chunk_size, remaining_chunk_size);
          first_chunk_size += obj_size;
          remaining_chunk_size += obj_size;
        }
        black_allocs += obj_size;
        obj = reinterpret_cast<mirror::Object*>(black_allocs);
      }
      DCHECK_LE(black_allocs, block_end);
      DCHECK_LT(remaining_chunk_size, kPageSize);
      // consume the unallocated portion of the block
      if (black_allocs < block_end) {
        // first-chunk of the current page ends here. Store it.
        if (first_chunk_size > 0) {
          black_alloc_pages_first_chunk_size_[black_page_idx] = first_chunk_size;
          first_objs_moving_space_[black_page_idx].Assign(first_obj);
          first_chunk_size = 0;
        }
        first_obj = nullptr;
        size_t page_remaining = kPageSize - remaining_chunk_size;
        size_t block_remaining = block_end - black_allocs;
        if (page_remaining <= block_remaining) {
          block_remaining -= page_remaining;
          // current page and the subsequent empty pages in the block
          black_page_idx += 1 + block_remaining / kPageSize;
          remaining_chunk_size = block_remaining % kPageSize;
        } else {
          remaining_chunk_size += block_remaining;
        }
        black_allocs = block_end;
      }
    }
    black_page_count_ = black_page_idx - moving_first_objs_count_;
    delete block_sizes;
  }
  // Update bump-pointer space by consuming all the pre-black blocks into the
  // main one.
  bump_pointer_space_->SetBlockSizes(thread_running_gc_,
                                     post_compact_end_ - begin,
                                     consumed_blocks_count);
}

void MarkCompact::UpdateNonMovingSpaceBlackAllocations() {
  accounting::ObjectStack* stack = heap_->GetAllocationStack();
  const StackReference<mirror::Object>* limit = stack->End();
  uint8_t* const space_begin = non_moving_space_->Begin();
  for (StackReference<mirror::Object>* it = stack->Begin(); it != limit; ++it) {
    mirror::Object* obj = it->AsMirrorPtr();
    if (obj != nullptr && non_moving_space_bitmap_->HasAddress(obj)) {
      non_moving_space_bitmap_->Set(obj);
      // Clear so that we don't try to set the bit again in the next GC-cycle.
      it->Clear();
      size_t idx = (reinterpret_cast<uint8_t*>(obj) - space_begin) / kPageSize;
      uint8_t* page_begin = AlignDown(reinterpret_cast<uint8_t*>(obj), kPageSize);
      mirror::Object* first_obj = first_objs_non_moving_space_[idx].AsMirrorPtr();
      if (first_obj == nullptr
          || (obj < first_obj && reinterpret_cast<uint8_t*>(first_obj) > page_begin)) {
        first_objs_non_moving_space_[idx].Assign(obj);
      }
      mirror::Object* next_page_first_obj = first_objs_non_moving_space_[++idx].AsMirrorPtr();
      uint8_t* next_page_begin = page_begin + kPageSize;
      if (next_page_first_obj == nullptr
          || reinterpret_cast<uint8_t*>(next_page_first_obj) > next_page_begin) {
        size_t obj_size = RoundUp(obj->SizeOf<kDefaultVerifyFlags>(), kAlignment);
        uint8_t* obj_end = reinterpret_cast<uint8_t*>(obj) + obj_size;
        while (next_page_begin < obj_end) {
          first_objs_non_moving_space_[idx++].Assign(obj);
          next_page_begin += kPageSize;
        }
      }
      // update first_objs count in case we went past non_moving_first_objs_count_
      non_moving_first_objs_count_ = std::max(non_moving_first_objs_count_, idx);
    }
  }
}

class MarkCompact::ImmuneSpaceUpdateObjVisitor {
 public:
  explicit ImmuneSpaceUpdateObjVisitor(MarkCompact* collector) : collector_(collector) {}

  ALWAYS_INLINE void operator()(mirror::Object* obj) const REQUIRES(Locks::mutator_lock_) {
    RefsUpdateVisitor</*kCheckBegin*/false, /*kCheckEnd*/false> visitor(collector_,
                                                                        obj,
                                                                        /*begin_*/nullptr,
                                                                        /*end_*/nullptr);
    obj->VisitRefsForCompaction</*kFetchObjSize*/false>(visitor,
                                                        MemberOffset(0),
                                                        MemberOffset(-1));
  }

  static void Callback(mirror::Object* obj, void* arg) REQUIRES(Locks::mutator_lock_) {
    reinterpret_cast<ImmuneSpaceUpdateObjVisitor*>(arg)->operator()(obj);
  }

 private:
  MarkCompact* const collector_;
};

class MarkCompact::ClassLoaderRootsUpdater : public ClassLoaderVisitor {
 public:
  explicit ClassLoaderRootsUpdater(MarkCompact* collector) : collector_(collector) {}

  void Visit(ObjPtr<mirror::ClassLoader> class_loader) override
      REQUIRES_SHARED(Locks::classlinker_classes_lock_, Locks::mutator_lock_) {
    ClassTable* const class_table = class_loader->GetClassTable();
    if (class_table != nullptr) {
      class_table->VisitRoots(
          BufferedRootVisitor</*kBufferSize=*/16>(collector_, RootInfo(RootType::kRootVMInternal)));
    }
  }

 private:
  MarkCompact* collector_;
};

class MarkCompact::LinearAllocPageUpdater {
 public:
  explicit LinearAllocPageUpdater(MarkCompact* collector) : collector_(collector) {}

  void operator()(uint8_t* page_begin, uint8_t* first_obj) const
      ALWAYS_INLINE REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK_ALIGNED(page_begin, kPageSize);
    uint8_t* page_end = page_begin + kPageSize;
    uint32_t obj_size;
    for (uint8_t* byte = first_obj; byte < page_end;) {
      TrackingHeader* header = reinterpret_cast<TrackingHeader*>(byte);
      obj_size = header->GetSize();
      LinearAllocKind kind = header->GetKind();
      if (obj_size == 0) {
        // No more objects in this page to visit.
        DCHECK_EQ(kind, LinearAllocKind::kNoGCRoots);
        break;
      }
      uint8_t* obj = byte + sizeof(TrackingHeader);
      uint8_t* obj_end = byte + obj_size;
      if (header->Is16Aligned()) {
        obj = AlignUp(obj, 16);
      }
      if (UNLIKELY(obj >= page_end)) {
        break;
      }
      VisitObject(kind, obj, std::max(obj, page_begin), std::min(obj_end, page_end));
      if (ArenaAllocator::IsRunningOnMemoryTool()) {
        obj_size += ArenaAllocator::kMemoryToolRedZoneBytes;
      }
      byte += RoundUp(obj_size, LinearAlloc::kAlignment);
    }
  }

  void VisitRootIfNonNull(mirror::CompressedReference<mirror::Object>* root) const
      ALWAYS_INLINE REQUIRES_SHARED(Locks::mutator_lock_) {
    VisitRoot(root);
  }

  void VisitRoot(mirror::CompressedReference<mirror::Object>* root) const
      ALWAYS_INLINE REQUIRES_SHARED(Locks::mutator_lock_) {
    mirror::Object* old_ref = root->AsMirrorPtr();
    DCHECK_NE(old_ref, nullptr);
    if (collector_->live_words_bitmap_->HasAddress(old_ref)) {
      if (reinterpret_cast<uint8_t*>(old_ref) >= collector_->black_allocations_begin_) {
        mirror::Object* new_ref = collector_->PostCompactBlackObjAddr(old_ref);
        root->Assign(new_ref);
      } else if (collector_->live_words_bitmap_->Test(old_ref)) {
        DCHECK(collector_->moving_space_bitmap_->Test(old_ref)) << old_ref;
        mirror::Object* new_ref = collector_->PostCompactOldObjAddr(old_ref);
        root->Assign(new_ref);
      }
    }
  }

 private:
  void VisitObject(LinearAllocKind kind,
                   void* obj,
                   uint8_t* start_boundary,
                   uint8_t* end_boundary) const
      REQUIRES_SHARED(Locks::mutator_lock_) {
    switch (kind) {
      case LinearAllocKind::kNoGCRoots:
        break;
      case LinearAllocKind::kGCRootArray:
        {
          GcRoot<mirror::Object>* root = reinterpret_cast<GcRoot<mirror::Object>*>(start_boundary);
          GcRoot<mirror::Object>* last = reinterpret_cast<GcRoot<mirror::Object>*>(end_boundary);
          for (; root < last; root++) {
            VisitRootIfNonNull(root->AddressWithoutBarrier());
          }
        }
        break;
      case LinearAllocKind::kArtMethodArray:
        {
          LengthPrefixedArray<ArtMethod>* array = static_cast<LengthPrefixedArray<ArtMethod>*>(obj);
          // Old methods are clobbered in debug builds. Check size to confirm if the array
          // has any GC roots to visit. See ClassLinker::LinkMethodsHelper::ClobberOldMethods()
          if (array->size() > 0) {
            if (collector_->pointer_size_ == PointerSize::k64) {
              ArtMethod::VisitArrayRoots<PointerSize::k64>(*this,
                                                           start_boundary,
                                                           end_boundary,
                                                           array);
            } else {
              DCHECK_EQ(collector_->pointer_size_, PointerSize::k32);
              ArtMethod::VisitArrayRoots<PointerSize::k32>(*this,
                                                           start_boundary,
                                                           end_boundary,
                                                           array);
            }
          }
        }
        break;
      case LinearAllocKind::kArtMethod:
        ArtMethod::VisitRoots(*this, start_boundary, end_boundary, static_cast<ArtMethod*>(obj));
        break;
      case LinearAllocKind::kArtFieldArray:
        ArtField::VisitArrayRoots(*this,
                                  start_boundary,
                                  end_boundary,
                                  static_cast<LengthPrefixedArray<ArtField>*>(obj));
        break;
      case LinearAllocKind::kDexCacheArray:
        {
          mirror::DexCachePair<mirror::Object>* first =
              reinterpret_cast<mirror::DexCachePair<mirror::Object>*>(start_boundary);
          mirror::DexCachePair<mirror::Object>* last =
              reinterpret_cast<mirror::DexCachePair<mirror::Object>*>(end_boundary);
          mirror::DexCache::VisitDexCachePairRoots(*this, first, last);
        }
    }
  }

  MarkCompact* const collector_;
};

void MarkCompact::PreCompactionPhase() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  Runtime* runtime = Runtime::Current();
  non_moving_space_bitmap_ = non_moving_space_->GetLiveBitmap();
  if (kIsDebugBuild) {
    pthread_attr_t attr;
    size_t stack_size;
    void* stack_addr;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, &stack_addr, &stack_size);
    pthread_attr_destroy(&attr);
    stack_addr_ = stack_addr;
    stack_end_ = reinterpret_cast<char*>(stack_addr) + stack_size;
  }

  compacting_ = true;

  {
    TimingLogger::ScopedTiming t2("(Paused)UpdateCompactionDataStructures", GetTimings());
    ReaderMutexLock rmu(thread_running_gc_, *Locks::heap_bitmap_lock_);
    // Refresh data-structures to catch-up on allocations that may have
    // happened since marking-phase pause.
    // There could be several TLABs that got allocated since marking pause. We
    // don't want to compact them and instead update the TLAB info in TLS and
    // let mutators continue to use the TLABs.
    // We need to set all the bits in live-words bitmap corresponding to allocated
    // objects. Also, we need to find the objects that are overlapping with
    // page-begin boundaries. Unlike objects allocated before
    // black_allocations_begin_, which can be identified via mark-bitmap, we can get
    // this info only via walking the space past black_allocations_begin_, which
    // involves fetching object size.
    // TODO: We can reduce the time spent on this in a pause by performing one
    // round of this concurrently prior to the pause.
    UpdateMovingSpaceBlackAllocations();
    // TODO: If we want to avoid this allocation in a pause then we will have to
    // allocate an array for the entire moving-space size, which can be made
    // part of info_map_.
    moving_pages_status_ = new Atomic<PageState>[moving_first_objs_count_ + black_page_count_];
    if (kIsDebugBuild) {
      size_t len = moving_first_objs_count_ + black_page_count_;
      for (size_t i = 0; i < len; i++) {
        CHECK_EQ(moving_pages_status_[i].load(std::memory_order_relaxed), PageState::kUnprocessed);
      }
    }
    // Iterate over the allocation_stack_, for every object in the non-moving
    // space:
    // 1. Mark the object in live bitmap
    // 2. Erase the object from allocation stack
    // 3. In the corresponding page, if the first-object vector needs updating
    // then do so.
    UpdateNonMovingSpaceBlackAllocations();

    heap_->GetReferenceProcessor()->UpdateRoots(this);
  }

  {
    // Thread roots must be updated first (before space mremap and native root
    // updation) to ensure that pre-update content is accessible.
    TimingLogger::ScopedTiming t2("(Paused)UpdateThreadRoots", GetTimings());
    MutexLock mu1(thread_running_gc_, *Locks::runtime_shutdown_lock_);
    MutexLock mu2(thread_running_gc_, *Locks::thread_list_lock_);
    std::list<Thread*> thread_list = runtime->GetThreadList()->GetList();
    for (Thread* thread : thread_list) {
      thread->VisitRoots(this, kVisitRootFlagAllRoots);
      // Interpreter cache is thread-local so it needs to be swept either in a
      // checkpoint, or a stop-the-world pause.
      thread->SweepInterpreterCache(this);
      thread->AdjustTlab(black_objs_slide_diff_);
    }
  }
  {
    TimingLogger::ScopedTiming t2("(Paused)UpdateClassLoaderRoots", GetTimings());
    ReaderMutexLock rmu(thread_running_gc_, *Locks::classlinker_classes_lock_);
    {
      ClassLoaderRootsUpdater updater(this);
      runtime->GetClassLinker()->VisitClassLoaders(&updater);
    }
  }

  GcVisitedArenaPool *arena_pool =
      static_cast<GcVisitedArenaPool*>(runtime->GetLinearAllocArenaPool());
  if (uffd_ == kFallbackMode) {
    LinearAllocPageUpdater updater(this);
    arena_pool->VisitRoots(updater);
  } else {
    arena_pool->ForEachAllocatedArena([this](const TrackedArena& arena)
                                      REQUIRES_SHARED(Locks::mutator_lock_) {
      uint8_t* last_byte = arena.GetLastUsedByte();
      CHECK(linear_alloc_arenas_.insert({const_cast<TrackedArena*>(&arena), last_byte}).second);
    });
  }

  SweepSystemWeaks(thread_running_gc_, runtime, /*paused*/true);
  KernelPreparation();

  {
    TimingLogger::ScopedTiming t2("(Paused)UpdateConcurrentRoots", GetTimings());
    runtime->VisitConcurrentRoots(this, kVisitRootFlagAllRoots);
  }
  {
    // TODO: don't visit the transaction roots if it's not active.
    TimingLogger::ScopedTiming t2("(Paused)UpdateNonThreadRoots", GetTimings());
    runtime->VisitNonThreadRoots(this);
  }

  {
    // TODO: Immune space updation has to happen either before or after
    // remapping pre-compact pages to from-space. And depending on when it's
    // done, we have to invoke VisitRefsForCompaction() with or without
    // read-barrier.
    TimingLogger::ScopedTiming t2("(Paused)UpdateImmuneSpaces", GetTimings());
    accounting::CardTable* const card_table = heap_->GetCardTable();
    for (auto& space : immune_spaces_.GetSpaces()) {
      DCHECK(space->IsImageSpace() || space->IsZygoteSpace());
      accounting::ContinuousSpaceBitmap* live_bitmap = space->GetLiveBitmap();
      accounting::ModUnionTable* table = heap_->FindModUnionTableFromSpace(space);
      ImmuneSpaceUpdateObjVisitor visitor(this);
      if (table != nullptr) {
        table->ProcessCards();
        table->VisitObjects(ImmuneSpaceUpdateObjVisitor::Callback, &visitor);
      } else {
        WriterMutexLock wmu(thread_running_gc_, *Locks::heap_bitmap_lock_);
        card_table->Scan<false>(
            live_bitmap,
            space->Begin(),
            space->Limit(),
            visitor,
            accounting::CardTable::kCardDirty - 1);
      }
    }
  }

  UpdateNonMovingSpace();
  // fallback mode
  if (uffd_ == kFallbackMode) {
    CompactMovingSpace<kFallbackMode>(nullptr);

    int32_t freed_bytes = black_objs_slide_diff_;
    bump_pointer_space_->RecordFree(freed_objects_, freed_bytes);
    RecordFree(ObjectBytePair(freed_objects_, freed_bytes));
  } else {
    // Initialize with 1 so that uffd worker threads don't call brodcast unless
    // gc-thread is ready to wait.
    gc_barrier_.Init(thread_running_gc_, 1);
    // We must start worker threads before resuming mutators to avoid deadlocks.
    heap_->GetThreadPool()->StartWorkers(thread_running_gc_);
  }
  stack_end_ = nullptr;
}

void MarkCompact::KernelPrepareRange(uint8_t* mremap_src,
                                     uint8_t* mremap_dst,
                                     size_t size,
                                     int fd) {
  // TODO: Create mapping's at 2MB aligned addresses to benefit from optimized
  // mremap.
  int mremap_flags = MREMAP_MAYMOVE | MREMAP_FIXED;
  if (gHaveMremapDontunmap) {
    mremap_flags |= MREMAP_DONTUNMAP;
  }

  void* ret = mremap(mremap_src, size, size, mremap_flags, mremap_dst);
  CHECK_EQ(ret, static_cast<void*>(mremap_dst))
        << "mremap to move pages failed: " << strerror(errno)
        << ". space-addr=" << reinterpret_cast<void*>(mremap_src)
        << " size=" << PrettySize(size);

  // Without MREMAP_DONTUNMAP the source mapping is unmapped by mremap. So mmap
  // the moving space again.
  if (!gHaveMremapDontunmap || fd >= 0) {
    int mmap_flags = fd < 0 ? MAP_PRIVATE | MAP_ANON : MAP_SHARED;
    mmap_flags |= MAP_FIXED;
    // On some platforms MAP_ANON expects fd to be -1.
    if (fd < 0) {
      fd = -1;
    }
    ret = mmap(mremap_src, size, PROT_READ | PROT_WRITE, mmap_flags, fd, 0);
    CHECK_EQ(ret, static_cast<void*>(mremap_src))
          << "mmap for moving space failed: " << strerror(errno);
  }
  if (uffd_ >= 0) {
    // Userfaultfd registration
    struct uffdio_register uffd_register;
    uffd_register.range.start = reinterpret_cast<uintptr_t>(mremap_src);
    uffd_register.range.len = size;
    uffd_register.mode = minor_fault_initialized_
                         ? UFFDIO_REGISTER_MODE_MINOR
                         : UFFDIO_REGISTER_MODE_MISSING;
    CHECK_EQ(ioctl(uffd_, UFFDIO_REGISTER, &uffd_register), 0)
          << "ioctl_userfaultfd: register moving-space: " << strerror(errno);
  }
}

void MarkCompact::KernelPreparation() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  size_t moving_space_size = bump_pointer_space_->Capacity();
  KernelPrepareRange(bump_pointer_space_->Begin(),
                     from_space_begin_,
                     moving_space_size,
                     moving_space_fds_[0]);
  DCHECK_EQ(mprotect(from_space_begin_, moving_space_size, PROT_READ), 0)
         << "mprotect failed: " << strerror(errno);

  for (auto& data : linear_alloc_spaces_data_) {
    KernelPrepareRange(data.begin_, data.shadow_.Begin(), data.shadow_.Size(), data.fd_);
  }
}

template <int kMode>
void MarkCompact::ConcurrentCompaction(uint8_t* buf) {
  DCHECK_NE(kMode, kFallbackMode);
  DCHECK(kMode != kCopyMode || buf != nullptr);
  auto zeropage_ioctl = [this] (void* addr, bool tolerate_eexist, bool tolerate_enoent) {
                          struct uffdio_zeropage uffd_zeropage;
                          DCHECK(IsAligned<kPageSize>(addr));
                          uffd_zeropage.range.start = reinterpret_cast<uintptr_t>(addr);
                          uffd_zeropage.range.len = kPageSize;
                          uffd_zeropage.mode = 0;
                          int ret = ioctl(uffd_, UFFDIO_ZEROPAGE, &uffd_zeropage);
                          CHECK(ret == 0
                                || (tolerate_enoent && errno == ENOENT)
                                || (tolerate_eexist && errno == EEXIST))
                              << "ioctl: zeropage: " << strerror(errno);
                          if (ret == 0) {
                            DCHECK_EQ(uffd_zeropage.zeropage, static_cast<ssize_t>(kPageSize));
                          } else {
                            DCHECK_EQ(ret, -1);
                          }
                        };

  auto copy_ioctl = [this] (void* fault_page, void* src) {
                          struct uffdio_copy uffd_copy;
                          uffd_copy.src = reinterpret_cast<uintptr_t>(src);
                          uffd_copy.dst = reinterpret_cast<uintptr_t>(fault_page);
                          uffd_copy.len = kPageSize;
                          uffd_copy.mode = 0;
                          int ret = ioctl(uffd_, UFFDIO_COPY, &uffd_copy);
                          CHECK_EQ(ret, 0) << "ioctl: copy: " << strerror(errno);
                          DCHECK_EQ(uffd_copy.copy, static_cast<ssize_t>(kPageSize));
                    };
  size_t nr_moving_space_used_pages = moving_first_objs_count_ + black_page_count_;

  while (true) {
    struct uffd_msg msg;
    ssize_t nread = read(uffd_, &msg, sizeof(msg));
    CHECK_GT(nread, 0);
    CHECK_EQ(msg.event, UFFD_EVENT_PAGEFAULT);
    DCHECK_EQ(nread, static_cast<ssize_t>(sizeof(msg)));
    uint8_t* fault_addr = reinterpret_cast<uint8_t*>(msg.arg.pagefault.address);
    if (fault_addr == conc_compaction_termination_page_) {
      // The counter doesn't need to be updated atomically as only one thread
      // would wake up against the gc-thread's load to this fault_addr. In fact,
      // the other threads would wake up serially because every exiting thread
      // will wake up gc-thread, which would retry load but again would find the
      // page missing. Also, the value will be flushed to caches due to the ioctl
      // syscall below.
      uint8_t ret = thread_pool_counter_--;
      // Only the last thread should map the zeropage so that the gc-thread can
      // proceed.
      if (ret == 1) {
        zeropage_ioctl(fault_addr, /*tolerate_eexist=*/false, /*tolerate_enoent=*/false);
      } else {
        struct uffdio_range uffd_range;
        uffd_range.start = msg.arg.pagefault.address;
        uffd_range.len = kPageSize;
        CHECK_EQ(ioctl(uffd_, UFFDIO_WAKE, &uffd_range), 0)
              << "ioctl: wake: " << strerror(errno);
      }
      break;
    }
    uint8_t* fault_page = AlignDown(fault_addr, kPageSize);
    if (bump_pointer_space_->HasAddress(reinterpret_cast<mirror::Object*>(fault_addr))) {
      ConcurrentlyProcessMovingPage<kMode>(zeropage_ioctl,
                                           copy_ioctl,
                                           fault_page,
                                           buf,
                                           nr_moving_space_used_pages);
    } else {
      ConcurrentlyProcessLinearAllocPage<kMode>(zeropage_ioctl,
                                                copy_ioctl,
                                                fault_page,
                                                msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_MINOR);
    }
  }
}

template <int kMode, typename ZeropageType, typename CopyType>
void MarkCompact::ConcurrentlyProcessMovingPage(ZeropageType& zeropage_ioctl,
                                                CopyType& copy_ioctl,
                                                uint8_t* fault_page,
                                                uint8_t* buf,
                                                size_t nr_moving_space_used_pages) {
  class ScopedBarrier {
   public:
    explicit ScopedBarrier(Barrier& barrier) : self_(Thread::Current()), barrier_(barrier) {
      barrier_.IncrementNoWait(self_);
    }

    ~ScopedBarrier() {
      barrier_.Pass(self_);
    }

   private:
    Thread* self_ = nullptr;
    Barrier& barrier_;
  };

  uint8_t* unused_space_begin = bump_pointer_space_->Begin()
                                + nr_moving_space_used_pages * kPageSize;
  DCHECK(IsAligned<kPageSize>(unused_space_begin));
  if (fault_page >= unused_space_begin) {
    // There is a race which allows more than one thread to install a
    // zero-page. But we can tolerate that. So absorb the EEXIST returned by
    // the ioctl and move on.
    zeropage_ioctl(fault_page, /*tolerate_eexist=*/true, /*tolerate_enoent=*/true);
    return;
  }
  size_t page_idx = (fault_page - bump_pointer_space_->Begin()) / kPageSize;
  mirror::Object* first_obj = first_objs_moving_space_[page_idx].AsMirrorPtr();
  if (first_obj == nullptr) {
    // We should never have a case where two workers are trying to install a
    // zeropage in this range as we synchronize using moving_pages_status_[page_idx].
    PageState expected_state = PageState::kUnprocessed;
    if (moving_pages_status_[page_idx].compare_exchange_strong(expected_state,
                                                               PageState::kProcessingAndMapping,
                                                               std::memory_order_relaxed)) {
      zeropage_ioctl(fault_page, /*tolerate_eexist=*/false, /*tolerate_enoent=*/true);
    } else {
      DCHECK_EQ(expected_state, PageState::kProcessingAndMapping);
    }
    return;
  }

  PageState state = moving_pages_status_[page_idx].load(std::memory_order_relaxed);
  switch (state) {
    case PageState::kUnprocessed:
      {
        // The increment to the barrier must be done before updating the page's
        // state. Otherwise, we will end up leaving a window wherein the
        // GC-thread could observe that no worker is working on compaction and
        // could end up unregistering the moving space from userfaultfd.
        ScopedBarrier barrier(gc_barrier_);
        // Acquire order to ensure we don't start writing to shadow map, which is
        // shared, before the CAS is successful. Release order to ensure that the
        // increment to moving_compactions_in_progress above is not re-ordered
        // after the CAS.
        if (moving_pages_status_[page_idx].compare_exchange_strong(state,
                                                                   PageState::kProcessingAndMapping,
                                                                   std::memory_order_acquire)) {
          if (kMode == kMinorFaultMode) {
            DCHECK_EQ(buf, nullptr);
            buf = shadow_to_space_map_.Begin() + page_idx * kPageSize;
          }

          if (fault_page < post_compact_end_) {
            // The page has to be compacted.
            CompactPage<kMode == kCopyMode>(first_obj, pre_compact_offset_moving_space_[page_idx], buf);
          } else {
            DCHECK_NE(first_obj, nullptr);
            DCHECK_GT(pre_compact_offset_moving_space_[page_idx], 0u);
            uint8_t* pre_compact_page = black_allocations_begin_ + (fault_page - post_compact_end_);
            DCHECK(IsAligned<kPageSize>(pre_compact_page));
            SlideBlackPage<kMode == kCopyMode>(first_obj, page_idx, pre_compact_page, buf);
          }
          if (kMode == kCopyMode) {
            copy_ioctl(fault_page, buf);
            return;
          } else {
            break;
          }
        } else if (state == PageState::kProcessed) {
          break;
        } else if (static_cast<uint8_t>(state) > static_cast<uint8_t>(PageState::kProcessed)) {
          // Somebody else took care of the page.
          return;
        }
      }
      // GC thread started compacting the page without the intent to map it.
      DCHECK_EQ(state, PageState::kProcessing);
      FALLTHROUGH_INTENDED;
    case PageState::kProcessing:
      DCHECK_EQ(kMode, kMinorFaultMode);
      if (moving_pages_status_[page_idx].compare_exchange_strong(state,
                                                                 PageState::kProcessingAndMapping,
                                                                 std::memory_order_relaxed)
          || static_cast<uint8_t>(state) > static_cast<uint8_t>(PageState::kProcessed)) {
        // Somebody else took or will take care of finishing the compaction and
        // then mapping the page.
        return;
      }
      // The page got processed (without mapping) in meantime. Attempt to map it.
      DCHECK_EQ(state, PageState::kProcessed);
      FALLTHROUGH_INTENDED;
    case PageState::kProcessed:
      // The page is processed but not mapped. We should map it.
      break;
    default:
      // Somebody else took care of the page.
      DCHECK_EQ(kMode, kMinorFaultMode);
      return;
  }

  DCHECK_EQ(kMode, kMinorFaultMode);
  if (state == PageState::kUnprocessed) {
    MapProcessedPages</*kFirstPageMapping=*/true>(fault_page,
                                                  moving_pages_status_,
                                                  page_idx,
                                                  nr_moving_space_used_pages);
  } else {
    DCHECK_EQ(state, PageState::kProcessed);
    MapProcessedPages</*kFirstPageMapping=*/false>(fault_page,
                                                   moving_pages_status_,
                                                   page_idx,
                                                   nr_moving_space_used_pages);
  }
}

template <int kMode, typename ZeropageType, typename CopyType>
void MarkCompact::ConcurrentlyProcessLinearAllocPage(ZeropageType& zeropage_ioctl,
                                                     CopyType& copy_ioctl,
                                                     uint8_t* fault_page,
                                                     bool is_minor_fault) {
  DCHECK(!is_minor_fault || kMode == kMinorFaultMode);
  auto arena_iter = linear_alloc_arenas_.find(fault_page);
  if ((kMode == kMinorFaultMode && !is_minor_fault)
      || (kMode == kCopyMode && (arena_iter == linear_alloc_arenas_.end()
                                 || arena_iter->second <= fault_page))) {
    // Fault page isn't in any of the arenas that existed before we started
    // compaction. So map zeropage and return.
    zeropage_ioctl(fault_page, /*tolerate_eexist=*/true, /*tolerate_enoent=*/false);
  } else {
    // Find the linear-alloc space containing fault-page
    LinearAllocSpaceData* space_data = nullptr;
    for (auto& data : linear_alloc_spaces_data_) {
      if (data.begin_ <= fault_page && fault_page < data.end_) {
        space_data = &data;
        break;
      }
    }
    DCHECK_NE(space_data, nullptr);
    size_t page_idx = (fault_page - space_data->begin_) / kPageSize;
    Atomic<PageState>* state_arr =
        reinterpret_cast<Atomic<PageState>*>(space_data->page_status_map_.Begin());
    PageState state = state_arr[page_idx].load(std::memory_order_relaxed);
    switch (state) {
      case PageState::kUnprocessed:
        if (state_arr[page_idx].compare_exchange_strong(state,
                                                        PageState::kProcessingAndMapping,
                                                        std::memory_order_acquire)) {
          // fault_page should always belong to some arena.
          DCHECK_NE(arena_iter->first, nullptr);
          uint8_t* first_obj = arena_iter->first->GetFirstObject(fault_page);
          DCHECK_NE(first_obj, nullptr);
          ptrdiff_t diff = space_data->shadow_.Begin() - space_data->begin_;
          LinearAllocPageUpdater{this}(fault_page + diff, first_obj + diff);
          if (kMode == kCopyMode) {
            copy_ioctl(fault_page, fault_page + diff);
            return;
          } else {
            break;
          }
        } else if (state == PageState::kProcessed) {
          break;
        } else if (static_cast<uint8_t>(state) > static_cast<uint8_t>(PageState::kProcessed)) {
          // Somebody else took care of the page.
          return;
        }
        // GC thread started compacting the page without the intent to map it.
        DCHECK_EQ(state, PageState::kProcessing);
        FALLTHROUGH_INTENDED;
      case PageState::kProcessing:
        DCHECK_EQ(kMode, kMinorFaultMode);
        if (state_arr[page_idx].compare_exchange_strong(state,
                                                        PageState::kProcessingAndMapping,
                                                        std::memory_order_relaxed)
            || static_cast<uint8_t>(state) > static_cast<uint8_t>(PageState::kProcessed)) {
          // Somebody else took or will take care of finishing the compaction and
          // then mapping the page.
          return;
        }
        // The page got processed (without mapping) in meantime. Attempt to map it.
        DCHECK_EQ(state, PageState::kProcessed);
        FALLTHROUGH_INTENDED;
      case PageState::kProcessed:
        // The page is processed but not mapped. We should map it.
        break;
      default:
        // Somebody else took care of the page.
        DCHECK_EQ(kMode, kMinorFaultMode);
        return;
    }

    DCHECK_EQ(kMode, kMinorFaultMode);
    if (state == PageState::kUnprocessed) {
      MapProcessedPages</*kFirstPageMapping=*/true>(fault_page,
                                                    state_arr,
                                                    page_idx,
                                                    space_data->page_status_map_.Size());
    } else {
      DCHECK_EQ(state, PageState::kProcessed);
      MapProcessedPages</*kFirstPageMapping=*/false>(fault_page,
                                                     state_arr,
                                                     page_idx,
                                                     space_data->page_status_map_.Size());
    }
  }
}

void MarkCompact::ProcessLinearAlloc() {
  LinearAllocPageUpdater updater(this);

  for (auto& pair : linear_alloc_arenas_) {
    TrackedArena* arena = pair.first;
    uint8_t* last_byte = pair.second;
    DCHECK_ALIGNED(last_byte, kPageSize);
    bool others_processing = false;
    // Find the linear-alloc space containing the arena
    LinearAllocSpaceData* space_data = nullptr;
    for (auto& data : linear_alloc_spaces_data_) {
      if (data.begin_ <= arena->Begin() && arena->Begin() < data.end_) {
        space_data = &data;
        break;
      }
    }
    DCHECK_NE(space_data, nullptr);
    ptrdiff_t diff = space_data->shadow_.Begin() - space_data->begin_;
    auto visitor = [space_data,
                    last_byte,
                    updater,
                    diff,
                    this,
                    &others_processing] (uint8_t* page_begin,
                                         uint8_t* first_obj) REQUIRES_SHARED(Locks::mutator_lock_) {
      // No need to process pages past last_byte as they already have updated
      // gc-roots, if any.
      if (page_begin >= last_byte) {
        return;
      }
      size_t page_idx = (page_begin - space_data->begin_) / kPageSize;
      Atomic<PageState>* state_arr =
          reinterpret_cast<Atomic<PageState>*>(space_data->page_status_map_.Begin());
      PageState expected_state = PageState::kUnprocessed;
      PageState desired_state = minor_fault_initialized_
                                ? PageState::kProcessing
                                : PageState::kProcessingAndMapping;
      // Acquire order to ensure that we don't start accessing the shadow page,
      // which is shared with other threads, prior to CAS. Also, for same
      // reason, we used 'release' order for changing the state to 'processed'.
      if (state_arr[page_idx].compare_exchange_strong(expected_state,
                                                      desired_state,
                                                      std::memory_order_acquire)) {
        updater(page_begin + diff, first_obj + diff);
        expected_state = PageState::kProcessing;
        if (!minor_fault_initialized_) {
          struct uffdio_copy uffd_copy;
          uffd_copy.src = reinterpret_cast<uintptr_t>(page_begin + diff);
          uffd_copy.dst = reinterpret_cast<uintptr_t>(page_begin);
          uffd_copy.len = kPageSize;
          uffd_copy.mode = 0;
          CHECK_EQ(ioctl(uffd_, UFFDIO_COPY, &uffd_copy), 0)
              << "ioctl: copy " << strerror(errno);
          DCHECK_EQ(uffd_copy.copy, static_cast<ssize_t>(kPageSize));
        } else if (!state_arr[page_idx].compare_exchange_strong(expected_state,
                                                                PageState::kProcessed,
                                                                std::memory_order_release)) {
          MapProcessedPages</*kFirstPageMapping=*/true>(page_begin,
                                                        state_arr,
                                                        page_idx,
                                                        space_data->page_status_map_.Size());
        }
      } else {
        others_processing = true;
      }
    };

    arena->VisitRoots(visitor);
    // If we are not in minor-fault mode and if no other thread was found to be
    // processing any pages in this arena, then we can madvise the shadow size.
    // Otherwise, we will double the memory use for linear-alloc.
    if (!minor_fault_initialized_ && !others_processing) {
      ZeroAndReleasePages(arena->Begin() + diff, arena->Size());
    }
  }
}

void MarkCompact::UnregisterUffd(uint8_t* start, size_t len) {
  struct uffdio_range unregister_range;
  unregister_range.start = reinterpret_cast<uintptr_t>(start);
  unregister_range.len = len;
  CHECK_EQ(ioctl(uffd_, UFFDIO_UNREGISTER, &unregister_range), 0)
        << "ioctl_userfaultfd: unregister moving-space: " << strerror(errno);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wframe-larger-than="
void MarkCompact::CompactionPhase() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  {
    int32_t freed_bytes = black_objs_slide_diff_;
    bump_pointer_space_->RecordFree(freed_objects_, freed_bytes);
    RecordFree(ObjectBytePair(freed_objects_, freed_bytes));
  }

  if (minor_fault_initialized_) {
    CompactMovingSpace<kMinorFaultMode>(/*page=*/nullptr);
  } else if (kObjPtrPoisoning) {
    CompactMovingSpace<kCopyMode>(compaction_buffers_map_.Begin());
    // madvise the page so that we can get userfaults on it. We don't need to
    // do this when not using poisoning as in that case the address location is
    // untouched during compaction.
    ZeroAndReleasePages(conc_compaction_termination_page_, kPageSize);
  } else {
    uint8_t buf[kPageSize];
    CompactMovingSpace<kCopyMode>(buf);
  }

  // Wait for the barrier to reach 0, which would indicate that no uffd worker
  // is compacting any page.
  gc_barrier_.Wait(thread_running_gc_);

  UnregisterUffd(bump_pointer_space_->Begin(), bump_pointer_space_->Capacity());

  // Release all of the memory taken by moving-space's from-map
  if (minor_fault_initialized_) {
    DCHECK_NE(moving_space_fds_[1], -1);
    int ret = ftruncate(moving_space_fds_[1], 0);
    CHECK_EQ(ret, 0) << "ftruncate to 0 failed for moving space:" << strerror(errno);
  } else {
    from_space_map_.MadviseDontNeedAndZero();
  }

  ProcessLinearAlloc();

  // The following triggers 'special' userfaults. When received by the
  // thread-pool workers, they will exit out of the compaction task. This fault
  // happens because we madvise info_map_ above and it is at least kPageSize in length.
  DCHECK(IsAligned<kPageSize>(conc_compaction_termination_page_));
  CHECK_EQ(*reinterpret_cast<volatile uint8_t*>(conc_compaction_termination_page_), 0);
  DCHECK_EQ(thread_pool_counter_, 0);

  // Unregister linear-alloc spaces
  for (auto& data : linear_alloc_spaces_data_) {
    DCHECK_EQ(data.end_ - data.begin_, static_cast<ssize_t>(data.shadow_.Size()));
    UnregisterUffd(data.begin_, data.shadow_.Size());
    // madvise linear-allocs's page-status array
    data.page_status_map_.MadviseDontNeedAndZero();
    // Madvise the entire linear-alloc space's shadow in copy-mode.
    if (!minor_fault_initialized_) {
      ZeroAndReleasePages(data.shadow_.Begin(), data.shadow_.Size());
    }
  }

  heap_->GetThreadPool()->StopWorkers(thread_running_gc_);
}
#pragma clang diagnostic pop

template <size_t kBufferSize>
class MarkCompact::ThreadRootsVisitor : public RootVisitor {
 public:
  explicit ThreadRootsVisitor(MarkCompact* mark_compact, Thread* const self)
        : mark_compact_(mark_compact), self_(self) {}

  ~ThreadRootsVisitor() {
    Flush();
  }

  void VisitRoots(mirror::Object*** roots, size_t count, const RootInfo& info ATTRIBUTE_UNUSED)
      override REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_) {
    for (size_t i = 0; i < count; i++) {
      mirror::Object* obj = *roots[i];
      if (mark_compact_->MarkObjectNonNullNoPush</*kParallel*/true>(obj)) {
        Push(obj);
      }
    }
  }

  void VisitRoots(mirror::CompressedReference<mirror::Object>** roots,
                  size_t count,
                  const RootInfo& info ATTRIBUTE_UNUSED)
      override REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_) {
    for (size_t i = 0; i < count; i++) {
      mirror::Object* obj = roots[i]->AsMirrorPtr();
      if (mark_compact_->MarkObjectNonNullNoPush</*kParallel*/true>(obj)) {
        Push(obj);
      }
    }
  }

 private:
  void Flush() REQUIRES_SHARED(Locks::mutator_lock_)
               REQUIRES(Locks::heap_bitmap_lock_) {
    StackReference<mirror::Object>* start;
    StackReference<mirror::Object>* end;
    {
      MutexLock mu(self_, mark_compact_->mark_stack_lock_);
      // Loop here because even after expanding once it may not be sufficient to
      // accommodate all references. It's almost impossible, but there is no harm
      // in implementing it this way.
      while (!mark_compact_->mark_stack_->BumpBack(idx_, &start, &end)) {
        mark_compact_->ExpandMarkStack();
      }
    }
    while (idx_ > 0) {
      *start++ = roots_[--idx_];
    }
    DCHECK_EQ(start, end);
  }

  void Push(mirror::Object* obj) REQUIRES_SHARED(Locks::mutator_lock_)
                                 REQUIRES(Locks::heap_bitmap_lock_) {
    if (UNLIKELY(idx_ >= kBufferSize)) {
      Flush();
    }
    roots_[idx_++].Assign(obj);
  }

  StackReference<mirror::Object> roots_[kBufferSize];
  size_t idx_ = 0;
  MarkCompact* const mark_compact_;
  Thread* const self_;
};

class MarkCompact::CheckpointMarkThreadRoots : public Closure {
 public:
  explicit CheckpointMarkThreadRoots(MarkCompact* mark_compact) : mark_compact_(mark_compact) {}

  void Run(Thread* thread) override NO_THREAD_SAFETY_ANALYSIS {
    ScopedTrace trace("Marking thread roots");
    // Note: self is not necessarily equal to thread since thread may be
    // suspended.
    Thread* const self = Thread::Current();
    CHECK(thread == self
          || thread->IsSuspended()
          || thread->GetState() == ThreadState::kWaitingPerformingGc)
        << thread->GetState() << " thread " << thread << " self " << self;
    {
      ThreadRootsVisitor</*kBufferSize*/ 20> visitor(mark_compact_, self);
      thread->VisitRoots(&visitor, kVisitRootFlagAllRoots);
    }

    // If thread is a running mutator, then act on behalf of the garbage
    // collector. See the code in ThreadList::RunCheckpoint.
    mark_compact_->GetBarrier().Pass(self);
  }

 private:
  MarkCompact* const mark_compact_;
};

void MarkCompact::MarkRootsCheckpoint(Thread* self, Runtime* runtime) {
  // We revote TLABs later during paused round of marking.
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  CheckpointMarkThreadRoots check_point(this);
  ThreadList* thread_list = runtime->GetThreadList();
  gc_barrier_.Init(self, 0);
  // Request the check point is run on all threads returning a count of the threads that must
  // run through the barrier including self.
  size_t barrier_count = thread_list->RunCheckpoint(&check_point);
  // Release locks then wait for all mutator threads to pass the barrier.
  // If there are no threads to wait which implys that all the checkpoint functions are finished,
  // then no need to release locks.
  if (barrier_count == 0) {
    return;
  }
  Locks::heap_bitmap_lock_->ExclusiveUnlock(self);
  Locks::mutator_lock_->SharedUnlock(self);
  {
    ScopedThreadStateChange tsc(self, ThreadState::kWaitingForCheckPointsToRun);
    gc_barrier_.Increment(self, barrier_count);
  }
  Locks::mutator_lock_->SharedLock(self);
  Locks::heap_bitmap_lock_->ExclusiveLock(self);
}

void MarkCompact::MarkNonThreadRoots(Runtime* runtime) {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  runtime->VisitNonThreadRoots(this);
}

void MarkCompact::MarkConcurrentRoots(VisitRootFlags flags, Runtime* runtime) {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  runtime->VisitConcurrentRoots(this, flags);
}

void MarkCompact::RevokeAllThreadLocalBuffers() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  bump_pointer_space_->RevokeAllThreadLocalBuffers();
}

class MarkCompact::ScanObjectVisitor {
 public:
  explicit ScanObjectVisitor(MarkCompact* const mark_compact) ALWAYS_INLINE
      : mark_compact_(mark_compact) {}

  void operator()(ObjPtr<mirror::Object> obj) const
      ALWAYS_INLINE
      REQUIRES(Locks::heap_bitmap_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    mark_compact_->ScanObject</*kUpdateLiveWords*/ false>(obj.Ptr());
  }

 private:
  MarkCompact* const mark_compact_;
};

void MarkCompact::UpdateAndMarkModUnion() {
  accounting::CardTable* const card_table = heap_->GetCardTable();
  for (const auto& space : immune_spaces_.GetSpaces()) {
    const char* name = space->IsZygoteSpace()
        ? "UpdateAndMarkZygoteModUnionTable"
        : "UpdateAndMarkImageModUnionTable";
    DCHECK(space->IsZygoteSpace() || space->IsImageSpace()) << *space;
    TimingLogger::ScopedTiming t(name, GetTimings());
    accounting::ModUnionTable* table = heap_->FindModUnionTableFromSpace(space);
    if (table != nullptr) {
      // UpdateAndMarkReferences() doesn't visit Reference-type objects. But
      // that's fine because these objects are immutable enough (referent can
      // only be cleared) and hence the only referents they can have are intra-space.
      table->UpdateAndMarkReferences(this);
    } else {
      // No mod-union table, scan all dirty/aged cards in the corresponding
      // card-table. This can only occur for app images.
      card_table->Scan</*kClearCard*/ false>(space->GetMarkBitmap(),
                                             space->Begin(),
                                             space->End(),
                                             ScanObjectVisitor(this),
                                             gc::accounting::CardTable::kCardAged);
    }
  }
}

void MarkCompact::MarkReachableObjects() {
  UpdateAndMarkModUnion();
  // Recursively mark all the non-image bits set in the mark bitmap.
  ProcessMarkStack();
}

class MarkCompact::CardModifiedVisitor {
 public:
  explicit CardModifiedVisitor(MarkCompact* const mark_compact,
                               accounting::ContinuousSpaceBitmap* const bitmap,
                               accounting::CardTable* const card_table)
      : visitor_(mark_compact), bitmap_(bitmap), card_table_(card_table) {}

  void operator()(uint8_t* card,
                  uint8_t expected_value,
                  uint8_t new_value ATTRIBUTE_UNUSED) const {
    if (expected_value == accounting::CardTable::kCardDirty) {
      uintptr_t start = reinterpret_cast<uintptr_t>(card_table_->AddrFromCard(card));
      bitmap_->VisitMarkedRange(start, start + accounting::CardTable::kCardSize, visitor_);
    }
  }

 private:
  ScanObjectVisitor visitor_;
  accounting::ContinuousSpaceBitmap* bitmap_;
  accounting::CardTable* const card_table_;
};

void MarkCompact::ScanDirtyObjects(bool paused, uint8_t minimum_age) {
  accounting::CardTable* card_table = heap_->GetCardTable();
  for (const auto& space : heap_->GetContinuousSpaces()) {
    const char* name = nullptr;
    switch (space->GetGcRetentionPolicy()) {
    case space::kGcRetentionPolicyNeverCollect:
      name = paused ? "(Paused)ScanGrayImmuneSpaceObjects" : "ScanGrayImmuneSpaceObjects";
      break;
    case space::kGcRetentionPolicyFullCollect:
      name = paused ? "(Paused)ScanGrayZygoteSpaceObjects" : "ScanGrayZygoteSpaceObjects";
      break;
    case space::kGcRetentionPolicyAlwaysCollect:
      name = paused ? "(Paused)ScanGrayAllocSpaceObjects" : "ScanGrayAllocSpaceObjects";
      break;
    default:
      LOG(FATAL) << "Unreachable";
      UNREACHABLE();
    }
    TimingLogger::ScopedTiming t(name, GetTimings());
    ScanObjectVisitor visitor(this);
    const bool is_immune_space = space->IsZygoteSpace() || space->IsImageSpace();
    if (paused) {
      DCHECK_EQ(minimum_age, gc::accounting::CardTable::kCardDirty);
      // We can clear the card-table for any non-immune space.
      if (is_immune_space) {
        card_table->Scan</*kClearCard*/false>(space->GetMarkBitmap(),
                                              space->Begin(),
                                              space->End(),
                                              visitor,
                                              minimum_age);
      } else {
        card_table->Scan</*kClearCard*/true>(space->GetMarkBitmap(),
                                             space->Begin(),
                                             space->End(),
                                             visitor,
                                             minimum_age);
      }
    } else {
      DCHECK_EQ(minimum_age, gc::accounting::CardTable::kCardAged);
      accounting::ModUnionTable* table = heap_->FindModUnionTableFromSpace(space);
      if (table) {
        table->ProcessCards();
        card_table->Scan</*kClearCard*/false>(space->GetMarkBitmap(),
                                              space->Begin(),
                                              space->End(),
                                              visitor,
                                              minimum_age);
      } else {
        CardModifiedVisitor card_modified_visitor(this, space->GetMarkBitmap(), card_table);
        // For the alloc spaces we should age the dirty cards and clear the rest.
        // For image and zygote-space without mod-union-table, age the dirty
        // cards but keep the already aged cards unchanged.
        // In either case, visit the objects on the cards that were changed from
        // dirty to aged.
        if (is_immune_space) {
          card_table->ModifyCardsAtomic(space->Begin(),
                                        space->End(),
                                        [](uint8_t card) {
                                          return (card == gc::accounting::CardTable::kCardClean)
                                                  ? card
                                                  : gc::accounting::CardTable::kCardAged;
                                        },
                                        card_modified_visitor);
        } else {
          card_table->ModifyCardsAtomic(space->Begin(),
                                        space->End(),
                                        AgeCardVisitor(),
                                        card_modified_visitor);
        }
      }
    }
  }
}

void MarkCompact::RecursiveMarkDirtyObjects(bool paused, uint8_t minimum_age) {
  ScanDirtyObjects(paused, minimum_age);
  ProcessMarkStack();
}

void MarkCompact::MarkRoots(VisitRootFlags flags) {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  Runtime* runtime = Runtime::Current();
  // Make sure that the checkpoint which collects the stack roots is the first
  // one capturning GC-roots. As this one is supposed to find the address
  // everything allocated after that (during this marking phase) will be
  // considered 'marked'.
  MarkRootsCheckpoint(thread_running_gc_, runtime);
  MarkNonThreadRoots(runtime);
  MarkConcurrentRoots(flags, runtime);
}

void MarkCompact::PreCleanCards() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  CHECK(!Locks::mutator_lock_->IsExclusiveHeld(thread_running_gc_));
  MarkRoots(static_cast<VisitRootFlags>(kVisitRootFlagClearRootLog | kVisitRootFlagNewRoots));
  RecursiveMarkDirtyObjects(/*paused*/ false, accounting::CardTable::kCardDirty - 1);
}

// In a concurrent marking algorithm, if we are not using a write/read barrier, as
// in this case, then we need a stop-the-world (STW) round in the end to mark
// objects which were written into concurrently while concurrent marking was
// performed.
// In order to minimize the pause time, we could take one of the two approaches:
// 1. Keep repeating concurrent marking of dirty cards until the time spent goes
// below a threshold.
// 2. Do two rounds concurrently and then attempt a paused one. If we figure
// that it's taking too long, then resume mutators and retry.
//
// Given the non-trivial fixed overhead of running a round (card table and root
// scan), it might be better to go with approach 2.
void MarkCompact::MarkingPhase() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  DCHECK_EQ(thread_running_gc_, Thread::Current());
  WriterMutexLock mu(thread_running_gc_, *Locks::heap_bitmap_lock_);
  BindAndResetBitmaps();
  MarkRoots(
        static_cast<VisitRootFlags>(kVisitRootFlagAllRoots | kVisitRootFlagStartLoggingNewRoots));
  MarkReachableObjects();
  // Pre-clean dirtied cards to reduce pauses.
  PreCleanCards();

  // Setup reference processing and forward soft references once before enabling
  // slow path (in MarkingPause)
  ReferenceProcessor* rp = GetHeap()->GetReferenceProcessor();
  bool clear_soft_references = GetCurrentIteration()->GetClearSoftReferences();
  rp->Setup(thread_running_gc_, this, /*concurrent=*/ true, clear_soft_references);
  if (!clear_soft_references) {
    // Forward as many SoftReferences as possible before inhibiting reference access.
    rp->ForwardSoftReferences(GetTimings());
  }
}

class MarkCompact::RefFieldsVisitor {
 public:
  ALWAYS_INLINE explicit RefFieldsVisitor(MarkCompact* const mark_compact)
    : mark_compact_(mark_compact) {}

  ALWAYS_INLINE void operator()(mirror::Object* obj,
                                MemberOffset offset,
                                bool is_static ATTRIBUTE_UNUSED) const
      REQUIRES(Locks::heap_bitmap_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (kCheckLocks) {
      Locks::mutator_lock_->AssertSharedHeld(Thread::Current());
      Locks::heap_bitmap_lock_->AssertExclusiveHeld(Thread::Current());
    }
    mark_compact_->MarkObject(obj->GetFieldObject<mirror::Object>(offset), obj, offset);
  }

  void operator()(ObjPtr<mirror::Class> klass, ObjPtr<mirror::Reference> ref) const
      REQUIRES(Locks::heap_bitmap_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    mark_compact_->DelayReferenceReferent(klass, ref);
  }

  void VisitRootIfNonNull(mirror::CompressedReference<mirror::Object>* root) const
      REQUIRES(Locks::heap_bitmap_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (!root->IsNull()) {
      VisitRoot(root);
    }
  }

  void VisitRoot(mirror::CompressedReference<mirror::Object>* root) const
      REQUIRES(Locks::heap_bitmap_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (kCheckLocks) {
      Locks::mutator_lock_->AssertSharedHeld(Thread::Current());
      Locks::heap_bitmap_lock_->AssertExclusiveHeld(Thread::Current());
    }
    mark_compact_->MarkObject(root->AsMirrorPtr());
  }

 private:
  MarkCompact* const mark_compact_;
};

template <size_t kAlignment>
size_t MarkCompact::LiveWordsBitmap<kAlignment>::LiveBytesInBitmapWord(size_t chunk_idx) const {
  const size_t index = chunk_idx * kBitmapWordsPerVectorWord;
  size_t words = 0;
  for (uint32_t i = 0; i < kBitmapWordsPerVectorWord; i++) {
    words += POPCOUNT(Bitmap::Begin()[index + i]);
  }
  return words * kAlignment;
}

void MarkCompact::UpdateLivenessInfo(mirror::Object* obj) {
  DCHECK(obj != nullptr);
  uintptr_t obj_begin = reinterpret_cast<uintptr_t>(obj);
  size_t size = RoundUp(obj->SizeOf<kDefaultVerifyFlags>(), kAlignment);
  uintptr_t bit_index = live_words_bitmap_->SetLiveWords(obj_begin, size);
  size_t chunk_idx = (obj_begin - live_words_bitmap_->Begin()) / kOffsetChunkSize;
  // Compute the bit-index within the chunk-info vector word.
  bit_index %= kBitsPerVectorWord;
  size_t first_chunk_portion = std::min(size, (kBitsPerVectorWord - bit_index) * kAlignment);

  chunk_info_vec_[chunk_idx++] += first_chunk_portion;
  DCHECK_LE(first_chunk_portion, size);
  for (size -= first_chunk_portion; size > kOffsetChunkSize; size -= kOffsetChunkSize) {
    DCHECK_EQ(chunk_info_vec_[chunk_idx], 0u);
    chunk_info_vec_[chunk_idx++] = kOffsetChunkSize;
  }
  chunk_info_vec_[chunk_idx] += size;
  freed_objects_--;
}

template <bool kUpdateLiveWords>
void MarkCompact::ScanObject(mirror::Object* obj) {
  RefFieldsVisitor visitor(this);
  DCHECK(IsMarked(obj)) << "Scanning marked object " << obj << "\n" << heap_->DumpSpaces();
  if (kUpdateLiveWords && moving_space_bitmap_->HasAddress(obj)) {
    UpdateLivenessInfo(obj);
  }
  obj->VisitReferences(visitor, visitor);
}

// Scan anything that's on the mark stack.
void MarkCompact::ProcessMarkStack() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  // TODO: try prefetch like in CMS
  while (!mark_stack_->IsEmpty()) {
    mirror::Object* obj = mark_stack_->PopBack();
    DCHECK(obj != nullptr);
    ScanObject</*kUpdateLiveWords*/ true>(obj);
  }
}

void MarkCompact::ExpandMarkStack() {
  const size_t new_size = mark_stack_->Capacity() * 2;
  std::vector<StackReference<mirror::Object>> temp(mark_stack_->Begin(),
                                                   mark_stack_->End());
  mark_stack_->Resize(new_size);
  for (auto& ref : temp) {
    mark_stack_->PushBack(ref.AsMirrorPtr());
  }
  DCHECK(!mark_stack_->IsFull());
}

inline void MarkCompact::PushOnMarkStack(mirror::Object* obj) {
  if (UNLIKELY(mark_stack_->IsFull())) {
    ExpandMarkStack();
  }
  mark_stack_->PushBack(obj);
}

inline void MarkCompact::MarkObjectNonNull(mirror::Object* obj,
                                           mirror::Object* holder,
                                           MemberOffset offset) {
  DCHECK(obj != nullptr);
  if (MarkObjectNonNullNoPush</*kParallel*/false>(obj, holder, offset)) {
    PushOnMarkStack(obj);
  }
}

template <bool kParallel>
inline bool MarkCompact::MarkObjectNonNullNoPush(mirror::Object* obj,
                                                 mirror::Object* holder,
                                                 MemberOffset offset) {
  // We expect most of the referenes to be in bump-pointer space, so try that
  // first to keep the cost of this function minimal.
  if (LIKELY(moving_space_bitmap_->HasAddress(obj))) {
    return kParallel ? !moving_space_bitmap_->AtomicTestAndSet(obj)
                     : !moving_space_bitmap_->Set(obj);
  } else if (non_moving_space_bitmap_->HasAddress(obj)) {
    return kParallel ? !non_moving_space_bitmap_->AtomicTestAndSet(obj)
                     : !non_moving_space_bitmap_->Set(obj);
  } else if (immune_spaces_.ContainsObject(obj)) {
    DCHECK(IsMarked(obj) != nullptr);
    return false;
  } else {
    // Must be a large-object space, otherwise it's a case of heap corruption.
    if (!IsAligned<kPageSize>(obj)) {
      // Objects in large-object space are page aligned. So if we have an object
      // which doesn't belong to any space and is not page-aligned as well, then
      // it's memory corruption.
      // TODO: implement protect/unprotect in bump-pointer space.
      heap_->GetVerification()->LogHeapCorruption(holder, offset, obj, /*fatal*/ true);
    }
    DCHECK_NE(heap_->GetLargeObjectsSpace(), nullptr)
        << "ref=" << obj
        << " doesn't belong to any of the spaces and large object space doesn't exist";
    accounting::LargeObjectBitmap* los_bitmap = heap_->GetLargeObjectsSpace()->GetMarkBitmap();
    DCHECK(los_bitmap->HasAddress(obj));
    return kParallel ? !los_bitmap->AtomicTestAndSet(obj)
                     : !los_bitmap->Set(obj);
  }
}

inline void MarkCompact::MarkObject(mirror::Object* obj,
                                    mirror::Object* holder,
                                    MemberOffset offset) {
  if (obj != nullptr) {
    MarkObjectNonNull(obj, holder, offset);
  }
}

mirror::Object* MarkCompact::MarkObject(mirror::Object* obj) {
  MarkObject(obj, nullptr, MemberOffset(0));
  return obj;
}

void MarkCompact::MarkHeapReference(mirror::HeapReference<mirror::Object>* obj,
                                    bool do_atomic_update ATTRIBUTE_UNUSED) {
  MarkObject(obj->AsMirrorPtr(), nullptr, MemberOffset(0));
}

void MarkCompact::VisitRoots(mirror::Object*** roots,
                             size_t count,
                             const RootInfo& info) {
  if (compacting_) {
    for (size_t i = 0; i < count; ++i) {
      UpdateRoot(roots[i], info);
    }
  } else {
    for (size_t i = 0; i < count; ++i) {
      MarkObjectNonNull(*roots[i]);
    }
  }
}

void MarkCompact::VisitRoots(mirror::CompressedReference<mirror::Object>** roots,
                             size_t count,
                             const RootInfo& info) {
  // TODO: do we need to check if the root is null or not?
  if (compacting_) {
    for (size_t i = 0; i < count; ++i) {
      UpdateRoot(roots[i], info);
    }
  } else {
    for (size_t i = 0; i < count; ++i) {
      MarkObjectNonNull(roots[i]->AsMirrorPtr());
    }
  }
}

mirror::Object* MarkCompact::IsMarked(mirror::Object* obj) {
  if (moving_space_bitmap_->HasAddress(obj)) {
    const bool is_black = reinterpret_cast<uint8_t*>(obj) >= black_allocations_begin_;
    if (compacting_) {
      if (is_black) {
        return PostCompactBlackObjAddr(obj);
      } else if (live_words_bitmap_->Test(obj)) {
        return PostCompactOldObjAddr(obj);
      } else {
        return nullptr;
      }
    }
    return (is_black || moving_space_bitmap_->Test(obj)) ? obj : nullptr;
  } else if (non_moving_space_bitmap_->HasAddress(obj)) {
    return non_moving_space_bitmap_->Test(obj) ? obj : nullptr;
  } else if (immune_spaces_.ContainsObject(obj)) {
    return obj;
  } else {
    DCHECK(heap_->GetLargeObjectsSpace())
        << "ref=" << obj
        << " doesn't belong to any of the spaces and large object space doesn't exist";
    accounting::LargeObjectBitmap* los_bitmap = heap_->GetLargeObjectsSpace()->GetMarkBitmap();
    if (los_bitmap->HasAddress(obj)) {
      DCHECK(IsAligned<kPageSize>(obj));
      return los_bitmap->Test(obj) ? obj : nullptr;
    } else {
      // The given obj is not in any of the known spaces, so return null. This could
      // happen for instance in interpreter caches wherein a concurrent updation
      // to the cache could result in obj being a non-reference. This is
      // tolerable because SweepInterpreterCaches only updates if the given
      // object has moved, which can't be the case for the non-reference.
      return nullptr;
    }
  }
}

bool MarkCompact::IsNullOrMarkedHeapReference(mirror::HeapReference<mirror::Object>* obj,
                                              bool do_atomic_update ATTRIBUTE_UNUSED) {
  mirror::Object* ref = obj->AsMirrorPtr();
  if (ref == nullptr) {
    return true;
  }
  return IsMarked(ref);
}

// Process the 'referent' field in a java.lang.ref.Reference. If the referent
// has not yet been marked, put it on the appropriate list in the heap for later
// processing.
void MarkCompact::DelayReferenceReferent(ObjPtr<mirror::Class> klass,
                                         ObjPtr<mirror::Reference> ref) {
  heap_->GetReferenceProcessor()->DelayReferenceReferent(klass, ref, this);
}

void MarkCompact::FinishPhase() {
  bool is_zygote = Runtime::Current()->IsZygote();
  minor_fault_initialized_ = !is_zygote && uffd_minor_fault_supported_;
  // When poisoning ObjPtr, we are forced to use buffers for page compaction in
  // lower 4GB. Now that the usage is done, madvise them. But skip the first
  // page, which is used by the gc-thread for the next iteration. Otherwise, we
  // get into a deadlock due to userfault on it in the next iteration. This page
  // is not consuming any physical memory because we already madvised it above
  // and then we triggered a read userfault, which maps a special zero-page.
  if (kObjPtrPoisoning) {
    if (minor_fault_initialized_) {
      // Now that we are going to use minor-faults from next GC cycle, we can
      // unmap the buffers used by worker threads.
      compaction_buffers_map_.SetSize(kPageSize);
      // The first page should also be madvised, identical to
      // !kObjPtrPoisoning case.
      ZeroAndReleasePages(conc_compaction_termination_page_, kPageSize);
    } else {
      ZeroAndReleasePages(compaction_buffers_map_.Begin() + kPageSize,
                          compaction_buffers_map_.Size() - kPageSize);
    }
  } else {
    ZeroAndReleasePages(conc_compaction_termination_page_, kPageSize);
  }

  info_map_.MadviseDontNeedAndZero();
  live_words_bitmap_->ClearBitmap();

  if (UNLIKELY(is_zygote && uffd_ >= 0)) {
    heap_->DeleteThreadPool();
    close(uffd_);
    uffd_ = -1;
    uffd_initialized_ = false;
  }
  CHECK(mark_stack_->IsEmpty());  // Ensure that the mark stack is empty.
  mark_stack_->Reset();
  updated_roots_.clear();
  delete[] moving_pages_status_;
  linear_alloc_arenas_.clear();
  {
    DCHECK_EQ(thread_running_gc_, Thread::Current());
    ReaderMutexLock mu(thread_running_gc_, *Locks::mutator_lock_);
    WriterMutexLock mu2(thread_running_gc_, *Locks::heap_bitmap_lock_);
    heap_->ClearMarkedObjects();
  }
  if (gHaveMremapDontunmap) {
    for (auto& data : linear_alloc_spaces_data_) {
      if (data.fd_ >= 0) {
        close(data.fd_);
        data.fd_ = -2;
      }
    }
  }
  std::swap(moving_space_fds_[0], moving_space_fds_[1]);
}

}  // namespace collector
}  // namespace gc
}  // namespace art
