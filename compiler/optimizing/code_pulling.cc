/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "code_pulling.h"

#include "android-base/logging.h"
#include "optimizing/nodes.h"
#include "optimizing/optimizing_compiler_stats.h"

static constexpr bool kEnableCodePulling = true;

namespace art HIDDEN {

// This works fast enough for now. We can consider adding a cache, if we want to speed this up since
// we might be asking StrictlyDominates for the same blocks/instructions.
static bool InputsAreDefinedBeforeCursor(HInstruction* instr, HInstruction* cursor) {
  // Regular inputs
  for (const HInstruction* input : instr->GetInputs()) {
    if (!input->StrictlyDominates(cursor)) {
      return false;
    }
  }

  // Environment inputs.
  for (HEnvironment* environment = instr->GetEnvironment(); environment != nullptr;
       environment = environment->GetParent()) {
    for (size_t i = 0, e = environment->Size(); i < e; ++i) {
      HInstruction* env_input = environment->GetInstructionAt(i);
      if (env_input != nullptr) {
        if (!env_input->StrictlyDominates(cursor)) {
          return false;
        }
      }
    }
  }
  return true;
}

bool CodePulling::Run() {
  if (!kEnableCodePulling) {
    return false;
  }

  bool did_opt = false;
  // Post order visit to correctly deal with recursive optimizations.
  for (HBasicBlock* block : graph_->GetPostOrder()) {
    if (!block->EndsWithIf()) {
      // Only visit blocks that end with ifs.
      continue;
    }

    if (block->IsInLoop()) {
      // Don't deal with loops.
      // TODO(solanes): We could enable this on loops as long as it is the same loop, and this is
      // not part of a backwards branch e.g. an if/else inside of a loop.
      continue;
    }

    HIf* if_instruction = block->GetLastInstruction()->AsIf();
    HBasicBlock* true_block = if_instruction->IfTrueSuccessor();
    HBasicBlock* false_block = if_instruction->IfFalseSuccessor();

    if (true_block->GetPredecessors().size() != 1u || false_block->GetPredecessors().size() != 1u) {
      // If a block has another predecessor, we cannot pull the code to `block`.
      continue;
    }

    HInstruction* next_true = true_block->GetFirstInstruction();
    HInstruction* next_false = false_block->GetFirstInstruction();
    while (!next_true->IsControlFlow() && next_true->Equals(next_false)) {
      // Grab the next instruction, just in case we perform the optimization.
      HInstruction* current_true = next_true;
      next_true = next_true->GetNext();
      DCHECK(next_true != nullptr);
      HInstruction* current_false = next_false;
      next_false = next_false->GetNext();
      DCHECK(next_false != nullptr);

      // TODO(solanes): I think we can skip this as we are not reordering instructions. However,
      // from local testing there wasn't much of a difference. It might be tied to the fact that
      // instructions that don't return `true` from `CanBeMoved` also don't return `true` from
      // `InstructionDataEquals` e.g. `HInstanceFieldSet`. We could potentially investigate defining
      // `InstructionDataEquals` for more instructions and removing this check, and see if it makes
      // any difference.
      if (!current_true->CanBeMoved()) {
        break;
      }
      DCHECK(current_false->CanBeMoved());

      // All inputs should have been defined before the `If` instruction as:
      // * Both `true_block` and `next_false` have one successor (and therefore no Phi
      // instructions), and
      // * This is either the first instruction of the block or we moved all of the previous
      // instructions too.
      DCHECK(InputsAreDefinedBeforeCursor(current_true, if_instruction));
      DCHECK(InputsAreDefinedBeforeCursor(current_false, if_instruction));

      // We want to move both instructions before the `if`. We can move `current_true`, and
      // deduplicate `current_false` to `current_true`.
      current_true->MoveBefore(if_instruction);
      current_false->ReplaceWith(current_true);
      current_false->GetBlock()->RemoveInstruction(current_false);
      MaybeRecordStat(stats_, MethodCompilationStat::kDeduplicatedCommonCode);
      did_opt = true;
    }
  }
  return did_opt;
}

}  // namespace art
