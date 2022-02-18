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

#ifndef ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_H_
#define ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_H_

#include <memory>
#include <unordered_set>

#include "base/atomic.h"
#include "barrier.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "garbage_collector.h"
#include "gc/accounting/atomic_stack.h"
#include "gc/accounting/bitmap-inl.h"
#include "gc/accounting/heap_bitmap.h"
#include "gc_root.h"
#include "immune_spaces.h"
#include "offsets.h"

namespace art {

namespace mirror {
class DexCache;
}

namespace gc {

class Heap;

namespace space {
class BumpPointerSpace;
}  // namespace space

namespace collector {
class MarkCompact : public GarbageCollector {
 public:
  static constexpr size_t kAlignment = kObjectAlignment;
  static constexpr int kFallbackMode = -2;

  explicit MarkCompact(Heap* heap);

  ~MarkCompact() {}

  void RunPhases() override;

  // TODO: Add condition that current thread is running gc.
  bool IsCompacting() const {
    return compacting_;
  }

  GcType GetGcType() const override {
    return kGcTypeFull;
  }

  CollectorType GetCollectorType() const override {
    return kCollectorTypeCMC;
  }

  Barrier& GetBarrier() {
    return gc_barrier_;
  }

  mirror::Object* MarkObject(mirror::Object* obj) override
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  void MarkHeapReference(mirror::HeapReference<mirror::Object>* obj,
                         bool do_atomic_update) override
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  void VisitRoots(mirror::Object*** roots,
                  size_t count,
                  const RootInfo& info) override
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  void VisitRoots(mirror::CompressedReference<mirror::Object>** roots,
                  size_t count,
                  const RootInfo& info) override
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  bool IsNullOrMarkedHeapReference(mirror::HeapReference<mirror::Object>* obj,
                                   bool do_atomic_update) override
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  void RevokeAllThreadLocalBuffers() override;

  void DelayReferenceReferent(ObjPtr<mirror::Class> klass,
                              ObjPtr<mirror::Reference> reference) override
      REQUIRES_SHARED(Locks::mutator_lock_, Locks::heap_bitmap_lock_);

  mirror::Object* IsMarked(mirror::Object* obj) override
      REQUIRES_SHARED(Locks::mutator_lock_, Locks::heap_bitmap_lock_);

  mirror::Object* ReadBarrier(mirror::Object* old_ref) {
      CHECK(compacting_);
      if (live_words_bitmap_->HasAddress(old_ref)) {
        return GetFromSpaceAddr(old_ref);
      }
      return old_ref;
  }

 private:
  using ObjReference = mirror::ObjectReference</*kPoisonReferences*/ false, mirror::Object>;
  // TODO: consider increasing it to 128-bit per chunk.
  static constexpr uint32_t kBitsPerVectorWord = kBitsPerIntPtrT;
  static constexpr uint32_t kOffsetChunkSize = kBitsPerVectorWord * kAlignment;
  static_assert(kOffsetChunkSize < kPageSize);
  // Bitmap with bits corresponding to every live word set. For an object
  // which is 4 words in size will have the corresponding 4 bits set. This is
  // required for efficient computation of new-address (post-compaction) from
  // the given old-address (pre-compaction).
  template <size_t kAlignment>
  class LiveWordsBitmap : private accounting::MemoryRangeBitmap<kAlignment> {
    using Bitmap = accounting::Bitmap;
    using MemRangeBitmap = accounting::MemoryRangeBitmap<kAlignment>;

   public:
    static_assert(IsPowerOfTwo(kBitsPerVectorWord));
    static_assert(IsPowerOfTwo(Bitmap::kBitsPerBitmapWord));
    static_assert(kBitsPerVectorWord >= Bitmap::kBitsPerBitmapWord);
    static constexpr uint32_t kBitmapWordsPerVectorWord =
            kBitsPerVectorWord / Bitmap::kBitsPerBitmapWord;
    static LiveWordsBitmap* Create(uintptr_t begin, uintptr_t end);

    // Return offset (within the offset-vector chunk) of the nth live word.
    uint32_t FindNthLiveWordOffset(size_t offset_vec_idx, uint32_t n) const;
    // Sets all bits in the bitmap corresponding to the given range. Also
    // returns the bit-index of the first word.
    ALWAYS_INLINE uintptr_t SetLiveWords(uintptr_t begin, size_t size);
    // Count number of live words upto the given bit-index. This is to be used
    // to compute the post-compact address of an old reference.
    ALWAYS_INLINE size_t CountLiveWordsUpto(size_t bit_idx) const;
    // Call visitor for every stride of contiguous marked bits in the live-word
    // bitmap. Passes to the visitor index of the first marked bit in the
    // stride, stride-size and whether it's the last stride in the given range
    // or not.
    template <typename Visitor>
    ALWAYS_INLINE void VisitLiveStrides(uintptr_t begin_bit_idx,
                                        uint8_t* end,
                                        const size_t bytes,
                                        Visitor&& visitor) const
        REQUIRES_SHARED(Locks::mutator_lock_);
    // Count the number of live bytes in the given vector idx.
    size_t LiveBytesInBitmapWord(size_t vec_idx) const;
    void ClearBitmap() { Bitmap::Clear(); }
    ALWAYS_INLINE uintptr_t Begin() const { return MemRangeBitmap::CoverBegin(); }
    ALWAYS_INLINE bool HasAddress(mirror::Object* obj) const {
      return MemRangeBitmap::HasAddress(reinterpret_cast<uintptr_t>(obj));
    }
    ALWAYS_INLINE bool Test(uintptr_t bit_index) const {
      return Bitmap::TestBit(bit_index);
    }
    ALWAYS_INLINE bool Test(mirror::Object* obj) const {
      return MemRangeBitmap::Test(reinterpret_cast<size_t>(obj));
    }
    ALWAYS_INLINE uintptr_t GetWord(size_t index) const {
      static_assert(kBitmapWordsPerVectorWord == 1);
      return Bitmap::Begin()[index * kBitmapWordsPerVectorWord];
    }
  };

  // For a given object address in pre-compact space, return the corresponding
  // address in the from-space, where heap pages are relocated in the compaction
  // pause.
  mirror::Object* GetFromSpaceAddr(mirror::Object* obj) const {
    DCHECK(live_words_bitmap_->HasAddress(obj)) << " obj=" << obj;
    uintptr_t offset = reinterpret_cast<uintptr_t>(obj) - live_words_bitmap_->Begin();
    return reinterpret_cast<mirror::Object*>(from_space_begin_ + offset);
  }

  // Check if the obj is within heap and has a klass which is likely to be valid
  // mirror::Class.
  bool IsValidObject(mirror::Object* obj) const REQUIRES_SHARED(Locks::mutator_lock_);
  void InitializePhase();
  void FinishPhase() REQUIRES(!Locks::mutator_lock_, !Locks::heap_bitmap_lock_);
  void MarkingPhase() REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!Locks::heap_bitmap_lock_);
  void CompactionPhase() REQUIRES_SHARED(Locks::mutator_lock_);

  void SweepSystemWeaks(Thread* self, Runtime* runtime, const bool paused)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::heap_bitmap_lock_);
  // Update the reference at given offset in the given object with post-compact
  // address.
  ALWAYS_INLINE void UpdateRef(mirror::Object* obj, MemberOffset offset)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Verify that the gc-root is updated only once. Returns false if the update
  // shouldn't be done.
  ALWAYS_INLINE bool VerifyRootDoubleUpdation(void* root,
                                              mirror::Object* old_ref,
                                              const RootInfo& info)
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Update the given root with post-compact address.
  ALWAYS_INLINE void UpdateRoot(mirror::CompressedReference<mirror::Object>* root,
                                const RootInfo& info = RootInfo(RootType::kRootUnknown))
      REQUIRES_SHARED(Locks::mutator_lock_);
  ALWAYS_INLINE void UpdateRoot(mirror::Object** root,
                                const RootInfo& info = RootInfo(RootType::kRootUnknown))
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Given the pre-compact address, the function returns the post-compact
  // address of the given object.
  ALWAYS_INLINE mirror::Object* PostCompactAddress(mirror::Object* old_ref) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Compute post-compact address of an object in moving space. This function
  // assumes that old_ref is in moving space.
  ALWAYS_INLINE mirror::Object* PostCompactAddressUnchecked(mirror::Object* old_ref) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Compute the new address for an object which was black allocated during this
  // GC cycle.
  ALWAYS_INLINE mirror::Object* PostCompactOldObjAddr(mirror::Object* old_ref) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Compute the new address for an object which was allocated prior to starting
  // this GC cycle.
  ALWAYS_INLINE mirror::Object* PostCompactBlackObjAddr(mirror::Object* old_ref) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Identify immune spaces and reset card-table, mod-union-table, and mark
  // bitmaps.
  void BindAndResetBitmaps() REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  // Perform one last round of marking, identifying roots from dirty cards
  // during a stop-the-world (STW) pause.
  void MarkingPause() REQUIRES(Locks::mutator_lock_, !Locks::heap_bitmap_lock_);
  // Perform stop-the-world pause prior to concurrent compaction.
  // Updates GC-roots and protects heap so that during the concurrent
  // compaction phase we can receive faults and compact the corresponding pages
  // on the fly.
  void PreCompactionPhase() REQUIRES(Locks::mutator_lock_);
  // Compute offset-vector and other data structures required during concurrent
  // compaction.
  void PrepareForCompaction() REQUIRES_SHARED(Locks::mutator_lock_);

  // Compact contents into the given post-compact page. obj either starts in
  // some preceding page or at the beginning of the page. offset is the offset
  // within obj from where the contents should be copied to the page.
  void CompactPage(mirror::Object* obj, uint32_t offset, uint8_t* addr)
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Compact the bump-pointer space. pass page that should be used as buffer for
  // userfaultfd.
  template <bool kFallback>
  void CompactMovingSpace(uint8_t* page = nullptr) REQUIRES_SHARED(Locks::mutator_lock_);
  // Update all the objects in the given non-moving space page. holder is the
  // first object in the page, which could have started in some preceding page.
  void UpdateNonMovingPage(mirror::Object* holder, uint8_t* page)
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Update all the references in the non-moving space.
  void UpdateNonMovingSpace() REQUIRES_SHARED(Locks::mutator_lock_);
  // Compute first-objects for every page in the non-moving space. Pages which
  // doesn't have any live words in them, will have null first-object. The
  // first-object may start in some preceding page, but in that case would
  // overlap with the page-begin boundary.
  void InitNonMovingSpaceFirstObjects() REQUIRES_SHARED(Locks::mutator_lock_);
  // Compuate first-objects for every page in the post-compact moving space.
  // Also, compute the offset within these first-objects from which the contents
  // should start getting copied to the page.
  void InitMovingSpaceFirstObjects(const size_t vec_len) REQUIRES_SHARED(Locks::mutator_lock_);

  // Gather the info related to black allocations from bump-pointer space to
  // enable concurrent sliding of these pages.
  void UpdateMovingSpaceFirstObjects() REQUIRES(Locks::mutator_lock_, Locks::heap_bitmap_lock_);
  // Update first-object info from allocation-stack for non-moving space black
  // allocations.
  void UpdateNonMovingSpaceFirstObjects() REQUIRES(Locks::mutator_lock_, Locks::heap_bitmap_lock_);

  // Slide black page in moving space.
  void SlideBlackPage(mirror::Object* first_obj,
                      const uint32_t first_chunk_size,
                      mirror::Object* next_page_first_obj,
                      uint8_t* const pre_compact_page,
                      uint8_t* dest)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Perform reference-processing and the likes before sweeping the non-movable
  // spaces.
  void ReclaimPhase() REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!Locks::heap_bitmap_lock_);

  // Mark GC-roots (except from immune spaces and thread-stacks) during a STW pause.
  void ReMarkRoots(Runtime* runtime) REQUIRES(Locks::mutator_lock_, Locks::heap_bitmap_lock_);
  // Concurrently mark GC-roots, except from immune spaces.
  void MarkRoots(VisitRootFlags flags) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Collect thread stack roots via a checkpoint.
  void MarkRootsCheckpoint(Thread* self, Runtime* runtime) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Second round of concurrent marking. Mark all gray objects that got dirtied
  // since the first round.
  void PreCleanCards() REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(Locks::heap_bitmap_lock_);

  void MarkNonThreadRoots(Runtime* runtime) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  void MarkConcurrentRoots(VisitRootFlags flags, Runtime* runtime)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(Locks::heap_bitmap_lock_);

  // Traverse through the reachable objects and mark them.
  void MarkReachableObjects() REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Scan (only) immune spaces looking for references into the garbage collected
  // spaces.
  void UpdateAndMarkModUnion() REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Scan mod-union and card tables, covering all the spaces, to identify gray objects.
  // These are in 'minimum age' cards, which is 'kCardAged' in case of concurrent (second round)
  // marking and kCardDirty during the STW pause.
  void ScanGrayObjects(bool paused, uint8_t minimum_age) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Recursively mark dirty objects. Invoked both concurrently as well in a STW
  // pause in PausePhase().
  void RecursiveMarkDirtyObjects(bool paused, uint8_t minimum_age)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Go through all the objects in the mark-stack until it's empty.
  void ProcessMarkStack() override REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  void ExpandMarkStack() REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  template <bool kUpdateLiveWords>
  void ScanObject(mirror::Object* obj) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Push objects to the mark-stack right after successfully marking objects.
  void PushOnMarkStack(mirror::Object* obj)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  // Updae the live-words bitmap as well as add the object size to the
  // offset_vector. Both are required for computation of post-compact addresses.
  void UpdateLivenessInfo(mirror::Object* obj) REQUIRES_SHARED(Locks::mutator_lock_);

  void ProcessReferences(Thread* self)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::heap_bitmap_lock_);

  void MarkObjectNonNull(mirror::Object* obj,
                         mirror::Object* holder = nullptr,
                         MemberOffset offset = MemberOffset(0))
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  void MarkObject(mirror::Object* obj, mirror::Object* holder, MemberOffset offset)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  template <bool kParallel>
  bool MarkObjectNonNullNoPush(mirror::Object* obj,
                               mirror::Object* holder = nullptr,
                               MemberOffset offset = MemberOffset(0))
      REQUIRES(Locks::heap_bitmap_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void Sweep(bool swap_bitmaps) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  void SweepLargeObjects(bool swap_bitmaps) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  void RememberClassAndDexCache(mirror::Object* obj) REQUIRES_SHARED(Locks::mutator_lock_);
  // Perform all kernel operations required for concurrent compaction. Includes
  // mremap to move pre-compact pages to from-space, followed by userfaultfd
  // registration on the moving space.
  void KernelPreparation();
  // Called by thread-pool workers to read uffd_ and process fault events.
  void ConcurrentCompaction(uint8_t* page) REQUIRES_SHARED(Locks::mutator_lock_);
  // Enables fallback mode.
  void SetFallback() { *static_cast<volatile int*>(&uffd_) = kFallbackMode; }

  enum PageState : uint8_t {
    kUncompacted = 0,  // The page has been not compacted yet
    kCompacting       // Some thread (GC or mutator) is compacting the page
  };

  std::unique_ptr<MemMap> compaction_buffers_map_;
  // For checkpoints
  Barrier gc_barrier_;
  // Every object inside the immune spaces is assumed to be marked.
  ImmuneSpaces immune_spaces_;
  // Required only when mark-stack is accessed in shared mode, which happens
  // when collecting thread-stack roots using checkpoint.
  Mutex mark_stack_lock_;
  accounting::ObjectStack* mark_stack_;
  // Special bitmap wherein all the bits corresponding to an object are set.
  // TODO: make LiveWordsBitmap encapsulated in this class rather than a
  // pointer. We tend to access its members in performance-sensitive
  // code-path. Also, use a single MemMap for all the GC's data structures,
  // which we will clear in the end. This would help in limiting the number of
  // VMAs that get created in the kernel.
  std::unique_ptr<LiveWordsBitmap<kAlignment>> live_words_bitmap_;
  // Track GC-roots updated so far in a GC-cycle. This is to confirm that no
  // GC-root is updated twice.
  // TODO: Must be replaced with an efficient mechanism eventually. Or ensure
  // that double updation doesn't happen in the first place.
  std::unordered_set<void*> updated_roots_;
  std::unordered_set<uint32_t> dex_caches_;
  MemMap from_space_map_;
  // Any array of live-bytes in logical chunks of kOffsetChunkSize size
  // in the 'to-be-compacted' space.
  MemMap info_map_;
  // The main space bitmap
  accounting::ContinuousSpaceBitmap* current_space_bitmap_;
  accounting::ContinuousSpaceBitmap* non_moving_space_bitmap_;
  space::ContinuousSpace* non_moving_space_;
  space::BumpPointerSpace* const bump_pointer_space_;
  Thread* thread_running_gc_;
  // Array of pages' compaction status.
  Atomic<PageState>* moving_pages_status_;
  size_t vector_length_;
  size_t live_stack_freeze_size_;

  // For every page in the to-space (post-compact heap) we need to know the
  // first object from which we must compact and/or update references. This is
  // for both non-moving and moving space. Additionally, for the moving-space,
  // we also need the offset within the object from where we need to start
  // copying.
  uint32_t* offset_vector_;
  // For pages before black allocations, every element of this array stores the
  // offset within the space from where the objects need to be copied within a
  // post-compact page.
  // For pages which has black allocations, every element tells size of the
  // first chunk containing black objects within the page.
  uint32_t* pre_compact_offset_moving_space_;
  // For every post-compact page, the element in this array stores the first
  // object, which fully or partially, get copied to the page.
  ObjReference* first_objs_moving_space_;
  // First object for every page. It could be greater than the page's start
  // address or null if the page is empty.
  ObjReference* first_objs_non_moving_space_;
  size_t non_moving_first_objs_count_;
  // Length of first_objs_moving_space_ and pre_compact_offset_moving_space_
  // arrays. Also the number of pages which are to be compacted.
  size_t moving_first_objs_count_;
  // Number of pages consumed by black objects, indicating number of pages to be
  // slid.
  size_t black_page_count_;

  uint8_t* from_space_begin_;
  // moving-space's end pointer at the marking pause. All allocations beyond
  // this will be considered black in the current GC cycle. Aligned up to page
  // size.
  uint8_t* black_allocations_begin_;
  // End of compacted space. Use for computing post-compact addr of black
  // allocated objects. Aligned up to page size.
  uint8_t* post_compact_end_;

  // TODO: Remove once an efficient mechanism to deal with double root updation
  // is incorporated.
  void* stack_addr_;
  void* stack_end_;

  uint8_t* conc_compaction_termination_page_;
  // Userfault file descriptor
  // kFallbackMode value indicates that we are in the fallback mode.
  int uffd_;
  // Used to exit from compaction loop at the end of concurrent compaction
  uint8_t thread_pool_counter_;
  // Set to true when compacting.
  bool compacting_;

  class VerifyRootMarkedVisitor;
  class ScanObjectVisitor;
  class CheckpointMarkThreadRoots;
  template<size_t kBufferSize> class ThreadRootsVisitor;
  class CardModifiedVisitor;
  class RefFieldsVisitor;
  template <bool kCheckBegin, bool kCheckEnd> class RefsUpdateVisitor;
  class NativeRootsUpdateVisitor;
  class ImmuneSpaceUpdateObjVisitor;
  class ConcurrentCompactionGcTask;

  DISALLOW_IMPLICIT_CONSTRUCTORS(MarkCompact);
};

}  // namespace collector
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_H_
