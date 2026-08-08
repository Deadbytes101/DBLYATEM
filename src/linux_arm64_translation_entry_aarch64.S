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

.macro db_helper_prologue
    stp x29, x30, [sp, #-16]!
    mov x29, sp
.endm

.macro db_helper_epilogue
    ldp x29, x30, [sp], #16
    ret
.endm

.global db_linux_arm64_translation_enter_asm
.type db_linux_arm64_translation_enter_asm, %function
db_linux_arm64_translation_enter_asm:
    stp x29, x30, [sp, #-48]!
    stp x19, x20, [sp, #16]
    str x21, [sp, #32]
    mov x29, sp
    mov x16, x0
    mov x19, x1
    mov x20, x2
    adr x21, .Ldb_linux_arm64_translation_block_return
    blr x16
.Ldb_linux_arm64_translation_block_return:
    ldr x21, [sp, #32]
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #48
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
    br x21
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
    br x21
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
    br x21
.size db_linux_arm64_translation_conditional_helper, .-db_linux_arm64_translation_conditional_helper

.align 2
.global db_linux_arm64_translation_trap_helper
.type db_linux_arm64_translation_trap_helper, %function
db_linux_arm64_translation_trap_helper:
    db_copy_planned_exit
    mov w9, #4
    str w9, [x20, #112]
    db_increment_terminal_count
    br x21
.size db_linux_arm64_translation_trap_helper, .-db_linux_arm64_translation_trap_helper

.align 2
.global db_linux_arm64_helper_form_guest_address
.type db_linux_arm64_helper_form_guest_address, %function
db_linux_arm64_helper_form_guest_address:
    db_helper_prologue
    mov x9, x0
    mov x10, x1
    mov x11, x2
    mov x12, x3
    mov x13, x4
    mov x14, x5
    mov x0, x19
    mov x1, x20
    mov x2, x9
    mov x3, x10
    mov x4, x11
    mov x5, x12
    mov x6, x13
    mov x7, x14
    bl db_linux_arm64_helper_form_guest_address_core
    mov x16, x0
    db_helper_epilogue
.size db_linux_arm64_helper_form_guest_address, .-db_linux_arm64_helper_form_guest_address

.align 2
.global db_linux_arm64_helper_read_guest_u64
.type db_linux_arm64_helper_read_guest_u64, %function
db_linux_arm64_helper_read_guest_u64:
    db_helper_prologue
    mov x1, x16
    mov x0, x20
    bl db_linux_arm64_helper_read_guest_u64_core
    mov x15, x0
    mov x17, x0
    db_helper_epilogue
.size db_linux_arm64_helper_read_guest_u64, .-db_linux_arm64_helper_read_guest_u64

.align 2
.global db_linux_arm64_helper_write_guest_u64
.type db_linux_arm64_helper_write_guest_u64, %function
db_linux_arm64_helper_write_guest_u64:
    db_helper_prologue
    mov x2, x17
    mov x1, x16
    mov x0, x20
    bl db_linux_arm64_helper_write_guest_u64_core
    db_helper_epilogue
.size db_linux_arm64_helper_write_guest_u64, .-db_linux_arm64_helper_write_guest_u64

.align 2
.global db_linux_arm64_helper_push_guest_u64
.type db_linux_arm64_helper_push_guest_u64, %function
db_linux_arm64_helper_push_guest_u64:
    db_helper_prologue
    mov x2, x17
    mov x1, x20
    mov x0, x19
    bl db_linux_arm64_helper_push_guest_u64_core
    db_helper_epilogue
.size db_linux_arm64_helper_push_guest_u64, .-db_linux_arm64_helper_push_guest_u64

.align 2
.global db_linux_arm64_helper_pop_guest_u64
.type db_linux_arm64_helper_pop_guest_u64, %function
db_linux_arm64_helper_pop_guest_u64:
    db_helper_prologue
    mov x1, x20
    mov x0, x19
    bl db_linux_arm64_helper_pop_guest_u64_core
    mov x15, x0
    mov x17, x0
    db_helper_epilogue
.size db_linux_arm64_helper_pop_guest_u64, .-db_linux_arm64_helper_pop_guest_u64

.align 2
.global db_linux_arm64_helper_update_logic_flags
.type db_linux_arm64_helper_update_logic_flags, %function
db_linux_arm64_helper_update_logic_flags:
    db_helper_prologue
    mov x9, x0
    mov x10, x1
    mov x0, x19
    mov x1, x20
    mov x2, x9
    mov x3, x10
    bl db_linux_arm64_helper_update_logic_flags_core
    db_helper_epilogue
.size db_linux_arm64_helper_update_logic_flags, .-db_linux_arm64_helper_update_logic_flags

.align 2
.global db_linux_arm64_helper_test_guest_condition
.type db_linux_arm64_helper_test_guest_condition, %function
db_linux_arm64_helper_test_guest_condition:
    db_helper_prologue
    mov x9, x0
    mov x0, x19
    mov x1, x20
    mov x2, x9
    bl db_linux_arm64_helper_test_guest_condition_core
    mov x15, x0
    db_helper_epilogue
.size db_linux_arm64_helper_test_guest_condition, .-db_linux_arm64_helper_test_guest_condition

.section .note.GNU-stack,"",%progbits
