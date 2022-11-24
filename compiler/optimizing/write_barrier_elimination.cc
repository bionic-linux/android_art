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

#include "android-base/macros.h"
#include "base/arena_allocator.h"
#include "base/scoped_arena_allocator.h"
#include "base/scoped_arena_containers.h"
#include "optimizing/nodes.h"
#include "scoped_thread_state_change-inl.h"
#include "thread.h"

namespace art HIDDEN {

// Use HGraphDelegateVisitor for which all VisitInvokeXXX() delegate to VisitInvoke().
class WBEVisitor : public HGraphDelegateVisitor {
 public:
  WBEVisitor(HGraph* graph, OptimizingCompilerStats* stats)
      : HGraphDelegateVisitor(graph),
        scoped_allocator_(graph->GetArenaStack()),
        last_set_(scoped_allocator_.Adapter(kArenaAllocWBE)),
        stats_(stats) {}

  void VisitBasicBlock(HBasicBlock* block) override {
    // Let's do it per block at the moment.
    last_set_.clear();
    // Visit all instructions in block.
    HGraphDelegateVisitor::VisitBasicBlock(block);
  }

  void VisitInstanceFieldSet(HInstanceFieldSet* instruction) override {
    DCHECK(!instruction->CanThrow());

    if (instruction->GetFieldInfo().GetFieldType() != DataType::Type::kReference ||
        instruction->InputAt(1)->IsNullConstant()) {
      return;
    }

    MaybeRecordStat(stats_, MethodCompilationStat::kWBETotalSets);
    HInstruction* obj = HuntForOriginalReference(instruction->InputAt(0));
    auto it = last_set_.find(obj);
    if (it != last_set_.end()) {
      CHECK(it->second->IsInstanceFieldSet());
      CHECK(!it->second->AsInstanceFieldSet()->GetIgnoreWriteBarrier());
      // TODO(solanes): Enabling this line makes it fail to boot. Investigate why.
      MaybeRecordStat(stats_, MethodCompilationStat::kWBEPossibleRemovedSets);
      CHECK_EQ(it->second->GetBlock(), instruction->GetBlock());
      instruction->SetIgnoreWriteBarrier();
      // LOG(ERROR) << "Before for obj " << obj->DebugName() << " with id: " << obj->GetId() << " " << it->second->DebugName() << " with id: " << it->second->GetId();
      // LOG(ERROR) << "After for obj " << obj->DebugName() << " with id: " << obj->GetId() << " " << it->second->DebugName() << " with id: " << it->second->GetId();
    } else {
      bool inserted = last_set_.insert({obj, instruction}).second;
      CHECK(inserted);
      CHECK(!instruction->GetIgnoreWriteBarrier());
    }
  }

  void VisitStaticFieldSet(HStaticFieldSet* instruction) override {
    DCHECK(!instruction->CanThrow());

    if (instruction->GetFieldInfo().GetFieldType() != DataType::Type::kReference ||
        instruction->InputAt(1)->IsNullConstant()) {
      return;
    }

    MaybeRecordStat(stats_, MethodCompilationStat::kWBETotalSets);
    HInstruction* cls = HuntForOriginalReference(instruction->InputAt(0));
    auto it = last_set_.find(cls);
    if (it != last_set_.end()) {
      CHECK(it->second->IsStaticFieldSet());
      CHECK(!it->second->AsStaticFieldSet()->GetIgnoreWriteBarrier());
      MaybeRecordStat(stats_, MethodCompilationStat::kWBEPossibleRemovedSets);
      CHECK_EQ(it->second->GetBlock(), instruction->GetBlock());
      instruction->SetIgnoreWriteBarrier();
      // LOG(ERROR) << "Before for class " << cls->DebugName() << " with id: " << cls->GetId() << " " << it->second->DebugName() << " with id: " << it->second->GetId();
      // LOG(ERROR) << "After for class " << cls->DebugName() << " with id: " << cls->GetId() << " " << it->second->DebugName() << " with id: " << it->second->GetId();
    } else {
      bool inserted = last_set_.insert({cls, instruction}).second;
      CHECK(inserted);
      CHECK(!instruction->GetIgnoreWriteBarrier());
    }
  }

  void VisitArraySet(HArraySet* instruction) override {
    // ArraySet instructions can throw if they need a type check. The type check happens before we
    // mark for GC, so we have to clear the current values just in case we throw.
    if (instruction->CanThrow()) {
      ClearCurrentValues();
    }

    if (instruction->GetComponentType() != DataType::Type::kReference ||
        instruction->InputAt(2)->IsNullConstant()) {
      return;
    }

    HInstruction* arr = HuntForOriginalReference(instruction->InputAt(0));
    MaybeRecordStat(stats_, MethodCompilationStat::kWBETotalSets);
    auto it = last_set_.find(arr);
    if (it != last_set_.end()) {
      CHECK(it->second->IsArraySet());
      CHECK(!it->second->AsArraySet()->GetIgnoreWriteBarrier());
      MaybeRecordStat(stats_, MethodCompilationStat::kWBEPossibleRemovedSets);
      CHECK_EQ(it->second->GetBlock(), instruction->GetBlock());
      instruction->SetIgnoreWriteBarrier();
      // LOG(ERROR) << "Before for array " << arr->DebugName() << " with id: " << arr->GetId() << " " << it->second->DebugName() << " with id: " << it->second->GetId();
      // LOG(ERROR) << "After for array " << arr->DebugName() << " with id: " << arr->GetId() << " " << it->second->DebugName() << " with id: " << it->second->GetId();
    } else {
      bool inserted = last_set_.insert({arr, instruction}).second;
      CHECK(inserted);
      CHECK(!instruction->GetIgnoreWriteBarrier());
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
    last_set_.clear();
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

  // Stores a map of <Object, InstructionWhereItWasLastSet>.
  ScopedArenaHashMap<HInstruction*, HInstruction*> last_set_;

  OptimizingCompilerStats* const stats_;

  DISALLOW_COPY_AND_ASSIGN(WBEVisitor);
};

bool WriteBarrierElimination::Run() {
  WBEVisitor wbe_visitor(graph_, stats_);
  // ScopedObjectAccess soa(Thread::Current());
  // LOG(ERROR) << "---" << graph_->GetArtMethod()->PrettyMethod();

  wbe_visitor.VisitReversePostOrder();
  // TODO(solanes): return true only if we eliminated at least one?
  return true;
}

}  // namespace art
