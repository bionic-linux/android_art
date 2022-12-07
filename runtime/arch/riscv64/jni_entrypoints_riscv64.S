/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "asm_support_riscv64.S"


    .balign 16

    /*
     * Jni dlsym lookup stub.
     */
    .extern artFindNativeMethod
    .extern artFindNativeMethodRunnable


    /*
     * Jni dlsym lookup stub.
     */
    .extern artFindNativeMethod
    .extern artFindNativeMethodRunnable
ENTRY art_jni_dlsym_lookup_stub

    // spill regs.
    SAVE_ALL_ARGS_INCREASE_FRAME 2 * 8
    sd  fp, (ALL_ARGS_SIZE + 0)(sp)
    sd  ra, (ALL_ARGS_SIZE + 8)(sp)
    .cfi_rel_offset fp, (ALL_ARGS_SIZE + 0)
    .cfi_rel_offset ra, (ALL_ARGS_SIZE + 8)
    add   fp, sp, ALL_ARGS_SIZE

    mv a0, xSELF   // pass Thread::Current()
    // Call artFindNativeMethod() for normal native and artFindNativeMethodRunnable()
    // for @FastNative or @CriticalNative.
    ld    t0,  THREAD_TOP_QUICK_FRAME_OFFSET(a0)          // uintptr_t tagged_quick_frame
    li    t1,  TAGGED_JNI_SP_MASK
    not   t1, t1
    and   t0, t0, t1                                      // ArtMethod** sp
    ld    t0, (t0)                                        // ArtMethod* method
    ld    t0, ART_METHOD_ACCESS_FLAGS_OFFSET(t0)          // uint32_t access_flags
    li    t1, (ACCESS_FLAGS_METHOD_IS_FAST_NATIVE | ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE)
    bne   t0, t1, .Llookup_stub_fast_or_critical_native
    call   artFindNativeMethod
    j     .Llookup_stub_continue

.Llookup_stub_fast_or_critical_native:
    call   artFindNativeMethodRunnable
    mv    t0, a0    // store result in scratch reg.

.Llookup_stub_continue:
    // load spill regs.
    ld    fp, (ALL_ARGS_SIZE + 0)(sp)
    ld    ra, (ALL_ARGS_SIZE + 8)(sp)
    .cfi_restore fp
    .cfi_restore ra
    RESTORE_ALL_ARGS_DECREASE_FRAME 2 * 8

    beqz  t0, 1f    // is method code null ?
    jr    t0        // if non-null, tail call to method code.
1:
    ret             // restore regs and return to caller to handle exception.
END art_jni_dlsym_lookup_stub


ENTRY art_jni_dlsym_lookup_critical_stub
    // The hidden arg holding the tagged method (bit 0 set means GenericJNI) is t0.
    // For Generic JNI we already have a managed frame, so we reuse the art_jni_dlsym_lookup_stub.
    andi  t6, t0, 1
    bnez  t6, art_jni_dlsym_lookup_stub

    // Save args, the hidden arg and caller PC. No CFI needed for args and the hidden arg.
    SAVE_ALL_ARGS_INCREASE_FRAME 2 * 8
    sd  fp, (ALL_ARGS_SIZE + 0)(sp)
    sd  ra, (ALL_ARGS_SIZE + 8)(sp)
    .cfi_rel_offset fp, (ALL_ARGS_SIZE + 0)
    .cfi_rel_offset ra, (ALL_ARGS_SIZE + 8)
    add   fp, sp, ALL_ARGS_SIZE

    // Call artCriticalNativeFrameSize(method, caller_pc)
    move   a0, t0   // a0 := method (from hidden arg)
    move   a1, ra   // a1 := caller_pc
    la     t6, artCriticalNativeFrameSize
    jalr   ra, t6

    // Move frame size to t5.
    move   t5, a0

    // load spill regs.
    ld    fp, (ALL_ARGS_SIZE + 0)(sp)
    ld    ra, (ALL_ARGS_SIZE + 8)(sp)
    .cfi_restore fp
    .cfi_restore ra
    RESTORE_ALL_ARGS_DECREASE_FRAME 2 * 8

    // Reserve space for a SaveRefsAndArgs managed frame, either for the actual runtime
    // method or for a GenericJNI frame which is similar but has a native method and a tag.
    INCREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS

    // Calculate the base address of the managed frame.
    add   t4, sp, t5

    // Prepare the return address for managed stack walk of the SaveRefsAndArgs frame.
    // If we're coming from JNI stub with tail call, it is RA. If we're coming from
    // JNI stub that saved the return address, it will be the last value we copy below.
    // If we're coming directly from compiled code, it is RA, set further down.
    move   t3, ra

    // Move the stack args if any.
    beqz   t5, .Lcritical_skip_copy_args
    move   t6, sp
.Lcritical_copy_args_loop:
    ld   t3, FRAME_SIZE_SAVE_REFS_AND_ARGS(t6)
    addi  t5, t5, -8
    sd   t3, 0(t6)
    addi t6, t6, 8
    bnez  t5,   .Lcritical_copy_args_loop

.Lcritical_skip_copy_args:

    // Spill registers for the SaveRefsAndArgs frame above the stack args.
    // Note that the runtime shall not examine the args here, otherwise we would have to
    // move them in registers and stack to account for the difference between managed and
    // native ABIs. Do not update CFI while we hold the frame address in x13 and the values
    // in registers are unchanged.
    sd      t3, 216(t4)      // t3: Save return address for tail call from JNI stub.   
    sd      s0, 208(t4)
    sd      s10, 200(t4)
    sd      s9, 192(t4)
    sd      s8, 184(t4)
    sd      s7, 176(t4)
    sd      s6, 168(t4)
    sd      s5, 160(t4)
    sd      s4, 152(t4)
    sd      s3, 144(t4)
    sd      s2, 136(t4)
    sd      a7, 128(t4)
    sd      a6, 120(t4)
    sd      a5, 112(t4)
    sd      a4, 104(t4)
    sd      a3,  96(t4)
    sd      a2,  88(t4)
    sd      a1,  80(t4)
    fsd     f17, 72(t4)
    fsd     f16, 64(t4)
    fsd     f15, 56(t4)
    fsd     f14, 48(t4)
    fsd     f13, 40(t4)
    fsd     f12, 32(t4)
    fsd     f11, 24(t4)
    fsd     f10, 16(t4)
    // (If there were any stack args, we're storing the value that's already there.
    // For direct calls from compiled managed code, we shall overwrite this below.)

    // Move the managed frame address to native callee-save register fp(s0) and update CFI.
    move   s0, t4
    // Skip args f10-f17, a1-a7
    CFI_EXPRESSION_BREG 18, 8, 136
    CFI_EXPRESSION_BREG 19, 8, 144
    CFI_EXPRESSION_BREG 20, 8, 152
    CFI_EXPRESSION_BREG 21, 8, 160
    CFI_EXPRESSION_BREG 22, 8, 168
    CFI_EXPRESSION_BREG 23, 8, 176
    CFI_EXPRESSION_BREG 24, 8, 184
    CFI_EXPRESSION_BREG 25, 8, 192
    CFI_EXPRESSION_BREG 26, 8, 200
    CFI_EXPRESSION_BREG 8, 8, 208
    // The saved return PC for managed stack walk is not necessarily our RA.

    // Save our return PC in the padding.
    sd   ra, __SIZEOF_POINTER__(s0)
    CFI_EXPRESSION_BREG 1, 8, __SIZEOF_POINTER__

    ld   t6, ART_METHOD_ACCESS_FLAGS_OFFSET(t0)    // Load access flags.  ??
    addi  t5, s0, 1            // Prepare managed SP tagged for a GenericJNI frame.
    andi  t6, t6, ACCESS_FLAGS_METHOD_IS_NATIVE
    bnez  t6, .Lcritical_skip_prepare_runtime_method

    // When coming from a compiled method, the return PC for managed stack walk is RA.
    // (When coming from a compiled stub, the correct return PC is already stored above.)
    sd   ra, (FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__)(s0)

    // Replace the target method with the SaveRefsAndArgs runtime method.
    LOAD_RUNTIME_INSTANCE t0
    ld   t0, RUNTIME_SAVE_REFS_AND_ARGS_METHOD_OFFSET(t0)

    move   t5, s0                // Prepare untagged managed SP for the runtime method.

.Lcritical_skip_prepare_runtime_method:
    // Store the method on the bottom of the managed frame.
    sd    t0,  0(s0)

    // Place (maybe tagged) managed SP in Thread::Current()->top_quick_frame.
    sd   t5, THREAD_TOP_QUICK_FRAME_OFFSET(xSELF)

    // Preserve the native arg register a0 in callee-save register s10 which was saved above.
    move   s10, a0

    // Call artFindNativeMethodRunnable()
    move   a0, xSELF   // pass Thread::Current()
    la    t6, artFindNativeMethodRunnable
    jalr  ra, t6

    // Store result in scratch reg.
    move   t4, a0

    // Restore the native arg register a0.
    move   a0, s10

    // Restore our return PC.
    RESTORE_REG_BASE s0, ra, __SIZEOF_POINTER__

    // Remember the stack args size, negated because SP cannot be on the right-hand side in SUB.
    sub   t5, sp, s0

    // Restore the frame. We shall not need the method anymore.
    fld     f17, 72(s0)
    fld     f16, 64(s0)
    fld     f15, 56(s0)
    fld     f14, 48(s0)
    fld     f13, 40(s0)
    fld     f12, 32(s0)
    fld     f11, 24(s0)
    fld     f10, 16(s0)
    ld      a7, 128(s0)
    ld      a6, 120(s0)
    ld      a5, 112(s0)
    ld      a4, 104(s0)
    ld      a3,  96(s0)
    ld      a2,  88(s0)
    ld      a1,  80(s0)

    ld    s10, 200(s0)
    .cfi_restore s10
    ld    s9, 192(s0)
    .cfi_restore s9
    ld    s8, 184(s0)
    .cfi_restore s8
    ld    s7, 176(s0)
    .cfi_restore s7
    ld    s6, 168(s0)
    .cfi_restore s6
    ld    s5, 160(s0)
    .cfi_restore s5
    ld    s4, 152(s0)
    .cfi_restore s4
    ld      s3, 144(s0)
    .cfi_restore s3
    ld      s2, 136(s0)
    .cfi_restore s2
    ld    s0, 208(s0)
    .cfi_restore s0

    // Check for exception before moving args back to keep the return PC for managed stack walk.
    beqz   t4, .Lcritical_deliver_exception

    .cfi_remember_state

    // Move stack args to their original place.
    beqz   t5, .Lcritical_skip_copy_args_back
    sub   t6, sp, t5
.Lcritical_copy_args_back_loop:
    ld  t3, -8(t6)
    addi t6, t6, -8
    addi t5, t5, 8
    sd  t3, FRAME_SIZE_SAVE_REFS_AND_ARGS(t6)
    bnez t5, .Lcritical_copy_args_back_loop
.Lcritical_skip_copy_args_back:

    // Remove the frame reservation.
    DECREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS

    // Do the tail call.
    jalr   zero, t4
    .cfi_restore_state
    .cfi_def_cfa sp, FRAME_SIZE_SAVE_REFS_AND_ARGS

.Lcritical_deliver_exception:
    // The exception delivery checks that xSELF was saved but the SaveRefsAndArgs
    // frame does not save it, so we cannot use the existing SaveRefsAndArgs frame.
    // That's why we checked for exception after restoring registers from it.
    // We need to build a SaveAllCalleeSaves frame instead. Args are irrelevant at this
    // point but keep the area allocated for stack args to keep CFA definition simple.
    DECREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS - FRAME_SIZE_SAVE_ALL_CALLEE_SAVES

    // Calculate the base address of the managed frame.
    sub   t4, sp, t5

    // Spill registers for the SaveAllCalleeSaves frame above the stack args area. Do not update
    // CFI while we hold the frame address in t4 and the values in registers are unchanged.
    sd     s0, 192(t4)
    sd     s11, 184(t4)
    sd     s10, 176(t4)
    sd     s9, 168(t4)
    sd     s8, 160(t4)
    sd     s7, 152(t4)
    sd     s6,  144(t4)
    sd     s5,  136(t4)
    sd     s4,  128(t4)
    sd     s3,  120(t4)
    sd     s2,  112(t4)
    sd     s1,  104(t4)

    // FP callee-saves
    fsd    f27, 96(t4)
    fsd    f26, 88(t4)
    fsd    f25, 80(t4)
    fsd    f24, 72(t4)
    fsd    f23, 64(t4)
    fsd    f22, 56(t4)
    fsd    f21, 48(t4)
    fsd    f20, 40(t4)
    fsd    f19, 32(t4)
    fsd    f18, 24(t4)
    fsd    f9, 16(t4)
    fsd    f8,  8(t4)
    // Keep the caller PC for managed stack walk.

    // Move the managed frame address to native callee-save register fp(s0) and update CFI.
    move   s0, t4
    CFI_EXPRESSION_BREG 9, 8, 104
    CFI_EXPRESSION_BREG 18, 8, 112
    CFI_EXPRESSION_BREG 19, 8, 120
    CFI_EXPRESSION_BREG 20, 8, 128
    CFI_EXPRESSION_BREG 21, 8, 136
    CFI_EXPRESSION_BREG 22, 8, 144
    CFI_EXPRESSION_BREG 23, 8, 152
    CFI_EXPRESSION_BREG 24, 8, 160
    CFI_EXPRESSION_BREG 25, 8, 168
    CFI_EXPRESSION_BREG 26, 8, 176
    CFI_EXPRESSION_BREG 27, 8, 184
    CFI_EXPRESSION_BREG 8, 8, 192
    // The saved return PC for managed stack walk is not necessarily our RA.

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveAllCalleeSaves] to the managed frame.
    LOAD_RUNTIME_INSTANCE t6
    ld t6, RUNTIME_SAVE_ALL_CALLEE_SAVES_METHOD_OFFSET(t6)
    sd t6, 0(s0)

    // Place the managed frame SP in Thread::Current()->top_quick_frame.
    sd s0, THREAD_TOP_QUICK_FRAME_OFFSET(xSELF)

    DELIVER_PENDING_EXCEPTION_FRAME_READY
END art_jni_dlsym_lookup_critical_stub


UNDEFINED art_quick_aput_obj
UNDEFINED art_jni_method_start
UNDEFINED art_jni_method_end
UNDEFINED art_jni_read_barrier
UNDEFINED art_jni_method_entry_hook
UNDEFINED art_jni_lock_object_no_inline
UNDEFINED art_jni_lock_object
UNDEFINED art_jni_unlock_object_no_inline
UNDEFINED art_jni_unlock_object
UNDEFINED art_jni_monitored_method_start
UNDEFINED art_jni_monitored_method_end