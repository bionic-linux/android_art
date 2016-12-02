@include "arm/unopWider.S" {"instr":"bl      __aeabi_f2lz"}
%include "arm/unopWider.S" {"instr":"bl      f2l_doconv"}

%break
/*
 * Convert the float in r0 to a long in r0/r1.
 *
 * We have to clip values to long min/max per the specification.  The
 * expected common case is a "reasonable" value that converts directly
 * to modest integer.  The EABI convert function isn't doing this for us.
 */
f2l_doconv:
    ubfx    r2, r0, #23, #8           @ extract exponent
    cmp     r2, #0xbe                 @ if (isNan(x) || abs(x) >= pow(2.0, 63.0))
    bhs     f2l_doconv_special_case   @   goto f2l_doconv_special_case
    b       __aeabi_f2lz              @ tail call to convert float to long

f2l_doconv_special_case:
    lsls    r1, r0, #9                @ Check if there are any non-zero significant bits.
    asr     r3, r0, #31               @ extract sign; needed at d2l_f2l_doconv_min_max
    beq     d2l_f2l_doconv_min_max    @ If all significant bits are 0, x may be Inf but not NaN.
    subs    r0, r2, #0xff             @ if (!isNaN(x))
    bne     d2l_f2l_doconv_min_max    @   goto d2l_f2l_doconv_min_max (see op_double_to_long.S)
f2l_d2l_doconv_nan:                   @ Shared code for f2l_doconv and d2l_doconv.
    mov     r1, #0                    @ result = 0; r0 already set to 0.
    bx      lr                        @ return result
