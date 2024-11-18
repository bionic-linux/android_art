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

#ifndef ART_COMPILER_OPTIMIZING_OPTIMIZATION_H_
#define ART_COMPILER_OPTIMIZING_OPTIMIZATION_H_

#include <ostream>

#include "base/arena_object.h"
#include "base/macros.h"
#include "nodes.h"
#include "optimizing_compiler_stats.h"

namespace art HIDDEN {

class CodeGenerator;
class DexCompilationUnit;

/**
 * Abstraction to implement an optimization pass.
 */
class HOptimization : public ArenaObject<kArenaAllocOptimization> {
 public:
  HOptimization(HGraph* graph,
                const char* pass_name,
                OptimizingCompilerStats* stats = nullptr,
                std::ostream* diagnostic_output = nullptr)
      : graph_(graph),
        stats_(stats),
        diagnostic_output_(diagnostic_output),
        pass_name_(pass_name) {}

  virtual ~HOptimization() {}

  // Return the name of the pass. Pass names for a single HOptimization should be of form
  // <optimization_name> or <optimization_name>$<pass_name> for common <optimization_name> prefix.
  // Example: 'instruction_simplifier', 'instruction_simplifier$before_codegen',
  // 'instruction_simplifier$before_codegen'.
  const char* GetPassName() const { return pass_name_; }

  // Perform the pass or analysis. Returns false if no optimizations occurred or no useful
  // information was computed (this is best effort, returning true is always ok).
  virtual bool Run() = 0;

 protected:
  bool IsDiagnosticsEnabled() const { return diagnostic_output_ != nullptr; }

  std::ostream& GetDiagnosticsOutput() const {
    DCHECK(IsDiagnosticsEnabled());
    return *diagnostic_output_;
  }

  // Emit a diagnostic report if diagnostics are enabled.
  // Report should be either:
  // - A callable object that accepts a reference to an ostream.
  //   Can be used to evaluate diagnostic message lazily.
  // - An object that can be written to an ostream using << operator.
  template <typename Report>
  void MaybeReportDiagnostic(HInstruction* instruction, Report&& report) const {
    if (!IsDiagnosticsEnabled()) {
      return;
    }
    ReportDiagnostic(
        instruction,
        GetDiagnosticsOutput(),
        [this](HInstruction* instruction, std::ostream& output) {
          WriteDiagnosticPrefix(instruction, output);
        },
        std::forward<Report>(report));
  }

  // Write prefix for a diagnostic message in the following format:
  //   <source>:<line number>: note: <pass name>:
  // or if information about source location is not available:
  //   <method name>:<bytecode offset>: note: <pass name>:
  void WriteDiagnosticPrefix(HInstruction* instruction, std::ostream& output) const;

  // Emit a diagnostic report if diagnostics are enabled.
  // - PrefixWriter should be a callable that accepts pointer to an instruction
  //   and a reference to an ostream.
  // - Report should be either:
  //   - A callable object that accepts a reference to an ostream.
  //     Can be used to evaluate diagnostic message lazily.
  //   - An object that can be written to an ostream using << operator.
  template <typename Report, typename PrefixWriter>
  static void ReportDiagnostic(HInstruction* instruction,
                               std::ostream& output,
                               PrefixWriter&& prefix_writer,
                               Report&& report) {
    std::stringstream ss;
    prefix_writer(instruction, ss);
    if constexpr (std::is_invocable_v<Report, std::ostream&>) {
      report(ss);
    } else {
      ss << report;
    }
    output << ss.str() << std::endl;
  }

  HGraph* const graph_;
  // Used to record stats about the optimization.
  OptimizingCompilerStats* const stats_;

 private:
  // Output stream for diagnostic messages.
  std::ostream* diagnostic_output_;

  // Optimization pass name.
  const char* pass_name_;

  DISALLOW_COPY_AND_ASSIGN(HOptimization);
};

// Optimization passes that can be constructed by the helper method below. An enum
// field is preferred over a string lookup at places where performance matters.
// TODO: generate this table and lookup methods below automatically?
enum class OptimizationPass {
  kAggressiveInstructionSimplifier,
  kBoundsCheckElimination,
  kCHAGuardOptimization,
  kCodeSinking,
  kConstantFolding,
  kConstructorFenceRedundancyElimination,
  kDeadCodeElimination,
  kGlobalValueNumbering,
  kInductionVarAnalysis,
  kInliner,
  kInstructionSimplifier,
  kInvariantCodeMotion,
  kLoadStoreElimination,
  kLoopOptimization,
  kReferenceTypePropagation,
  kScheduling,
  kSelectGenerator,
  kSideEffectsAnalysis,
  kWriteBarrierElimination,
#ifdef ART_ENABLE_CODEGEN_arm
  kInstructionSimplifierArm,
  kCriticalNativeAbiFixupArm,
#endif
#ifdef ART_ENABLE_CODEGEN_arm64
  kInstructionSimplifierArm64,
#endif
#ifdef ART_ENABLE_CODEGEN_riscv64
  kCriticalNativeAbiFixupRiscv64,
  kInstructionSimplifierRiscv64,
#endif
#ifdef ART_ENABLE_CODEGEN_x86
  kPcRelativeFixupsX86,
  kInstructionSimplifierX86,
#endif
#ifdef ART_ENABLE_CODEGEN_x86_64
  kInstructionSimplifierX86_64,
#endif
#if defined(ART_ENABLE_CODEGEN_x86) || defined(ART_ENABLE_CODEGEN_x86_64)
  kX86MemoryOperandGeneration,
#endif
  kNone,
  kLast = kNone
};

// Lookup name of optimization pass.
const char* OptimizationPassName(OptimizationPass pass);

// Lookup optimization pass by name.
OptimizationPass OptimizationPassByName(const std::string& pass_name);

// Optimization definition consisting of an optimization pass
// an optional alternative name (nullptr denotes default), and
// an optional pass dependence (kNone denotes no dependence).
struct OptimizationDef {
  OptimizationDef(OptimizationPass p, const char* pn, OptimizationPass d)
      : pass(p), pass_name(pn), depends_on(d) {}
  OptimizationPass pass;
  const char* pass_name;
  OptimizationPass depends_on;
};

// Helper method for optimization definition array entries.
inline OptimizationDef OptDef(OptimizationPass pass,
                              const char* pass_name = nullptr,
                              OptimizationPass depends_on = OptimizationPass::kNone) {
  return OptimizationDef(pass, pass_name, depends_on);
}

// Helper method to construct series of optimization passes.
// The array should consist of the requested optimizations
// and optional alternative names for repeated passes.
// Example:
//    { OptPass(kConstantFolding),
//      OptPass(Inliner),
//      OptPass(kConstantFolding, "constant_folding$after_inlining")
//    }
ArenaVector<HOptimization*> ConstructOptimizations(
    const OptimizationDef definitions[],
    size_t length,
    ArenaAllocator* allocator,
    HGraph* graph,
    OptimizingCompilerStats* stats,
    CodeGenerator* codegen,
    const DexCompilationUnit& dex_compilation_unit);

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_OPTIMIZATION_H_
