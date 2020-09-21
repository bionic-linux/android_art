/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef ART_COMPILER_OPTIMIZING_LOAD_STORE_ANALYSIS_H_
#define ART_COMPILER_OPTIMIZING_LOAD_STORE_ANALYSIS_H_

#include <array>
#include <cstdio>
#include <deque>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "base/arena_allocator.h"
#include "base/arena_bit_vector.h"
#include "base/arena_containers.h"
#include "base/array_slice.h"
#include "base/bit_vector-inl.h"
#include "base/iteration_range.h"
#include "base/scoped_arena_allocator.h"
#include "base/scoped_arena_containers.h"
#include "base/stl_util.h"
#include "base/transform_iterator.h"
#include "escape.h"
#include "nodes.h"
#include "optimization.h"
#include "optimizing/optimizing_compiler_stats.h"
#include "scoped_thread_state_change.h"

namespace art {
// A representation of a particular section of the graph. Only some executions
// might go through this subgraph. The graph is split into an excluded and included area.
class ExecutionSubgraph : public ArenaObject<kArenaAllocLSA> {
 public:
  // A set of connected blocks which are connected and all unreachable.
  struct ExcludedCohort : public ArenaObject<kArenaAllocLSA> {
   public:
    ExcludedCohort(ExcludedCohort&&) = default;
    ExcludedCohort(const ExcludedCohort&) = delete;
    explicit ExcludedCohort(ScopedArenaAllocator* allocator, HGraph* graph)
        : graph_(graph),
          entry_blocks_(allocator, graph_->GetBlocks().size(), false, kArenaAllocLSE),
          exit_blocks_(allocator, graph_->GetBlocks().size(), false, kArenaAllocLSE),
          blocks_(allocator, graph_->GetBlocks().size(), false, kArenaAllocLSE) { }

    ~ExcludedCohort() = default;

   private:
    auto BlockIterRange(const ArenaBitVector& bv) const {
      auto indexes = bv.Indexes();
      HGraph* graph = graph_;
      auto res = MakeTransformRange(indexes, [graph](uint32_t idx) -> HBasicBlock* {
        auto ret = graph->GetBlocks()[idx];
        DCHECK(ret != nullptr);
        return ret;
      });
      return res;
    }

   public:
    // All blocks in the cohort.
    auto Blocks() const {
      return BlockIterRange(blocks_);
    }

    // Blocks that have predecessors outside of the cohort. These blocks will
    // need to have PHIs/control-flow added to create the escaping value.
    auto EntryBlocks() const {
      return BlockIterRange(entry_blocks_);
    }

    // Blocks that have successors outside of the cohort. The successors of
    // these blocks will need to have PHI's to restore state.
    auto ExitBlocks() const {
      return BlockIterRange(exit_blocks_);
    }

    bool operator==(const ExcludedCohort& other) const {
      return blocks_.Equal(&other.blocks_);
    }

    bool ContainsBlock(const HBasicBlock* blk) const {
      return blocks_.IsBitSet(blk->GetBlockId());
    }

    bool SucceedsBlock(const HBasicBlock* blk) const {
      if (ContainsBlock(blk)) {
        return false;
      }
      auto idxs = entry_blocks_.Indexes();
      return std::any_of(idxs.begin(), idxs.end(), [&](auto entry) -> bool {
        return blk->GetGraph()->PathBetween(blk->GetBlockId(), entry);
      });
    }
    bool PrecedesBlock(const HBasicBlock* blk) const {
      if (ContainsBlock(blk)) {
        return false;
      }
      auto idxs = exit_blocks_.Indexes();
      return std::any_of(idxs.begin(), idxs.end(), [&](auto exit) -> bool {
        return blk->GetGraph()->PathBetween(exit, blk->GetBlockId());
      });
    }

    void Dump(std::ostream& os) const;

   private:
    ExcludedCohort() = delete;

    HGraph* graph_;
    ArenaBitVector entry_blocks_;
    ArenaBitVector exit_blocks_;
    ArenaBitVector blocks_;

    friend class ExecutionSubgraph;
    friend class LoadStoreAnalysisTest;
  };

  // The number of successors we can track on a single block. Graphs which
  // contain a block with a branching factor greater than this will not be
  // analysed.
  static constexpr uint32_t kMaxFilterableSuccessors = 8;

  ExecutionSubgraph(HGraph* graph, ScopedArenaAllocator* allocator)
      : graph_(graph),
        allocator_(allocator),
        allowed_successors_(std::less<const HBasicBlock*>(), allocator_->Adapter(kArenaAllocLSA)),
        unreachable_blocks_vec_(allocator_, graph_->GetBlocks().size(), false, kArenaAllocLSA),
        valid_(std::all_of(graph->GetBlocks().begin(),
                           graph->GetBlocks().end(),
                           [](HBasicBlock* it) {
                             return it == nullptr ||
                                    it->GetSuccessors().size() < kMaxFilterableSuccessors;
                           })),
        needs_prune_(false),
        finalized_(false) {}

  void Invalidate() {
    valid_ = false;
  }

  bool ContainsBlock(const HBasicBlock* blk) const {
    DCHECK(!finalized_ || !needs_prune_) << "finalized: " << finalized_;
    if (!valid_) {
      return false;
    }
    return !unreachable_blocks_vec_.IsBitSet(blk->GetBlockId());
  }

  void RemoveBlock(const HBasicBlock* to_remove) {
    if (!valid_) {
      return;
    }
    unreachable_blocks_vec_.SetBit(to_remove->GetBlockId());
    for (HBasicBlock* pred : to_remove->GetPredecessors()) {
      std::bitset<kMaxFilterableSuccessors> set;
      for (auto [succ, i] : ZipCount(MakeIterationRange(pred->GetSuccessors()))) {
        if (succ != to_remove) {
          set.set(i);
        } else {
          set.reset(i);
        }
      }
      LimitBlockSuccessors(pred, set);
    }
  }

  void Finalize() {
    Prune();
    RemoveConcavity();
    finalized_ = true;
  }

  auto UnreachableBlocks() const {
    auto idxs = unreachable_blocks_vec_.Indexes();
    HGraph* graph = graph_;
    return MakeTransformRange(idxs, [graph](uint32_t idx) {
      DCHECK_LT(idx, graph->GetBlocks().size());
      DCHECK(graph->GetBlocks()[idx] != nullptr);
      return graph->GetBlocks()[idx];
    });
  }

  // Returns true if all allowed execution paths from start eventually reach 'end' (or diverge).
  bool IsValid() const {
    return valid_;
  }

  ArrayRef<const ExcludedCohort> GetExcludedCohorts() const {
    DCHECK(!valid_ || !needs_prune_);
    if (!valid_ || unreachable_blocks_vec_.NumSetBits() == 0) {
      return ArrayRef<const ExcludedCohort>();
    } else {
      return ArrayRef<const ExcludedCohort>(*excluded_list_);
    }
  }

  // Returns an iterator over reachable blocks (filtered as we go). This is primarially for testing.
  // NB This is auto because it makes it simpler to write. The type is
  // FilterIterator<typeof(graph_->GetBlocks().begin()), typeof([this](HBasicBlock* v) -> bool)>
  auto ReachableBlocks() const {
    return Filter(MakeIterationRange(graph_->GetBlocks()), [this](HBasicBlock* v) -> bool {
      static_assert(std::is_same_v<typeof(this), const ExecutionSubgraph*>, "Bad substitution");
      return v != nullptr && ContainsBlock(v);
    });
  }

 private:
  std::bitset<kMaxFilterableSuccessors> GetAllowedSuccessors(const HBasicBlock* blk) const {
    auto it = allowed_successors_.find(blk);
    if (it == allowed_successors_.end()) {
      return ~(std::bitset<kMaxFilterableSuccessors>());
    } else {
      return it->second;
    }
  }

  void RemoveConcavity() {
    if (!valid_) {
      return;
    }
    DCHECK(!needs_prune_);
    ArenaBitVector initial(allocator_, graph_->GetBlocks().size(), false, kArenaAllocLSA);
    initial.Copy(&unreachable_blocks_vec_);
    for (auto blk : graph_->GetBlocks()) {
      if (blk == nullptr || initial.IsBitSet(blk->GetBlockId())) {
        continue;
      }
      for (auto skipped1 : initial.Indexes()) {
        if (LIKELY(!graph_->PathBetween(skipped1, blk->GetBlockId()))) {
          continue;
        }
        for (auto skipped2 : initial.Indexes()) {
          if (graph_->PathBetween(blk->GetBlockId(), skipped2)) {
            RemoveBlock(blk);
          }
        }
      }
    }
    Prune();
  }

  // Removes sink nodes.
  void Prune() {
    if (!valid_) {
      return;
    }
    needs_prune_ = false;
    // Grab blocks further up the tree.
    ScopedArenaVector<std::optional<std::bitset<kMaxFilterableSuccessors>>> results(
        graph_->GetBlocks().size(), allocator_->Adapter(kArenaAllocLSA));
    ArenaBitVector visiting(allocator_, graph_->GetBlocks().size(), false, kArenaAllocLSE);
    unreachable_blocks_vec_.ClearAllBits();
    results[graph_->GetExitBlock()->GetBlockId()] = ~(std::bitset<kMaxFilterableSuccessors>());
    // Fills up the 'results' map with what we need to add to update
    // allowed_successors to in order to prune sink nodes.
    // NB C++ Doesn't like recursive calls of lambdas like this so just pass
    // down the 'reaches_end' function explicitly.
    auto reaches_end = [&](const HBasicBlock* blk, auto reaches_end_recur) {
      auto it = results[blk->GetBlockId()];
      if (visiting.IsBitSet(blk->GetBlockId())) {
        // We are in a loop so the block is live.
        return true;
      } else if (it) {
        CHECK(it->any() || unreachable_blocks_vec_.IsBitSet(blk->GetBlockId()));
        return it->any();
      }
      visiting.SetBit(blk->GetBlockId());
      // what we currently allow.
      std::bitset<kMaxFilterableSuccessors> succ_bitmap = GetAllowedSuccessors(blk);
      // The new allowed successors. We use visiting to break loops so we don't
      // need to figure out how many bits to turn on.
      results[blk->GetBlockId()] = std::bitset<kMaxFilterableSuccessors>();
      std::bitset<kMaxFilterableSuccessors>& result = results[blk->GetBlockId()].value();
      for (auto [succ, i] :
           ZipCount(MakeIterationRange(blk->GetSuccessors().begin(), blk->GetSuccessors().end()))) {
        if (succ_bitmap.test(i) && reaches_end_recur(succ, reaches_end_recur)) {
          result.set(i);
        }
      }
      visiting.ClearBit(blk->GetBlockId());
      bool res = result.any();
      if (!res) {
        // If this is a sink block it will be removed from the successors of all
        // its predecessors and made unreachable.
        CHECK(blk != nullptr);
        unreachable_blocks_vec_.SetBit(blk->GetBlockId());
      }
      return res;
    };
    bool start_reaches_end = reaches_end(graph_->GetEntryBlock(), reaches_end);
    if (!start_reaches_end) {
      valid_ = false;
      return;
    }
    for (const HBasicBlock* blk : graph_->GetBlocks()) {
      if (blk != nullptr && !results[blk->GetBlockId()] && blk != graph_->GetEntryBlock()) {
        // We never visited this block, must be unreachable.
        unreachable_blocks_vec_.SetBit(blk->GetBlockId());
      }
    }
    results[graph_->GetExitBlock()->GetBlockId()].reset();
    allowed_successors_.clear();
    for (auto [v, id] : ZipCount(MakeIterationRange(results))) {
      if (!v) {
        continue;
      }
      HBasicBlock* block = graph_->GetBlocks()[id];
      if (v->count() != block->GetSuccessors().size()) {
        allowed_successors_.Put(block, *v);
      }
    }
    RecalculateExcludedCohort();
  }

  void LimitBlockSuccessors(const HBasicBlock* block,
                            std::bitset<kMaxFilterableSuccessors> allowed) {
    needs_prune_ = true;
    allowed_successors_.GetOrCreate(
        block, []() { return ~std::bitset<kMaxFilterableSuccessors>(); }) &= allowed;
  }

  void RecalculateExcludedCohort() {
    DCHECK(!needs_prune_);
    excluded_list_.emplace(allocator_->Adapter(kArenaAllocLSA));
    ScopedArenaVector<ExcludedCohort>& res = excluded_list_.value();
    // Make a copy of unreachable_blocks_;
    ArenaBitVector unreachable(allocator_, graph_->GetBlocks().size(), false, kArenaAllocLSA);
    unreachable.Copy(&unreachable_blocks_vec_);
    // Split cohorts with union-find
    while (unreachable.NumSetBits() > 0) {
      res.emplace_back(allocator_, graph_);
      ExcludedCohort& cohort = res.back();
      // We don't allocate except for the queue beyond here so create another arena to save memory.
      ScopedArenaAllocator alloc(graph_->GetArenaStack());
      ScopedArenaQueue<const HBasicBlock*> worklist(alloc.Adapter(kArenaAllocLSA));
      // Select a random node
      const HBasicBlock* first = graph_->GetBlocks()[unreachable.GetHighestBitSet()];
      worklist.push(first);
      do {
        // Flood-fill both forwards and backwards.
        const HBasicBlock* cur = worklist.front();
        worklist.pop();
        if (!unreachable.IsBitSet(cur->GetBlockId())) {
          // Already visited or reachable somewhere else.
          continue;
        }
        unreachable.ClearBit(cur->GetBlockId());
        if (cur == nullptr) {
          continue;
        }
        cohort.blocks_.SetBit(cur->GetBlockId());
        // don't bother filtering here, it's done next go-around
        for (const HBasicBlock* pred : cur->GetPredecessors()) {
          worklist.push(pred);
        }
        for (const HBasicBlock* succ : cur->GetSuccessors()) {
          worklist.push(succ);
        }
      } while (!worklist.empty());
    }
    // Figure out entry & exit nodes.
    for (ExcludedCohort& cohort : res) {
      CHECK_GT(cohort.blocks_.NumSetBits(), 0u);
      auto is_external = [&](const HBasicBlock* ext) -> bool {
        return !cohort.blocks_.IsBitSet(ext->GetBlockId());
      };
      for (const HBasicBlock* blk : cohort.Blocks()) {
        auto preds = blk->GetPredecessors();
        auto succs = blk->GetSuccessors();
        if (std::any_of(preds.cbegin(), preds.cend(), is_external)) {
          cohort.entry_blocks_.SetBit(blk->GetBlockId());
        }
        if (std::any_of(succs.cbegin(), succs.cend(), is_external)) {
          cohort.exit_blocks_.SetBit(blk->GetBlockId());
        }
      }
    }
  }

  HGraph* graph_;
  ScopedArenaAllocator* allocator_;
  ScopedArenaSafeMap<const HBasicBlock*, std::bitset<kMaxFilterableSuccessors>> allowed_successors_;
  ArenaBitVector unreachable_blocks_vec_;
  mutable std::optional<ScopedArenaVector<ExcludedCohort>> excluded_list_;
  bool valid_;
  bool needs_prune_;
  bool finalized_;

  DISALLOW_COPY_AND_ASSIGN(ExecutionSubgraph);
};

std::ostream& operator<<(std::ostream& os, const ExecutionSubgraph::ExcludedCohort& ex);

// A ReferenceInfo contains additional info about a reference such as
// whether it's a singleton, returned, etc.
class ReferenceInfo : public DeletableArenaObject<kArenaAllocLSA> {
 public:
  ReferenceInfo(HInstruction* reference,
                ScopedArenaAllocator* allocator,
                size_t pos,
                bool for_elimination)
      : reference_(reference),
        position_(pos),
        is_singleton_(true),
        is_singleton_and_not_returned_(true),
        is_singleton_and_not_deopt_visible_(true),
        subgraph_(reference->GetBlock()->GetGraph(), allocator) {
    // TODO We can do this in one pass.
    // TODO NewArray is possible but will need to get a handle on how to deal with the dynamic loads
    // for now just ignore it.
    bool can_be_partial =
        for_elimination && (/* reference_->IsNewArray() || */ reference_->IsNewInstance());
    if (can_be_partial) {
      FuncEscapeVisitor func([&](HInstruction* inst) {
        return HandleEscapes(inst);
      });
      VisitEscapes(reference_, func);
    }
    CalculateEscape(reference_,
                    nullptr,
                    &is_singleton_,
                    &is_singleton_and_not_returned_,
                    &is_singleton_and_not_deopt_visible_);
    if (can_be_partial) {
      // This is to mark writes to partially escaped values as also part of the escaped subset.
      // TODO We can avoid this if we have a 'ConditionalWrite' instruction. Will reqire testing
      //      to see if the additional branches are worth it.
      PrunePartialEscapeWrites();
      subgraph_.Finalize();
    } else {
      subgraph_.Invalidate();
    }
  }

  const ExecutionSubgraph* GetNoEscapeSubgraph() const {
    return &subgraph_;
  }

  HInstruction* GetReference() const {
    return reference_;
  }

  size_t GetPosition() const {
    return position_;
  }

  // Returns true if reference_ is the only name that can refer to its value during
  // the lifetime of the method. So it's guaranteed to not have any alias in
  // the method (including its callees).
  bool IsSingleton() const {
    return is_singleton_;
  }

  // This is a singleton and there are paths that don't escape the method
  bool IsPartialSingleton() const {
    auto ref = GetReference();
    // TODO NewArray is possible but will need to get a handle on how to deal with the dynamic loads
    // for now just ignore it.
    return (/* ref->IsNewArray() || */ ref->IsNewInstance()) && GetNoEscapeSubgraph()->IsValid();
  }

  // Returns true if reference_ is a singleton and not returned to the caller or
  // used as an environment local of an HDeoptimize instruction.
  // The allocation and stores into reference_ may be eliminated for such cases.
  bool IsSingletonAndRemovable() const {
    return is_singleton_and_not_returned_ && is_singleton_and_not_deopt_visible_;
  }

  // Returns true if reference_ is a singleton and returned to the caller or
  // used as an environment local of an HDeoptimize instruction.
  bool IsSingletonAndNonRemovable() const {
    return is_singleton_ &&
           (!is_singleton_and_not_returned_ || !is_singleton_and_not_deopt_visible_);
  }

 private:
  bool HandleEscapes(HInstruction* escape) {
    subgraph_.RemoveBlock(escape->GetBlock());
    return true;
  }

  // Make sure we mark any writes/potential writes to heap-locations within partially
  // escaped values as escaping.
  void PrunePartialEscapeWrites() {
    if (!subgraph_.IsValid()) {
      // All paths escape.
      return;
    }
    std::unordered_set<const HBasicBlock*> additional_exclusions;
    for (const HUseListNode<HInstruction*>& use : reference_->GetUses()) {
      const HInstruction* user = use.GetUser();
      if (additional_exclusions.find(user->GetBlock()) == additional_exclusions.end() &&
          subgraph_.ContainsBlock(user->GetBlock()) &&
          (user->IsUnresolvedInstanceFieldSet() || user->IsUnresolvedStaticFieldSet() ||
           user->IsInstanceFieldSet() || user->IsStaticFieldSet() || user->IsArraySet()) &&
          (reference_ == user->InputAt(0)) &&
          std::any_of(subgraph_.UnreachableBlocks().begin(),
                      subgraph_.UnreachableBlocks().end(),
                      [&](const HBasicBlock* excluded) -> bool {
                        return reference_->GetBlock()->GetGraph()->PathBetween(excluded,
                                                                               user->GetBlock());
                      })) {
        // This object had memory written to it somewhere, if it escaped along
        // some paths prior to the current block this write also counts as an
        additional_exclusions.insert(user->GetBlock());
      }
    }
    if (UNLIKELY(!additional_exclusions.empty())) {
      for (auto exc : additional_exclusions) {
        subgraph_.RemoveBlock(exc);
      }
    }
  }

  HInstruction* const reference_;
  const size_t position_;  // position in HeapLocationCollector's ref_info_array_.

  // Can only be referred to by a single name in the method.
  bool is_singleton_;
  // Is singleton and not returned to caller.
  bool is_singleton_and_not_returned_;
  // Is singleton and not used as an environment local of HDeoptimize.
  bool is_singleton_and_not_deopt_visible_;

  ExecutionSubgraph subgraph_;

  DISALLOW_COPY_AND_ASSIGN(ReferenceInfo);
};

// A heap location is a reference-offset/index pair that a value can be loaded from
// or stored to.
class HeapLocation : public ArenaObject<kArenaAllocLSA> {
 public:
  static constexpr size_t kInvalidFieldOffset = -1;
  // Default value for heap locations which are not vector data.
  static constexpr size_t kScalar = 1;
  // TODO: more fine-grained array types.
  static constexpr int16_t kDeclaringClassDefIndexForArrays = -1;

  HeapLocation(ReferenceInfo* ref_info,
               DataType::Type type,
               size_t offset,
               HInstruction* index,
               size_t vector_length,
               int16_t declaring_class_def_index)
      : ref_info_(ref_info),
        type_(DataType::ToSigned(type)),
        offset_(offset),
        index_(index),
        vector_length_(vector_length),
        declaring_class_def_index_(declaring_class_def_index),
        has_aliased_locations_(false) {
    DCHECK(ref_info != nullptr);
    DCHECK((offset == kInvalidFieldOffset && index != nullptr) ||
           (offset != kInvalidFieldOffset && index == nullptr));
  }

  ReferenceInfo* GetReferenceInfo() const { return ref_info_; }
  DataType::Type GetType() const { return type_; }
  size_t GetOffset() const { return offset_; }
  HInstruction* GetIndex() const { return index_; }
  size_t GetVectorLength() const { return vector_length_; }

  // Returns the definition of declaring class' dex index.
  // It's kDeclaringClassDefIndexForArrays for an array element.
  int16_t GetDeclaringClassDefIndex() const {
    return declaring_class_def_index_;
  }

  bool IsArray() const {
    return index_ != nullptr;
  }

  bool HasAliasedLocations() const {
    return has_aliased_locations_;
  }

  void SetHasAliasedLocations(bool val) {
    has_aliased_locations_ = val;
  }

 private:
  // Reference for instance/static field, array element or vector data.
  ReferenceInfo* const ref_info_;
  // Type of data residing at HeapLocation (always signed for integral
  // data since e.g. a[i] and a[i] & 0xff are represented by differently
  // signed types; char vs short are disambiguated through the reference).
  const DataType::Type type_;
  // Offset of static/instance field.
  // Invalid when this HeapLocation is not field.
  const size_t offset_;
  // Index of an array element or starting index of vector data.
  // Invalid when this HeapLocation is not array.
  HInstruction* const index_;
  // Vector length of vector data.
  // When this HeapLocation is not vector data, it's value is kScalar.
  const size_t vector_length_;
  // Declaring class's def's dex index.
  // Invalid when this HeapLocation is not field access.
  const int16_t declaring_class_def_index_;

  // Has aliased heap locations in the method, due to either the
  // reference is aliased or the array element is aliased via different
  // index names.
  bool has_aliased_locations_;

  DISALLOW_COPY_AND_ASSIGN(HeapLocation);
};

// A HeapLocationCollector collects all relevant heap locations and keeps
// an aliasing matrix for all locations.
class HeapLocationCollector : public HGraphVisitor {
 public:
  static constexpr size_t kHeapLocationNotFound = -1;
  // Start with a single uint32_t word. That's enough bits for pair-wise
  // aliasing matrix of 8 heap locations.
  static constexpr uint32_t kInitialAliasingMatrixBitVectorSize = 32;

  explicit HeapLocationCollector(HGraph* graph,
                                 ScopedArenaAllocator* allocator,
                                 bool for_elimination = true)
      : HGraphVisitor(graph),
        allocator_(allocator),
        ref_info_array_(allocator->Adapter(kArenaAllocLSA)),
        heap_locations_(allocator->Adapter(kArenaAllocLSA)),
        aliasing_matrix_(allocator, kInitialAliasingMatrixBitVectorSize, true, kArenaAllocLSA),
        has_heap_stores_(false),
        has_volatile_(false),
        has_monitor_operations_(false),
        for_elimination_(for_elimination) {
    aliasing_matrix_.ClearAllBits();
  }

  ~HeapLocationCollector() {
    CleanUp();
  }

  void CleanUp() {
    heap_locations_.clear();
    STLDeleteContainerPointers(ref_info_array_.begin(), ref_info_array_.end());
    ref_info_array_.clear();
  }

  size_t GetNumberOfHeapLocations() const {
    return heap_locations_.size();
  }

  HeapLocation* GetHeapLocation(size_t index) const {
    return heap_locations_[index];
  }

  HInstruction* HuntForOriginalReference(HInstruction* ref) const {
    // An original reference can be transformed by instructions like:
    //   i0 NewArray
    //   i1 HInstruction(i0)  <-- NullCheck, BoundType, IntermediateAddress.
    //   i2 ArrayGet(i1, index)
    DCHECK(ref != nullptr);
    while (ref->IsNullCheck() || ref->IsBoundType() || ref->IsIntermediateAddress()) {
      ref = ref->InputAt(0);
    }
    return ref;
  }

  ReferenceInfo* FindReferenceInfoOf(HInstruction* ref) const {
    for (size_t i = 0; i < ref_info_array_.size(); i++) {
      ReferenceInfo* ref_info = ref_info_array_[i];
      if (ref_info->GetReference() == ref) {
        DCHECK_EQ(i, ref_info->GetPosition());
        return ref_info;
      }
    }
    return nullptr;
  }

  size_t GetFieldHeapLocation(HInstruction* object, const FieldInfo* field) const {
    DCHECK(object != nullptr);
    DCHECK(field != nullptr);
    return FindHeapLocationIndex(FindReferenceInfoOf(HuntForOriginalReference(object)),
                                 field->GetFieldType(),
                                 field->GetFieldOffset().SizeValue(),
                                 nullptr,
                                 HeapLocation::kScalar,
                                 field->GetDeclaringClassDefIndex());
  }

  size_t GetArrayHeapLocation(HInstruction* instruction) const {
    DCHECK(instruction != nullptr);
    HInstruction* array = instruction->InputAt(0);
    HInstruction* index = instruction->InputAt(1);
    DataType::Type type = instruction->GetType();
    size_t vector_length = HeapLocation::kScalar;
    if (instruction->IsArraySet()) {
      type = instruction->AsArraySet()->GetComponentType();
    } else if (instruction->IsVecStore() ||
               instruction->IsVecLoad()) {
      HVecOperation* vec_op = instruction->AsVecOperation();
      type = vec_op->GetPackedType();
      vector_length = vec_op->GetVectorLength();
    } else {
      DCHECK(instruction->IsArrayGet());
    }
    return FindHeapLocationIndex(FindReferenceInfoOf(HuntForOriginalReference(array)),
                                 type,
                                 HeapLocation::kInvalidFieldOffset,
                                 index,
                                 vector_length,
                                 HeapLocation::kDeclaringClassDefIndexForArrays);
  }

  bool HasHeapStores() const {
    return has_heap_stores_;
  }

  bool HasVolatile() const {
    return has_volatile_;
  }

  bool HasMonitorOps() const {
    return has_monitor_operations_;
  }

  // Find and return the heap location index in heap_locations_.
  // NOTE: When heap locations are created, potentially aliasing/overlapping
  // accesses are given different indexes. This find function also
  // doesn't take aliasing/overlapping into account. For example,
  // this function returns three different indexes for:
  // - ref_info=array, index=i, vector_length=kScalar;
  // - ref_info=array, index=i, vector_length=2;
  // - ref_info=array, index=i, vector_length=4;
  // In later analysis, ComputeMayAlias() and MayAlias() compute and tell whether
  // these indexes alias.
  size_t FindHeapLocationIndex(ReferenceInfo* ref_info,
                               DataType::Type type,
                               size_t offset,
                               HInstruction* index,
                               size_t vector_length,
                               int16_t declaring_class_def_index) const {
    DataType::Type lookup_type = DataType::ToSigned(type);
    for (size_t i = 0; i < heap_locations_.size(); i++) {
      HeapLocation* loc = heap_locations_[i];
      if (loc->GetReferenceInfo() == ref_info &&
          loc->GetType() == lookup_type &&
          loc->GetOffset() == offset &&
          loc->GetIndex() == index &&
          loc->GetVectorLength() == vector_length &&
          loc->GetDeclaringClassDefIndex() == declaring_class_def_index) {
        return i;
      }
    }
    return kHeapLocationNotFound;
  }

  bool InstructionEligibleForLSERemoval(HInstruction* inst) {
    if (inst->IsNewInstance()) {
      return !inst->AsNewInstance()->NeedsChecks();
    } else if (inst->IsNewArray()) {
      return inst->AsNewArray()->GetLength()->IsIntConstant() &&
             inst->AsNewArray()->GetLength()->AsIntConstant()->GetValue() >= 0 &&
             std::all_of(inst->GetUses().cbegin(),
                         inst->GetUses().cend(),
                         [&](const HUseListNode<HInstruction*>& user) {
                           if (user.GetUser()->IsArrayGet() || user.GetUser()->IsArraySet()) {
                             return user.GetUser()->InputAt(1)->IsIntConstant();
                           }
                           return true;
                         });
    } else {
      return false;
    }
  }

  // Get some estimated statistics based on our analysis.
  void DumpReferenceStats(OptimizingCompilerStats* stats) {
    if (stats == nullptr) {
      return;
    }
    std::vector<bool> seen_instructions(GetGraph()->GetCurrentInstructionId(), false);
    for (auto hl : heap_locations_) {
      auto ri = hl->GetReferenceInfo();
      if (ri == nullptr || seen_instructions[ri->GetReference()->GetId()]) {
        continue;
      }
      auto instruction = ri->GetReference();
      seen_instructions[instruction->GetId()] = true;
      if (ri->IsSingletonAndRemovable()) {
        if (InstructionEligibleForLSERemoval(instruction)) {
          MaybeRecordStat(stats, MethodCompilationStat::kFullLSEPossible);
        }
      }
      if (ri->IsPartialSingleton() && instruction->IsNewInstance() &&
          !ri->GetNoEscapeSubgraph()->GetExcludedCohorts().empty() &&
          InstructionEligibleForLSERemoval(instruction)) {
        MaybeRecordStat(stats, MethodCompilationStat::kPartialLSEPossible);
      }
    }
  }

  // Returns true if heap_locations_[index1] and heap_locations_[index2] may alias.
  bool MayAlias(size_t index1, size_t index2) const {
    if (index1 < index2) {
      return aliasing_matrix_.IsBitSet(AliasingMatrixPosition(index1, index2));
    } else if (index1 > index2) {
      return aliasing_matrix_.IsBitSet(AliasingMatrixPosition(index2, index1));
    } else {
      DCHECK(false) << "index1 and index2 are expected to be different";
      return true;
    }
  }

  void BuildAliasingMatrix() {
    const size_t number_of_locations = heap_locations_.size();
    if (number_of_locations == 0) {
      return;
    }
    size_t pos = 0;
    // Compute aliasing info between every pair of different heap locations.
    // Save the result in a matrix represented as a BitVector.
    for (size_t i = 0; i < number_of_locations - 1; i++) {
      for (size_t j = i + 1; j < number_of_locations; j++) {
        if (ComputeMayAlias(i, j)) {
          aliasing_matrix_.SetBit(CheckedAliasingMatrixPosition(i, j, pos));
        }
        pos++;
      }
    }
  }

 private:
  // An allocation cannot alias with a name which already exists at the point
  // of the allocation, such as a parameter or a load happening before the allocation.
  bool MayAliasWithPreexistenceChecking(ReferenceInfo* ref_info1, ReferenceInfo* ref_info2) const {
    if (ref_info1->GetReference()->IsNewInstance() || ref_info1->GetReference()->IsNewArray()) {
      // Any reference that can alias with the allocation must appear after it in the block/in
      // the block's successors. In reverse post order, those instructions will be visited after
      // the allocation.
      return ref_info2->GetPosition() >= ref_info1->GetPosition();
    }
    return true;
  }

  bool CanReferencesAlias(ReferenceInfo* ref_info1, ReferenceInfo* ref_info2) const {
    if (ref_info1 == ref_info2) {
      return true;
    } else if (ref_info1->IsSingleton()) {
      return false;
    } else if (ref_info2->IsSingleton()) {
      return false;
    } else if (!MayAliasWithPreexistenceChecking(ref_info1, ref_info2) ||
        !MayAliasWithPreexistenceChecking(ref_info2, ref_info1)) {
      return false;
    }
    return true;
  }

  bool CanArrayElementsAlias(const HInstruction* idx1,
                             const size_t vector_length1,
                             const HInstruction* idx2,
                             const size_t vector_length2) const;

  // `index1` and `index2` are indices in the array of collected heap locations.
  // Returns the position in the bit vector that tracks whether the two heap
  // locations may alias.
  size_t AliasingMatrixPosition(size_t index1, size_t index2) const {
    DCHECK(index2 > index1);
    const size_t number_of_locations = heap_locations_.size();
    // It's (num_of_locations - 1) + ... + (num_of_locations - index1) + (index2 - index1 - 1).
    return (number_of_locations * index1 - (1 + index1) * index1 / 2 + (index2 - index1 - 1));
  }

  // An additional position is passed in to make sure the calculated position is correct.
  size_t CheckedAliasingMatrixPosition(size_t index1, size_t index2, size_t position) {
    size_t calculated_position = AliasingMatrixPosition(index1, index2);
    DCHECK_EQ(calculated_position, position);
    return calculated_position;
  }

  // Compute if two locations may alias to each other.
  bool ComputeMayAlias(size_t index1, size_t index2) const {
    DCHECK_NE(index1, index2);
    HeapLocation* loc1 = heap_locations_[index1];
    HeapLocation* loc2 = heap_locations_[index2];
    if (loc1->GetOffset() != loc2->GetOffset()) {
      // Either two different instance fields, or one is an instance
      // field and the other is an array data.
      return false;
    }
    if (loc1->GetDeclaringClassDefIndex() != loc2->GetDeclaringClassDefIndex()) {
      // Different types.
      return false;
    }
    if (!CanReferencesAlias(loc1->GetReferenceInfo(), loc2->GetReferenceInfo())) {
      return false;
    }
    if (loc1->IsArray() && loc2->IsArray()) {
      HInstruction* idx1 = loc1->GetIndex();
      HInstruction* idx2 = loc2->GetIndex();
      size_t vector_length1 = loc1->GetVectorLength();
      size_t vector_length2 = loc2->GetVectorLength();
      if (!CanArrayElementsAlias(idx1, vector_length1, idx2, vector_length2)) {
        return false;
      }
    }
    loc1->SetHasAliasedLocations(true);
    loc2->SetHasAliasedLocations(true);
    return true;
  }

  ReferenceInfo* GetOrCreateReferenceInfo(HInstruction* instruction) {
    ReferenceInfo* ref_info = FindReferenceInfoOf(instruction);
    if (ref_info == nullptr) {
      size_t pos = ref_info_array_.size();
      ref_info = new (allocator_) ReferenceInfo(instruction, allocator_, pos, for_elimination_);
      ref_info_array_.push_back(ref_info);
    }
    return ref_info;
  }

  void CreateReferenceInfoForReferenceType(HInstruction* instruction) {
    if (instruction->GetType() != DataType::Type::kReference) {
      return;
    }
    DCHECK(FindReferenceInfoOf(instruction) == nullptr);
    GetOrCreateReferenceInfo(instruction);
  }

  void MaybeCreateHeapLocation(HInstruction* ref,
                               DataType::Type type,
                               size_t offset,
                               HInstruction* index,
                               size_t vector_length,
                               int16_t declaring_class_def_index) {
    HInstruction* original_ref = HuntForOriginalReference(ref);
    ReferenceInfo* ref_info = GetOrCreateReferenceInfo(original_ref);
    size_t heap_location_idx = FindHeapLocationIndex(
        ref_info, type, offset, index, vector_length, declaring_class_def_index);
    if (heap_location_idx == kHeapLocationNotFound) {
      HeapLocation* heap_loc = new (allocator_)
          HeapLocation(ref_info, type, offset, index, vector_length, declaring_class_def_index);
      heap_locations_.push_back(heap_loc);
    }
  }

  void VisitFieldAccess(HInstruction* ref, const FieldInfo& field_info) {
    if (field_info.IsVolatile()) {
      has_volatile_ = true;
    }
    DataType::Type type = field_info.GetFieldType();
    const uint16_t declaring_class_def_index = field_info.GetDeclaringClassDefIndex();
    const size_t offset = field_info.GetFieldOffset().SizeValue();
    MaybeCreateHeapLocation(ref,
                            type,
                            offset,
                            nullptr,
                            HeapLocation::kScalar,
                            declaring_class_def_index);
  }

  void VisitArrayAccess(HInstruction* array,
                        HInstruction* index,
                        DataType::Type type,
                        size_t vector_length) {
    MaybeCreateHeapLocation(array,
                            type,
                            HeapLocation::kInvalidFieldOffset,
                            index,
                            vector_length,
                            HeapLocation::kDeclaringClassDefIndexForArrays);
  }

  void VisitInstanceFieldGet(HInstanceFieldGet* instruction) override {
    VisitFieldAccess(instruction->InputAt(0), instruction->GetFieldInfo());
    CreateReferenceInfoForReferenceType(instruction);
  }

  void VisitInstanceFieldSet(HInstanceFieldSet* instruction) override {
    VisitFieldAccess(instruction->InputAt(0), instruction->GetFieldInfo());
    has_heap_stores_ = true;
  }

  void VisitStaticFieldGet(HStaticFieldGet* instruction) override {
    VisitFieldAccess(instruction->InputAt(0), instruction->GetFieldInfo());
    CreateReferenceInfoForReferenceType(instruction);
  }

  void VisitStaticFieldSet(HStaticFieldSet* instruction) override {
    VisitFieldAccess(instruction->InputAt(0), instruction->GetFieldInfo());
    has_heap_stores_ = true;
  }

  // We intentionally don't collect HUnresolvedInstanceField/HUnresolvedStaticField accesses
  // since we cannot accurately track the fields.

  void VisitArrayGet(HArrayGet* instruction) override {
    HInstruction* array = instruction->InputAt(0);
    HInstruction* index = instruction->InputAt(1);
    DataType::Type type = instruction->GetType();
    VisitArrayAccess(array, index, type, HeapLocation::kScalar);
    CreateReferenceInfoForReferenceType(instruction);
  }

  void VisitArraySet(HArraySet* instruction) override {
    HInstruction* array = instruction->InputAt(0);
    HInstruction* index = instruction->InputAt(1);
    DataType::Type type = instruction->GetComponentType();
    VisitArrayAccess(array, index, type, HeapLocation::kScalar);
    has_heap_stores_ = true;
  }

  void VisitVecLoad(HVecLoad* instruction) override {
    HInstruction* array = instruction->InputAt(0);
    HInstruction* index = instruction->InputAt(1);
    DataType::Type type = instruction->GetPackedType();
    VisitArrayAccess(array, index, type, instruction->GetVectorLength());
    CreateReferenceInfoForReferenceType(instruction);
  }

  void VisitVecStore(HVecStore* instruction) override {
    HInstruction* array = instruction->InputAt(0);
    HInstruction* index = instruction->InputAt(1);
    DataType::Type type = instruction->GetPackedType();
    VisitArrayAccess(array, index, type, instruction->GetVectorLength());
    has_heap_stores_ = true;
  }

  void VisitInstruction(HInstruction* instruction) override {
    // Any new-instance or new-array cannot alias with references that
    // pre-exist the new-instance/new-array. We append entries into
    // ref_info_array_ which keeps track of the order of creation
    // of reference values since we visit the blocks in reverse post order.
    //
    // By default, VisitXXX() (including VisitPhi()) calls VisitInstruction(),
    // unless VisitXXX() is overridden. VisitInstanceFieldGet() etc. above
    // also call CreateReferenceInfoForReferenceType() explicitly.
    CreateReferenceInfoForReferenceType(instruction);
  }

  void VisitMonitorOperation(HMonitorOperation* monitor ATTRIBUTE_UNUSED) override {
    has_monitor_operations_ = true;
  }

  ScopedArenaAllocator* allocator_;
  ScopedArenaVector<ReferenceInfo*> ref_info_array_;   // All references used for heap accesses.
  ScopedArenaVector<HeapLocation*> heap_locations_;    // All heap locations.
  ArenaBitVector aliasing_matrix_;    // aliasing info between each pair of locations.
  bool has_heap_stores_;    // If there is no heap stores, LSE acts as GVN with better
                            // alias analysis and won't be as effective.
  bool has_volatile_;       // If there are volatile field accesses.
  bool has_monitor_operations_;    // If there are monitor operations.
  bool for_elimination_;

  DISALLOW_COPY_AND_ASSIGN(HeapLocationCollector);
};

class LoadStoreAnalysis {
 public:
  // For elimination controls whether we should keep track of escapes at a per-block level for
  // partial LSE.
  explicit LoadStoreAnalysis(HGraph* graph,
                             OptimizingCompilerStats* stats,
                             ScopedArenaAllocator* local_allocator,
                             bool for_elimination = true)
      : graph_(graph),
        stats_(stats),
        heap_location_collector_(graph, local_allocator, for_elimination) {}

  const HeapLocationCollector& GetHeapLocationCollector() const {
    return heap_location_collector_;
  }

  bool Run();

 private:
  HGraph* graph_;
  OptimizingCompilerStats* stats_;
  HeapLocationCollector heap_location_collector_;

  DISALLOW_COPY_AND_ASSIGN(LoadStoreAnalysis);
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_LOAD_STORE_ANALYSIS_H_
