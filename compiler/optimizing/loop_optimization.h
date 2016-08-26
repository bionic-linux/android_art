/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef ART_COMPILER_OPTIMIZING_LOOP_OPTIMIZATION_H_
#define ART_COMPILER_OPTIMIZING_LOOP_OPTIMIZATION_H_

#include <string>

#include "induction_var_range.h"
#include "nodes.h"
#include "optimization.h"

namespace art {

/**
 * Loop optimizations. Builds a loop hierarchy and applies optimizations to
 * the detected nested loops, such a removal of dead induction and empty loops.
 */
class HLoopOptimization : public HOptimization {
 public:
  HLoopOptimization(HGraph* graph, HInductionVarAnalysis* induction_analysis);

  void Run() OVERRIDE;

  static constexpr const char* kLoopOptimizationPassName = "loop_optimization";

 private:
  /**
   * A single loop inside the loop hierarchy representation.
   */
  struct LoopNode : public ArenaObject<kArenaAllocInductionVarAnalysis> {
    explicit LoopNode(HLoopInformation* l)
        : loop(l),
          outer(nullptr),
          inner(nullptr),
          prev(nullptr),
          next(nullptr) {}
    HLoopInformation* loop;
    LoopNode* outer;
    LoopNode* inner;
    LoopNode* prev;
    LoopNode* next;
  };

  void AddLoop(HLoopInformation* loop);
  void TraverseLoopsInnerToOuter(LoopNode* node);
  void SimplifyInduction(LoopNode* node);
  void RemoveEmptyLoop(LoopNode* node);

  void ReplaceAllUses(HInstruction* instruction,
                      HInstruction* replacement,
                      HInstruction* exclusion);

  // Range analysis based on induction variables.
  InductionVarRange induction_range_;

  // Entries into the loop hierarchy representation.
  LoopNode* top_loop_;
  LoopNode* last_loop_;

  friend class LoopOptimizationTest;

  DISALLOW_COPY_AND_ASSIGN(HLoopOptimization);
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_LOOP_OPTIMIZATION_H_
