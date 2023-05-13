%def header():
/*
 * Copyright (C) 2023 The Android Open Source Project
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

/*
 * This is a #include, not a %include, because we want the C pre-processor
 * to expand the macros into assembler assignment statements.
 */
#include "asm_support.h"
#include "arch/riscv64/asm_support_riscv64.S"

/**
 * RISC-V 64 ABI general notes
 *
 * References
 * - https://github.com/riscv-non-isa/riscv-elf-psabi-doc/blob/master/riscv-cc.adoc
 * - runtime/arch/riscv64/registers_riscv64.h
 *
 * 32 general purposes registers
 * - fixed purpose: zero, ra, sp, gp, tp, s1
 *     gp/scs: shadow call stack - do not clobber!
 *     s1/tr: ART thread register - do not clobber!
 * - temporaries: t0-t6
 * - arguments: a0-a7
 * - callee saved: ra, s0/fp, s2-s11
 *     s0 is flexible, available to use as a frame pointer if needed.
 *
 * 32 floating point registers
 * - temporaries: ft0-ft11
 * - arguments: fa0-fa7
 * - callee saved: fs0-fs11
 */

// Android references
//   Bytecodes: https://source.android.com/docs/core/runtime/dalvik-bytecode
//   Instruction formats: https://source.android.com/docs/core/runtime/instruction-formats

// Fixed register usages in Nterp.
//    nickname  ABI    reg   purpose
#define xSELF    s1  // x9,   Thread* self pointer
#define xFP      s2  // x18,  interpreted frame pointer: to access locals and args
#define xPC      s3  // x19,  interpreted program counter: to fetch instructions
#define xINST    s4  // x20,  first 16-bit code unit of current instruction
#define xIBASE   s5  // x21,  interpreted instruction base pointer: for computed goto
#define xREFS    s6  // x22,  base of object references of dex registers

#define CFI_TMP  10  // DWARF register number for       a0/x10
#define CFI_DEX  19  // DWARF register number for xPC  /s3/x19
#define CFI_REFS 22  // DWARF register number for xREFS/s6/x22

// An assembly entry that has a OatQuickMethodHeader prefix.
.macro OAT_ENTRY name, end
    .type \name, @function
    .hidden \name
    .global \name
    .balign 16
    // Padding of 3 * 4 bytes to get 16 bytes alignment of code entry.
    .4byte 0, 0, 0
    // OatQuickMethodHeader `data_` field. Note that the top two bits must be clear.
    .4byte (\end - \name)
\name:
.endm

.macro SIZE name
    .size \name, .-\name
.endm

// Similar to ENTRY but without the CFI directives.
.macro NAME_START name
    .type \name, @function
    .hidden \name  // Hide this as a global symbol, so we do not incur plt calls.
    .global \name
    /* Cache alignment for function entry */
    .balign 16
\name:
.endm

.macro NAME_END name
  SIZE \name
.endm

// Macro for defining entrypoints into runtime. We don't need to save registers (we're not holding
// references there), but there is no kDontSave runtime method. So just use the kSaveRefsOnly
// runtime method.
.macro NTERP_TRAMPOLINE name, helper
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME
    call \helper
    RESTORE_SAVE_REFS_ONLY_FRAME
    ld t0, THREAD_EXCEPTION_OFFSET(xSELF)
    bnez t0, nterp_deliver_pending_exception
    ret
END \name
.endm

// Unpack code items from dex format.
// Input:
//   - \code_item: CodeItem*
// Output:
//   - \registers: register count
//   - \outs: out count
//   - \ins: in count. If set to register "zero" (x0), load is skipped.
//   - \code_item: holds instruction array on exit
// Clobbers: t0
.macro FETCH_CODE_ITEM_INFO code_item, registers, outs, ins
    // Check LSB of \code_item. If 1, it's a compact dex file.
    andi t0, \code_item, 0x1
    beqz t0, 1f  // Regular dex.
    unimp  // Compact dex: unimplemented.
1:
    // Unpack values from regular dex format.
    lhu \registers, CODE_ITEM_REGISTERS_SIZE_OFFSET(\code_item)
    lhu \outs, CODE_ITEM_OUTS_SIZE_OFFSET(\code_item)
    .ifnes "\ins", "zero"
    lhu \ins, CODE_ITEM_INS_SIZE_OFFSET(\code_item)
    .endif
    addi \code_item, \code_item, CODE_ITEM_INSNS_OFFSET
.endm

// Set up the stack to start executing the method.
// See runtime/nterp_helpers.cc for a diagram of the setup.
// Input:
//   - a0: ArtMethod*
//   - sp
//   - \code_item: CodeItem*
//   - \cfi_refs: DWARF register number for \refs
// Output:
//   - \code_item: pointer to instruction array `insns_*` on exit
//   - \refs: pointer to obj reference array
//   - \fp: pointer to dex register array
//   - \regs: count of dex registers
//   - \ins: count of in-registers. If set to register "zero" (x0), load is skipped.
//   - \old_sp: old stack pointer
//   - sp modified
//
// Clobbers: t0, t1
.macro SETUP_STACK_FRAME code_item, cfi_refs, refs, fp, regs, ins, old_sp
    FETCH_CODE_ITEM_INFO \code_item, \regs, /*out count*/ t1, \ins

    // Compute required frame size: ((2 * \regs) + t1) * 4 + 24
    // - The register array and reference array are each \regs in length.
    // - The out array is t1 in length.
    // - Each register is 4 bytes.
    // - Additional 24 bytes for 3 fields: saved frame pointer, dex pc, and ArtMethod*.
    sll t0, \regs, 1
    add t0, t0, t1
    sll t0, t0, 2
    add t0, t0, 24

    // Compute new stack pointer in t0.
    sub t0, sp, t0
    // 16-byte alignment.
    andi t0, t0, ~0xF

    // Set \refs to base of reference array. Align to pointer size for the frame pointer and dex pc
    // pointer, below the reference array.
    sll t1, t1, 2  // 4 bytes per entry.
    add t1, t0, t1
    add t1, t1, 28  // 24 bytes from 3 fields mentioned earlier, plus 4 for alignment slack.
    andi \refs, t1, -__SIZEOF_POINTER__

    // Set \fp to base of register array, above the reference array. This region is already aligned.
    sll t1, \regs, 2
    add \fp, \refs, t1

    // Set up the stack pointer.
    mv \old_sp, sp
    .cfi_def_cfa_register \old_sp
    mv sp, t0
    sd \old_sp, -8(\refs)
    CFI_DEF_CFA_BREG_PLUS_UCONST \cfi_refs, -8, FRAME_SIZE_SAVE_ALL_CALLEE_SAVES

    // Put nulls in reference array.
    beqz \regs, 2f
    mv t1, \refs  // t1 as iterator
1:
    // Write in 8-byte increments, so vreg(0) gets zero'ed too, if \regs is odd.
    sd zero, (t1)
    addi t1, t1, 8
    bltu t1, \fp, 1b
2:
    // Save the ArtMethod*.
    sd a0, (sp)
.endm

// Set up the stack after doing an nterp-to-nterp call.
// Input
//   - a0: ArtMethod*
//   - sp
//   - \code_item: CodeItem*
//   - \cfi_new_refs: DWARF register number for \new_refs
// Output
//   - \new_refs: callee xREFS
//   - \new_fp: callee xFP
//   - \regs: vreg count in callee's xFP array
//   - xPC
// Clobbers: t0, t1, t2, \code_item
.macro SETUP_STACK_FOR_INVOKE code_item, cfi_new_refs, new_refs, new_fp, regs
    // Check guard page for stack overflow.
    li t0, -STACK_OVERFLOW_RESERVED_BYTES
    add t0, t0, sp
    ld zero, (t0)

    INCREASE_FRAME FRAME_SIZE_SAVE_ALL_CALLEE_SAVES
    SAVE_ALL_CALLEE_SAVES

    SETUP_STACK_FRAME \
        \code_item, \cfi_new_refs, \new_refs, \new_fp, \regs, /*ins*/zero, /*old_sp*/t2

    mv xPC, \code_item
    CFI_DEFINE_DEX_PC_WITH_OFFSET(/*tmpReg*/CFI_TMP, /*dexReg*/CFI_DEX, /*dexOffset*/0)
.endm

// Input
//   - xINST: code unit #0 from caller's old xPC, A|G|op.
//   - xPC: new dex pc
//   - a0: ArtMethod*
//   - a1: instance receiver
//   - a2: code unit #2 from invoke opcode, F|E|D|C.
// Clobbers: t0, t1, t2, t3, t4
.macro SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE new_refs, new_fp, regs, is_static=0
    // op vA, vB, {vC, vD, vE, vF, vG}

    // Byte A in xINST encodes arg count, 0-5.
    srliw t0, xINST, 12  // t0 := A
    beqz t0, 0f  // Skip arg processing.

    // Set up end-of-array offset before arg-store jumps.
    add t1, \regs, -1  // regs-1 is the last slot in the fp/regs array
    slliw t1, t1, 2  // t1 := reverse iterator; initially byte offset to last slot

    addi t0, t0, -2
    bltz t0, 1f
    beqz t0, 2f
    addi t0, t0, -2
    bltz t0, 3f
    beqz t0, 4f
    // t0 > 0: fall through to 5f
5:  // G
    slliw t0, xINST, 8
    andi t0, t0, 0xF  // t0 := G's vreg id
    jal t2, 6f
4:  // F
    srliw t0, a2, 12  // t0 := F's vreg id
    jal t2, 6f
3:  // E
    srliw t0, a2, 8
    andi t0, t0, 0xF  // t0 := E's vreg id
    jal t2, 6f
2:  // D
    srliw t0, a2, 4
    andi t0, t0, 0xF  // t0 := D's vreg id
    jal t2, 6f
1:  // C
    .if \is_static
    andi t0, a2, 0xF  // t0 := C's vreg id
    jal t2, 6f
    .else
    // C's load was performed earlier and is held in a1.
    add t4, \new_refs, t1
    sw a1, (t4)
    add t4, \new_fp, t1
    sw a1, (t4)
    .endif
0:
    mv xREFS, \new_refs
    mv xFP, \new_fp
    CFI_DEF_CFA_BREG_PLUS_UCONST CFI_REFS, -8, FRAME_SIZE_SAVE_ALL_CALLEE_SAVES
    START_EXECUTING_INSTRUCTIONS
    unimp  // NOTE: no fallthrough

6: // loop body; assumes t0 (vreg id), t1 (offset), t2 (return) set properly
    GET_VREG_OBJECT t3, t0
    add t4, \new_refs, t1
    sw t3, (t4)
    GET_VREG t3, t0
    add t4, \new_fp, t1
    sw t3, (t4)
    addi t1, t1, -4

    jalr zero, t2, 0  // continue with args
.endm

.macro EXPORT_PC
    sd xPC, -16(xREFS)
.endm

.macro TEST_IF_MARKING reg, label
    lb \reg, THREAD_IS_GC_MARKING_OFFSET(xSELF)
    bnez \reg, \label
.endm

.macro DO_SUSPEND_CHECK continue
    lw t0, THREAD_FLAGS_OFFSET(xSELF)
    andi t0, t0, THREAD_SUSPEND_OR_CHECKPOINT_REQUEST
    beqz t0, \continue
    EXPORT_PC
    call art_quick_test_suspend
.endm

// Fetch the next instruction, from xPC into xINST.
// Does not advance xPC.
.macro FETCH_INST
    lhu xINST, (xPC)  // zero in upper 48 bits
.endm

// Fetch the next instruction, from xPC into xINST. Advance xPC by \count units, each 2 bytes.
//
// Immediates have a 12-bit offset range from xPC. Thus, \count can range from -1024 to 1023.
//
// Note: Must be placed AFTER anything that can throw an exception, or the exception catch may miss.
// Thus, this macro must be placed after EXPORT_PC.
.macro FETCH_ADVANCE_INST count
    lhu xINST, (\count*2)(xPC)  // zero in upper 48 bits
    addi xPC, xPC, (\count*2)
.endm

// Fetch a half-word unit from an offset past the current PC.
// The offset is specified in \count units, each 2 bytes.
// Does not advance xPC.
.macro FETCH reg, count
    lhu \reg, (\count*2)(xPC)
.endm

// Uses: \reg
.macro GET_INST_OPCODE reg
    and \reg, xINST, 0xFF
.endm

// Clobbers: \reg
.macro GOTO_OPCODE reg
    slliw \reg, \reg, ${handler_size_bits}
    add \reg, xIBASE, \reg
    jr \reg
.endm

// Clobbers: t0, t1
.macro FETCH_FROM_THREAD_CACHE reg, miss_label
    li t0, THREAD_INTERPRETER_CACHE_OFFSET  // imm > 12 bit signed
    add t0, t0, xSELF  // cache address
    // See art::InterpreterCache::IndexOf() for computing index of key within cache array.
    // The roundabout computation accounts for different cache sizes.
    // Step 1. Move index (LOG2 bits), starting from LSB #2, to MSB.
    slliw t1, xPC, 32 - 2 - THREAD_INTERPRETER_CACHE_SIZE_LOG2
    // Step 2. Move index to LSB, then multiply by 16: entry's byte offset within cache.
    srliw t1, t1, 32 - 4 - THREAD_INTERPRETER_CACHE_SIZE_LOG2
    // Step 3. Remove stray bits below the byte offset.
    andi t1, t1, ~0xF
    add t0, t0, t1  // t0: entry's address
    ld t1, (t0)     // key: dex PC
    ld \reg, 8(t0)  // value: depends on context; see call site

    bne xPC, t1, \miss_label
.endm

// Inputs:
//   - a0
//   - xSELF
// Clobbers: t0
.macro CHECK_AND_UPDATE_SHARED_MEMORY_METHOD if_hot, if_not_hot
    lw t0, ART_METHOD_ACCESS_FLAGS_OFFSET(a0)
    // Send flag bit to MSB, branch if bit is unset.
    sll t0, t0, 63 - ART_METHOD_IS_MEMORY_SHARED_FLAG_BIT
    bgez t0, \if_hot

    lw t0, THREAD_SHARED_METHOD_HOTNESS_OFFSET(xSELF)
    beqz t0, \if_hot

    addi t0, t0, -1  // Reduce hotness
    sw t0,  THREAD_SHARED_METHOD_HOTNESS_OFFSET(xSELF)
    j \if_not_hot
.endm

// Increase method hotness before starting the method.
// Clobbers: a0, t0
.macro START_EXECUTING_INSTRUCTIONS
    ld a0, (sp)
    lhu t0, ART_METHOD_HOTNESS_COUNT_OFFSET(a0)
#if (NTERP_HOTNESS_VALUE != 0)
#error Expected 0 for hotness value
#endif
    // If the counter is at zero (hot), handle it in the runtime.
    beqz t0, 3f
    addi t0, t0, -1
    sh t0, ART_METHOD_HOTNESS_COUNT_OFFSET(a0)
1:
    DO_SUSPEND_CHECK continue=2f
2:
    FETCH_INST
    GET_INST_OPCODE t0
    GOTO_OPCODE t0
3:
    CHECK_AND_UPDATE_SHARED_MEMORY_METHOD if_hot=4f, if_not_hot=1b
4:
    mv a1, zero  // dex_pc_ptr=nullptr
    mv a2, zero  // vergs=nullptr
    call nterp_hot_method
    j 2b
.endm

// Clobbers: \reg
// Safe if \reg == \vreg.
.macro GET_VREG reg, vreg
    slliw \vreg, \vreg, 2  // vreg id to byte offset
    add \vreg, xFP, \vreg  // vreg address inside register array
    lw \reg, (\vreg)  // load value from vreg into \reg
.endm

// Clobbers: t0, \vreg
.macro SET_VREG reg, vreg
    slliw \vreg, \vreg, 2  // vreg id to byte offset
    add t0, xFP, \vreg  // vreg address inside register array
    sw \reg, (t0)  // store value in vreg
    add t0, xREFS, \vreg  // vreg address inside reference array
    sw zero, (t0)  // not an object, null out reference
.endm

// Clobbers: \reg
// Safe if \reg == \vreg.
.macro GET_VREG_OBJECT reg, vreg
    slliw \vreg, \vreg, 2  // vreg id to byte offset
    add \vreg, xREFS, \vreg  // vreg address inside reference array
    lw \reg, (\vreg)  // load value from vreg into \reg
.endm

// Clobbers: t0, \vreg
.macro SET_VREG_OBJECT reg, vreg
    slliw \vreg, \vreg, 2  // vreg id to byte offset
    add t0, xFP, \vreg  // vreg address inside register array
    sw \reg, (t0)
    add t0, xREFS, \vreg
    sw \reg, (t0)  // also in reference array
.endm

// Input
//   - a0: ArtMethod*
// Clobbers: t0, t1
.macro DO_ENTRY_POINT_CHECK call_compiled_code
    la t0, ExecuteNterpImpl
    ld t1, ART_METHOD_QUICK_CODE_OFFSET_64(a0)
    bne t0, t1, \call_compiled_code
.endm

// Input
//   - a0: ArtMethod*
.macro GET_CODE_ITEM reg
    ld \reg, ART_METHOD_DATA_OFFSET_64(a0)
.endm

// Clobbers: t0, t1
.macro COMMON_INVOKE_NON_RANGE is_string_init, suffix
    DO_ENTRY_POINT_CHECK .Lcall_compiled_code_\suffix
    GET_CODE_ITEM s7 // XXX make this a macro arg?
    call nterp_to_nterp_instance_non_range
    j .Ldone_return_\suffix

.Lcall_compiled_code_\suffix:
    unimp

.Ldone_return_\suffix:
    // Resume execution of caller.
    FETCH_ADVANCE_INST 3
    GET_INST_OPCODE t0
    GOTO_OPCODE t0
.endm

.macro COMMON_INVOKE_RANGE is_string_init, suffix
    unimp
.endm

%def entry():
/*
 * ArtMethod entry point.
 *
 * On entry:
 *  a0     ArtMethod* callee
 *  a1-a7  method parameters
 */

OAT_ENTRY ExecuteNterpWithClinitImpl, EndExecuteNterpWithClinitImpl
    unimp
EndExecuteNterpWithClinitImpl:

OAT_ENTRY ExecuteNterpImpl, EndExecuteNterpImpl
    .cfi_startproc

    // Check guard page for stack overflow.
    li t0, -STACK_OVERFLOW_RESERVED_BYTES
    add t0, t0, sp
    ld zero, (t0)

    INCREASE_FRAME FRAME_SIZE_SAVE_ALL_CALLEE_SAVES
    SAVE_ALL_CALLEE_SAVES

    ld xPC, ART_METHOD_DATA_OFFSET_64(a0)
    SETUP_STACK_FRAME xPC, CFI_REFS, xREFS, xFP, /*reg count*/s7, /*in count*/s8, /*old sp*/s9

    beqz s8, .Lsetup_execution  // no args
    unimp

.Lsetup_execution:
    CFI_DEFINE_DEX_PC_WITH_OFFSET(/*tmpReg*/CFI_TMP, /*dexReg*/CFI_DEX, /*dexOffset*/0)

    la xIBASE, artNterpAsmInstructionStart
    START_EXECUTING_INSTRUCTIONS
    // NOTE: no fallthrough
    // cfi info continues, and covers the whole nterp implementation.
    SIZE ExecuteNterpImpl

%def footer():
/*
 * ===========================================================================
 *  Common subroutines and data
 * ===========================================================================
 */

    .text
    .align  2


// Enclose all code below in a symbol (which gets printed in backtraces).
NAME_START nterp_helper

common_errNullObject:
    EXPORT_PC
    call art_quick_throw_null_pointer_exception

NterpCommonInvokeInstance:
    COMMON_INVOKE_NON_RANGE suffix="invokeInstance"

NterpHandleStringInit:
    COMMON_INVOKE_NON_RANGE is_string_init=1, suffix="stringInit"

NterpHandleStringInitRange:
    COMMON_INVOKE_RANGE is_string_init=1, suffix="stringInit"

// This is the logical end of ExecuteNterpImpl, where the frame info applies.
.cfi_endproc

// Input:
//   - a0: ArtMethod*
//   - a1: instance receiver ("this" obj ptr)
//   - a2: code unit #2 (F|E|D|C) from invoke opcode
//   - s7: CodeItem*
// Clobbers: t0, t1, t2, s7
nterp_to_nterp_instance_non_range:
    .cfi_startproc
    SETUP_STACK_FOR_INVOKE \
      /*code item*/s7, /*CFI for s8*/24, /*new refs=x24*/s8, /*new fp*/s9, /*regs*/ s10
    SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE s8, s9, s10
    .cfi_endproc

nterp_to_nterp_instance_range:
    unimp
nterp_to_nterp_static_non_range:
    unimp
nterp_to_nterp_static_range:
    unimp
nterp_to_nterp_string_init_non_range:
    unimp
nterp_to_nterp_string_init_range:
    unimp
NAME_END nterp_helper

// EndExecuteNterpImpl includes the methods after .cfi_endproc, as we want the runtime to see them
// as part of the Nterp PCs. This label marks the end of PCs contained by the OatQuickMethodHeader
// created for the interpreter entry point.
    .type EndExecuteNterpImpl, @function
    .hidden EndExecuteNterpImpl
    .global EndExecuteNterpImpl
EndExecuteNterpImpl:

// Entrypoints into runtime.
NTERP_TRAMPOLINE nterp_allocate_object, NterpAllocateObject
NTERP_TRAMPOLINE nterp_get_class, NterpGetClass
NTERP_TRAMPOLINE nterp_get_method, NterpGetMethod
NTERP_TRAMPOLINE nterp_get_static_field, NterpGetStaticField
NTERP_TRAMPOLINE nterp_hot_method, NterpHotMethod

ENTRY nterp_deliver_pending_exception
    DELIVER_PENDING_EXCEPTION
END nterp_deliver_pending_exception

// gen_mterp.py will inline the following definitions
// within [ExecuteNterpImpl, EndExecuteNterpImpl).
%def instruction_start():
    .type artNterpAsmInstructionStart, @function
    .hidden artNterpAsmInstructionStart
    .global artNterpAsmInstructionStart
artNterpAsmInstructionStart = .L_op_nop
    .text

%def instruction_end():
    .type artNterpAsmInstructionEnd, @function
    .hidden artNterpAsmInstructionEnd
    .global artNterpAsmInstructionEnd
artNterpAsmInstructionEnd:
    unimp

%def opcode_pre():
%   pass
%def opcode_name_prefix():
%   return "nterp_"
%def opcode_start():
    NAME_START nterp_${opcode}
%def opcode_end():
    NAME_END nterp_${opcode}
%def opcode_slow_path_start(name):
    NAME_START ${name}
%def opcode_slow_path_end(name):
    NAME_END ${name}
