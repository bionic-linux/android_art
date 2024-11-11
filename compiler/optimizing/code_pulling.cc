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
#include "base/globals.h"
#include "optimizing/nodes.h"
#include "optimizing/optimizing_compiler_stats.h"

static constexpr bool kEnableCodePulling = false;

namespace art HIDDEN {

// This works fast enough for now. We can consider adding a cache, if we want to speed this up since
// we might be asking StrictlyDominates for the same blocks/instructions. Due to how the
// optimization works currently, this is only used for DCHECKs.
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

static bool EnvironmentMatches(HInstruction* current_true, HInstruction* current_false) {
  HEnvironment* environment_true = current_true->GetEnvironment();
  HEnvironment* environment_false = current_false->GetEnvironment();
  while (environment_true != nullptr && environment_false != nullptr) {
    if (environment_true->Size() != environment_false->Size()) {
      return false;
    }
    for (size_t i = 0, e = environment_true->Size(); i < e; ++i) {
      if (environment_true->GetInstructionAt(i) != environment_false->GetInstructionAt(i)) {
        return false;
      }
    }
    environment_true = environment_true->GetParent();
    environment_false = environment_false->GetParent();
  }

  return (environment_true == nullptr) == (environment_false == nullptr);
}

bool CodePulling::Run() {
  if (!kEnableCodePulling) {
    return false;
  }

  bool did_opt = false;
  // Post order visit to be able to be able to optimize nested ifs.
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

    DCHECK_EQ(true_block->GetPredecessors().size(), 1u);
    DCHECK_EQ(false_block->GetPredecessors().size(), 1u);

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
      // * Both `true_block` and `next_false` have one predecessor (and therefore no Phi
      // instructions), and
      // * This is either the first instruction of the block or we moved all of the previous
      // instructions too.
      DCHECK(InputsAreDefinedBeforeCursor(current_true, if_instruction));
      DCHECK(InputsAreDefinedBeforeCursor(current_false, if_instruction));

      // If the instructions can throw, we have to have the same environment and dex pc.
      if (current_true->CanThrow()) {
        DCHECK(current_false->CanThrow());
        if (current_true->GetDexPc() != current_false->GetDexPc() ||
            !EnvironmentMatches(current_true, current_false)) {
          break;
        }
      }

      if (current_true->IsLoadClass()) {
        graph_->Dump(LOG_STREAM(FATAL) << "current_true: " << *current_true, nullptr);
      }

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
