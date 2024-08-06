/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "bounds_check_elimination.h"

#include "base/arena_allocator.h"
#include "base/macros.h"
#include "builder.h"
#include "gvn.h"
#include "induction_var_analysis.h"
#include "instruction_simplifier.h"
#include "nodes.h"
#include "optimizing_unit_test.h"
#include "side_effects_analysis.h"

#include "gtest/gtest.h"

namespace art HIDDEN {

/**
 * Fixture class for the BoundsCheckElimination tests.
 */
class BoundsCheckEliminationTest : public OptimizingUnitTest {
 public:
  BoundsCheckEliminationTest()  : graph_(CreateGraph()) {
    graph_->SetHasBoundsChecks(true);
  }

  ~BoundsCheckEliminationTest() { }

  void RunBCE() {
    graph_->BuildDominatorTree();

    InstructionSimplifier(graph_, /* codegen= */ nullptr).Run();

    SideEffectsAnalysis side_effects(graph_);
    side_effects.Run();

    GVNOptimization(graph_, side_effects).Run();

    HInductionVarAnalysis induction(graph_);
    induction.Run();

    BoundsCheckElimination(graph_, side_effects, &induction).Run();
  }

  HInstruction* BuildSSAGraph1(int initial, int increment, IfCondition cond = kCondGE);
  HInstruction* BuildSSAGraph2(int initial, int increment = -1, IfCondition cond = kCondLE);
  HInstruction* BuildSSAGraph3(int initial, int increment, IfCondition cond);
  HInstruction* BuildSSAGraph4(int initial, IfCondition cond = kCondGE);

  HGraph* graph_;
};


// if (i < 0) { array[i] = 1; // Can't eliminate. }
// else if (i >= array.length) { array[i] = 1; // Can't eliminate. }
// else { array[i] = 1; // Can eliminate. }
TEST_F(BoundsCheckEliminationTest, NarrowingRangeArrayBoundsElimination) {
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  HInstruction* parameter1 = MakeParam(DataType::Type::kReference);  // array
  HInstruction* parameter2 = MakeParam(DataType::Type::kInt32);  // i

  HInstruction* constant_1 = graph_->GetIntConstant(1);
  HInstruction* constant_0 = graph_->GetIntConstant(0);

  HBasicBlock* block1 = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block1);
  HInstruction* cmp = new (GetAllocator()) HGreaterThanOrEqual(parameter2, constant_0);
  HIf* if_inst = new (GetAllocator()) HIf(cmp);
  block1->AddInstruction(cmp);
  block1->AddInstruction(if_inst);
  entry->AddSuccessor(block1);

  HBasicBlock* block2 = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block2);
  HNullCheck* null_check = new (GetAllocator()) HNullCheck(parameter1, 0);
  HArrayLength* array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  HBoundsCheck* bounds_check2 = new (GetAllocator())
      HBoundsCheck(parameter2, array_length, 0);
  HArraySet* array_set = new (GetAllocator()) HArraySet(
    null_check, bounds_check2, constant_1, DataType::Type::kInt32, 0);
  block2->AddInstruction(null_check);
  block2->AddInstruction(array_length);
  block2->AddInstruction(bounds_check2);
  block2->AddInstruction(array_set);

  HBasicBlock* block3 = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block3);
  null_check = new (GetAllocator()) HNullCheck(parameter1, 0);
  array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  cmp = new (GetAllocator()) HLessThan(parameter2, array_length);
  if_inst = new (GetAllocator()) HIf(cmp);
  block3->AddInstruction(null_check);
  block3->AddInstruction(array_length);
  block3->AddInstruction(cmp);
  block3->AddInstruction(if_inst);

  HBasicBlock* block4 = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block4);
  null_check = new (GetAllocator()) HNullCheck(parameter1, 0);
  array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  HBoundsCheck* bounds_check4 = new (GetAllocator())
      HBoundsCheck(parameter2, array_length, 0);
  array_set = new (GetAllocator()) HArraySet(
    null_check, bounds_check4, constant_1, DataType::Type::kInt32, 0);
  block4->AddInstruction(null_check);
  block4->AddInstruction(array_length);
  block4->AddInstruction(bounds_check4);
  block4->AddInstruction(array_set);

  HBasicBlock* block5 = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block5);
  null_check = new (GetAllocator()) HNullCheck(parameter1, 0);
  array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  HBoundsCheck* bounds_check5 = new (GetAllocator())
      HBoundsCheck(parameter2, array_length, 0);
  array_set = new (GetAllocator()) HArraySet(
    null_check, bounds_check5, constant_1, DataType::Type::kInt32, 0);
  block5->AddInstruction(null_check);
  block5->AddInstruction(array_length);
  block5->AddInstruction(bounds_check5);
  block5->AddInstruction(array_set);

  HBasicBlock* exit = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(exit);
  block2->AddSuccessor(exit);
  block4->AddSuccessor(exit);
  block5->AddSuccessor(exit);
  MakeExit(exit);

  block1->AddSuccessor(block3);  // True successor
  block1->AddSuccessor(block2);  // False successor

  block3->AddSuccessor(block5);  // True successor
  block3->AddSuccessor(block4);  // False successor

  RunBCE();

  ASSERT_FALSE(IsRemoved(bounds_check2));
  ASSERT_FALSE(IsRemoved(bounds_check4));
  ASSERT_TRUE(IsRemoved(bounds_check5));
}

// if (i > 0) {
//   // Positive number plus MAX_INT will overflow and be negative.
//   int j = i + Integer.MAX_VALUE;
//   if (j < array.length) array[j] = 1;  // Can't eliminate.
// }
TEST_F(BoundsCheckEliminationTest, OverflowArrayBoundsElimination) {
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  HInstruction* parameter1 = MakeParam(DataType::Type::kReference);  // array
  HInstruction* parameter2 = MakeParam(DataType::Type::kInt32);  // i

  HInstruction* constant_1 = graph_->GetIntConstant(1);
  HInstruction* constant_0 = graph_->GetIntConstant(0);
  HInstruction* constant_max_int = graph_->GetIntConstant(INT_MAX);

  HBasicBlock* block1 = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block1);
  HInstruction* cmp = new (GetAllocator()) HLessThanOrEqual(parameter2, constant_0);
  HIf* if_inst = new (GetAllocator()) HIf(cmp);
  block1->AddInstruction(cmp);
  block1->AddInstruction(if_inst);
  entry->AddSuccessor(block1);

  HBasicBlock* block2 = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block2);
  HInstruction* add = MakeBinOp<HAdd>(block2, DataType::Type::kInt32, parameter2, constant_max_int);
  HNullCheck* null_check = new (GetAllocator()) HNullCheck(parameter1, 0);
  block2->AddInstruction(null_check);
  HArrayLength* array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  block2->AddInstruction(array_length);
  HInstruction* cmp2 = MakeCondition<HGreaterThanOrEqual>(block2, add, array_length);
  MakeIf(block2, cmp2);

  HBasicBlock* block3 = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block3);
  HBoundsCheck* bounds_check = new (GetAllocator())
      HBoundsCheck(add, array_length, 0);
  HArraySet* array_set = new (GetAllocator()) HArraySet(
    null_check, bounds_check, constant_1, DataType::Type::kInt32, 0);
  block3->AddInstruction(bounds_check);
  block3->AddInstruction(array_set);

  HBasicBlock* exit = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(exit);
  MakeExit(exit);
  block1->AddSuccessor(exit);    // true successor
  block1->AddSuccessor(block2);  // false successor
  block2->AddSuccessor(exit);    // true successor
  block2->AddSuccessor(block3);  // false successor
  block3->AddSuccessor(exit);

  RunBCE();

  ASSERT_FALSE(IsRemoved(bounds_check));
}

// if (i < array.length) {
//   int j = i - Integer.MAX_VALUE;
//   j = j - Integer.MAX_VALUE;  // j is (i+2) after subtracting MAX_INT twice
//   if (j > 0) array[j] = 1;    // Can't eliminate.
// }
TEST_F(BoundsCheckEliminationTest, UnderflowArrayBoundsElimination) {
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  HInstruction* parameter1 = MakeParam(DataType::Type::kReference);  // array
  HInstruction* parameter2 = MakeParam(DataType::Type::kInt32);  // i

  HInstruction* constant_1 = graph_->GetIntConstant(1);
  HInstruction* constant_0 = graph_->GetIntConstant(0);
  HInstruction* constant_max_int = graph_->GetIntConstant(INT_MAX);

  HBasicBlock* block1 = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block1);
  HNullCheck* null_check = new (GetAllocator()) HNullCheck(parameter1, 0);
  HArrayLength* array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  HInstruction* cmp = new (GetAllocator()) HGreaterThanOrEqual(parameter2, array_length);
  HIf* if_inst = new (GetAllocator()) HIf(cmp);
  block1->AddInstruction(null_check);
  block1->AddInstruction(array_length);
  block1->AddInstruction(cmp);
  block1->AddInstruction(if_inst);
  entry->AddSuccessor(block1);

  HBasicBlock* block2 = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block2);
  HInstruction* sub1 =
      new (GetAllocator()) HSub(DataType::Type::kInt32, parameter2, constant_max_int);
  HInstruction* sub2 = new (GetAllocator()) HSub(DataType::Type::kInt32, sub1, constant_max_int);
  HInstruction* cmp2 = new (GetAllocator()) HLessThanOrEqual(sub2, constant_0);
  if_inst = new (GetAllocator()) HIf(cmp2);
  block2->AddInstruction(sub1);
  block2->AddInstruction(sub2);
  block2->AddInstruction(cmp2);
  block2->AddInstruction(if_inst);

  HBasicBlock* block3 = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block3);
  HBoundsCheck* bounds_check = new (GetAllocator())
      HBoundsCheck(sub2, array_length, 0);
  HArraySet* array_set = new (GetAllocator()) HArraySet(
    null_check, bounds_check, constant_1, DataType::Type::kInt32, 0);
  block3->AddInstruction(bounds_check);
  block3->AddInstruction(array_set);

  HBasicBlock* exit = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(exit);
  MakeExit(exit);
  block1->AddSuccessor(exit);    // true successor
  block1->AddSuccessor(block2);  // false successor
  block2->AddSuccessor(exit);    // true successor
  block2->AddSuccessor(block3);  // false successor
  block3->AddSuccessor(exit);

  RunBCE();

  ASSERT_FALSE(IsRemoved(bounds_check));
}

// array[6] = 1; // Can't eliminate.
// array[5] = 1; // Can eliminate.
// array[4] = 1; // Can eliminate.
TEST_F(BoundsCheckEliminationTest, ConstantArrayBoundsElimination) {
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  HInstruction* parameter = MakeParam(DataType::Type::kReference);

  HInstruction* constant_5 = graph_->GetIntConstant(5);
  HInstruction* constant_4 = graph_->GetIntConstant(4);
  HInstruction* constant_6 = graph_->GetIntConstant(6);
  HInstruction* constant_1 = graph_->GetIntConstant(1);

  HBasicBlock* block = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block);
  entry->AddSuccessor(block);

  HNullCheck* null_check = new (GetAllocator()) HNullCheck(parameter, 0);
  HArrayLength* array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  HBoundsCheck* bounds_check6 = new (GetAllocator())
      HBoundsCheck(constant_6, array_length, 0);
  HInstruction* array_set = new (GetAllocator()) HArraySet(
    null_check, bounds_check6, constant_1, DataType::Type::kInt32, 0);
  block->AddInstruction(null_check);
  block->AddInstruction(array_length);
  block->AddInstruction(bounds_check6);
  block->AddInstruction(array_set);

  null_check = new (GetAllocator()) HNullCheck(parameter, 0);
  array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  HBoundsCheck* bounds_check5 = new (GetAllocator())
      HBoundsCheck(constant_5, array_length, 0);
  array_set = new (GetAllocator()) HArraySet(
    null_check, bounds_check5, constant_1, DataType::Type::kInt32, 0);
  block->AddInstruction(null_check);
  block->AddInstruction(array_length);
  block->AddInstruction(bounds_check5);
  block->AddInstruction(array_set);

  null_check = new (GetAllocator()) HNullCheck(parameter, 0);
  array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  HBoundsCheck* bounds_check4 = new (GetAllocator())
      HBoundsCheck(constant_4, array_length, 0);
  array_set = new (GetAllocator()) HArraySet(
    null_check, bounds_check4, constant_1, DataType::Type::kInt32, 0);
  block->AddInstruction(null_check);
  block->AddInstruction(array_length);
  block->AddInstruction(bounds_check4);
  block->AddInstruction(array_set);

  block->AddInstruction(new (GetAllocator()) HGoto());

  HBasicBlock* exit = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(exit);
  block->AddSuccessor(exit);
  MakeExit(exit);

  RunBCE();

  ASSERT_FALSE(IsRemoved(bounds_check6));
  ASSERT_TRUE(IsRemoved(bounds_check5));
  ASSERT_TRUE(IsRemoved(bounds_check4));
}

// for (int i=initial; i<array.length; i+=increment) { array[i] = 10; }
HInstruction* BoundsCheckEliminationTest::BuildSSAGraph1(int initial,
                                                         int increment,
                                                         IfCondition cond) {
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  HInstruction* parameter = MakeParam(DataType::Type::kReference);

  HInstruction* constant_initial = graph_->GetIntConstant(initial);
  HInstruction* constant_increment = graph_->GetIntConstant(increment);
  HInstruction* constant_10 = graph_->GetIntConstant(10);

  HBasicBlock* block = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block);
  entry->AddSuccessor(block);
  MakeGoto(block);

  HBasicBlock* loop_header = new (GetAllocator()) HBasicBlock(graph_);
  HBasicBlock* loop_body = new (GetAllocator()) HBasicBlock(graph_);
  HBasicBlock* exit = new (GetAllocator()) HBasicBlock(graph_);

  graph_->AddBlock(loop_header);
  graph_->AddBlock(loop_body);
  graph_->AddBlock(exit);
  block->AddSuccessor(loop_header);
  loop_header->AddSuccessor(exit);       // true successor
  loop_header->AddSuccessor(loop_body);  // false successor
  loop_body->AddSuccessor(loop_header);

  HPhi* phi = MakePhi(loop_header, {constant_initial, /* placeholder */ constant_initial});
  HInstruction* null_check = MakeNullCheck(loop_header, parameter);
  HInstruction* array_length = MakeArrayLength(loop_header, null_check);
  HInstruction* cmp = nullptr;
  if (cond == kCondGE) {
    cmp = MakeCondition<HGreaterThanOrEqual>(loop_header, phi, array_length);
  } else {
    DCHECK(cond == kCondGT);
    cmp = MakeCondition<HGreaterThan>(loop_header, phi, array_length);
  }
  MakeIf(loop_header, cmp);

  null_check = MakeNullCheck(loop_body, parameter);
  array_length = MakeArrayLength(loop_body, null_check);
  HInstruction* bounds_check = MakeBoundsCheck(loop_body, phi, array_length);
  HInstruction* array_set =
      MakeArraySet(loop_body, null_check, bounds_check, constant_10, DataType::Type::kInt32);

  HInstruction* add = MakeBinOp<HAdd>(loop_body, DataType::Type::kInt32, phi, constant_increment);
  MakeGoto(loop_body);

  phi->ReplaceInput(add, 1u);  // Update back-edge input.

  MakeExit(exit);

  return bounds_check;
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination1a) {
  // for (int i=0; i<array.length; i++) { array[i] = 10; // Can eliminate with gvn. }
  HInstruction* bounds_check = BuildSSAGraph1(0, 1);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination1b) {
  // for (int i=1; i<array.length; i++) { array[i] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph1(1, 1);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination1c) {
  // for (int i=-1; i<array.length; i++) { array[i] = 10; // Can't eliminate. }
  HInstruction* bounds_check = BuildSSAGraph1(-1, 1);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination1d) {
  // for (int i=0; i<=array.length; i++) { array[i] = 10; // Can't eliminate. }
  HInstruction* bounds_check = BuildSSAGraph1(0, 1, kCondGT);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination1e) {
  // for (int i=0; i<array.length; i += 2) {
  //   array[i] = 10; // Can't eliminate due to overflow concern. }
  HInstruction* bounds_check = BuildSSAGraph1(0, 2);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination1f) {
  // for (int i=1; i<array.length; i += 2) { array[i] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph1(1, 2);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

// for (int i=array.length; i>0; i+=increment) { array[i-1] = 10; }
HInstruction* BoundsCheckEliminationTest::BuildSSAGraph2(int initial,
                                                         int increment,
                                                         IfCondition cond) {
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  HInstruction* parameter = MakeParam(DataType::Type::kReference);

  HInstruction* constant_initial = graph_->GetIntConstant(initial);
  HInstruction* constant_increment = graph_->GetIntConstant(increment);
  HInstruction* constant_minus_1 = graph_->GetIntConstant(-1);
  HInstruction* constant_10 = graph_->GetIntConstant(10);

  HBasicBlock* block = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block);
  entry->AddSuccessor(block);
  HInstruction* null_check = MakeNullCheck(block, parameter);
  HInstruction* array_length = MakeArrayLength(block, null_check);
  MakeGoto(block);

  HBasicBlock* loop_header = new (GetAllocator()) HBasicBlock(graph_);
  HBasicBlock* loop_body = new (GetAllocator()) HBasicBlock(graph_);
  HBasicBlock* exit = new (GetAllocator()) HBasicBlock(graph_);

  graph_->AddBlock(loop_header);
  graph_->AddBlock(loop_body);
  graph_->AddBlock(exit);
  block->AddSuccessor(loop_header);
  loop_header->AddSuccessor(exit);       // true successor
  loop_header->AddSuccessor(loop_body);  // false successor
  loop_body->AddSuccessor(loop_header);

  HPhi* phi = MakePhi(loop_header, {array_length, /* placeholder */ array_length});
  HInstruction* cmp = nullptr;
  if (cond == kCondLE) {
    cmp = MakeCondition<HLessThanOrEqual>(loop_header, phi, constant_initial);
  } else {
    DCHECK(cond == kCondLT);
    cmp = MakeCondition<HLessThan>(loop_header, phi, constant_initial);
  }
  MakeIf(loop_header, cmp);

  HInstruction* add = MakeBinOp<HAdd>(loop_body, DataType::Type::kInt32, phi, constant_minus_1);
  null_check = MakeNullCheck(loop_body, parameter);
  array_length = MakeArrayLength(loop_body, null_check);
  HInstruction* bounds_check = MakeBoundsCheck(loop_body, add, array_length);
  HInstruction* array_set =
      MakeArraySet(loop_body, null_check, bounds_check, constant_10, DataType::Type::kInt32);
  HInstruction* add_phi =
      MakeBinOp<HAdd>(loop_body, DataType::Type::kInt32, phi, constant_increment);
  MakeGoto(loop_body);

  phi->ReplaceInput(add, 1u);  // Update back-edge input.

  MakeExit(exit);

  return bounds_check;
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination2a) {
  // for (int i=array.length; i>0; i--) { array[i-1] = 10; // Can eliminate with gvn. }
  HInstruction* bounds_check = BuildSSAGraph2(0);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination2b) {
  // for (int i=array.length; i>1; i--) { array[i-1] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph2(1);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination2c) {
  // for (int i=array.length; i>-1; i--) { array[i-1] = 10; // Can't eliminate. }
  HInstruction* bounds_check = BuildSSAGraph2(-1);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination2d) {
  // for (int i=array.length; i>=0; i--) { array[i-1] = 10; // Can't eliminate. }
  HInstruction* bounds_check = BuildSSAGraph2(0, -1, kCondLT);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination2e) {
  // for (int i=array.length; i>0; i-=2) { array[i-1] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph2(0, -2);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

// int[] array = new int[10];
// for (int i=0; i<10; i+=increment) { array[i] = 10; }
HInstruction* BoundsCheckEliminationTest::BuildSSAGraph3(int initial,
                                                         int increment,
                                                         IfCondition cond) {
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);

  HInstruction* constant_10 = graph_->GetIntConstant(10);
  HInstruction* constant_initial = graph_->GetIntConstant(initial);
  HInstruction* constant_increment = graph_->GetIntConstant(increment);

  HBasicBlock* block = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block);
  entry->AddSuccessor(block);
  // We pass a bogus constant for the class to avoid mocking one.
  HInstruction* new_array = MakeNewArray(block, /* cls= */ constant_10, /* length= */ constant_10);
  MakeGoto(block);

  HBasicBlock* loop_header = new (GetAllocator()) HBasicBlock(graph_);
  HBasicBlock* loop_body = new (GetAllocator()) HBasicBlock(graph_);
  HBasicBlock* exit = new (GetAllocator()) HBasicBlock(graph_);

  graph_->AddBlock(loop_header);
  graph_->AddBlock(loop_body);
  graph_->AddBlock(exit);
  block->AddSuccessor(loop_header);
  loop_header->AddSuccessor(exit);       // true successor
  loop_header->AddSuccessor(loop_body);  // false successor
  loop_body->AddSuccessor(loop_header);

  HPhi* phi = MakePhi(loop_header, {constant_initial, /* placeholder */ constant_initial});
  HInstruction* cmp = nullptr;
  if (cond == kCondGE) {
    cmp = MakeCondition<HGreaterThanOrEqual>(loop_header, phi, constant_10);
  } else {
    DCHECK(cond == kCondGT);
    cmp = MakeCondition<HGreaterThan>(loop_header, phi, constant_10);
  }
  MakeIf(loop_header, cmp);

  HNullCheck* null_check = MakeNullCheck(loop_body, new_array);
  HArrayLength* array_length = MakeArrayLength(loop_body, null_check);
  HInstruction* bounds_check = MakeBoundsCheck(loop_body, phi, array_length);
  HInstruction* array_set =
      MakeArraySet(loop_body, null_check, bounds_check, constant_10, DataType::Type::kInt32);
  HInstruction* add = MakeBinOp<HAdd>(loop_body, DataType::Type::kInt32, phi, constant_increment);
  MakeGoto(loop_body);

  phi->ReplaceInput(add, 1u);  // Update back-edge input.

  MakeExit(exit);

  return bounds_check;
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination3a) {
  // int[] array = new int[10];
  // for (int i=0; i<10; i++) { array[i] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph3(0, 1, kCondGE);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination3b) {
  // int[] array = new int[10];
  // for (int i=1; i<10; i++) { array[i] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph3(1, 1, kCondGE);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination3c) {
  // int[] array = new int[10];
  // for (int i=0; i<=10; i++) { array[i] = 10; // Can't eliminate. }
  HInstruction* bounds_check = BuildSSAGraph3(0, 1, kCondGT);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination3d) {
  // int[] array = new int[10];
  // for (int i=1; i<10; i+=8) { array[i] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph3(1, 8, kCondGE);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

// for (int i=initial; i<array.length; i++) { array[array.length-i-1] = 10; }
HInstruction* BoundsCheckEliminationTest::BuildSSAGraph4(int initial, IfCondition cond) {
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  HInstruction* parameter = MakeParam(DataType::Type::kReference);

  HInstruction* constant_initial = graph_->GetIntConstant(initial);
  HInstruction* constant_1 = graph_->GetIntConstant(1);
  HInstruction* constant_10 = graph_->GetIntConstant(10);
  HInstruction* constant_minus_1 = graph_->GetIntConstant(-1);

  HBasicBlock* block = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block);
  entry->AddSuccessor(block);
  MakeGoto(block);

  HBasicBlock* loop_header = new (GetAllocator()) HBasicBlock(graph_);
  HBasicBlock* loop_body = new (GetAllocator()) HBasicBlock(graph_);
  HBasicBlock* exit = new (GetAllocator()) HBasicBlock(graph_);

  graph_->AddBlock(loop_header);
  graph_->AddBlock(loop_body);
  graph_->AddBlock(exit);
  block->AddSuccessor(loop_header);
  loop_header->AddSuccessor(exit);       // true successor
  loop_header->AddSuccessor(loop_body);  // false successor
  loop_body->AddSuccessor(loop_header);

  HPhi* phi = MakePhi(loop_header, {constant_initial, /* placeholder */ constant_initial});
  HInstruction* null_check = MakeNullCheck(loop_header, parameter);
  HInstruction* array_length = MakeArrayLength(loop_header, null_check);
  HInstruction* cmp = nullptr;
  if (cond == kCondGE) {
    cmp = MakeCondition<HGreaterThanOrEqual>(loop_header, phi, array_length);
  } else if (cond == kCondGT) {
    cmp = MakeCondition<HGreaterThan>(loop_header, phi, array_length);
  }
  MakeIf(loop_header, cmp);

  null_check = MakeNullCheck(loop_body, parameter);
  array_length = MakeArrayLength(loop_body, null_check);
  HInstruction* sub = MakeBinOp<HSub>(loop_body, DataType::Type::kInt32, array_length, phi);
  HInstruction* add_minus_1 =
      MakeBinOp<HAdd>(loop_body, DataType::Type::kInt32, sub, constant_minus_1);
  HInstruction* bounds_check = MakeBoundsCheck(loop_body, add_minus_1, array_length);
  HInstruction* array_set =
      MakeArraySet(loop_body, null_check, bounds_check, constant_10, DataType::Type::kInt32);
  HInstruction* add = MakeBinOp<HAdd>(loop_body, DataType::Type::kInt32, phi, constant_1);
  MakeGoto(loop_body);

  phi->ReplaceInput(add, 1u);  // Update back-edge input.

  MakeExit(exit);

  return bounds_check;
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination4a) {
  // for (int i=0; i<array.length; i++) { array[array.length-i-1] = 10; // Can eliminate with gvn. }
  HInstruction* bounds_check = BuildSSAGraph4(0);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination4b) {
  // for (int i=1; i<array.length; i++) { array[array.length-i-1] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph4(1);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination4c) {
  // for (int i=0; i<=array.length; i++) { array[array.length-i] = 10; // Can't eliminate. }
  HInstruction* bounds_check = BuildSSAGraph4(0, kCondGT);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

// Bubble sort:
// (Every array access bounds-check can be eliminated.)
// for (int i=0; i<array.length-1; i++) {
//  for (int j=0; j<array.length-i-1; j++) {
//     if (array[j] > array[j+1]) {
//       int temp = array[j+1];
//       array[j+1] = array[j];
//       array[j] = temp;
//     }
//  }
// }
TEST_F(BoundsCheckEliminationTest, BubbleSortArrayBoundsElimination) {
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  HInstruction* parameter = MakeParam(DataType::Type::kReference);

  HInstruction* constant_0 = graph_->GetIntConstant(0);
  HInstruction* constant_minus_1 = graph_->GetIntConstant(-1);
  HInstruction* constant_1 = graph_->GetIntConstant(1);

  HBasicBlock* block = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block);
  entry->AddSuccessor(block);
  block->AddInstruction(new (GetAllocator()) HGoto());

  HBasicBlock* exit = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(exit);
  MakeExit(exit);

  HBasicBlock* outer_header = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(outer_header);
  HPhi* phi_i = MakePhi(outer_header, {constant_0, /* placeholder */ constant_0});
  HNullCheck* null_check = MakeNullCheck(outer_header, parameter);
  HArrayLength* array_length = MakeArrayLength(outer_header, null_check);
  HAdd* add = MakeBinOp<HAdd>(outer_header, DataType::Type::kInt32, array_length, constant_minus_1);
  HInstruction* cmp = MakeCondition<HGreaterThanOrEqual>(outer_header, phi_i, add);
  MakeIf(outer_header, cmp);

  HBasicBlock* inner_header = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(inner_header);
  HPhi* phi_j = MakePhi(inner_header, {constant_0, /* placeholder */ constant_0});
  null_check = MakeNullCheck(inner_header, parameter);
  array_length = MakeArrayLength(inner_header, null_check);
  HSub* sub = MakeBinOp<HSub>(inner_header, DataType::Type::kInt32, array_length, phi_i);
  add = MakeBinOp<HAdd>(inner_header, DataType::Type::kInt32, sub, constant_minus_1);
  cmp = MakeCondition<HGreaterThanOrEqual>(inner_header, phi_j, add);
  MakeIf(inner_header, cmp);

  HBasicBlock* inner_body_compare = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(inner_body_compare);
  null_check = MakeNullCheck(inner_body_compare, parameter);
  array_length = MakeArrayLength(inner_body_compare, null_check);
  HBoundsCheck* bounds_check1 = MakeBoundsCheck(inner_body_compare, phi_j, array_length);
  HArrayGet* array_get_j =
      MakeArrayGet(inner_body_compare, null_check, bounds_check1, DataType::Type::kInt32);
  HInstruction* j_plus_1 =
      MakeBinOp<HAdd>(inner_body_compare, DataType::Type::kInt32, phi_j, constant_1);
  null_check = MakeNullCheck(inner_body_compare, parameter);
  array_length = MakeArrayLength(inner_body_compare, null_check);
  HBoundsCheck* bounds_check2 = MakeBoundsCheck(inner_body_compare, j_plus_1, array_length);
  HArrayGet* array_get_j_plus_1 =
      MakeArrayGet(inner_body_compare, null_check, bounds_check2, DataType::Type::kInt32);
  cmp = MakeCondition<HGreaterThanOrEqual>(inner_body_compare, array_get_j, array_get_j_plus_1);
  MakeIf(inner_body_compare, cmp);

  HBasicBlock* inner_body_swap = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(inner_body_swap);
  j_plus_1 = new (GetAllocator()) HAdd(DataType::Type::kInt32, phi_j, constant_1);
  // temp = array[j+1]
  null_check = new (GetAllocator()) HNullCheck(parameter, 0);
  array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  HInstruction* bounds_check3 = new (GetAllocator()) HBoundsCheck(j_plus_1, array_length, 0);
  array_get_j_plus_1 = new (GetAllocator())
      HArrayGet(null_check, bounds_check3, DataType::Type::kInt32, 0);
  inner_body_swap->AddInstruction(j_plus_1);
  inner_body_swap->AddInstruction(null_check);
  inner_body_swap->AddInstruction(array_length);
  inner_body_swap->AddInstruction(bounds_check3);
  inner_body_swap->AddInstruction(array_get_j_plus_1);
  // array[j+1] = array[j]
  null_check = new (GetAllocator()) HNullCheck(parameter, 0);
  array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  HInstruction* bounds_check4 = new (GetAllocator()) HBoundsCheck(phi_j, array_length, 0);
  array_get_j = new (GetAllocator())
      HArrayGet(null_check, bounds_check4, DataType::Type::kInt32, 0);
  inner_body_swap->AddInstruction(null_check);
  inner_body_swap->AddInstruction(array_length);
  inner_body_swap->AddInstruction(bounds_check4);
  inner_body_swap->AddInstruction(array_get_j);
  null_check = new (GetAllocator()) HNullCheck(parameter, 0);
  array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  HInstruction* bounds_check5 = new (GetAllocator()) HBoundsCheck(j_plus_1, array_length, 0);
  HArraySet* array_set_j_plus_1 = new (GetAllocator())
      HArraySet(null_check, bounds_check5, array_get_j, DataType::Type::kInt32, 0);
  inner_body_swap->AddInstruction(null_check);
  inner_body_swap->AddInstruction(array_length);
  inner_body_swap->AddInstruction(bounds_check5);
  inner_body_swap->AddInstruction(array_set_j_plus_1);
  // array[j] = temp
  null_check = new (GetAllocator()) HNullCheck(parameter, 0);
  array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  HInstruction* bounds_check6 = new (GetAllocator()) HBoundsCheck(phi_j, array_length, 0);
  HArraySet* array_set_j = new (GetAllocator())
      HArraySet(null_check, bounds_check6, array_get_j_plus_1, DataType::Type::kInt32, 0);
  inner_body_swap->AddInstruction(null_check);
  inner_body_swap->AddInstruction(array_length);
  inner_body_swap->AddInstruction(bounds_check6);
  inner_body_swap->AddInstruction(array_set_j);
  inner_body_swap->AddInstruction(new (GetAllocator()) HGoto());

  HBasicBlock* inner_body_add = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(inner_body_add);
  add = new (GetAllocator()) HAdd(DataType::Type::kInt32, phi_j, constant_1);
  inner_body_add->AddInstruction(add);
  inner_body_add->AddInstruction(new (GetAllocator()) HGoto());

  phi_j->ReplaceInput(add, 1u);  // Update back-edge input.

  HBasicBlock* outer_body_add = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(outer_body_add);
  add = new (GetAllocator()) HAdd(DataType::Type::kInt32, phi_i, constant_1);
  outer_body_add->AddInstruction(add);
  outer_body_add->AddInstruction(new (GetAllocator()) HGoto());

  phi_i->ReplaceInput(add, 1u);  // Update back-edge input.

  block->AddSuccessor(outer_header);
  outer_header->AddSuccessor(exit);
  outer_header->AddSuccessor(inner_header);
  inner_header->AddSuccessor(outer_body_add);
  inner_header->AddSuccessor(inner_body_compare);
  inner_body_compare->AddSuccessor(inner_body_add);
  inner_body_compare->AddSuccessor(inner_body_swap);
  inner_body_swap->AddSuccessor(inner_body_add);
  inner_body_add->AddSuccessor(inner_header);
  outer_body_add->AddSuccessor(outer_header);

  RunBCE();  // gvn removes same bounds check already

  ASSERT_TRUE(IsRemoved(bounds_check1));
  ASSERT_TRUE(IsRemoved(bounds_check2));
  ASSERT_TRUE(IsRemoved(bounds_check3));
  ASSERT_TRUE(IsRemoved(bounds_check4));
  ASSERT_TRUE(IsRemoved(bounds_check5));
  ASSERT_TRUE(IsRemoved(bounds_check6));
}

// int[] array = new int[10];
// for (int i=0; i<200; i++) {
//   array[i%10] = 10;            // Can eliminate
//   array[i%1] = 10;             // Can eliminate
//   array[i%200] = 10;           // Cannot eliminate
//   array[i%-10] = 10;           // Can eliminate
//   array[i%array.length] = 10;  // Can eliminate
//   array[param_i%10] = 10;      // Can't eliminate, when param_i < 0
// }
TEST_F(BoundsCheckEliminationTest, ModArrayBoundsElimination) {
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  HInstruction* param_i = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(0), 0, DataType::Type::kInt32);
  entry->AddInstruction(param_i);

  HInstruction* constant_0 = graph_->GetIntConstant(0);
  HInstruction* constant_1 = graph_->GetIntConstant(1);
  HInstruction* constant_10 = graph_->GetIntConstant(10);
  HInstruction* constant_200 = graph_->GetIntConstant(200);
  HInstruction* constant_minus_10 = graph_->GetIntConstant(-10);

  HBasicBlock* block = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(block);
  entry->AddSuccessor(block);
  // We pass a bogus constant for the class to avoid mocking one.
  HInstruction* new_array = new (GetAllocator()) HNewArray(
      /* cls= */ constant_10,
      /* length= */ constant_10,
      /* dex_pc= */ 0,
      /* component_size_shift= */ 0);
  block->AddInstruction(new_array);
  block->AddInstruction(new (GetAllocator()) HGoto());

  HBasicBlock* loop_header = new (GetAllocator()) HBasicBlock(graph_);
  HBasicBlock* loop_body = new (GetAllocator()) HBasicBlock(graph_);
  HBasicBlock* exit = new (GetAllocator()) HBasicBlock(graph_);

  graph_->AddBlock(loop_header);
  graph_->AddBlock(loop_body);
  graph_->AddBlock(exit);
  block->AddSuccessor(loop_header);
  loop_header->AddSuccessor(exit);       // true successor
  loop_header->AddSuccessor(loop_body);  // false successor
  loop_body->AddSuccessor(loop_header);

  HPhi* phi = new (GetAllocator()) HPhi(GetAllocator(), 0, 0, DataType::Type::kInt32);
  HInstruction* cmp = new (GetAllocator()) HGreaterThanOrEqual(phi, constant_200);
  HInstruction* if_inst = new (GetAllocator()) HIf(cmp);
  loop_header->AddPhi(phi);
  loop_header->AddInstruction(cmp);
  loop_header->AddInstruction(if_inst);
  phi->AddInput(constant_0);

  //////////////////////////////////////////////////////////////////////////////////
  // LOOP BODY:
  // array[i % 10] = 10;
  HRem* i_mod_10 = new (GetAllocator()) HRem(DataType::Type::kInt32, phi, constant_10, 0);
  HBoundsCheck* bounds_check_i_mod_10 = new (GetAllocator()) HBoundsCheck(i_mod_10, constant_10, 0);
  HInstruction* array_set = new (GetAllocator()) HArraySet(
      new_array, bounds_check_i_mod_10, constant_10, DataType::Type::kInt32, 0);
  loop_body->AddInstruction(i_mod_10);
  loop_body->AddInstruction(bounds_check_i_mod_10);
  loop_body->AddInstruction(array_set);

  // array[i % 1] = 10;
  HRem* i_mod_1 = new (GetAllocator()) HRem(DataType::Type::kInt32, phi, constant_1, 0);
  HBoundsCheck* bounds_check_i_mod_1 = new (GetAllocator()) HBoundsCheck(i_mod_1, constant_10, 0);
  array_set = new (GetAllocator()) HArraySet(
      new_array, bounds_check_i_mod_1, constant_10, DataType::Type::kInt32, 0);
  loop_body->AddInstruction(i_mod_1);
  loop_body->AddInstruction(bounds_check_i_mod_1);
  loop_body->AddInstruction(array_set);

  // array[i % 200] = 10;
  HRem* i_mod_200 = new (GetAllocator()) HRem(DataType::Type::kInt32, phi, constant_1, 0);
  HBoundsCheck* bounds_check_i_mod_200 = new (GetAllocator()) HBoundsCheck(
      i_mod_200, constant_10, 0);
  array_set = new (GetAllocator()) HArraySet(
      new_array, bounds_check_i_mod_200, constant_10, DataType::Type::kInt32, 0);
  loop_body->AddInstruction(i_mod_200);
  loop_body->AddInstruction(bounds_check_i_mod_200);
  loop_body->AddInstruction(array_set);

  // array[i % -10] = 10;
  HRem* i_mod_minus_10 = new (GetAllocator()) HRem(
      DataType::Type::kInt32, phi, constant_minus_10, 0);
  HBoundsCheck* bounds_check_i_mod_minus_10 = new (GetAllocator()) HBoundsCheck(
      i_mod_minus_10, constant_10, 0);
  array_set = new (GetAllocator()) HArraySet(
      new_array, bounds_check_i_mod_minus_10, constant_10, DataType::Type::kInt32, 0);
  loop_body->AddInstruction(i_mod_minus_10);
  loop_body->AddInstruction(bounds_check_i_mod_minus_10);
  loop_body->AddInstruction(array_set);

  // array[i%array.length] = 10;
  HNullCheck* null_check = new (GetAllocator()) HNullCheck(new_array, 0);
  HArrayLength* array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  HRem* i_mod_array_length = new (GetAllocator()) HRem(
      DataType::Type::kInt32, phi, array_length, 0);
  HBoundsCheck* bounds_check_i_mod_array_len = new (GetAllocator()) HBoundsCheck(
      i_mod_array_length, array_length, 0);
  array_set = new (GetAllocator()) HArraySet(
      null_check, bounds_check_i_mod_array_len, constant_10, DataType::Type::kInt32, 0);
  loop_body->AddInstruction(null_check);
  loop_body->AddInstruction(array_length);
  loop_body->AddInstruction(i_mod_array_length);
  loop_body->AddInstruction(bounds_check_i_mod_array_len);
  loop_body->AddInstruction(array_set);

  // array[param_i % 10] = 10;
  HRem* param_i_mod_10 = new (GetAllocator()) HRem(DataType::Type::kInt32, param_i, constant_10, 0);
  HBoundsCheck* bounds_check_param_i_mod_10 = new (GetAllocator()) HBoundsCheck(
      param_i_mod_10, constant_10, 0);
  array_set = new (GetAllocator()) HArraySet(
      new_array, bounds_check_param_i_mod_10, constant_10, DataType::Type::kInt32, 0);
  loop_body->AddInstruction(param_i_mod_10);
  loop_body->AddInstruction(bounds_check_param_i_mod_10);
  loop_body->AddInstruction(array_set);

  // array[param_i%array.length] = 10;
  null_check = new (GetAllocator()) HNullCheck(new_array, 0);
  array_length = new (GetAllocator()) HArrayLength(null_check, 0);
  HRem* param_i_mod_array_length = new (GetAllocator()) HRem(
      DataType::Type::kInt32, param_i, array_length, 0);
  HBoundsCheck* bounds_check_param_i_mod_array_len = new (GetAllocator()) HBoundsCheck(
      param_i_mod_array_length, array_length, 0);
  array_set = new (GetAllocator()) HArraySet(
      null_check, bounds_check_param_i_mod_array_len, constant_10, DataType::Type::kInt32, 0);
  loop_body->AddInstruction(null_check);
  loop_body->AddInstruction(array_length);
  loop_body->AddInstruction(param_i_mod_array_length);
  loop_body->AddInstruction(bounds_check_param_i_mod_array_len);
  loop_body->AddInstruction(array_set);

  // i++;
  HInstruction* add = new (GetAllocator()) HAdd(DataType::Type::kInt32, phi, constant_1);
  loop_body->AddInstruction(add);
  loop_body->AddInstruction(new (GetAllocator()) HGoto());
  phi->AddInput(add);
  //////////////////////////////////////////////////////////////////////////////////

  MakeExit(exit);

  RunBCE();

  ASSERT_TRUE(IsRemoved(bounds_check_i_mod_10));
  ASSERT_TRUE(IsRemoved(bounds_check_i_mod_1));
  ASSERT_TRUE(IsRemoved(bounds_check_i_mod_200));
  ASSERT_TRUE(IsRemoved(bounds_check_i_mod_minus_10));
  ASSERT_TRUE(IsRemoved(bounds_check_i_mod_array_len));
  ASSERT_FALSE(IsRemoved(bounds_check_param_i_mod_10));
}

}  // namespace art
