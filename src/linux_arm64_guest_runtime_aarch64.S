.text
.align 2

.macro db_helper_prologue
    stp x29, x30, [sp, #-16]!
    mov x29, sp
.endm

.macro db_helper_epilogue
    ldp x29, x30, [sp], #16
    ret
.endm

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
