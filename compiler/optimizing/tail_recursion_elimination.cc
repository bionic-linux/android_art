/*
 * Copyright (C) 2018 The Android Open Source Project
 * Implementation of tail-recursion-elimination compiler optimization
 */

#include "tail_recursion_elimination.h"

#include "base/scoped_arena_allocator.h"
#include "base/scoped_arena_containers.h"
#include "nodes.h"

#include <string.h>
#include <algorithm>

namespace art {

// API to check if input instruction contain call to given method name
bool TailRecursionElimination::CheckTailRecursive(HInstruction* instr, const char* method_name, TREContext& trec) {
  if (!instr) {
    return false;
  }

  HInvoke* invoke = nullptr;
  if (instr->IsInvokeStaticOrDirect() || instr->IsInvokeVirtual()) {
    invoke = instr->AsInvoke();
    DCHECK(invoke);
  } else {
    return false;
  }

  DataType::Type type = invoke->GetType();
  if ((type == DataType::Type::kReference) || (type == DataType::Type::kVoid)) {
    return false;
  }

  uint32_t methodIdx = invoke->GetDexMethodIndex();
  std::string t_str = graph_->GetDexFile().PrettyMethod(methodIdx);
  const char* t_name = t_str.c_str();
  if (strncmp(t_name, method_name, strlen(t_name)) != 0) {
    return false;
  }

  // store invoke instruction
  trec.recursive_invoke_instr_.push_back(invoke);

  HBasicBlock* inst_blk = invoke->GetBlock();
  std::map<HBasicBlock*, std::vector<HInvoke*>>::iterator iter_map;
  iter_map = trec.recursive_invoke_map_.find(inst_blk);

  if (iter_map == trec.recursive_invoke_map_.end()) {
    trec.recursive_invoke_map_.insert(std::pair<HBasicBlock*,
      std::vector<HInvoke*>>(inst_blk, std::vector<HInvoke*>()));
  }

  trec.recursive_invoke_map_[inst_blk].push_back(invoke);

  // store return type
  trec.ret_type_ = type;

  return true;
}

/* API to check recursively if input instruction or (it's input) contain call to given method name.
 * We don't stop at first success. We check all return inputs recursively to find all recursive calls.
 */
bool TailRecursionElimination::IdentifyTailRecursion(HInstruction* intsr, const char* method_name, TREContext& trec) {
  if (!intsr) {
    return false;
  }

  bool find_flag = false;

  for (HInstruction* input : intsr->GetInputs()) {
    std::vector<HInstruction*>::iterator iter_inst;

    // storing checked input in vector in order to avoid deadlock due to recursive inputs
    iter_inst = std::find(trec.checked_instr_for_name_.begin(), trec.checked_instr_for_name_.end(), input);

    if (iter_inst >= trec.checked_instr_for_name_.end()) {
      trec.checked_instr_for_name_.push_back(input);

      if (CheckTailRecursive(input, method_name, trec)) {
        trec.ret_op_ = intsr;
        find_flag = true;
      } else if (input->IsInvoke()) {
        continue;  // don't check input of invoke instruction
      } else if (IdentifyTailRecursion(input, method_name, trec)) {
        find_flag = true;
      }
    }
  }
  return find_flag;
}

// build environment for incoming suspend check instruction
void BuildEnvManuallyFor(HGraph* graph, HInstruction* instruction, ArenaVector<HInstruction*>* current_locals) {
  ArenaAllocator* allocator = graph->GetAllocator();
  DCHECK(allocator);

  int num_vregs = graph->GetNumberOfVRegs();
  HEnvironment* environment = new (allocator) HEnvironment( allocator, num_vregs /*current_locals->size()*/,
        graph->GetArtMethod(), instruction->GetDexPc(), instruction);
  DCHECK(environment);

  environment->CopyFrom(ArrayRef<HInstruction* const>(*current_locals));
  instruction->SetRawEnvironment(environment);
}

// Iterating over all parameters and identifying important parameter (impact recursion), major IF condition (break recursion),
// accumulator parameter (modified inside method & included in method output) and return block
bool TailRecursionElimination::IdentifyIfRetBlkMainParam(HBasicBlock* exit_blk, HBasicBlock* first_block, TREContext& trec) {
  bool if_break_true = false;
  HIf* inst_if = nullptr;
  int flag_if = false;
  int flag_bin = false;
  int flag_ret = false;
  bool if_true_invoke = false;
  bool if_false_invoke = false;

  for (HInstructionIterator it(first_block->GetInstructions()); !it.Done(); it.Advance()) {
    HInstruction* cur_inst = it.Current();
    if (cur_inst->IsParameterValue()) {
      if_break_true = false;
      inst_if = nullptr;
      flag_if = false;
      flag_bin = false;
      flag_ret = false;

      trec.param_list_.push_back(cur_inst);

      for (const HUseListNode<HInstruction*>& use : cur_inst->GetUses()) {
        HInstruction* inst = use.GetUser();

        for (const HUseListNode<HInstruction*>& use1 : inst->GetUses()) {
          HInstruction* inst1 = use1.GetUser();
          if (inst1->IsIf()) {
            HIf* inst2 = inst1->AsIf();
            DCHECK(inst2);
            HBasicBlock* true_successor = inst2->IfTrueSuccessor();
            HBasicBlock* false_successor = inst2->IfFalseSuccessor();

            // traverse from both true_successor & false_successor till exit_blk's Single predecessor
            // and find which path contains recursive invoke instruction
            std::vector<HBasicBlock*> block_list;
            HBasicBlock* arr_blk[2] = {true_successor, false_successor};
            unsigned int num_blk[2] = {0, 0};

            for (int iter_blk = 0; iter_blk < 2; iter_blk++) {
              block_list.clear();
              HBasicBlock* cur_blk = arr_blk[iter_blk];
              while (cur_blk != exit_blk) {
                num_blk[iter_blk]++;
                if (trec.recursive_invoke_map_.find(cur_blk) != trec.recursive_invoke_map_.end()) {
                  if (iter_blk == 0) {
                    if_true_invoke = true;
                  } else {
                    if_false_invoke = true;
                  }
                }

                // identify return statements of both true and false paths
                HInstruction* last_inst = cur_blk->GetLastInstruction();
                if ((last_inst != nullptr) && (last_inst->IsReturn())) {
                  if (iter_blk == 0) {
                    trec.true_return_ = last_inst;
                  } else {
                    trec.false_return_ = last_inst;
                  }
                }

                for (HBasicBlock* succ_if : cur_blk->GetSuccessors()) {
                  std::vector<HBasicBlock*>::iterator iter_blk1;

                  iter_blk1 = std::find(block_list.begin(), block_list.end(), succ_if);

                  if (iter_blk1 >= block_list.end()) {
                    block_list.push_back(succ_if);
                  }
                }

                if (!(block_list.empty())) {
                  cur_blk = block_list.front();
                  block_list.erase(block_list.begin());
                }
              }
            }

            // the exit successor of IF condition should have only 1 block in between
            if ((if_true_invoke == true) && (if_false_invoke == true)) {
              return false;
            } else if (if_false_invoke == true) {
              if (num_blk[0] > 1) {
                return false;
              } else {
                if_break_true = true;
                inst_if = inst2;
                flag_if = true;
                break;
              }
            } else if (if_true_invoke == true) {
              if (num_blk[1] > 1) {
                return false;
              } else {
                if_break_true = false;
                inst_if = inst2;
                flag_if = true;
                break;
              }
            }
          } else if (inst1->IsReturn()) {
            flag_ret = true;
          }
        }

        if ((inst->IsBinaryOperation()) && (inst->IsAdd() || inst->IsSub()
          || inst->IsMul() || inst->IsDiv())) {
          if (inst->InputAt(0) == cur_inst) {
            flag_bin = true;
          }
        } else if (inst->IsReturn()) {
          flag_ret = true;
        }
      }

      if ((flag_if == true) && (flag_bin == true)) {
        if (trec.imp_param_ == nullptr) {
          trec.if_true_exit_ = if_break_true;
          trec.inst_if_exit_ = inst_if;
          trec.imp_param_ = cur_inst;
        } else {
          // we are not handling case where 2 or more parameter are impacting recursion
          return false;
        }
      } else if ((flag_ret == true) && (flag_bin == true)) {
        if (trec.acc_param_ == nullptr) {
          trec.acc_param_ = cur_inst;
        } else {
          // we are not handling case where 2 or more parameter are accumulating value across recursion
          return false;
        }
      }
    }
  }

  // if there is no important param, then do not handle
  if ((trec.imp_param_ == nullptr) || (trec.inst_if_exit_ == nullptr)) {
    return false;
  } else {
    return true;
  }
}

// API for creation of accumulator instruction which will be input for return
// statement inside new loop exit block
HInstruction* TailRecursionElimination::GetAccInstruction(HInstruction* val1, HInstruction* val2, TREContext& trec) {
  if ((val1 == nullptr) || (val2 == nullptr)) {
    return nullptr;
  }

  // get allocator
  ArenaAllocator* allocator = graph_->GetAllocator();
  DCHECK(allocator);
  DataType::Type ret_type = trec.ret_type_;
  HInstruction* new_instr = nullptr;

  if (trec.ret_op_->IsAdd()) {
    new_instr = new (allocator) HAdd(ret_type, val1, val2);
  } else if (trec.ret_op_->IsSub()) {
    new_instr = new (allocator) HSub(ret_type, val1, val2);
  } else if (trec.ret_op_->IsMul()) {
    new_instr = new (allocator) HMul(ret_type, val1, val2);
  } else if (trec.ret_op_->IsDiv()) {
    new_instr = new (allocator) HDiv(ret_type, val1, val2, trec.ret_op_->GetDexPc());
  } else if (trec.ret_op_->IsShl()) {
    new_instr = new (allocator) HShl(ret_type, val1, val2, trec.ret_op_->GetDexPc());
  } else if (trec.ret_op_->IsShr()) {
    new_instr = new (allocator) HShr(ret_type, val1, val2, trec.ret_op_->GetDexPc());
  }

  return new_instr;
}

// API to perform method graph transformations for tail-recursion elimination
bool TailRecursionElimination::TransformMethodGraph(HBasicBlock* exit_blk, HBasicBlock* first_block, TREContext& trec) {
  HBasicBlock* new_loop_header = nullptr;
  HBasicBlock* new_loop_exit = nullptr;

  // get allocator
  ArenaAllocator* allocator = graph_->GetAllocator();
  DCHECK(allocator);

  // Identifying major IF condition, return block and method parameter which impact recursion
  if (!IdentifyIfRetBlkMainParam(exit_blk, first_block, trec)) {
    return false;
  }

  HIf* inst_if_exit = trec.inst_if_exit_;
  HInstruction* imp_param = trec.imp_param_;
  DataType::Type ret_type = trec.ret_type_;

  // get the first block of graph and it's successor
  HBasicBlock* first_successor = first_block->GetSingleSuccessor();

  // don't handle if last instruction of first block successor isn't IF
  HIf* if_insn = first_successor->GetLastInstruction()->AsIf();
  if (if_insn == inst_if_exit) {
    new_loop_header = first_successor;
  } else {
    // for now, don't handle other cases
    return false;
  }
  DCHECK(new_loop_header);

  // avoid the case where 2 or more recursive invokes exist in same block with different induction var operation as input
  HInstruction* input_param_invoke = nullptr;
  for (std::map<HBasicBlock*, std::vector<HInvoke*>>::iterator iter_invoke_map = trec.recursive_invoke_map_.begin();
    iter_invoke_map != trec.recursive_invoke_map_.end(); ++iter_invoke_map) {
    if (iter_invoke_map->second.size() > 1) {
      std::vector<HInvoke*> vec_invoke = iter_invoke_map->second;
      for (std::vector<HInvoke*>::iterator it_inv = vec_invoke.begin(); it_inv != vec_invoke.end(); ++it_inv) {
        HInvoke* t_invoke = *it_inv;
        for (HInstruction* t_input : t_invoke->GetInputs()) {
          if (t_input->IsBinaryOperation() && (t_input->InputAt(0) == imp_param)) {
            if (input_param_invoke == nullptr) {
              input_param_invoke = t_input;
            } else if (input_param_invoke != t_input) {
              return false;
            }
          }
        }
      }
    }
  }

  // identify second input for accumulator PHI
  HInstruction* last_accu_op = nullptr;
  HInstruction* if_exit_val = nullptr;
  HInstruction* ret_loop_back_edge = nullptr;
  HBasicBlock* new_loop_back_edge = nullptr;

  if (trec.if_true_exit_ == true) {
    ret_loop_back_edge = trec.false_return_;
    if_exit_val = trec.true_return_->InputAt(0);
    last_accu_op = trec.false_return_->InputAt(0);
  } else {
    ret_loop_back_edge = trec.true_return_;
    if_exit_val = trec.false_return_->InputAt(0);
    last_accu_op = trec.true_return_->InputAt(0);
  }
  CHECK(last_accu_op);
  new_loop_back_edge = ret_loop_back_edge->GetBlock();

  // check whether we can decide initial value for accumulator PHI
  // initial value for accumulator variable
  HInstruction* inst_accumulator_val = nullptr;
  if (if_exit_val) {
    if (if_exit_val->IsConstant()) {
      inst_accumulator_val = if_exit_val;
    } else if (if_exit_val == imp_param) {
      inst_accumulator_val = (inst_if_exit->InputAt(0))->InputAt(1);
    } else if ((if_exit_val->IsBinaryOperation()) && (if_exit_val->InputAt(0) == imp_param)
              && (if_exit_val->InputAt(1)->IsConstant())) {
      HInstruction* t_inst = (inst_if_exit->InputAt(0))->InputAt(1);
      DCHECK(t_inst);
      uint64_t val1 = t_inst->AsConstant()->GetValueAsUint64();

      HConstant* t_inst1 = if_exit_val->InputAt(1)->AsConstant();
      DCHECK(t_inst1);
      uint64_t val2 = t_inst1->GetValueAsUint64();

      if (if_exit_val->IsAdd()) {
        inst_accumulator_val = graph_->GetConstant(ret_type, val1 + val2);
      } else if (if_exit_val->IsSub()) {
        inst_accumulator_val = graph_->GetConstant(ret_type, val1 - val2);
      } else if (if_exit_val->IsMul()) {
        inst_accumulator_val = graph_->GetConstant(ret_type, val1 * val2);
      } else if (if_exit_val->IsDiv()) {
        inst_accumulator_val = graph_->GetConstant(ret_type, val1 / val2);
      } else if (if_exit_val->IsShl()) {
        inst_accumulator_val = graph_->GetConstant(ret_type, val1 << val2);
      } else if (if_exit_val->IsShr()) {
        inst_accumulator_val = graph_->GetConstant(ret_type, val1 >> val2);
      } else {
        return false;
      }
    } else if (if_exit_val == trec.acc_param_) {
      if (trec.ret_op_ && trec.ret_op_->IsBinaryOperation() && (trec.ret_op_->IsDiv() || trec.ret_op_->IsMul())) {
        inst_accumulator_val = graph_->GetConstant(ret_type, 1);
      } else {
        inst_accumulator_val = graph_->GetConstant(ret_type, 0);
      }
    } else {
      return false;
    }
  }

  // add new PHI for induction variable in loop header (type same as recursion impacting parameter)
  HPhi* phi_induction = new (allocator) HPhi(allocator, kNoRegNumber, 0, HPhi::ToPhiType(imp_param->GetType()));
  DCHECK(phi_induction);
  new_loop_header->AddPhi(phi_induction);

  // add new PHI for accumulator function parameter in loop header (type same as accumulator method parameter)
  HPhi* phi_acc_param = nullptr;
  if (trec.acc_param_ != nullptr) {
    phi_acc_param = new (allocator) HPhi(allocator, kNoRegNumber, 0, HPhi::ToPhiType(trec.acc_param_->GetType()));
    DCHECK(phi_acc_param);
    new_loop_header->AddPhi(phi_acc_param);
    phi_acc_param->AddInput(trec.acc_param_);
  }

  // add new PHI for accumulator variable in loop header (type same as method return type)
  HPhi* phi_accumulator = new (allocator) HPhi(allocator, kNoRegNumber, 0, HPhi::ToPhiType(ret_type));
  DCHECK(phi_accumulator);
  new_loop_header->AddPhi(phi_accumulator);

  // make loop and add back-edge between new block and old pre-exit block
  new_loop_header->AddBackEdge(new_loop_back_edge);
  HLoopInformation* loop_info = new_loop_header->GetLoopInformation();
  loop_info->SetHeader(new_loop_header);

  // add suspend check in loop header "new_loop_header"
  HInstruction* first_ins = new_loop_header->GetFirstInstruction();
  HSuspendCheck* suspend_check = new (allocator) HSuspendCheck(new_loop_header->GetDexPc());
  DCHECK(suspend_check);
  new_loop_header->InsertInstructionBefore(suspend_check, first_ins);

  // Add environment for suspend check
  ArenaVector<HInstruction*> current_locals(allocator->Adapter(kArenaAllocInstruction));
  current_locals.push_back(phi_accumulator);

  for (std::vector<HInstruction*>::iterator iter1 = trec.param_list_.begin(); iter1 != trec.param_list_.end(); ++iter1) {
    HInstruction* t_inst = *iter1;
    DCHECK(t_inst);
    if (t_inst == imp_param) {
      current_locals.push_back(phi_induction);
    } else if (t_inst == trec.acc_param_) {
      current_locals.push_back(phi_acc_param);
    } else {
      current_locals.push_back(t_inst);
    }
  }

  // build (& add) Environment for suspend check
  BuildEnvManuallyFor(graph_, suspend_check, &current_locals);

  // update predecessor/successor relation beween "new_loop_back_edge" & "new_loop_header"
  new_loop_back_edge->AddSuccessor(new_loop_header);

  loop_info->SetSuspendCheck(suspend_check);
  graph_->OrderLoopHeaderPredecessors(new_loop_header);

  // removing old return instruction & inserting new goto to "new_loop_back_edge"
  new_loop_back_edge->RemoveInstruction(ret_loop_back_edge);
  new_loop_back_edge->AddInstruction(new (allocator) HGoto(kNoDexPc));

  // add new block as successor to new loop header block and make it new pre-exit block
  new_loop_exit = new (allocator) HBasicBlock(graph_);
  DCHECK(new_loop_exit);
  graph_->AddBlock(new_loop_exit);

  // copy all instructions from old exit-successor of IF to new exit-successor block
  HBasicBlock* old_if_exit_block = nullptr;
  old_if_exit_block = (trec.if_true_exit_ == true)? inst_if_exit->IfTrueSuccessor() : inst_if_exit->IfFalseSuccessor();
  for (HInstructionIterator it(old_if_exit_block->GetInstructions()); !it.Done(); it.Advance()) {
    HInstruction* cur_ins = it.Current();
    if (cur_ins->IsGoto() || cur_ins->IsReturn()) {
      old_if_exit_block->RemoveInstruction(cur_ins);
      continue;
    }

    // check usage of "cur_ins" and remove input corresponding to "cur_ins" usage
    const HUseList<HInstruction*>& uses_cur = cur_ins->GetUses();
    for (auto it1 = uses_cur.begin(), end1 = uses_cur.end(); it1 != end1;  /* ++it1 below */) {
      HInstruction* t_inst = it1->GetUser();
      unsigned int t_index = it1->GetIndex();
      ++it1;

      HBasicBlock* inst_blk = t_inst->GetBlock();
      if ((inst_blk != old_if_exit_block) && (t_inst->IsPhi())) {
        HPhi* t_phi = t_inst->AsPhi();
        HInstruction* other = t_phi->InputAt(1 - t_index);
        // if last_accu_op is same as this instruction, update last_accu_op
        if (last_accu_op == t_inst) {
          last_accu_op = other;
        }

        t_phi->ReplaceWith(other);
        inst_blk->RemovePhi(t_phi);
      }
    }

    // add "cur_ins" to new exit-successor block and remove from old exit-successor
    old_if_exit_block->RemoveInstruction(cur_ins);
    cur_ins->SetId(-1);
    first_block->InsertInstructionBefore(cur_ins, first_block->GetLastInstruction());
  }

  DCHECK(inst_accumulator_val);
  phi_accumulator->AddInput(inst_accumulator_val);
  phi_accumulator->AddInput(last_accu_op);

  // add new instruction for updating return value to accumulator inside new pre-exit block
  HInstruction* new_instr = nullptr;
  if (trec.ret_op_ && trec.ret_op_->IsBinaryOperation() && (if_exit_val == trec.acc_param_)) {
    new_instr = GetAccInstruction(phi_accumulator, if_exit_val, trec);
    DCHECK(new_instr);
    new_loop_exit->AddInstruction(new_instr);
  }


  // add new instruction for "return accumulator" inside new pre-exit block
  // new pre-exit block which contains return, doesn't contains Goto
  HInstruction* new_ret_input = nullptr;
  if (new_instr == nullptr) {
    new_ret_input = (if_exit_val == trec.acc_param_)? trec.acc_param_: phi_accumulator;
  } else {
    new_ret_input = new_instr;
  }
  DCHECK(new_ret_input);

  HInstruction* new_ret = new (allocator) HReturn(new_ret_input);
  DCHECK(new_ret);
  new_loop_exit->AddInstruction(new_ret);

  // Set up  predecessor & successor information for new_loop_exit.
  new_loop_header->ReplaceSuccessor(old_if_exit_block, new_loop_exit);
  exit_blk->ReplacePredecessor(new_loop_back_edge, new_loop_exit);

  // Set up "new_loop_header" as dominator of "new_loop_exit".
  new_loop_exit->SetDominator(new_loop_header);
  new_loop_header->AddDominatedBlock(new_loop_exit);

  // Set up "new_loop_exit" as dominator of "exit_blk".
  exit_blk->SetDominator(new_loop_exit);
  new_loop_exit->AddDominatedBlock(exit_blk);

  // Remove "exit_blk" from dominated blocks of "new_loop_back_edge"
  if (new_loop_back_edge->Dominates(exit_blk)) {
    new_loop_back_edge->RemoveDominatedBlock(exit_blk);
  }

  // Disconnects "old_if_exit_block" from all its predecessors, successors, dominator and removes from the graph.
  old_if_exit_block->DisconnectAndDelete();

  // populate newly created loop
  loop_info->Populate();

  // clear and rebuild graph dominance info
  graph_->ClearDominanceInformation();
  graph_->ComputeDominanceInformation();


  size_t input_index = 0;
  // iterate through all acc_parama uses and replace uses with acc_param  PHI
  if (trec.acc_param_ != nullptr) {
    HInstruction* acc_par_input = nullptr;
    const HUseList<HInstruction*>& uses_acc_par = trec.acc_param_->GetUses();
    for (auto it4 = uses_acc_par.begin(), end4 = uses_acc_par.end(); it4 != end4; /* ++it4 below */) {
      HInstruction* inst = it4->GetUser();
      input_index = it4->GetIndex();
      ++it4;  // increment before replacing

      HBasicBlock* inst_blk = inst->GetBlock();
      if (inst->IsPhi() && (inst_blk == new_loop_header)) {
        continue;
      } else if (new_loop_header->Dominates(inst_blk)) {
        if ((inst->IsBinaryOperation()) && (inst->IsAdd() || inst->IsSub()
          || inst->IsMul() || inst->IsDiv()) && (inst->InputAt(0) == trec.acc_param_)) {
            acc_par_input = inst;
        }
        inst->ReplaceInput(phi_acc_param, input_index);
      }
    }
    DCHECK(acc_par_input);
    phi_acc_param->AddInput(acc_par_input);
  }

  // iterate through all invoke uses and replace uses with accumulator PHI
  for (std::vector<HInvoke*>::iterator iter = trec.recursive_invoke_instr_.begin(); iter != trec.recursive_invoke_instr_.end(); ++iter) {
    HInvoke* t_invoke = *iter;

    const HUseList<HInstruction*>& uses_invoke = t_invoke->GetUses();
    for (auto it3 = uses_invoke.begin(), end3 = uses_invoke.end(); it3 != end3; /* ++it3 below */) {
      HInstruction* inst = it3->GetUser();
      input_index = it3->GetIndex();
      ++it3;  // increment before replacing

      HBasicBlock* inst_blk = inst->GetBlock();
      if (new_loop_header->Dominates(inst_blk)) {
        inst->ReplaceInput(phi_accumulator, input_index);
      }
    }

    const HUseList<HEnvironment*>& env_uses_invoke = t_invoke->GetEnvUses();
    for (auto it_3 = env_uses_invoke.begin(), end_3 = env_uses_invoke.end(); it_3 != end_3;) {
      HEnvironment* env_inst = it_3->GetUser();
      input_index = it_3->GetIndex();
      ++it_3;  // increment before replacing

      env_inst->RemoveAsUserOfInput(input_index);
      env_inst->SetRawEnvAt(input_index, phi_accumulator);
      phi_accumulator->AddEnvUseAt(env_inst, input_index);
    }

    // remove invoke instruction (not uses)
    HBasicBlock* invoke_blk = t_invoke->GetBlock();
    invoke_blk->RemoveInstruction(t_invoke);
  }

  phi_induction->AddInput(imp_param);

  // map which contains loop induction var usage along with block ID
  std::map<HBasicBlock*, std::vector<HInstruction*>> induction_use_map;

  // iterate through all uses of imp_param and replace uses with induction PHI
  const HUseList<HInstruction*>& uses2 = imp_param->GetUses();
  for (auto it2 = uses2.begin(), end2 = uses2.end(); it2 != end2; /* ++it2 below */) {
    HInstruction* inst = it2->GetUser();
    input_index = it2->GetIndex();
    ++it2;  // increment before replacing
    if (inst->IsPhi() && (inst->GetBlock() == new_loop_header)) {
      continue;
    }

    HBasicBlock* inst_blk = inst->GetBlock();
    if (new_loop_header->Dominates(inst_blk)) {
      if ((inst->IsBinaryOperation()) && (inst->IsAdd() || inst->IsSub()
        || inst->IsMul() || inst->IsDiv())) {
        if (inst->InputAt(0) == imp_param) {
          std::map<HBasicBlock*, std::vector<HInstruction*>>::iterator iter_map;
          iter_map = induction_use_map.find(inst_blk);

          if (iter_map == induction_use_map.end()) {
            induction_use_map.insert(std::pair<HBasicBlock*, std::vector<HInstruction*>>(inst_blk, std::vector<HInstruction*>()));
          }

          induction_use_map[inst_blk].push_back(inst);
        }
      }
      inst->ReplaceInput(phi_induction, input_index);
    }
  }

  // identify second input for induction PHI
  HInstruction* last_induction_op = nullptr;
  HBasicBlock* cur_blk = new_loop_back_edge;
  while (cur_blk != new_loop_header) {
    std::map<HBasicBlock*, std::vector<HInstruction*>>::iterator iter_map;
    iter_map = induction_use_map.find(cur_blk);
    if (iter_map != induction_use_map.end()) {
      last_induction_op = iter_map->second.back();
    } else {  // if there are more than 1 predecessor, iterate over predecessors
      if (cur_blk->GetPredecessors().size() > 1) {
        std::vector<HInstruction*> ind_inst_list;
        bool induction_flag = false;
        for (HBasicBlock* pre_blk : cur_blk->GetPredecessors()) {
          iter_map = induction_use_map.find(pre_blk);
          if (iter_map != induction_use_map.end()) {
            ind_inst_list.push_back(iter_map->second.back());
            induction_flag = true;
          } else {
            ind_inst_list.push_back(nullptr);
          }
        }

        if ((induction_flag == true) && (ind_inst_list.size() > 0)) {
          // add new PHI for induction variable use values in predecessors of current block
          HPhi* phi_ind_use = new (allocator) HPhi(allocator, kNoRegNumber, 0, HPhi::ToPhiType(ret_type));
          DCHECK(phi_ind_use);
          cur_blk->AddPhi(phi_ind_use);

          for (std::vector<HInstruction*>::iterator iter2 = ind_inst_list.begin(); iter2 != ind_inst_list.end(); ++iter2) {
            HInstruction* t_inst = *iter2;
            if (t_inst != nullptr) {
              phi_ind_use->AddInput(t_inst);
            } else {
              phi_ind_use->AddInput(phi_induction);
            }
          }
          last_induction_op = phi_ind_use;
        }
      }
    }

    if (last_induction_op != nullptr) {
      break;
    }
    cur_blk = cur_blk->GetDominator();
  }

  // add operation to "phi_induction"
  if (last_induction_op != nullptr) {
    phi_induction->AddInput(last_induction_op);
  }

  return true;
}

// API to check and perform tail-recursion elimination optimization
bool TailRecursionElimination::Run() {
  TREContext trec;
  // checking exit block
  HBasicBlock* exit = graph_->GetExitBlock();
  // In Android-master, the return statement is inserted, where it is used in program
  // Hence, there will be multiple predecessors of exit block
  if (exit == nullptr) {
    return false;
  }

  // not handling try catch
  if (graph_->HasTryCatch()) {
    return false;
  }

  // We currently don't perform TRE when the graph is debuggable
  if (graph_->IsDebuggable()) {
    return false;
  }

  // As of now, handling simple tail-recursive functions
  if (graph_->HasLoops()) {
    return false;
  }

  // don't handle non-recursive methods
  if (!(graph_->IsMethodRecursive())) {
    return false;
  }

  // get current method name
  uint32_t method_index = graph_->GetMethodIdx();
  std::string method_str = graph_->GetDexFile().PrettyMethod(method_index);
  const char* cur_method_name = method_str.c_str();
  if ((cur_method_name == NULL) || strlen(cur_method_name) == 0) {
    return false;
  }

  // get the first block of graph and it's successor
  HBasicBlock* first_block = graph_->GetEntryBlock();
  DCHECK(first_block);

  HBasicBlock* first_successor = first_block->GetSingleSuccessor();
  DCHECK(first_successor);
  if (!(first_successor->GetLastInstruction()->IsIf())) {
    return false;
  }

  // checking whether return statement contain self-call
  bool tre_success_flag = false;
  for (HBasicBlock* exit_predecessor : exit->GetPredecessors()) {
    DCHECK(exit_predecessor);
    HInstruction* last = exit_predecessor->GetLastInstruction();

    // checking return statement inputs recursively to identify recursive call
    if (last->IsReturn() && !last->IsReturnVoid()) {
      HInstruction* ret = last->InputAt(0);
      if (CheckTailRecursive(ret, cur_method_name, trec)) {
        if (TransformMethodGraph(exit, first_block, trec)) {
          tre_success_flag = true;
        }
        break;
      } else if (IdentifyTailRecursion(ret, cur_method_name, trec)) {
        if (TransformMethodGraph(exit, first_block, trec)) {
          tre_success_flag = true;
        }
        break;
      }
    }
  }

  if (!tre_success_flag) {
    LOG(INFO) << "TRE.Run():: exit block predecessor doesn't have tail-recursive call; method is " << method_str << "\n";
    return false;
  }

  return true;
}

}  // namespace art
