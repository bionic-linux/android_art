%def op_invoke_custom():
    unimp

%def op_invoke_custom_range():
    unimp

// Input
//   - xPC, xSELF
// Output to helper
//   - a0: resolved ArtMethod*
//   - a1: "this" obj ptr
//   - a2: code unit #2 from invoke opcode
// Clobbers: t0, t1
%def invoke_direct_or_super(helper="", range="", is_super=""):
    EXPORT_PC
    FETCH_FROM_THREAD_CACHE /*resolved method*/a0, /*slow path*/3f, /*near*/1
1:
    // Load first argument, the 'this' pointer.
    FETCH a1, 2
    .if !$range
    andi a1, a1, 0xF
    .endif
    GET_VREG a1, a1
    beqz a1, 2f  // throw if null
    call $helper
    unimp  // no return
2:
    call common_errNullObject
    unimp  // no return
3:
    // Slow path: find resolved method with NterpGetMethod
    mv a0, xSELF
    ld a1, (sp)
    mv a2, xPC
    call nterp_get_method
    .if $is_super
    j 1b
    .else
    andi t0, a0, 0x1
    beqz t0, 1b
    // LSB set, this is a string init method. Remove the marker.
    andi a0, a0, ~0x1
    .if $range
    call NterpHandleStringInitRange
    .else
    call NterpHandleStringInit
    .endif  // range
    .endif  // is_super

// invoke-direct: arg count A, meth@BBBB, arg registers vC vD vE vF vG
// Format 35c: A|G|op, BBBB, F|E|D|C
%def op_invoke_direct():
%   invoke_direct_or_super(helper="NterpCommonInvokeInstance", range="0", is_super="0")

%def op_invoke_direct_range():
    unimp

%def op_invoke_super():
    unimp

%def op_invoke_super_range():
    unimp

%def op_invoke_polymorphic():
    unimp

%def op_invoke_polymorphic_range():
    unimp

%def invoke_interface(range=""):
    unimp

%def op_invoke_interface_slow_path():
    unimp

%def op_invoke_interface():
    unimp

%def op_invoke_interface_range():
    unimp

%def invoke_static(helper=""):
    unimp

%def op_invoke_static():
    unimp

%def op_invoke_static_range():
    unimp

%def invoke_virtual(helper="", range=""):
    unimp

%def op_invoke_virtual():
    unimp

%def op_invoke_virtual_range():
    unimp
