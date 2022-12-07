/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef ART_RUNTIME_ARCH_X86_64_ASM_SUPPORT_RISCV64_S_
#define ART_RUNTIME_ARCH_X86_64_ASM_SUPPORT_RISCV64_S_

#include "asm_support_riscv64.h"
#include "interpreter/cfi_asm_support.h"

// Define special registers.

// Register holding Thread::Current().
#define xSELF x9


// The spec is not clear whether the CFA is part of the saved state and tools
// differ in the behaviour, so explicitly set the CFA to avoid any ambiguity.
// The restored CFA state should match the CFA state during CFI_REMEMBER_STATE.
.macro CFI_RESTORE_STATE_AND_DEF_CFA reg, offset
    .cfi_restore_state
    .cfi_def_cfa \reg, \offset
.endm


.macro ENTRY name
    .globl \name
    .byte 0xFF, 0xFF, 0xFF, 0xFF
    .balign 16, 0xFF
\name:
    .cfi_startproc
.endm

.macro END name
    .cfi_endproc
.endm

.macro UNDEFINED name
    ENTRY \name
        unimp
    END \name
.endm

.macro CFI_EXPRESSION_BREG n, b, offset
    .if (-0x40 <= (\offset)) && ((\offset) < 0x40)
        CFI_EXPRESSION_BREG_1(\n, \b, \offset)
    .elseif (-0x2000 <= (\offset)) && ((\offset) < 0x2000)
        CFI_EXPRESSION_BREG_2(\n, \b, \offset)
    .else
        .error "Unsupported offset"
    .endif
.endm

.macro INCREASE_FRAME frame_adjustment
    addi sp, sp, -\frame_adjustment
    .cfi_adjust_cfa_offset (\frame_adjustment)
.endm

.macro DECREASE_FRAME frame_adjustment
    addi sp, sp, \frame_adjustment
    .cfi_adjust_cfa_offset -(\frame_adjustment)
.endm

.macro SAVE_REG_BASE base, reg, offset
    sd \reg, (\offset)(\base)
    .cfi_rel_offset \reg, (\offset)
.endm

.macro SAVE_REG reg, offset
    SAVE_REG_BASE sp, \reg, \offset
.endm

.macro RESTORE_REG_BASE base, reg, offset
    ld \reg, (\offset)(\base)
    .cfi_restore \reg
.endm

.macro RESTORE_REG reg, offset
    RESTORE_REG_BASE sp, \reg, \offset
.endm

.macro LOAD_RUNTIME_INSTANCE reg
    la \reg, _ZN3art7Runtime9instance_E
    ld \reg, 0(\reg)
.endm

    /*
     * Macro that calls through to artDeliverPendingExceptionFromCode, where the pending
     * exception is Thread::Current()->exception_ when the runtime method frame is ready.
     */
.macro DELIVER_PENDING_EXCEPTION_FRAME_READY
    mv a0, xSELF

    // Point of no return.
    call artDeliverPendingExceptionFromCode  // artDeliverPendingExceptionFromCode(Thread*)
    unimp  // Unreached
.endm

    /*
     * Macro that calls through to artDeliverPendingExceptionFromCode, where the pending
     * exception is Thread::Current()->exception_.
     */
.macro DELIVER_PENDING_EXCEPTION
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    DELIVER_PENDING_EXCEPTION_FRAME_READY
.endm



.macro SAVE_ALL_ARGS_INCREASE_FRAME extra_space
    // Reserve spae for all argument registers, plus the extra space.
    addi sp, sp, -(ALL_ARGS_SIZE + \extra_space)

    // Save registers args a0-a7.
    sd   a0, (0*8)(sp)
    sd   a1, (1*8)(sp)
    sd   a2, (2*8)(sp)
    sd   a3, (3*8)(sp)
    sd   a4, (4*8)(sp)
    sd   a5, (5*8)(sp)
    sd   a6, (6*8)(sp)
    sd   a7, (7*8)(sp)

    // Save registers fa0-fa7.
    fsd  fa0, (8*8)(sp)
    fsd  fa1, (9*8)(sp)
    fsd  fa2, (10*8)(sp)
    fsd  fa3, (11*8)(sp)
    fsd  fa4, (12*8)(sp)
    fsd  fa5, (13*8)(sp)
    fsd  fa6, (14*8)(sp)
    fsd  fa7, (15*8)(sp)
.endm

.macro RESTORE_ALL_ARGS_DECREASE_FRAME extra_space
    // Restore registers args a0-a7.
    ld   a0, (0*8)(sp)
    ld   a1, (1*8)(sp)
    ld   a2, (2*8)(sp)
    ld   a3, (3*8)(sp)
    ld   a4, (4*8)(sp)
    ld   a5, (5*8)(sp)
    ld   a6, (6*8)(sp)
    ld   a7, (7*8)(sp)

    // Restore registers fa0-fa7.
    fld  fa0, (8*8)(sp)
    fld  fa1, (9*8)(sp)
    fld  fa2, (10*8)(sp)
    fld  fa3, (11*8)(sp)
    fld  fa4, (12*8)(sp)
    fld  fa5, (13*8)(sp)
    fld  fa6, (14*8)(sp)
    fld  fa7, (15*8)(sp)

    // Reserve spae for all argument registers, plus the extra space.
    addi sp, sp, (ALL_ARGS_SIZE + \extra_space)
.endm

#endif  // ART_RUNTIME_ARCH_X86_64_ASM_SUPPORT_RISCV64_S_
