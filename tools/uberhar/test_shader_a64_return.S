// CodexAstraLocal: A test-only ABI boundary saves its real caller, seeds every
// callee-saved register, and recovers its own SP from fixed private storage even
// when the old shader returns before its epilogue. No signal or device hook.
.text
.p2align 2
.global InvokeWithAbiCheck
.type InvokeWithAbiCheck, %function
InvokeWithAbiCheck:
    sub sp, sp, #224
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    stp q8, q9, [sp, #96]
    stp q10, q11, [sp, #128]
    stp q12, q13, [sp, #160]
    stp q14, q15, [sp, #192]
    adrp x16, bridge_stack
    add x16, x16, :lo12:bridge_stack
    mov x17, sp
    str x17, [x16]
    adrp x16, bridge_observation
    add x16, x16, :lo12:bridge_observation
    str x6, [x16]
    str x17, [x6, #0]
    adr x17, returned
    str x17, [x6, #16]
    mrs x17, fpcr
    str x17, [x6, #256]
    mrs x17, fpsr
    str x17, [x6, #272]
    mov x16, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov w3, w4
    mov x19, x5
    ldr x20, =0x2020202020202020
    ldr x21, =0x2121212121212121
    ldr x22, =0x2222222222222222
    ldr x23, =0x2323232323232323
    ldr x24, =0x2424242424242424
    ldr x25, =0x2525252525252525
    ldr x26, =0x2626262626262626
    ldr x27, =0x2727272727272727
    ldr x28, =0x2828282828282828
    ldr x29, =0x2929292929292929
    movi v8.16b, #8
    movi v9.16b, #9
    movi v10.16b, #10
    movi v11.16b, #11
    movi v12.16b, #12
    movi v13.16b, #13
    movi v14.16b, #14
    movi v15.16b, #15
    blr x16
returned:
    // CodexAstraLocal: Capture before repairing SP; using fixed globals avoids
    // following the corrupted test stack or mistaking recovery for JIT success.
    adrp x16, bridge_observation
    ldr x16, [x16, :lo12:bridge_observation]
    mov x17, sp
    str x17, [x16, #8]
    str x30, [x16, #24]
    stp x19, x20, [x16, #32]
    stp x21, x22, [x16, #48]
    stp x23, x24, [x16, #64]
    stp x25, x26, [x16, #80]
    stp x27, x28, [x16, #96]
    str x29, [x16, #112]
    stp q8, q9, [x16, #128]
    stp q10, q11, [x16, #160]
    stp q12, q13, [x16, #192]
    stp q14, q15, [x16, #224]
    mrs x17, fpcr
    str x17, [x16, #264]
    mrs x17, fpsr
    str x17, [x16, #280]
    adrp x16, bridge_stack
    ldr x17, [x16, :lo12:bridge_stack]
    mov sp, x17
    ldp q8, q9, [sp, #96]
    ldp q10, q11, [sp, #128]
    ldp q12, q13, [sp, #160]
    ldp q14, q15, [sp, #192]
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    add sp, sp, #224
    ret
.size InvokeWithAbiCheck, .-InvokeWithAbiCheck

.bss
.p2align 3
bridge_stack: .zero 8
bridge_observation: .zero 8
.section .note.GNU-stack,"",%progbits
