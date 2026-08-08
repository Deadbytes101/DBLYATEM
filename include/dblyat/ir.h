#ifndef DBLYAT_IR_H
#define DBLYAT_IR_H

#include <stdint.h>

#define DB_IR_REGISTER_NONE UINT8_C(0xff)
#define DB_IR_CONDITION_NONE UINT8_C(0xff)

typedef enum db_ir_opcode {
    DB_IR_OPCODE_NOP = 0,
    DB_IR_OPCODE_MOVE_IMMEDIATE,
    DB_IR_OPCODE_MOVE_REGISTER,
    DB_IR_OPCODE_XOR,
    DB_IR_OPCODE_LOAD_U64,
    DB_IR_OPCODE_STORE_U64,
    DB_IR_OPCODE_CALL_DIRECT,
    DB_IR_OPCODE_CALL_INDIRECT,
    DB_IR_OPCODE_RETURN,
    DB_IR_OPCODE_JUMP_DIRECT,
    DB_IR_OPCODE_JUMP_INDIRECT,
    DB_IR_OPCODE_JUMP_CONDITIONAL,
    DB_IR_OPCODE_TRAP
} db_ir_opcode;

typedef enum db_ir_width {
    DB_IR_WIDTH_NONE = 0,
    DB_IR_WIDTH_32 = 32,
    DB_IR_WIDTH_64 = 64
} db_ir_width;

typedef enum db_ir_condition {
    DB_IR_CONDITION_OVERFLOW = 0,
    DB_IR_CONDITION_NOT_OVERFLOW,
    DB_IR_CONDITION_BELOW,
    DB_IR_CONDITION_ABOVE_OR_EQUAL,
    DB_IR_CONDITION_EQUAL,
    DB_IR_CONDITION_NOT_EQUAL,
    DB_IR_CONDITION_BELOW_OR_EQUAL,
    DB_IR_CONDITION_ABOVE,
    DB_IR_CONDITION_SIGN,
    DB_IR_CONDITION_NOT_SIGN,
    DB_IR_CONDITION_PARITY,
    DB_IR_CONDITION_NOT_PARITY,
    DB_IR_CONDITION_LESS,
    DB_IR_CONDITION_GREATER_OR_EQUAL,
    DB_IR_CONDITION_LESS_OR_EQUAL,
    DB_IR_CONDITION_GREATER
} db_ir_condition;

typedef enum db_ir_trap {
    DB_IR_TRAP_NONE = 0,
    DB_IR_TRAP_BREAKPOINT,
    DB_IR_TRAP_INVALID_OPCODE,
    DB_IR_TRAP_SYSCALL
} db_ir_trap;

typedef enum db_ir_address_mode {
    DB_IR_ADDRESS_NONE = 0,
    DB_IR_ADDRESS_BASE_INDEX,
    DB_IR_ADDRESS_IP_RELATIVE,
    DB_IR_ADDRESS_ABSOLUTE
} db_ir_address_mode;

typedef enum db_ir_flag_effect {
    DB_IR_FLAG_EFFECT_NONE = 0,
    DB_IR_FLAG_EFFECT_LOGIC_RESULT
} db_ir_flag_effect;

typedef struct db_ir_address {
    int64_t displacement;
    db_ir_address_mode mode;
    uint8_t base_register;
    uint8_t index_register;
    uint8_t scale;
    uint8_t reserved[5];
} db_ir_address;

typedef struct db_ir_instruction {
    uint64_t source_rip;
    uint64_t fallthrough_rip;
    uint64_t direct_target;
    uint64_t immediate;
    db_ir_address address;
    db_ir_opcode opcode;
    db_ir_width width;
    db_ir_condition condition;
    db_ir_trap trap;
    db_ir_flag_effect flag_effect;
    uint8_t destination_register;
    uint8_t source_register;
    uint8_t has_direct_target;
    uint8_t reserved[5];
} db_ir_instruction;

const char *db_ir_opcode_name(db_ir_opcode opcode);
const char *db_ir_condition_name(db_ir_condition condition);
const char *db_ir_trap_name(db_ir_trap trap);

#endif
