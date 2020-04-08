/*
 * Copyright 2016 The Android Open Source Project
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

#include "base/atomic.h"
#include "barrier.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "garbage_collector.h"
#include "gc/accounting/heap_bitmap.h"
#include "gc_root.h"
#include "immune_spaces.h"
#include "offsets.h"

namespace art {
namespace gc {

class Heap;

namespace space {
class BumpPointerSpace;
}  // namespace space

namespace collector {
class MarkCompact : public GarbageCollector {
 public:
  explicit MarkCompact(Heap* heap);

  ~MarkCompact() {}

  void RunPhases() override;

  GcType GetGcType() const override {
    return kGcTypeFull;
  }

  CollectorType GetCollectorType() const override {
    return kCollectorTypeCMC;
  }

  mirror::Object* MarkObject(mirror::Object* obj) override;

  void MarkHeapReference(mirror::HeapReference<mirror::Object>* obj,
                         bool do_atomic_update) override;

  void VisitRoots(mirror::Object*** roots,
                  size_t count,
                  const RootInfo& info) override;
  void VisitRoots(mirror::CompressedReference<mirror::Object>** roots,
                  size_t count,
                  const RootInfo& info) override;
  mirror::Object* IsMarked(mirror::Object* obj) override;
  bool IsNullOrMarkedHeapReference(mirror::HeapReference<mirror::Object>* obj,
                                   bool do_atomic_update) override;
  void DelayReferenceReferent(ObjPtr<mirror::Class> klass,
                              ObjPtr<mirror::Reference> reference) override;

 protected:
  void RunPhases() override;

 private:
  void InitializePhase();
  void BindAndResetBitmaps() REQUIRES(Locks::heap_bitmap_lock_);
  void UpdateAndMarkModUnion() REQUIRES(Locks::heap_bitmap_lock_);
  void MarkingPhase() REQUIRES_SHARED(Locks::mutator_lock_);
  void PausePhase();
  void CompactionPhase();
  void ReclaimPhase();
  void FinishPhase();
  void ReMarkRoots(Runtime* runtime);
  void MarkRoots();
  void PreCleanCards();
  void Sweep(bool swap_bitmaps);
  void SweeoLargeObjects(bool swap_bitmaps);
  void MarkRootsCheckpoint(Thread* self, Runtime* runtime);
  void MarkNonThreadRoots(Runtime* runtime);
  void MarkConcurrentRoots(VisitRootFlags flags);

  void MarkReachableObjects();
  void ScanGrayObjects(bool paused, uint8_t minimum_age);
  void ScanObject(mirror::Object* obj);
  void RecursiveMarkDirtyObjects(bool paused, uint8_t minimum_age);
  void ProcessMarkStack();
  void ExpandMarkStack();
  void PushOnMarkStack(mirror::Object* obj);

  void MarkObjectNonNull(mirror::Object* obj,
                         mirror::Object* holder = nullptr,
                         MemberOffset offset = MemberOffset(0));

  void MarkObject(mirror::Object* obj,
                  mirror::Object* holder,
                  MemberOffset offset);

  template <bool kParallel>
  void MarkObjectNonNullNoPush(mirror::Object* obj,
                               mirror::Object* holder = nullptr,
                               MemberOffset offset = MemberOffset(0))
      REQUIRES(Locks::heap_bitmap_lock_)
      REQUIRES(!mark_stack_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  std::unique_ptr<Barrier> gc_barrier_;
  std::unique_ptr<accounting::ObjectStack> mark_stack_;
  Thread* thread_running_gc_;
  const space::BumpPointerSpace* bump_pointer_space_;
  // Current space, we check this space first to avoid searching for the appropriate space for an
  // object.
  accounting::ContinuousSpaceBitmap* current_space_bitmap_;
  accounting::ContinuousSpaceBitmap* non_moving_space_bitmap_;
  // Cache the heap's mark bitmap to prevent having to do 2 loads during slow path marking.
  accounting::HeapBitmap* heap_mark_bitmap_;
  // Every object inside the immune spaces is assumed to be marked. Immune spaces that aren't in the
  // immune region are handled by the normal marking logic.
  ImmuneSpaces immune_spaces_;

  class ScanObjectVisitor;
  class CheckpointMarkThreadRoots;
  class CardModifiedVisitor;
  class RefFieldsVisitor;

  DISALLOW_IMPLICIT_CONSTRUCTORS(MarkCompact);
};

}  // namespace collector
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_H_
