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

#ifndef ART_COMPILER_OPTIMIZING_CODE_PULLING_H_
#define ART_COMPILER_OPTIMIZING_CODE_PULLING_H_

#include "base/macros.h"
#include "nodes.h"
#include "optimization.h"

namespace art HIDDEN {

// Pulls common code from HIf block successors to the parent block, as long as the instructions are
// the same and they can be moved e.g. from
//
//       BB1
//       If
//        |
//       / \
//    BB2: BB3:
//     A    A
//     B    B
//     C    D
//
// to:
//       BB1
//        A
//        B
//       If
//        |
//       / \
//    BB2: BB3:
//     C    D
//
// It stops searching for common instructions when:
//   A) The next instruction from both blocks is different, or
//   B) The instruction can't be moved, or
//   C) It reaches the control flow instruction.
//
// TODO(solanes): Potentially we could still move instructions before the HIf, even when one
// instruction wasn't moved. However, we should take extra care as we would be reordering
// instructions if we do this.
//
// We don't perform this optimization if any of the block's successor has another predecessor e.g.
//      BB1    BB4
//       |     /
//      / \   /
//    BB2  BB3
// as we would be removing code from the BB4 code path.
//
// We don't perform this optimization for loops.
class CodePulling : public HOptimization {
 public:
  CodePulling(HGraph* graph,
              OptimizingCompilerStats* stats,
              const char* name = kCodePullingPassName)
      : HOptimization(graph, name, stats) {}

  bool Run() override;

  static constexpr const char* kCodePullingPassName = "code_pulling";

 private:
  DISALLOW_COPY_AND_ASSIGN(CodePulling);
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_CODE_PULLING_H_
