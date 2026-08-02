.text
.align 2

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
    ldr x9, [x20]
    add x9, x9, #1
    str x9, [x20]
    ret
.size db_linux_arm64_translation_return_helper, .-db_linux_arm64_translation_return_helper

.section .note.GNU-stack,"",%progbits
