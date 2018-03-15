/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "loop_analysis.h"

#include "base/bit_vector-inl.h"

namespace art {

void LoopAnalysis::CalculateLoopBasicProperties(HLoopInformation* loop_info,
                                                LoopAnalysisInfo* analysis_results) {
  bool has_instructions_preventing_scalar_peeling = false;
  bool has_instructions_preventing_scalar_unrolling = false;
  size_t bb_num = 0;
  size_t instr_num = 0;
  size_t exits_num = 0;

  for (HBlocksInLoopIterator block_it(*loop_info);
       !block_it.Done();
       block_it.Advance()) {
    HBasicBlock* block = block_it.Current();

    for (HBasicBlock* successor : block->GetSuccessors()) {
      if (!loop_info->Contains(*successor)) {
        exits_num++;
      }
    }

    for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
      HInstruction* instruction = it.Current();
      if (MakesScalarPeelingUnrollingNonBeneficial(instruction)) {
        has_instructions_preventing_scalar_peeling = true;
        has_instructions_preventing_scalar_unrolling = true;
      }
      instr_num++;
    }
    bb_num++;
  }

  analysis_results->bb_num_ = bb_num;
  analysis_results->instr_num_ = instr_num;
  analysis_results->exits_num_ = exits_num;
  analysis_results->has_instructions_preventing_scalar_peeling_ =
      has_instructions_preventing_scalar_peeling;
  analysis_results->has_instructions_preventing_scalar_unrolling_ =
      has_instructions_preventing_scalar_unrolling;
}

bool LoopAnalysis::HasLoopAtLeastOneInvariantExit(HLoopInformation* loop_info) {
  HGraph* graph = loop_info->GetHeader()->GetGraph();
  for (uint32_t block_id : loop_info->GetBlocks().Indexes()) {
    HBasicBlock* block = graph->GetBlocks()[block_id];
    DCHECK(block != nullptr);
    if (block->EndsWithIf()) {
      HIf* hif = block->GetLastInstruction()->AsIf();
      HInstruction* input = hif->InputAt(0);

      if (IsLoopExit(loop_info, hif) && !loop_info->Contains(*input->GetBlock())) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace art
