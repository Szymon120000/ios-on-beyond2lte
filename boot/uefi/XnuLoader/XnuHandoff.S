/* SPDX-License-Identifier: BSD-2-Clause */
#include <AsmMacroLib.h>

.text
/* x0 = kernel entry PA, x1 = boot_args PA, x2 = clean start,
 * x3 = clean end.  No stack, EFI call, console write or global cache
 * operation after this point. */
ASM_FUNC(B2LXnuHandoff)
    mov     x19, x0
    mov     x20, x1
    mov     x8, x2
    mov     x9, x3
    msr     daifset, #0xf
    dsb     sy

    mov     x0, x8
    mov     x1, x9
    bl      .Lclean_range

    mrs     x1, sctlr_el1
    bic     x1, x1, #1          // MMU off
    bic     x1, x1, #4          // D-cache off
    bic     x1, x1, #0x1000     // I-cache off
    dsb     sy
    msr     sctlr_el1, x1
    isb

    tlbi    vmalle1
    dsb     sy
    isb

    mov     x0, x20
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    br      x19

/* Clean data to PoC and invalidate instructions to PoU.  CTR_EL0 describes
 * the line sizes; no CSSELR/CSSIDR or set/way operations are used.
 * x0 start, x1 exclusive end; x2..x8 and flags clobbered. */
.Lclean_range:
    mov     x8, x0
    mrs     x2, ctr_el0
    ubfx    x3, x2, #16, #4
    mov     x4, #4
    lsl     x3, x4, x3
    sub     x5, x3, #1
    bic     x0, x0, x5
.Ldc_line:
    dc      cvac, x0
    add     x0, x0, x3
    cmp     x0, x1
    b.lo    .Ldc_line
    dsb     sy

    and     x3, x2, #0xf
    lsl     x3, x4, x3
    sub     x5, x3, #1
    bic     x0, x8, x5
.Lic_line:
    ic      ivau, x0
    add     x0, x0, x3
    cmp     x0, x1
    b.lo    .Lic_line
    dsb     sy
    isb
    ret
