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

/*
 * Detects a Phi + c construct. When c == 0 the environment of anything that
 * is moved out of the loop can use the initial value of Phi. When c != 0,
 * it can still be moved by introducing a single add to the initial value of
 * Phi in the header to ensure the environment sees the right value there.
 * Savings from LICM typically outweighs the overhead of this extra add.
 */
static bool IsPhiOf(HInstruction* instruction,
                    HBasicBlock* block,
                    /*out*/ HInstruction** incoming,
                    /*out*/ int32_t* value) {
  if (instruction->IsPhi()) {
    if (instruction->GetBlock() == block) {
      *incoming = instruction->InputAt(0);
      *value = 0;
      return true;
    }
  } else if (instruction->GetType() == Primitive::kPrimInt && (instruction->IsAdd() ||
                                                               instruction->IsSub())) {
    HInstruction* x = instruction->InputAt(0);
    HInstruction* y = instruction->InputAt(1);
    if (y->IsIntConstant() && IsPhiOf(x, block, incoming, value)) {
      int32_t c = y->AsIntConstant()->GetValue();
      *value += (instruction->IsAdd() ? c : -c);
      return true;
    }
  }
  return false;
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
          int32_t value = 0;
          HInstruction* incoming = nullptr;
          if (!IsPhiOf(input, info->GetHeader(), &incoming, &value)) {
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
static void UpdateLoopPhisIn(HGraph* graph,
                             HBasicBlock* preheader,
                             HEnvironment* environment,
                             HLoopInformation* info) {
  for (; environment != nullptr; environment = environment->GetParent()) {
    for (size_t i = 0, e = environment->Size(); i < e; ++i) {
      HInstruction* input = environment->GetInstructionAt(i);
      HInstruction* incoming = nullptr;
      int32_t value = 0;
      if (input != nullptr && IsPhiOf(input, info->GetHeader(), &incoming, &value)) {
        if (value != 0) {
          // Adjust the initial value with constant.
          incoming = new (graph->GetArena())
              HAdd(Primitive::kPrimInt, incoming, graph->GetIntConstant(value));
          preheader->InsertInstructionBefore(incoming, preheader->GetLastInstruction());
        }
        environment->RemoveAsUserOfInput(i);
        environment->SetRawEnvAt(i, incoming);
        incoming->AddEnvUseAt(environment, i);
      }
    }
  }
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

  // Post order visit to visit inner loops before outer loops.
  for (HBasicBlock* block : graph_->GetPostOrder()) {
    if (!block->IsLoopHeader()) {
      // Only visit the loop when we reach the header.
      continue;
    }

    HLoopInformation* loop_info = block->GetLoopInformation();
    SideEffects loop_effects = side_effects_.GetLoopEffects(block);
    HBasicBlock* pre_header = loop_info->GetPreHeader();

    for (HBlocksInLoopIterator it_loop(*loop_info); !it_loop.Done(); it_loop.Advance()) {
      HBasicBlock* inner = it_loop.Current();
      DCHECK(inner->IsInLoop());
      if (inner->GetLoopInformation() != loop_info) {
        // Thanks to post order visit, inner loops were already visited.
        DCHECK(visited->IsBitSet(inner->GetBlockId()));
        continue;
      }
      if (kIsDebugBuild) {
        visited->SetBit(inner->GetBlockId());
      }

      if (loop_info->ContainsIrreducibleLoop()) {
        // We cannot licm in an irreducible loop, or in a natural loop containing an
        // irreducible loop.
        continue;
      }
      DCHECK(!loop_info->IsIrreducible());

      // We can move an instruction that can throw only if it is the first
      // throwing instruction in the loop. Note that the first potentially
      // throwing instruction encountered that is not hoisted stops this
      // optimization. Non-throwing instruction can still be hoisted.
      bool found_first_non_hoisted_throwing_instruction_in_loop = !inner->IsLoopHeader();
      for (HInstructionIterator inst_it(inner->GetInstructions());
           !inst_it.Done();
           inst_it.Advance()) {
        HInstruction* instruction = inst_it.Current();
        if (instruction->CanBeMoved()
            && (!instruction->CanThrow() || !found_first_non_hoisted_throwing_instruction_in_loop)
            && !instruction->GetSideEffects().MayDependOn(loop_effects)
            && InputsAreDefinedBeforeLoop(instruction)) {
          // We need to update the environment if the instruction has a loop header
          // phi in it.
          if (instruction->NeedsEnvironment()) {
            UpdateLoopPhisIn(graph_, pre_header, instruction->GetEnvironment(), loop_info);
          } else {
            DCHECK(!instruction->HasEnvironment());
          }
          instruction->MoveBefore(pre_header->GetLastInstruction());
          MaybeRecordStat(MethodCompilationStat::kLoopInvariantMoved);
        } else if (instruction->CanThrow()) {
          // If `instruction` can throw, we cannot move further instructions
          // that can throw as well.
          found_first_non_hoisted_throwing_instruction_in_loop = true;
        }
      }
    }
  }
}

}  // namespace art
