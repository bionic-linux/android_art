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

#ifndef ART_SIMULATOR_INCLUDE_CODE_SIMULATOR_H_
#define ART_SIMULATOR_INCLUDE_CODE_SIMULATOR_H_

#include "arch/instruction_set.h"
#include "runtime.h"
#include <signal.h>

namespace art {

class ArtMethod;
union JValue;
class Thread;

//
// Classes in this file model simulator executors - a per thread entities with their own contexts:
// simulated stack, registers, etc.
//

// This class implements a basic simulator executor which is capable of executing sequences
// of simulated ISA instructions. It is not aware of ART runtime so it can manage only trivial
// ART methods.
//
// Currently only used by code generator tests.
class BasicCodeSimulator {
 public:
  BasicCodeSimulator() {}
  virtual ~BasicCodeSimulator() {}

  // Starts simulating instructions of a target isa from a buffer.
  virtual void RunFrom(intptr_t code_buffer) = 0;

  // Get return value according to C ABI.
  virtual bool GetCReturnBool() const = 0;
  virtual int32_t GetCReturnInt32() const = 0;
  virtual int64_t GetCReturnInt64() const = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(BasicCodeSimulator);
};

// Returns a null pointer if a simulator cannot be found for target_isa.
extern "C" BasicCodeSimulator* CreateBasicCodeSimulator(InstructionSet target_isa,
                                                        size_t stack_size);

#ifdef ART_USE_SIMULATOR

// This class implements a ART runtime aware simulator executor which can execute all* quick ABI
// code: is aware of ART entrypoints, ABI/ISA transitions, etc.
class CodeSimulator {
 public:
  CodeSimulator() : stack_end_(nullptr), stack_begin_(nullptr), stack_size_(0) {}
  virtual ~CodeSimulator() {}

  // Invokes (starts to simulate) a method; this should follow the semantics of
  // art_quick_invoke_stub.
  virtual void Invoke(ArtMethod* method,
                      uint32_t* args,
                      uint32_t args_size,
                      Thread* self,
                      JValue* result,
                      const char* shorty,
                      bool isStatic) REQUIRES_SHARED(Locks::mutator_lock_) = 0;

  // Get the method, return program counter and stack pointer from a signal that occured during
  // simulation. Returns false if the signal did not occur during simulation, true otherwise.
  virtual void TryToGetMethodAndReturnPcAndSp(siginfo_t* siginfo,
                                              void* context,
                                              ArtMethod** out_method,
                                              uintptr_t* out_return_pc,
                                              uintptr_t* out_sp,
                                              bool* out_is_stack_overflow) = 0;
  // Try to handle an implicit check that occured during simulation.
  virtual bool HandleNullPointer(int sig, siginfo_t* siginfo, void* context) = 0;

  uint8_t* GetStackEnd() const { return stack_end_; }
  uint8_t* GetStackBegin() const  { return stack_begin_; }
  size_t GetStackSize() const { return stack_size_; }

 protected:
  void SetStackBegin(uint8_t* stack_begin) { stack_begin_ = stack_begin; }
  void SetStackEnd(uint8_t* stack_end) { stack_end_ = stack_end; }
  void SetStackSize(size_t stack_size) { stack_size_ = stack_size; }

 private:
  // The fields below are Simulator's versions of stack_begin, stack_end and stack_size
  // (see Thread::tlsPtr_.* and the stack diagram near class Thread.)
  uint8_t* stack_end_;
  uint8_t* stack_begin_;
  size_t stack_size_;

  DISALLOW_COPY_AND_ASSIGN(CodeSimulator);
};

extern "C" CodeSimulator* CreateCodeSimulator(InstructionSet target_isa, size_t stack_size);

#endif

}  // namespace art

#endif  // ART_SIMULATOR_INCLUDE_CODE_SIMULATOR_H_
