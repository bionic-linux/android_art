@include "arm/unopWide.S" {"instr":"bl      __aeabi_d2lz"}
%include "arm/unopWide.S" {"instr":"bl      d2l_doconv"}

%break
/*
 * Convert the double in r0/r1 to a long in r0/r1.
 *
 * We have to clip values to long min/max per the specification.  The
 * expected common case is a "reasonable" value that converts directly
 * to modest integer.  The EABI convert function isn't doing this for us.
 */
d2l_doconv:
    sbfx    r2, r1, #20, #11          @ extract exponent; sign-extension helps with isNaN(x)
    movw    r3, #0x3c2                @ pow(2.0, 63.0) has exponent 0x43e, sign-extended -0x3c2
    cmn     r2, r3                    @ if (isNan(x) || abs(x) >= pow(2.0, 63.0))
    bhs     d2l_doconv_special_case   @   goto d2l_doconv_special_case
    b       __aeabi_d2lz              @ tail call to convert double to long

d2l_doconv_special_case:
    orrs    r0, r0, r1, lsl #12       @ Check if there are any non-zero significant bits.
    asr     r3, r1, #31               @ extract sign; needed at d2l_f2l_doconv_min_max
    beq     d2l_f2l_doconv_min_max    @ If all significant bits are 0, x may be Inf but not NaN.
    adds    r0, r2, #1                @ if (isNaN(x))
    beq     f2l_d2l_doconv_nan        @   goto f2l_d2l_doconv_nan (see op_float_to_long.S)
d2l_f2l_doconv_min_max:               @ Shared code for d2l_doconv and f2l_doconv.
    mvn     r0, #0                    @ result = (x >= 0.0) ? maxlong (0x7fffffffffffffff)
    mvn     r1, #0x80000000
    eor     r0, r3                    @                     : minlong (0x8000000000000000)
    eor     r1, r3
    bx      lr                        @ return result
