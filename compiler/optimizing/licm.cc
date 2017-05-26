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

#include "licm.h"

#include "side_effects_analysis.h"

namespace art {

static bool IsPhiOf(HInstruction* instruction, HBasicBlock* block) {
  return instruction->IsPhi() && instruction->GetBlock() == block;
}

/**
 * Returns whether `instruction` has all its inputs and environment defined
 * before the loop it is in.
 */
static bool InputsAreDefinedBeforeLoop(HInstruction* instruction) {
  DCHECK(instruction->IsInLoop());
  HLoopInformation* info = instruction->GetBlock()->GetLoopInformation();
  for (const HInstruction* input : instruction->GetInputs()) {
    HLoopInformation* input_loop = input->GetBlock()->GetLoopInformation();
    // We only need to check whether the input is defined in the loop. If it is not
    // it is defined before the loop.
    if (input_loop != nullptr && input_loop->IsIn(*info)) {
      return false;
    }
  }

  for (HEnvironment* environment = instruction->GetEnvironment();
       environment != nullptr;
       environment = environment->GetParent()) {
    for (size_t i = 0, e = environment->Size(); i < e; ++i) {
      HInstruction* input = environment->GetInstructionAt(i);
      if (input != nullptr) {
        HLoopInformation* input_loop = input->GetBlock()->GetLoopInformation();
        if (input_loop != nullptr && input_loop->IsIn(*info)) {
          // We can move an instruction that takes a loop header phi in the environment:
          // we will just replace that phi with its first input later in `UpdateLoopPhisIn`.
          bool is_loop_header_phi = IsPhiOf(input, info->GetHeader());
          if (!is_loop_header_phi) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

/**
 * If `environment` has a loop header phi, we replace it with its first input.
 */
static void UpdateLoopPhisIn(HEnvironment* environment, HLoopInformation* info) {
  for (; environment != nullptr; environment = environment->GetParent()) {
    for (size_t i = 0, e = environment->Size(); i < e; ++i) {
      HInstruction* input = environment->GetInstructionAt(i);
      if (input != nullptr && IsPhiOf(input, info->GetHeader())) {
        environment->RemoveAsUserOfInput(i);
        HInstruction* incoming = input->InputAt(0);
        environment->SetRawEnvAt(i, incoming);
        incoming->AddEnvUseAt(environment, i);
      }
    }
  }
}

/** Checks if instruction is used outside the given loop. */
static bool IsUsedOutsideLoop(HLoopInformation* loop_info, HInstruction* instruction) {
  for (const HUseListNode<HInstruction*>& use : instruction->GetUses()) {
    if (use.GetUser()->GetBlock()->GetLoopInformation() != loop_info) {
      return true;
    }
  }
  for (const HUseListNode<HEnvironment*>& env : instruction->GetEnvUses()) {
    if (env.GetUser()->GetHolder()->GetBlock()->GetLoopInformation() != loop_info) {
      return true;
    }
  }
  return false;
}

/**
 * Hoists an invariant control dependence out of the loop.
 * Returns true on success.
 *
 * Header: <nothing visible, no phi-uses outside loop>
 *         if (invariant) goto exit
 *
 * Example:
 *    while (true) {                   if (x == 1) return;
 *      if (x == 1) return;        ->  while (true) {
 *      .... no def of x ....            ....
 *    }                                }
 */
static bool HoistControlDependence(HGraph* graph, HLoopInformation* loop_info, HIf* if_stmt) {
  HBasicBlock* true_succ = if_stmt->IfTrueSuccessor();
  HBasicBlock* false_succ = if_stmt->IfFalseSuccessor();
  bool is_true_loop = loop_info->Contains(*true_succ);
  bool is_false_loop = loop_info->Contains(*false_succ);
  if ((!is_true_loop && is_false_loop) || (is_true_loop & !is_false_loop)) {
    HBasicBlock* pre_header = loop_info->GetPreHeader();
    HBasicBlock* header = loop_info->GetHeader();
    HBasicBlock* exit = is_true_loop ? false_succ : true_succ;
    HBasicBlock* entry = is_true_loop ? true_succ : false_succ;
    // Do not apply this optimization if any phis inside the header are
    // used outside the loop, since this would require repairing the Phi
    // structure along the hoisted and non-hoisted exits.
    //
    // This currently prevents hoisting the a == null tests in
    //
    //       for (int i = 0; a == null && i < a.length; i++) {
    //          reduction += a[i];
    //       }
    //
    // TODO: do this anyway?
    for (HInstructionIterator it(header->GetPhis()); !it.Done(); it.Advance()) {
      if (IsUsedOutsideLoop(loop_info, it.Current())) {
        return false;
      }
    }
    // Remove control from header and merge header with entry if possible.
    header->AddInstruction(new (graph->GetArena()) HGoto());
    header->RemoveSuccessor(exit);
    exit->RemovePredecessor(header);
    DCHECK_EQ(entry, header->GetSingleSuccessor());
    if (entry->GetPredecessors().size() == 1u) {
      header->MergeWith(entry);
    }
    // Relink hoisted control.
    if_stmt->MoveBefore(pre_header->GetLastInstruction(), false);
    pre_header->RemoveInstruction(pre_header->GetLastInstruction());
    pre_header->AddSuccessor(exit);
    if (is_false_loop) {
      pre_header->SwapSuccessors();
    }
    header->RemoveDominatedBlock(exit);
    pre_header->AddDominatedBlock(exit);
    exit->SetDominator(pre_header);
    graph->TransformForSplit(pre_header, header);
    return true;
  }
  return false;
}

void LICM::Run() {
  DCHECK(side_effects_.HasRun());

  // Only used during debug.
  ArenaBitVector* visited = nullptr;
  if (kIsDebugBuild) {
    visited = new (graph_->GetArena()) ArenaBitVector(graph_->GetArena(),
                                                      graph_->GetBlocks().size(),
                                                      false,
                                                      kArenaAllocLICM);
  }

  // Post order visit to visit inner loops before outer loops
  // (made safe against inserts/merges to the right).
  for (size_t i = graph_->GetReversePostOrder().size(); i != 0; i--) {
    HBasicBlock* block = graph_->GetReversePostOrder()[i - 1];
    if (!block->IsLoopHeader()) {
      // Only visit the loop when we reach the header.
      continue;
    }

    HLoopInformation* loop_info = block->GetLoopInformation();
    SideEffects loop_effects = side_effects_.GetLoopEffects(block);
    HBasicBlock* pre_header = loop_info->GetPreHeader();

    for (HBlocksInLoopIterator it_loop(*loop_info); !it_loop.Done(); ) {
      HBasicBlock* inner = it_loop.Current();
      DCHECK(inner->IsInLoop());
      if (inner->GetLoopInformation() != loop_info) {
        // Thanks to post order visit, inner loops were already visited.
        DCHECK(visited->IsBitSet(inner->GetBlockId()));
        it_loop.Advance();
        continue;
      }
      if (kIsDebugBuild) {
        visited->SetBit(inner->GetBlockId());
      }

      if (loop_info->ContainsIrreducibleLoop()) {
        // We cannot licm in an irreducible loop, or in a natural loop containing an
        // irreducible loop.
        it_loop.Advance();
        continue;
      }
      DCHECK(!loop_info->IsIrreducible());

      // We can move an instruction that can throw only as long as it is the first visible
      // instruction (throw or write) in the loop. Note that the first potentially visible
      // instruction that is not hoisted stops this optimization. Non-throwing instructions,
      // on the other hand, can still be hoisted.
      bool found_first_non_hoisted_visible_instruction_in_loop = !inner->IsLoopHeader();
      for (HInstructionIterator inst_it(inner->GetInstructions());
           !inst_it.Done();
           inst_it.Advance()) {
        HInstruction* instruction = inst_it.Current();
        if (instruction->CanBeMoved()
            && (!instruction->CanThrow() || !found_first_non_hoisted_visible_instruction_in_loop)
            && !instruction->GetSideEffects().MayDependOn(loop_effects)
            && InputsAreDefinedBeforeLoop(instruction)) {
          // We need to update the environment if the instruction has a loop header
          // phi in it.
          if (instruction->NeedsEnvironment()) {
            UpdateLoopPhisIn(instruction->GetEnvironment(), loop_info);
          } else {
            DCHECK(!instruction->HasEnvironment());
          }
          instruction->MoveBefore(pre_header->GetLastInstruction());
          MaybeRecordStat(MethodCompilationStat::kLoopInvariantMoved);
        } else if (instruction->CanThrow() || instruction->DoesAnyWrite()) {
          // If `instruction` can do something visible (throw or write),
          // we cannot move further instructions that can throw.
          found_first_non_hoisted_visible_instruction_in_loop = true;
        }
      }

      // Hoist invariant control dependence out of the loop.
      //   Header: <nothing visible>
      //           if (invariant) ..
      // NOTE: even though the optimization may add and merge basic blocks,
      //       it behaves correctly within the two surrounding block iterators.
      if (!found_first_non_hoisted_visible_instruction_in_loop &&
          inner->IsLoopHeader() &&
          inner->EndsWithIf()) {
        HIf* if_stmt = inner->GetLastInstruction()->AsIf();
        if (loop_info->IsDefinedOutOfTheLoop(if_stmt->InputAt(0)) &&
            HoistControlDependence(graph_, loop_info, if_stmt)) {
          MaybeRecordStat(MethodCompilationStat::kLoopInvariantMoved);
          continue;  // try same block again
        }
      }

      // Continue.
      it_loop.Advance();
    }
  }
}

}  // namespace art
