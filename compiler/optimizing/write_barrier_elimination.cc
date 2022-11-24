/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "write_barrier_elimination.h"

#include "base/arena_allocator.h"
#include "base/scoped_arena_allocator.h"
#include "base/scoped_arena_containers.h"

namespace art HIDDEN {

// Use HGraphDelegateVisitor for which all VisitInvokeXXX() delegate to VisitInvoke().
class WBEVisitor : public HGraphDelegateVisitor {
 public:
  WBEVisitor(HGraph* graph, OptimizingCompilerStats* stats)
      : HGraphDelegateVisitor(graph),
        scoped_allocator_(graph->GetArenaStack()),
        current_write_barriers_(scoped_allocator_.Adapter(kArenaAllocWBE)),
        stats_(stats) {}

  void VisitBasicBlock(HBasicBlock* block) override {
    // We clear the map to perform this optimization only in the same block. Doing it across blocks
    // would entail non-trivial merging of states, and most write barriers are eliminated in-block.
    current_write_barriers_.clear();
    HGraphDelegateVisitor::VisitBasicBlock(block);
  }

  void VisitInstanceFieldSet(HInstanceFieldSet* instruction) override {
    DCHECK(!instruction->CanThrow());

    if (instruction->GetFieldType() != DataType::Type::kReference ||
        instruction->GetValue()->IsNullConstant()) {
      return;
    }

    MaybeRecordStat(stats_, MethodCompilationStat::kPossibleWriteBarrier);
    HInstruction* obj = HuntForOriginalReference(instruction->InputAt(0));
    auto it = current_write_barriers_.find(obj);
    if (it != current_write_barriers_.end()) {
      DCHECK(it->second->IsInstanceFieldSet());
      DCHECK(!it->second->AsInstanceFieldSet()->GetIgnoreWriteBarrier());
      DCHECK_EQ(it->second->GetBlock(), instruction->GetBlock());
      instruction->SetIgnoreWriteBarrier();
      MaybeRecordStat(stats_, MethodCompilationStat::kRemovedWriteBarrier);
    } else {
      const bool inserted = current_write_barriers_.insert({obj, instruction}).second;
      DCHECK(inserted);
      DCHECK(!instruction->GetIgnoreWriteBarrier());
    }
  }

  void VisitStaticFieldSet(HStaticFieldSet* instruction) override {
    DCHECK(!instruction->CanThrow());

    if (instruction->GetFieldType() != DataType::Type::kReference ||
        instruction->GetValue()->IsNullConstant()) {
      return;
    }

    MaybeRecordStat(stats_, MethodCompilationStat::kPossibleWriteBarrier);
    HInstruction* cls = HuntForOriginalReference(instruction->InputAt(0));
    auto it = current_write_barriers_.find(cls);
    if (it != current_write_barriers_.end()) {
      DCHECK(it->second->IsStaticFieldSet());
      DCHECK(!it->second->AsStaticFieldSet()->GetIgnoreWriteBarrier());
      DCHECK_EQ(it->second->GetBlock(), instruction->GetBlock());
      instruction->SetIgnoreWriteBarrier();
      MaybeRecordStat(stats_, MethodCompilationStat::kRemovedWriteBarrier);
    } else {
      const bool inserted = current_write_barriers_.insert({cls, instruction}).second;
      DCHECK(inserted);
      DCHECK(!instruction->GetIgnoreWriteBarrier());
    }
  }

  void VisitArraySet(HArraySet* instruction) override {
    // ArraySet instructions can throw if they need a type check. The type check happens before we
    // mark for GC, so we have to clear the current values just in case we throw.
    if (instruction->CanThrow()) {
      ClearCurrentValues();
    }

    if (instruction->GetComponentType() != DataType::Type::kReference ||
        instruction->GetValue()->IsNullConstant()) {
      return;
    }

    HInstruction* arr = HuntForOriginalReference(instruction->InputAt(0));
    MaybeRecordStat(stats_, MethodCompilationStat::kPossibleWriteBarrier);
    auto it = current_write_barriers_.find(arr);
    if (it != current_write_barriers_.end()) {
      DCHECK(it->second->IsArraySet());
      DCHECK(!it->second->AsArraySet()->GetIgnoreWriteBarrier());
      DCHECK_EQ(it->second->GetBlock(), instruction->GetBlock());
      instruction->SetIgnoreWriteBarrier();
      MaybeRecordStat(stats_, MethodCompilationStat::kRemovedWriteBarrier);
    } else {
      const bool inserted = current_write_barriers_.insert({arr, instruction}).second;
      DCHECK(inserted);
      DCHECK(!instruction->GetIgnoreWriteBarrier());
    }
  }

  void VisitInstruction(HInstruction* instruction) override {
    if (instruction->CanThrow()) {
      ClearCurrentValues();
    }
  }

  void VisitSuspendCheck(HSuspendCheck* instruction ATTRIBUTE_UNUSED) override {
    ClearCurrentValues();
  }

  void VisitInvoke(HInvoke* invoke ATTRIBUTE_UNUSED) override {
    ClearCurrentValues();
  }

 private:
  void ClearCurrentValues() {
    current_write_barriers_.clear();
  }

  HInstruction* HuntForOriginalReference(HInstruction* ref) const {
    // An original reference can be transformed by instructions like:
    //   i0 NewArray
    //   i1 HInstruction(i0)  <-- NullCheck, BoundType, IntermediateAddress.
    //   i2 ArraySet(i1, index, value)
    DCHECK(ref != nullptr);
    while (ref->IsNullCheck() || ref->IsBoundType() || ref->IsIntermediateAddress()) {
      ref = ref->InputAt(0);
    }
    return ref;
  }

  ScopedArenaAllocator scoped_allocator_;

  // Stores a map of <Receiver, InstructionWhereTheWriteBarrierIs>.
  // `InstructionWhereTheWriteBarrierIs` is used for DCHECKs only.
  ScopedArenaHashMap<HInstruction*, HInstruction*> current_write_barriers_;

  OptimizingCompilerStats* const stats_;

  DISALLOW_COPY_AND_ASSIGN(WBEVisitor);
};

bool WriteBarrierElimination::Run() {
  WBEVisitor wbe_visitor(graph_, stats_);
  wbe_visitor.VisitReversePostOrder();
  return true;
}

}  // namespace art
