
#include "asm_support_riscv64.S"
#include "interpreter/cfi_asm_support.h"



.macro SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL base
#if (FRAME_SIZE_SAVE_REFS_AND_ARGS != 28 * 8)
#error "FRAME_SIZE_SAVE_REFS_AND_ARGS(RISCV64) size not as expected."
#endif
    // We need to save callee-save GPRs on the stack in as they may contain references, and must be
    // visible to GC (unless the called method holds mutator lock and prevents GC from happening).
    // FP callee-saves shall be preserved by whatever runtime function we call, so they do not need
    // to be saved. 

    sd   ra,  (27*8)(\base)  // x1, return address

    sd   s11, (26*8)(\base)  // x27
    sd   s10, (25*8)(\base)  // x26
    sd   s9,  (24*8)(\base)  // x25
    sd   s8,  (23*8)(\base)  // x24
    sd   s7,  (22*8)(\base)  // x23
    sd   s6,  (21*8)(\base)  // x22
    sd   s5,  (20*8)(\base)  // x21
    sd   s4,  (19*8)(\base)  // x20
    sd   s3,  (18*8)(\base)  // x19
    sd   s2,  (17*8)(\base)  // x18

    // a0 is the method pointer
    sd   a7,  (16*8)(\base)  // x17
    sd   a6,  (15*8)(\base)  // x16
    sd   a5,  (14*8)(\base)  // x15
    sd   a4,  (13*8)(\base)  // x14
    sd   a3,  (12*8)(\base)  // x13
    sd   a2,  (11*8)(\base)  // x12
    sd   a1,  (10*8)(\base)  // x11

    sd   fp,   (9*8)(\base)  // x8, frame pointer

    fsd  fa7,  (8*8)(\base)
    fsd  fa6,  (7*8)(\base)
    fsd  fa5,  (6*8)(\base)
    fsd  fa4,  (5*8)(\base)
    fsd  fa3,  (4*8)(\base)
    fsd  fa2,  (3*8)(\base)
    fsd  fa1,  (2*8)(\base)
    fsd  fa0,  (1*8)(\base)
.endm


.macro RESTORE_SAVE_REFS_AND_ARGS_FRAME_INTERNAL base
    ld   ra,  (27*8)(\base)  // x1, return address

    ld   s11, (26*8)(\base)  // x27
    ld   s10, (25*8)(\base)  // x26
    ld   s9,  (24*8)(\base)  // x25
    ld   s8,  (23*8)(\base)  // x24
    ld   s7,  (22*8)(\base)  // x23
    ld   s6,  (21*8)(\base)  // x22
    ld   s5,  (20*8)(\base)  // x21
    ld   s4,  (19*8)(\base)  // x20
    ld   s3,  (18*8)(\base)  // x19
    ld   s2,  (17*8)(\base)  // x18

    // a0 is the method pointer
    ld   a7,  (16*8)(\base)  // x17
    ld   a6,  (15*8)(\base)  // x16
    ld   a5,  (14*8)(\base)  // x15
    ld   a4,  (13*8)(\base)  // x14
    ld   a3,  (12*8)(\base)  // x13
    ld   a2,  (11*8)(\base)  // x12
    ld   a1,  (10*8)(\base)  // x11

    ld   fp,   (9*8)(\base)  // x8, frame pointer

    fld  fa7,  (8*8)(\base)
    fld  fa6,  (7*8)(\base)
    fld  fa5,  (6*8)(\base)
    fld  fa4,  (5*8)(\base)
    fld  fa3,  (4*8)(\base)
    fld  fa2,  (3*8)(\base)
    fld  fa1,  (2*8)(\base)
    fld  fa0,  (1*8)(\base)
.endm


.macro SETUP_SAVE_REFS_AND_ARGS_FRAME
    addi sp, sp, -FRAME_SIZE_SAVE_REFS_AND_ARGS
    SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL sp

    // art::Runtime* t0 = art::Runtime::instance_;
    // Our registers are not intermixed - just spill in order.
    LOAD_RUNTIME_INSTANCE t0

    // ArtMethod* t0 = Runtime::instance_->callee_save_methods_[kSaveRefAndArgs];
    ld t0, RUNTIME_SAVE_REFS_AND_ARGS_METHOD_OFFSET(t0)

    sd t0, 0(sp)    // Store ArtMethod* Runtime::callee_save_methods_[kSaveRefsAndArgs].

    // Place sp in Thread::Current()->top_quick_frame.
    sd    sp, THREAD_TOP_QUICK_FRAME_OFFSET(xSELF)
.endm


.macro SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_A0
    addi sp, sp, -FRAME_SIZE_SAVE_REFS_AND_ARGS
    SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL sp

    // Store ArtMethod* to bottom of stack.
    sd   a0, 0(sp)

    // Place sp in Thread::Current()->top_quick_frame.
    sd    sp, THREAD_TOP_QUICK_FRAME_OFFSET(xSELF)
.endm


.macro RESTORE_SAVE_REFS_AND_ARGS_FRAME
    RESTORE_SAVE_REFS_AND_ARGS_FRAME_INTERNAL sp
    addi sp, sp, FRAME_SIZE_SAVE_REFS_AND_ARGS
.endm

.macro SETUP_SAVE_EVERYTHING_FRAME
    // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_EVERYTHING != 8 * (32 + 29 + 1))
#error "FRAME_SIZE_SAVE_EVERYTHING(ARM64) size not as expected."
#endif
    addi sp, sp, -FRAME_SIZE_SAVE_EVERYTHING

    // 32 slots for FPRs
    fsd  f0,   8*1(sp)
    fsd  f1,   8*2(sp)
    fsd  f2,   8*3(sp)
    fsd  f3,   8*4(sp)
    fsd  f4,   8*5(sp)
    fsd  f5,   8*6(sp)
    fsd  f6,   8*7(sp)
    fsd  f7,   8*8(sp)
    fsd  f8,   8*9(sp)
    fsd  f9,   8*10(sp)
    fsd  f10,  8*11(sp)
    fsd  f11,  8*12(sp)
    fsd  f12,  8*13(sp)
    fsd  f13,  8*14(sp)
    fsd  f14,  8*15(sp)
    fsd  f15,  8*16(sp)
    fsd  f16,  8*17(sp)
    fsd  f17,  8*18(sp)
    fsd  f18,  8*19(sp)
    fsd  f19,  8*20(sp)
    fsd  f20,  8*21(sp)
    fsd  f21,  8*22(sp)
    fsd  f22,  8*23(sp)
    fsd  f23,  8*24(sp)
    fsd  f24,  8*25(sp)
    fsd  f25,  8*26(sp)
    fsd  f26,  8*27(sp)
    fsd  f27,  8*28(sp)
    fsd  f28,  8*29(sp)
    fsd  f29,  8*30(sp)
    fsd  f30,  8*31(sp)
    fsd  f31,  8*32(sp)

    // 29 slots for GPRs (skip x0, x3, x4)
    // delay x1, x2
    sd  x5,   8*34(sp)
    sd  x6,   8*35(sp)
    sd  x7,   8*36(sp)
    sd  x8,   8*37(sp)
    sd  x9,   8*38(sp)
    sd  x10,  8*39(sp)
    sd  x11,  8*40(sp)
    sd  x12,  8*41(sp)
    sd  x13,  8*42(sp)
    sd  x14,  8*43(sp)
    sd  x15,  8*44(sp)
    sd  x16,  8*45(sp)
    sd  x17,  8*46(sp)
    sd  x18,  8*47(sp)
    sd  x19,  8*48(sp)
    sd  x20,  8*49(sp)
    sd  x21,  8*50(sp)
    sd  x22,  8*51(sp)
    sd  x23,  8*52(sp)
    sd  x24,  8*53(sp)
    sd  x25,  8*54(sp)
    sd  x26,  8*55(sp)
    sd  x27,  8*56(sp)
    sd  x28,  8*57(sp)
    sd  x29,  8*58(sp)
    sd  x30,  8*59(sp)
    sd  x31,  8*60(sp)

    sd  x1,   8*61(sp)
    sd  x2,   8*33(sp) // SP goes last

    // art::Runtime* xIP0 = art::Runtime::instance_;
    LOAD_RUNTIME_INSTANCE t0

    // ArtMethod* t0 = Runtime::instance_->callee_save_methods_[kSaveEverything];
    ld t0, RUNTIME_SAVE_EVERYTHING_METHOD_OFFSET(t0)

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveEverything].
    sd t0, (sp)
    // Place sp in Thread::Current()->top_quick_frame.
    mv t0, sp
    sd t0, THREAD_TOP_QUICK_FRAME_OFFSET(xSELF)
.endm


.macro RESTORE_SAVE_EVERYTHING_FRAME
    // 32 slots for FPRs
    fld  f0,   8*1(sp)
    fld  f1,   8*2(sp)
    fld  f2,   8*3(sp)
    fld  f3,   8*4(sp)
    fld  f4,   8*5(sp)
    fld  f5,   8*6(sp)
    fld  f6,   8*7(sp)
    fld  f7,   8*8(sp)
    fld  f8,   8*9(sp)
    fld  f9,   8*10(sp)
    fld  f10,  8*11(sp)
    fld  f11,  8*12(sp)
    fld  f12,  8*13(sp)
    fld  f13,  8*14(sp)
    fld  f14,  8*15(sp)
    fld  f15,  8*16(sp)
    fld  f16,  8*17(sp)
    fld  f17,  8*18(sp)
    fld  f18,  8*19(sp)
    fld  f19,  8*20(sp)
    fld  f20,  8*21(sp)
    fld  f21,  8*22(sp)
    fld  f22,  8*23(sp)
    fld  f23,  8*24(sp)
    fld  f24,  8*25(sp)
    fld  f25,  8*26(sp)
    fld  f26,  8*27(sp)
    fld  f27,  8*28(sp)
    fld  f28,  8*29(sp)
    fld  f29,  8*30(sp)
    fld  f30,  8*31(sp)
    fld  f31,  8*32(sp)

    // 29 slots for GPRs (skip x0, x3, x4)
    // delay x1, x2
    ld  x5,   8*34(sp)
    ld  x6,   8*35(sp)
    ld  x7,   8*36(sp)
    ld  x8,   8*37(sp)
    ld  x9,   8*38(sp)
    ld  x10,  8*39(sp)
    ld  x11,  8*40(sp)
    ld  x12,  8*41(sp)
    ld  x13,  8*42(sp)
    ld  x14,  8*43(sp)
    ld  x15,  8*44(sp)
    ld  x16,  8*45(sp)
    ld  x17,  8*46(sp)
    ld  x18,  8*47(sp)
    ld  x19,  8*48(sp)
    ld  x20,  8*49(sp)
    ld  x21,  8*50(sp)
    ld  x22,  8*51(sp)
    ld  x23,  8*52(sp)
    ld  x24,  8*53(sp)
    ld  x25,  8*54(sp)
    ld  x26,  8*55(sp)
    ld  x27,  8*56(sp)
    ld  x28,  8*57(sp)
    ld  x29,  8*58(sp)
    ld  x30,  8*59(sp)
    ld  x31,  8*60(sp)

    ld  x1,   8*61(sp)
    ld  x2,   8*33(sp) // SP goes last

    addi sp, sp, FRAME_SIZE_SAVE_EVERYTHING
.endm


.macro INVOKE_STUB_CREATE_FRAME
    // No need to spill callee-save regs that are unused.
    addi sp, sp, -64
    sd   ra,       (8*7)(sp)
    sd   fp,       (8*6)(sp)
    sd   xSELF,    (8*5)(sp)
    sd   s4,       (8*4)(sp)
    sd   a4,       (8*3)(sp)
    sd   a5,       (8*2)(sp)
    sd   s2,       (8*1)(sp)
    sd   s3,       (8*0)(sp)

    mv   fp, sp  // Use xFP for frame pointer, as it is callee-saved.
    .cfi_def_cfa_register fp

    addi t0, a2, (__SIZEOF_POINTER__ + 0xf) // Reserve space for ArtMethod*, arguments and
    andi t0, t0, ~0xf                       // round up for 16-byte stack alignment.
    sub  sp, sp, t0                         // Adjust SP for ArtMethod*, args and alignment padding.

    mv xSELF, a3

    // Copy parameters into the stack.
    // Use numeric label as this is a macro and Clang assembler does not have unique-id variables.
    // Use simple copy routine for now.
    // 4 bytes per slot.
    // A1 - source address
    // A2 - args length
    // S3 - destination address.
    // T0, T1 - temporaries

    add s3, sp, 8  // Destination address is bottom of stack + null.
1:
// TODO: see what GCC generates for *p++ = *q++ loops
    beqz a2, 2f
    addi a2, a2, -4      // Need 65536 bytes of range. what does it mean ???????????????????????????
    add  t0, a1, a2
    lw   t1, 0(t0)
    add  t0, s3, a2
    sw   t1, 0(t0)
    j    1b

2:
    // Store null into ArtMethod* at bottom of frame.
    sd x0, 0(sp)
.endm


.macro INVOKE_STUB_CALL_AND_RETURN
    // Call the method.
    ld   t0, ART_METHOD_QUICK_CODE_OFFSET_64(a0)
    jalr t0

    // Pop the ArtMethod* (null), arguments and alignment padding from the stack.
    mv sp, fp
    .cfi_def_cfa_register sp

    ld   ra,       (8*7)(sp)
    ld   fp,       (8*6)(sp)
    ld   xSELF,    (8*5)(sp)
    ld   s4,       (8*4)(sp)
    ld   a4,       (8*3)(sp)
    ld   a5,       (8*2)(sp)
    ld   s2,       (8*1)(sp)
    ld   s3,       (8*0)(sp)

    addi sp, sp,   64


    // Store result (w0/x0/s0/d0) appropriately, depending on resultType.
    lbu  t0, 0(a5)

    // Check the return type and store the correct register into the jvalue in memory.
    // Use numeric label as this is a macro and Clang assembler does not have unique-id variables.

    // Do not set anything for a void type.
    li t1, 'V'
    beq t1, t0, 1f

    li t1, 'D'
    beq t1, t0, 2f

    li t1, 'F'
    beq t1, t0, 3f

    // Just store x0. Does not matter if it is 64 or 32 bits.
    sd a0, 0(a4)

1:  // Finish up.
    ret

2:  // Store double.
    fsd fa0, 0(a4)
    ret

3:  // Store float.
    fsw fa0, 0(a4)
    ret
.endm


.macro SAVE_ALL_CALLEE_SAVES
    // Stack slot 0 at the bottom of the frame is reseved for 16-byte alignment.

    // FP callee-saves.
    fsd  fs0,  (8*1)(sp)   // f8
    fsd  fs1,  (8*2)(sp)   // f9
    fsd  fs2,  (8*3)(sp)   // f18
    fsd  fs3,  (8*4)(sp)   // f19
    fsd  fs4,  (8*5)(sp)   // f20
    fsd  fs5,  (8*6)(sp)   // f21
    fsd  fs6,  (8*7)(sp)   // f22
    fsd  fs7,  (8*8)(sp)   // f23
    fsd  fs8,  (8*9)(sp)   // f24
    fsd  fs9,  (8*10)(sp)  // f25
    fsd  fs10, (8*11)(sp)  // f26
    fsd  fs11, (8*12)(sp)  // f27

    // GP callee-saves
    sd   s0,  (8*13)(sp)  // x8/fp, frame pointer
    sd   s1,  (8*14)(sp)  // x9
    sd   s2,  (8*15)(sp)  // x18
    sd   s3,  (8*16)(sp)  // x19
    sd   s4,  (8*17)(sp)  // x20
    sd   s5,  (8*18)(sp)  // x21
    sd   s6,  (8*19)(sp)  // x22
    sd   s7,  (8*20)(sp)  // x23
    sd   s8,  (8*21)(sp)  // x24
    sd   s9,  (8*22)(sp)  // x25
    sd   s10, (8*23)(sp)  // x26
    sd   s11, (8*24)(sp)  // x27

    // Return address (at the top pf stack frame).
    sd   ra,  (8*25)(sp)
.endm


    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveAllCalleeSaves)
     */
.macro SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    // art::Runtime* t0 = art::Runtime::instance_;
    // Our registers are not intermixed - just spill in order.
    LOAD_RUNTIME_INSTANCE t0

    // ArtMethod* t0 = Runtime::instance_->callee_save_methods_[kSaveAllCalleeSaves];
    ld t0, RUNTIME_SAVE_ALL_CALLEE_SAVES_METHOD_OFFSET(t0)

    addi sp, sp, -208

    // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_ALL_CALLEE_SAVES != 208)
#error "FRAME_SIZE_SAVE_ALL_CALLEE_SAVES(RISCV64) size not as expected."
#endif

    // Stack alignment filler?
    SAVE_ALL_CALLEE_SAVES

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveAllCalleeSaves].
    sd t0, (sp)
    // Place sp in Thread::Current()->top_quick_frame.
    mv t0, sp
    sd t0, THREAD_TOP_QUICK_FRAME_OFFSET(xSELF)
.endm

.macro RETURN_OR_DELIVER_PENDING_EXCEPTION_REG reg
    ld \reg, THREAD_EXCEPTION_OFFSET(xSELF)
    bnez \reg, 1f
    ret
1:
    DELIVER_PENDING_EXCEPTION
.endm


ENTRY art_deliver_pending_exception
    # This will create a new save-all frame, required by the runtime.
    DELIVER_PENDING_EXCEPTION
END art_deliver_pending_exception


#define LOADREG_SIZE 10


// Macro for loading a parameter into a register.
//  counter - the register with offset into these tables
//  size - the size of the register - 4 or 8 bytes.
//  register - the name of the register to be loaded.
.macro LOADREG_I4 counter register return
    lwu    \register, 0(s3)
    addi s3, s3, 4
    add    \counter, \counter, LOADREG_SIZE
    j      \return
.endm
.macro LOADREG_I8 counter register return
    ld     \register, 0(s3)
    addi s3, s3, 8
    add    \counter, \counter, LOADREG_SIZE
    j      \return
.endm
.macro LOADREG_F4 counter register return
    flw    \register, 0(s3)
    addi s3, s3, 4
    add    \counter, \counter, LOADREG_SIZE
    j      \return
.endm
.macro LOADREG_F8 counter register return
    fld    \register, 0(s3)
    addi s3, s3, 8
    add    \counter, \counter, LOADREG_SIZE
    j      \return
.endm



/*
 *  extern"C" void art_quick_invoke_stub(ArtMethod *method,   a0
 *                                       uint32_t  *args,     a1
 *                                       uint32_t argsize,    a2
 *                                       Thread *self,        a3
 *                                       JValue *result,      a4
 *                                       char   *shorty);     a5
 */
ENTRY art_quick_invoke_stub
.option push
.option norelax
    INVOKE_STUB_CREATE_FRAME

    // Fill registers a1 - a7 and fa0 - fa7 with parameters.
    // Parse the passed shorty to determine which register to load.
    // Load addresses for routines that load registers.
    la  t3, .LstoreA4_1_1
    la  t4, .LstoreA8_1_1
    la  t5, .LstoreF4_0_1
    la  t6, .LstoreF8_0_1

//#ifndef NDEBUG
    la t0, .LstoreA4_1_1_size_check
    la t1, .LstoreA4_1_1
    sub t0, t0, t1
    li t1, LOADREG_SIZE
    beq t0, t1, 1f
    unimp
1:
//.long .L2 - .L1
//auipc t0, -4
//li t1, K
//beq t0, t1, 1f
//unimp
//1:
//#endif

    // Initialize routine offsets to 0 for integers and floats.
    // s4 for integers, s2 for floating point.
    mv  s4, zero
    mv  s2, zero

    mv   t0, a5                // Load shorty address.
    lw   a1, 0(s3)             // Load "this" parameter, and increment arg pointer.
    addi s3, s3, 4

    // Loop to fill registers.
.LfillRegisters1:
    addi t0, t0, 1            // Increment (first increment skips return value).
    lb  t1, 0(t0)            // Load next character in signature.
    beqz t1, .LcallFunction1  // Exit at end of signature. Shorty 0 terminated.

    li t2, 'F'
    bne t1, t2, .LisDouble1

    li t2, 8*LOADREG_SIZE       // Skip this load if all registers full.
    beq s2, t2, .Ladvance4_1

    add    t2, t5, s2         // Calculate subroutine to jump to.
    jr     t2

.LisDouble1:

    li t2, 'D'
    bne t1, t2, .LisLong1

    li t2, 8*LOADREG_SIZE       // Skip this load if all registers full.
    beq s2, t2, .Ladvance8_1

    add    t2, t6, s2         // Calculate subroutine to jump to.
    jr     t2

.LisLong1:

    li t2, 'J'
    bne t1, t2, .LisOther1

    li t2, 6*LOADREG_SIZE       // Skip this load if all registers full.
    beq s4, t2, .Ladvance8_1

    add    t2, t4, s4         // Calculate subroutine to jump to.
    jr     t2

.LisOther1:                   // Everything else takes one vReg.

    li t2, 6*LOADREG_SIZE       // Skip this load if all registers full.
    beq s4, t2, .Ladvance4_1

    add    t2, t3, s4         // Calculate subroutine to jump to.
    jr     t2

.Ladvance4_1:
    addi   s3, s3, 4
    j      .LfillRegisters1

.Ladvance8_1:
    addi   s3, s3, 8
    j      .LfillRegisters1

// Store ints.
.LstoreA4_1_1:
    LOADREG_I4 s4 a2 .LfillRegisters1
.LstoreA4_1_1_size_check:
    LOADREG_I4 s4 a3 .LfillRegisters1
    LOADREG_I4 s4 a4 .LfillRegisters1
    LOADREG_I4 s4 a5 .LfillRegisters1
    LOADREG_I4 s4 a6 .LfillRegisters1
    LOADREG_I4 s4 a7 .LfillRegisters1

// Store longs.
.LstoreA8_1_1:
    LOADREG_I8 s4 a2 .LfillRegisters1
    LOADREG_I8 s4 a3 .LfillRegisters1
    LOADREG_I8 s4 a4 .LfillRegisters1
    LOADREG_I8 s4 a5 .LfillRegisters1
    LOADREG_I8 s4 a6 .LfillRegisters1
    LOADREG_I8 s4 a7 .LfillRegisters1

// Store singles.
.LstoreF4_0_1:
    LOADREG_F4 s2 fa0 .LfillRegisters1
    LOADREG_F4 s2 fa1 .LfillRegisters1
    LOADREG_F4 s2 fa2 .LfillRegisters1
    LOADREG_F4 s2 fa3 .LfillRegisters1
    LOADREG_F4 s2 fa4 .LfillRegisters1
    LOADREG_F4 s2 fa5 .LfillRegisters1
    LOADREG_F4 s2 fa6 .LfillRegisters1
    LOADREG_F4 s2 fa7 .LfillRegisters1

// Store doubles.
.LstoreF8_0_1:
    LOADREG_F8 s2 fa0 .LfillRegisters1
    LOADREG_F8 s2 fa1 .LfillRegisters1
    LOADREG_F8 s2 fa2 .LfillRegisters1
    LOADREG_F8 s2 fa3 .LfillRegisters1
    LOADREG_F8 s2 fa4 .LfillRegisters1
    LOADREG_F8 s2 fa5 .LfillRegisters1
    LOADREG_F8 s2 fa6 .LfillRegisters1
    LOADREG_F8 s2 fa7 .LfillRegisters1
.LcallFunction1:
    INVOKE_STUB_CALL_AND_RETURN
.option pop
END art_quick_invoke_stub

/*  extern"C"
 *     void art_quick_invoke_static_stub(ArtMethod* method,   a0
 *                                       uint32_t*  args,     a1
 *                                       uint32_t   argsize,  a2
 *                                       Thread*    self,     a3
 *                                       JValue*    result,   a4
 *                                       char*      shorty);  a5
 */
ENTRY art_quick_invoke_static_stub
    INVOKE_STUB_CREATE_FRAME

    // Fill registers a1 - a7 and fa0 - fa7 with parameters.
    // Parse the passed shorty to determine which register to load.
    // Load addresses for routines that load registers.
    la  t3, .LstoreA4_1_2
    la  t4, .LstoreA8_1_2
    la  t5, .LstoreF4_0_2
    la  t6, .LstoreF8_0_2

    // Initialize routine offsets to 0 for integers and floats.
    // s4 for integers, s2 for floating point.
    mv  s4, zero
    mv  s2, zero

    mv  t0, a5                // Load shorty address.

    // Loop to fill registers.
.LfillRegisters2:
    addi t0, t0, 1            // Increment (first increment skips return value).
    lb  t1, 0(t0)            // Load next character in signature.
    beqz t1, .LcallFunction2  // Exit at end of signature. Shorty 0 terminated.

    li t2, 'F'
    bne t1, t2, .LisDouble2

    li t2, 8*LOADREG_SIZE       // Skip this load if all registers full.
    beq s2, t2, .Ladvance4_2

    add    t2, t5, s2         // Calculate subroutine to jump to.
    jr     t2

.LisDouble2:

    li t2, 'D'
    bne t1, t2, .LisLong2

    li t2, 8*LOADREG_SIZE       // Skip this load if all registers full.
    beq s2, t2, .Ladvance8_2

    add    t2, t6, s2         // Calculate subroutine to jump to.
    jr     t2

.LisLong2:

    li t2, 'J'
    bne t1, t2, .LisOther2

    li t2, 7*LOADREG_SIZE       // Skip this load if all registers full.
    beq s4, t2, .Ladvance8_2

    add    t2, t4, s4         // Calculate subroutine to jump to.
    jr     t2

.LisOther2:                   // Everything else takes one vReg.

    li t2, 7*LOADREG_SIZE       // Skip this load if all registers full.
    beq s4, t2, .Ladvance4_2

    add    t2, t3, s4         // Calculate subroutine to jump to.
    jr     t2

.Ladvance4_2:
    addi   s3, s3, 4
    j      .LfillRegisters2

.Ladvance8_2:
    addi   s3, s3, 8
    j      .LfillRegisters2

// Store ints.
.LstoreA4_1_2:
    LOADREG_I4 s4 a1 .LfillRegisters2
    LOADREG_I4 s4 a2 .LfillRegisters2
    LOADREG_I4 s4 a3 .LfillRegisters2
    LOADREG_I4 s4 a4 .LfillRegisters2
    LOADREG_I4 s4 a5 .LfillRegisters2
    LOADREG_I4 s4 a6 .LfillRegisters2
    LOADREG_I4 s4 a7 .LfillRegisters2

// Store longs.
.LstoreA8_1_2:
    LOADREG_I8 s4 a1 .LfillRegisters2
    LOADREG_I8 s4 a2 .LfillRegisters2
    LOADREG_I8 s4 a3 .LfillRegisters2
    LOADREG_I8 s4 a4 .LfillRegisters2
    LOADREG_I8 s4 a5 .LfillRegisters2
    LOADREG_I8 s4 a6 .LfillRegisters2
    LOADREG_I8 s4 a7 .LfillRegisters2

// Store singles.
.LstoreF4_0_2:
    LOADREG_F4 s2 fa0 .LfillRegisters2
    LOADREG_F4 s2 fa1 .LfillRegisters2
    LOADREG_F4 s2 fa2 .LfillRegisters2
    LOADREG_F4 s2 fa3 .LfillRegisters2
    LOADREG_F4 s2 fa4 .LfillRegisters2
    LOADREG_F4 s2 fa5 .LfillRegisters2
    LOADREG_F4 s2 fa6 .LfillRegisters2
    LOADREG_F4 s2 fa7 .LfillRegisters2

// Store doubles.
.LstoreF8_0_2:
    LOADREG_F8 s2 fa0 .LfillRegisters2
    LOADREG_F8 s2 fa1 .LfillRegisters2
    LOADREG_F8 s2 fa2 .LfillRegisters2
    LOADREG_F8 s2 fa3 .LfillRegisters2
    LOADREG_F8 s2 fa4 .LfillRegisters2
    LOADREG_F8 s2 fa5 .LfillRegisters2
    LOADREG_F8 s2 fa6 .LfillRegisters2
    LOADREG_F8 s2 fa7 .LfillRegisters2
.LcallFunction2:

    INVOKE_STUB_CALL_AND_RETURN
END art_quick_invoke_static_stub




ENTRY art_quick_imt_conflict_trampoline
    unimp
END art_quick_imt_conflict_trampoline



ENTRY art_quick_to_interpreter_bridge
    SETUP_SAVE_REFS_AND_ARGS_FRAME         // Set up frame and save arguments.

    //  a0 will contain mirror::ArtMethod* method.
    mv a1, xSELF                          // How to get Thread::Current() ???
    mv a2, sp

    // uint64_t artQuickToInterpreterBridge(mirror::ArtMethod* method, Thread* self,
    //                                      mirror::ArtMethod** sp)
    call artQuickToInterpreterBridge

    RESTORE_SAVE_REFS_AND_ARGS_FRAME       // TODO: no need to restore arguments in this case.

    fmv.d.x fa0, a0

    RETURN_OR_DELIVER_PENDING_EXCEPTION_REG t0
END art_quick_to_interpreter_bridge


ENTRY art_invoke_obsolete_method_stub
    unimp
END art_invoke_obsolete_method_stub


ENTRY art_quick_generic_jni_trampoline
    SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_A0

    mv   fp, sp

    li   t0, 5120
    sub  sp, sp, t0

    mv   a0, xSELF   // Thread*
    mv   a1, fp      // SP for the managed frame.
    mv   a2, sp      // reserved area for arguments and other saved data (up to managed frame)
    call  artQuickGenericJniTrampoline

    // Check for error (class init check or locking for synchronized native method can throw).
    beqz a0, .Lexception_in_native

    mv   t0, a0       // save pointer to native method code into temporary

    ld   a0, 0(sp)    // JniEnv* for the native method
    ld   a1, 8(sp)
    ld   a2, 16(sp)
    ld   a3, 24(sp)
    ld   a4, 32(sp)
    ld   a5, 40(sp)
    ld   a6, 48(sp)
    ld   a7, 56(sp)

    fld  fa0, 64(sp)
    fld  fa1, 72(sp)
    fld  fa2, 80(sp)
    fld  fa3, 88(sp)
    fld  fa4, 96(sp)
    fld  fa5, 104(sp)
    fld  fa6, 112(sp)
    fld  fa7, 120(sp)

    ld   t1, 128(sp) // @CriticalNative arg

    ld   t1, 136(sp) // restore stack
    mv   sp, t1

    jalr t0 // call native method

    // result sign extension is handled in C code
    // prepare for artQuickGenericJniEndTrampoline call
    // (Thread*, result, result_f)
    //   a0       a1       a2        <= C calling convention
    mv      a1, a0     // Result (from saved).
    mv      a0, xSELF  // Thread*
    fmv.x.d a2, fa0    // fa0 will contain floating point result, but needs to go into a2
    call     artQuickGenericJniEndTrampoline

    // Pending exceptions possible.
    ld   t0, THREAD_EXCEPTION_OFFSET(xSELF)
    bnez t0, .Lexception_in_native

    mv   sp, fp

    LOAD_RUNTIME_INSTANCE a1
    lb   a1, INSTRUMENTATION_STUBS_INSTALLED_OFFSET_FROM_RUNTIME_INSTANCE(a1)
    bnez a1, .Lcall_method_exit_hook
.Lcall_method_exit_hook_done:

    RESTORE_SAVE_REFS_AND_ARGS_FRAME

    // TODO: call art_quick_method_exit_hook

    fmv.d.x fa0, a0
    ret

.Lcall_method_exit_hook:
    fmv.d.x fa0, a0
    li      a4, FRAME_SIZE_SAVE_REFS_AND_ARGS
    call     art_quick_method_exit_hook
    j       .Lcall_method_exit_hook_done

.Lexception_in_native:
    // Move to a1 then sp to please assembler.
    ld a1, THREAD_TOP_QUICK_FRAME_OFFSET(xSELF)
    addi sp, a1, -1  // Remove the GenericJNI tag.
    la t0, art_deliver_pending_exception
    jalr t0

END art_quick_generic_jni_trampoline


    .extern artMethodExitHook
ENTRY art_quick_method_exit_hook
    SETUP_SAVE_EVERYTHING_FRAME

    // frame_size is passed from JITed code in x4
    addi a3, sp, 8*11                         // floating-point result ptr in kSaveEverything frame
    addi a2, sp, 8*39                         // integer result ptr in kSaveEverything frame
    addi a1, sp, FRAME_SIZE_SAVE_EVERYTHING   // ArtMethod**
    mv   a0, xSELF                            // Thread::Current
    call artMethodExitHook                     // (Thread*, ArtMethod**, gpr_res*, fpr_res*,
                                              // frame_size)

    // Normal return.
    RESTORE_SAVE_EVERYTHING_FRAME
    ret
END art_quick_method_exit_hook


     /*
     * Called by managed code that is attempting to call a method on a proxy class. On entry
     * x0 holds the proxy method and x1 holds the receiver; The frame size of the invoked proxy
     * method agrees with a ref and args callee save frame.
     */
     .extern artQuickProxyInvokeHandler
ENTRY art_quick_proxy_invoke_handler
    SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_A0
    mv      a2, xSELF                   // pass Thread::Current
    mv      a3, sp                      // pass SP
    call     artQuickProxyInvokeHandler  // (Method* proxy method, receiver, Thread*, SP)
    ld      a2, THREAD_EXCEPTION_OFFSET(xSELF)
    bnez    a2, .Lexception_in_proxy    // success if no exception is pending
    .cfi_remember_state
    RESTORE_SAVE_REFS_AND_ARGS_FRAME    // Restore frame
    fmv.d.x  fa0, a0                    // Store result in d0 in case it was float or double
    ret                                 // return on success
.Lexception_in_proxy:
    CFI_RESTORE_STATE_AND_DEF_CFA sp, FRAME_SIZE_SAVE_REFS_AND_ARGS
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    DELIVER_PENDING_EXCEPTION
END art_quick_proxy_invoke_handler


ENTRY art_quick_resolution_trampoline
    unimp
END art_quick_resolution_trampoline

ENTRY art_quick_deoptimize
    unimp
END art_quick_deoptimize

ENTRY art_quick_instrumentation_entry
    unimp
END art_quick_instrumentation_entry

ENTRY art_quick_deoptimize_from_compiled_code
    unimp
END art_quick_deoptimize_from_compiled_code

ENTRY art_quick_instrumentation_exit
    unimp
END art_quick_instrumentation_exit

ENTRY art_quick_string_builder_append
    unimp
END art_quick_string_builder_append

ENTRY art_quick_compile_optimized
    unimp
END art_quick_compile_optimized

ENTRY art_quick_method_entry_hook
    unimp
END art_quick_method_entry_hook


    /*
     * Entry from managed code that calls artInstanceOfFromCode and on failure calls
     * artThrowClassCastExceptionForObject.
     */
    .extern artInstanceOfFromCode
    .extern artThrowClassCastExceptionForObject
ENTRY art_quick_check_instance_of
    unimp
END art_quick_check_instance_of



    /*
     * On entry x0 is uintptr_t* gprs_ and x1 is uint64_t* fprs_.
     * Both must reside on the stack, between current SP and target SP.
     * IP0 and IP1 shall be clobbered rather than retrieved from gprs_.
     */

ENTRY art_quick_do_long_jump

    // Load FPRs

    fld  f0,   0(a1)
    fld  f1,   8(a1)
    fld  f2,   16(a1)
    fld  f3,   24(a1)
    fld  f4,   32(a1)
    fld  f5,   40(a1)
    fld  f6,   48(a1)
    fld  f7,   56(a1)
    fld  f8,   64(a1)
    fld  f9,   72(a1)
    fld  f10,  80(a1)
    fld  f11,  88(a1)
    fld  f12,  96(a1)
    fld  f13,  104(a1)
    fld  f14,  112(a1)
    fld  f15,  120(a1)
    fld  f16,  128(a1)
    fld  f17,  136(a1)
    fld  f18,  144(a1)
    fld  f19,  152(a1)
    fld  f20,  160(a1)
    fld  f21,  168(a1)
    fld  f22,  176(a1)
    fld  f23,  184(a1)
    fld  f24,  192(a1)
    fld  f25,  200(a1)
    fld  f26,  208(a1)
    fld  f27,  216(a1)
    fld  f28,  224(a1)
    fld  f29,  232(a1)
    fld  f30,  240(a1)
    fld  f31,  248(a1)

    // Load GPRs. Delay loading x10/a0 because a0 is used as gprs_.

//    ld  x0,   0(a0) // x0 register is hard-wired zero
    ld  x1,   8(a0)
//    ld  x2,   16(a0)
//    ld  x3,   24(a0)
//    ld  x4,   32(a0)
//    ld  x5,   40(a0)  // t0 is clobbered below
//    ld  x6,   48(a0)  // t1 is clobbered below
    ld  x7,   56(a0)

    ld  x8,   64(a0)
    ld  x9,   72(a0)
//    ld  x10,  80(a0) // delay loading a0
    ld  x11,  88(a0)
    ld  x12,  96(a0)
    ld  x13,  104(a0)
    ld  x14,  112(a0)
    ld  x15,  120(a0)
    ld  x16,  128(a0)
    ld  x17,  136(a0)

    ld  x18,  144(a0)
    ld  x19,  152(a0)
    ld  x20,  160(a0)
    ld  x21,  168(a0)
    ld  x22,  176(a0)
    ld  x23,  184(a0)
    ld  x24,  192(a0)
    ld  x25,  200(a0)
    ld  x26,  208(a0)
    ld  x27,  216(a0)
    ld  x28,  224(a0)
    ld  x29,  232(a0)
    ld  x30,  240(a0)
    ld  x31,  248(a0)

    // Load SP to t0
    ld  t0, 16(a0)

    // Load PC to t1, it is at the end.
    ld t1, (32*8)(a0)

    // Load a0.
    ld  a0,  64(a0)

    // Set SP. Do not access fprs_ and gprs_ from now, they are below SP.
    mv sp, t0

    jr  t1
END art_quick_do_long_jump

UNDEFINED art_quick_alloc_array_resolved_dlmalloc
UNDEFINED art_quick_alloc_array_resolved_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved8_dlmalloc
UNDEFINED art_quick_alloc_array_resolved8_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved16_dlmalloc
UNDEFINED art_quick_alloc_array_resolved16_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved32_dlmalloc
UNDEFINED art_quick_alloc_array_resolved32_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved64_dlmalloc
UNDEFINED art_quick_alloc_array_resolved64_dlmalloc_instrumented
UNDEFINED art_quick_alloc_object_resolved_dlmalloc
UNDEFINED art_quick_alloc_object_resolved_dlmalloc_instrumented
UNDEFINED art_quick_alloc_object_initialized_dlmalloc
UNDEFINED art_quick_alloc_object_initialized_dlmalloc_instrumented
UNDEFINED art_quick_alloc_object_with_checks_dlmalloc
UNDEFINED art_quick_alloc_object_with_checks_dlmalloc_instrumented
UNDEFINED art_quick_alloc_string_object_dlmalloc
UNDEFINED art_quick_alloc_string_object_dlmalloc_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_dlmalloc
UNDEFINED art_quick_alloc_string_from_bytes_dlmalloc_instrumented
UNDEFINED art_quick_alloc_string_from_chars_dlmalloc
UNDEFINED art_quick_alloc_string_from_chars_dlmalloc_instrumented
UNDEFINED art_quick_alloc_string_from_string_dlmalloc
UNDEFINED art_quick_alloc_string_from_string_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved_rosalloc
UNDEFINED art_quick_alloc_array_resolved_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved8_rosalloc
UNDEFINED art_quick_alloc_array_resolved8_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved16_rosalloc
UNDEFINED art_quick_alloc_array_resolved16_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved32_rosalloc
UNDEFINED art_quick_alloc_array_resolved32_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved64_rosalloc
UNDEFINED art_quick_alloc_array_resolved64_rosalloc_instrumented
UNDEFINED art_quick_alloc_object_resolved_rosalloc
UNDEFINED art_quick_alloc_object_resolved_rosalloc_instrumented
UNDEFINED art_quick_alloc_object_initialized_rosalloc
UNDEFINED art_quick_alloc_object_initialized_rosalloc_instrumented
UNDEFINED art_quick_alloc_object_with_checks_rosalloc
UNDEFINED art_quick_alloc_object_with_checks_rosalloc_instrumented
UNDEFINED art_quick_alloc_string_object_rosalloc
UNDEFINED art_quick_alloc_string_object_rosalloc_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_rosalloc
UNDEFINED art_quick_alloc_string_from_bytes_rosalloc_instrumented
UNDEFINED art_quick_alloc_string_from_chars_rosalloc
UNDEFINED art_quick_alloc_string_from_chars_rosalloc_instrumented
UNDEFINED art_quick_alloc_string_from_string_rosalloc
UNDEFINED art_quick_alloc_string_from_string_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved_bump_pointer
UNDEFINED art_quick_alloc_array_resolved_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved8_bump_pointer
UNDEFINED art_quick_alloc_array_resolved8_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved16_bump_pointer
UNDEFINED art_quick_alloc_array_resolved16_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved32_bump_pointer
UNDEFINED art_quick_alloc_array_resolved32_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved64_bump_pointer
UNDEFINED art_quick_alloc_array_resolved64_bump_pointer_instrumented
UNDEFINED art_quick_alloc_object_resolved_bump_pointer
UNDEFINED art_quick_alloc_object_resolved_bump_pointer_instrumented
UNDEFINED art_quick_alloc_object_initialized_bump_pointer
UNDEFINED art_quick_alloc_object_initialized_bump_pointer_instrumented
UNDEFINED art_quick_alloc_object_with_checks_bump_pointer
UNDEFINED art_quick_alloc_object_with_checks_bump_pointer_instrumented
UNDEFINED art_quick_alloc_string_object_bump_pointer
UNDEFINED art_quick_alloc_string_object_bump_pointer_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_bump_pointer
UNDEFINED art_quick_alloc_string_from_bytes_bump_pointer_instrumented
UNDEFINED art_quick_alloc_string_from_chars_bump_pointer
UNDEFINED art_quick_alloc_string_from_chars_bump_pointer_instrumented
UNDEFINED art_quick_alloc_string_from_string_bump_pointer
UNDEFINED art_quick_alloc_string_from_string_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved_tlab
UNDEFINED art_quick_alloc_array_resolved_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved8_tlab
UNDEFINED art_quick_alloc_array_resolved8_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved16_tlab
UNDEFINED art_quick_alloc_array_resolved16_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved32_tlab
UNDEFINED art_quick_alloc_array_resolved32_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved64_tlab
UNDEFINED art_quick_alloc_array_resolved64_tlab_instrumented
UNDEFINED art_quick_alloc_object_resolved_tlab
UNDEFINED art_quick_alloc_object_resolved_tlab_instrumented
UNDEFINED art_quick_alloc_object_initialized_tlab
UNDEFINED art_quick_alloc_object_initialized_tlab_instrumented
UNDEFINED art_quick_alloc_object_with_checks_tlab
UNDEFINED art_quick_alloc_object_with_checks_tlab_instrumented
UNDEFINED art_quick_alloc_string_object_tlab
UNDEFINED art_quick_alloc_string_object_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_tlab
UNDEFINED art_quick_alloc_string_from_bytes_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_chars_tlab
UNDEFINED art_quick_alloc_string_from_chars_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_string_tlab
UNDEFINED art_quick_alloc_string_from_string_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved_region
UNDEFINED art_quick_alloc_array_resolved_region_instrumented
UNDEFINED art_quick_alloc_array_resolved8_region
UNDEFINED art_quick_alloc_array_resolved8_region_instrumented
UNDEFINED art_quick_alloc_array_resolved16_region
UNDEFINED art_quick_alloc_array_resolved16_region_instrumented
UNDEFINED art_quick_alloc_array_resolved32_region
UNDEFINED art_quick_alloc_array_resolved32_region_instrumented
UNDEFINED art_quick_alloc_array_resolved64_region
UNDEFINED art_quick_alloc_array_resolved64_region_instrumented
UNDEFINED art_quick_alloc_object_resolved_region
UNDEFINED art_quick_alloc_object_resolved_region_instrumented
UNDEFINED art_quick_alloc_object_initialized_region
UNDEFINED art_quick_alloc_object_initialized_region_instrumented
UNDEFINED art_quick_alloc_object_with_checks_region
UNDEFINED art_quick_alloc_object_with_checks_region_instrumented
UNDEFINED art_quick_alloc_string_object_region
UNDEFINED art_quick_alloc_string_object_region_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_region
UNDEFINED art_quick_alloc_string_from_bytes_region_instrumented
UNDEFINED art_quick_alloc_string_from_chars_region
UNDEFINED art_quick_alloc_string_from_chars_region_instrumented
UNDEFINED art_quick_alloc_string_from_string_region
UNDEFINED art_quick_alloc_string_from_string_region_instrumented
UNDEFINED art_quick_alloc_array_resolved_region_tlab
UNDEFINED art_quick_alloc_array_resolved_region_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved8_region_tlab
UNDEFINED art_quick_alloc_array_resolved8_region_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved16_region_tlab
UNDEFINED art_quick_alloc_array_resolved16_region_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved32_region_tlab
UNDEFINED art_quick_alloc_array_resolved32_region_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved64_region_tlab
UNDEFINED art_quick_alloc_array_resolved64_region_tlab_instrumented
UNDEFINED art_quick_alloc_object_resolved_region_tlab
UNDEFINED art_quick_alloc_object_resolved_region_tlab_instrumented
UNDEFINED art_quick_alloc_object_initialized_region_tlab
UNDEFINED art_quick_alloc_object_initialized_region_tlab_instrumented
UNDEFINED art_quick_alloc_object_with_checks_region_tlab
UNDEFINED art_quick_alloc_object_with_checks_region_tlab_instrumented
UNDEFINED art_quick_alloc_string_object_region_tlab
UNDEFINED art_quick_alloc_string_object_region_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_region_tlab
UNDEFINED art_quick_alloc_string_from_bytes_region_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_chars_region_tlab
UNDEFINED art_quick_alloc_string_from_chars_region_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_string_region_tlab
UNDEFINED art_quick_alloc_string_from_string_region_tlab_instrumented
UNDEFINED art_quick_read_barrier_mark_reg00
UNDEFINED art_quick_read_barrier_mark_reg01
UNDEFINED art_quick_read_barrier_mark_reg02
UNDEFINED art_quick_read_barrier_mark_reg03
UNDEFINED art_quick_read_barrier_mark_reg04
UNDEFINED art_quick_read_barrier_mark_reg05
UNDEFINED art_quick_read_barrier_mark_reg06
UNDEFINED art_quick_read_barrier_mark_reg07
UNDEFINED art_quick_read_barrier_mark_reg08
UNDEFINED art_quick_read_barrier_mark_reg09
UNDEFINED art_quick_read_barrier_mark_reg10
UNDEFINED art_quick_read_barrier_mark_reg11
UNDEFINED art_quick_read_barrier_mark_reg12
UNDEFINED art_quick_read_barrier_mark_reg13
UNDEFINED art_quick_read_barrier_mark_reg14
UNDEFINED art_quick_read_barrier_mark_reg15
UNDEFINED art_quick_read_barrier_mark_reg16
UNDEFINED art_quick_read_barrier_mark_reg17
UNDEFINED art_quick_read_barrier_mark_reg18
UNDEFINED art_quick_read_barrier_mark_reg19
UNDEFINED art_quick_read_barrier_mark_reg20
UNDEFINED art_quick_read_barrier_mark_reg21
UNDEFINED art_quick_read_barrier_mark_reg22
UNDEFINED art_quick_read_barrier_mark_reg23
UNDEFINED art_quick_read_barrier_mark_reg24
UNDEFINED art_quick_read_barrier_mark_reg25
UNDEFINED art_quick_read_barrier_mark_reg26
UNDEFINED art_quick_read_barrier_mark_reg27
UNDEFINED art_quick_read_barrier_mark_reg28
UNDEFINED art_quick_read_barrier_mark_reg29
UNDEFINED art_quick_read_barrier_mark_reg30
UNDEFINED art_quick_read_barrier_mark_reg31
UNDEFINED art_quick_initialize_static_storage
UNDEFINED art_quick_resolve_type_and_verify_access
UNDEFINED art_quick_resolve_type
UNDEFINED art_quick_resolve_method_handle
UNDEFINED art_quick_resolve_method_type
UNDEFINED art_quick_resolve_string
UNDEFINED art_quick_set8_instance
UNDEFINED art_quick_set8_static
UNDEFINED art_quick_set16_instance
UNDEFINED art_quick_set16_static
UNDEFINED art_quick_set32_instance
UNDEFINED art_quick_set32_static
UNDEFINED art_quick_set64_instance
UNDEFINED art_quick_set64_static
UNDEFINED art_quick_set_obj_instance
UNDEFINED art_quick_set_obj_static
UNDEFINED art_quick_get_byte_instance
UNDEFINED art_quick_get_boolean_instance
UNDEFINED art_quick_get_short_instance
UNDEFINED art_quick_get_char_instance
UNDEFINED art_quick_get32_instance
UNDEFINED art_quick_get64_instance
UNDEFINED art_quick_get_obj_instance
UNDEFINED art_quick_get_byte_static
UNDEFINED art_quick_get_boolean_static
UNDEFINED art_quick_get_short_static
UNDEFINED art_quick_get_char_static
UNDEFINED art_quick_get32_static
UNDEFINED art_quick_get64_static
UNDEFINED art_quick_get_obj_static
UNDEFINED art_quick_lock_object_no_inline
UNDEFINED art_quick_lock_object
UNDEFINED art_quick_unlock_object_no_inline
UNDEFINED art_quick_unlock_object
UNDEFINED art_quick_invoke_direct_trampoline_with_access_check
UNDEFINED art_quick_invoke_interface_trampoline_with_access_check
UNDEFINED art_quick_invoke_static_trampoline_with_access_check
UNDEFINED art_quick_invoke_super_trampoline_with_access_check
UNDEFINED art_quick_invoke_virtual_trampoline_with_access_check
UNDEFINED art_quick_invoke_polymorphic
UNDEFINED art_quick_invoke_custom
UNDEFINED art_quick_test_suspend
UNDEFINED art_quick_deliver_exception
UNDEFINED art_quick_throw_array_bounds
UNDEFINED art_quick_throw_div_zero
UNDEFINED art_quick_throw_null_pointer_exception
UNDEFINED art_quick_throw_stack_overflow
UNDEFINED art_quick_throw_string_bounds
UNDEFINED art_quick_update_inline_cache
UNDEFINED art_quick_indexof


// Wrap ExecuteSwitchImpl in assembly method which specifies DEX PC for unwinding.
//  Argument 0: a0: The context pointer for ExecuteSwitchImpl.
//  Argument 1: a1: Pointer to the templated ExecuteSwitchImpl to call.
//  Argument 2: a2: The value of DEX PC (memory address of the methods bytecode).
UNDEFINED ExecuteSwitchImplAsm
