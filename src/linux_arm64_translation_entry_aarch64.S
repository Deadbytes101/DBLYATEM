.text
.align 2

.macro db_copy_planned_exit
    ldp x9, x10, [x20, #8]
    stp x9, x10, [x20, #72]
    ldp x9, x10, [x20, #24]
    stp x9, x10, [x20, #88]
    ldp x9, x10, [x20, #40]
    stp x9, x10, [x20, #104]
    ldp x9, x10, [x20, #56]
    stp x9, x10, [x20, #120]
.endm

.macro db_increment_terminal_count
    ldr x9, [x20]
    add x9, x9, #1
    str x9, [x20]
.endm

.global db_linux_arm64_translation_enter_asm
.type db_linux_arm64_translation_enter_asm, %function
db_linux_arm64_translation_enter_asm:
    stp x29, x30, [sp, #-32]!
    stp x19, x20, [sp, #16]
    mov x29, sp
    mov x16, x0
    mov x19, x1
    mov x20, x2
    blr x16
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #32
    ret
.size db_linux_arm64_translation_enter_asm, .-db_linux_arm64_translation_enter_asm

.align 2
.global db_linux_arm64_translation_return_helper
.type db_linux_arm64_translation_return_helper, %function
db_linux_arm64_translation_return_helper:
    db_copy_planned_exit
    mov w9, #1
    str w9, [x20, #112]
    db_increment_terminal_count
    ret
.size db_linux_arm64_translation_return_helper, .-db_linux_arm64_translation_return_helper

.align 2
.global db_linux_arm64_translation_target_helper
.type db_linux_arm64_translation_target_helper, %function
db_linux_arm64_translation_target_helper:
    db_copy_planned_exit
    mov w9, #2
    str w9, [x20, #112]
    str x15, [x20, #96]
    mov w9, #1
    strb w9, [x20, #128]
    db_increment_terminal_count
    ret
.size db_linux_arm64_translation_target_helper, .-db_linux_arm64_translation_target_helper

.align 2
.global db_linux_arm64_translation_conditional_helper
.type db_linux_arm64_translation_conditional_helper, %function
db_linux_arm64_translation_conditional_helper:
    db_copy_planned_exit
    mov w9, #3
    str w9, [x20, #112]
    str x15, [x20, #104]
    cbz x15, 1f
    ldr x10, [x20, #16]
    b 2f
1:
    ldr x10, [x20, #24]
2:
    str x10, [x20, #96]
    mov w9, #1
    strb w9, [x20, #128]
    strb w9, [x20, #129]
    strb w15, [x20, #130]
    strb w9, [x20, #131]
    db_increment_terminal_count
    ret
.size db_linux_arm64_translation_conditional_helper, .-db_linux_arm64_translation_conditional_helper

.align 2
.global db_linux_arm64_translation_trap_helper
.type db_linux_arm64_translation_trap_helper, %function
db_linux_arm64_translation_trap_helper:
    db_copy_planned_exit
    mov w9, #4
    str w9, [x20, #112]
    db_increment_terminal_count
    ret
.size db_linux_arm64_translation_trap_helper, .-db_linux_arm64_translation_trap_helper

.section .note.GNU-stack,"",%progbits
