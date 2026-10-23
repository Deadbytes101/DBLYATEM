#ifndef DBLYAT_ARM64_SELECT_H
#define DBLYAT_ARM64_SELECT_H

#include "dblyat/ir.h"

#include <stdint.h>

typedef enum db_arm64_select_opcode {
    DB_ARM64_SELECT_NOP = 0,
    DB_ARM64_SELECT_MATERIALIZE_U64,
    DB_ARM64_SELECT_MOVE_GUEST_GPR,
    DB_ARM64_SELECT_EOR_GUEST_GPR,
    DB_ARM64_SELECT_UPDATE_LOGIC_FLAGS,
    DB_ARM64_SELECT_FORM_GUEST_ADDRESS,
    DB_ARM64_SELECT_LOAD_U64,
    DB_ARM64_SELECT_LOAD_U64_TARGET,
    DB_ARM64_SELECT_STORE_U64,
    DB_ARM64_SELECT_PUSH_GUEST_U64,
    DB_ARM64_SELECT_POP_GUEST_U64,
    DB_ARM64_SELECT_BRANCH_GUEST_DIRECT,
    DB_ARM64_SELECT_BRANCH_GUEST_TARGET,
    DB_ARM64_SELECT_TEST_GUEST_CONDITION,
    DB_ARM64_SELECT_BRANCH_IF_TRUE,
    DB_ARM64_SELECT_RAISE_TRAP,
    DB_ARM64_SELECT_SUB_GUEST_IMM8
} db_arm64_select_opcode;

typedef enum db_arm64_select_temp {
    DB_ARM64_SELECT_TEMP_NONE = 0,
    DB_ARM64_SELECT_TEMP_ADDRESS,
    DB_ARM64_SELECT_TEMP_TARGET,
    DB_ARM64_SELECT_TEMP_PREDICATE
} db_arm64_select_temp;

typedef struct db_arm64_select_record {
    uint64_t source_rip;
    uint64_t value;
    int64_t displacement;
    uint32_t source_ir_index;
    db_arm64_select_opcode opcode;
    db_ir_width width;
    db_ir_condition condition;
    db_ir_trap trap;
    db_ir_address_mode address_mode;
    uint8_t destination_register;
    uint8_t source_register;
    uint8_t base_register;
    uint8_t index_register;
    uint8_t scale;
    db_arm64_select_temp temp;
    uint8_t reserved[2];
} db_arm64_select_record;

typedef enum db_arm64_select_status {
    DB_ARM64_SELECT_OK = 0,
    DB_ARM64_SELECT_NULL_ARGUMENT,
    DB_ARM64_SELECT_BUFFER_OVERLAP,
    DB_ARM64_SELECT_BAD_COUNT,
    DB_ARM64_SELECT_BAD_IR,
    DB_ARM64_SELECT_DISCONTINUOUS_IR,
    DB_ARM64_SELECT_TERMINATOR_ORDER,
    DB_ARM64_SELECT_OUTPUT_CAPACITY,
    DB_ARM64_SELECT_COUNT_OVERFLOW
} db_arm64_select_status;

typedef struct db_arm64_select_summary {
    uint64_t source_rip;
    uint64_t final_fallthrough_rip;
    uint32_t ir_count;
    uint32_t operation_count;
    uint32_t required_capacity;
    db_arm64_select_opcode terminator_opcode;
    uint8_t has_terminator;
    uint8_t reserved[7];
} db_arm64_select_summary;

db_arm64_select_status db_arm64_select_block(
    const db_ir_instruction *instructions,
    uint32_t instruction_count,
    db_arm64_select_record *records,
    uint32_t record_capacity,
    db_arm64_select_summary *summary);

const char *db_arm64_select_status_name(db_arm64_select_status status);
const char *db_arm64_select_opcode_name(db_arm64_select_opcode opcode);

#endif
