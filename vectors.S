// Hudson Strauss
// AArch64 exception vector table
// 16 entries, 0x80 bytes each, 0x800-aligned
// Groups: current EL SP0, current EL SPx, lower EL AArch64, lower EL AArch32

.macro VECTOR_ENTRY label
.balign 0x80
\label:
.endm

// save x0-x30, elr, esr, far onto the stack (272 bytes)
.macro SAVE_CONTEXT
    sub sp, sp, #272
    stp x0,  x1,  [sp, #0]
    stp x2,  x3,  [sp, #16]
    stp x4,  x5,  [sp, #32]
    stp x6,  x7,  [sp, #48]
    stp x8,  x9,  [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30,      [sp, #240]
    mrs x0, ELR_EL2
    mrs x1, ESR_EL2
    mrs x2, FAR_EL2
    stp x0, x1,   [sp, #248]
    str x2,       [sp, #264]
.endm

.macro RESTORE_CONTEXT
    ldp x0, x1,   [sp, #248]
    msr ELR_EL2, x0
    ldp x0,  x1,  [sp, #0]
    ldp x2,  x3,  [sp, #16]
    ldp x4,  x5,  [sp, #32]
    ldp x6,  x7,  [sp, #48]
    ldp x8,  x9,  [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30,      [sp, #240]
    add sp, sp, #272
.endm

// ---------- vector table ----------
.section ".text"
.balign 0x800
.global exception_vector_table
exception_vector_table:

// --- current EL with SP0 (not used, we use SPx) ---
VECTOR_ENTRY curr_el_sp0_sync
    SAVE_CONTEXT
    mov x0, #0
    mov x1, sp
    bl  exception_handler
    b   halt

VECTOR_ENTRY curr_el_sp0_irq
    SAVE_CONTEXT
    bl  irq_handler
    RESTORE_CONTEXT
    eret

VECTOR_ENTRY curr_el_sp0_fiq
    SAVE_CONTEXT
    mov x0, #2
    mov x1, sp
    bl  exception_handler
    b   halt

VECTOR_ENTRY curr_el_sp0_serror
    SAVE_CONTEXT
    mov x0, #3
    mov x1, sp
    bl  exception_handler
    b   halt

// --- current EL with SPx (this is what fires) ---
VECTOR_ENTRY curr_el_spx_sync
    SAVE_CONTEXT
    mov x0, #0
    mov x1, sp
    bl  exception_handler
    b   halt

VECTOR_ENTRY curr_el_spx_irq
    SAVE_CONTEXT
    bl  irq_handler
    RESTORE_CONTEXT
    eret

VECTOR_ENTRY curr_el_spx_fiq
    SAVE_CONTEXT
    mov x0, #2
    mov x1, sp
    bl  exception_handler
    b   halt

VECTOR_ENTRY curr_el_spx_serror
    SAVE_CONTEXT
    mov x0, #3
    mov x1, sp
    bl  exception_handler
    b   halt

// --- lower EL using AArch64 ---
VECTOR_ENTRY lower_el_a64_sync
    SAVE_CONTEXT
    mov x0, #0
    mov x1, sp
    bl  exception_handler
    b   halt

VECTOR_ENTRY lower_el_a64_irq
    SAVE_CONTEXT
    bl  irq_handler
    RESTORE_CONTEXT
    eret

VECTOR_ENTRY lower_el_a64_fiq
    SAVE_CONTEXT
    mov x0, #2
    mov x1, sp
    bl  exception_handler
    b   halt

VECTOR_ENTRY lower_el_a64_serror
    SAVE_CONTEXT
    mov x0, #3
    mov x1, sp
    bl  exception_handler
    b   halt

// --- lower EL using AArch32 ---
VECTOR_ENTRY lower_el_a32_sync
    SAVE_CONTEXT
    mov x0, #0
    mov x1, sp
    bl  exception_handler
    b   halt

VECTOR_ENTRY lower_el_a32_irq
    SAVE_CONTEXT
    bl  irq_handler
    RESTORE_CONTEXT
    eret

VECTOR_ENTRY lower_el_a32_fiq
    SAVE_CONTEXT
    mov x0, #2
    mov x1, sp
    bl  exception_handler
    b   halt

VECTOR_ENTRY lower_el_a32_serror
    SAVE_CONTEXT
    mov x0, #3
    mov x1, sp
    bl  exception_handler
    b   halt

halt:
    wfe
    b halt
