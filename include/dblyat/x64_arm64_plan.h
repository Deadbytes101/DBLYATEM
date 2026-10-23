#ifndef DBLYAT_X64_ARM64_PLAN_H
#define DBLYAT_X64_ARM64_PLAN_H

#include "dblyat/arm64_select.h"
#include "dblyat/x64_interpret.h"

#include <stdint.h>

#define DB_X64_ARM64_HELPER_OPERAND_ABI_VERSION UINT32_C(1)
#define DB_X64_ARM64_HELPER_ARG0 UINT8_C(0)
#define DB_X64_ARM64_HELPER_ARG1 UINT8_C(1)
#define DB_X64_ARM64_HELPER_ARG2 UINT8_C(2)
#define DB_X64_ARM64_HELPER_ARG3 UINT8_C(3)
#define DB_X64_ARM64_HELPER_ARG4 UINT8_C(4)
#define DB_X64_ARM64_HELPER_ARG5 UINT8_C(5)
#define DB_X64_ARM64_HOST_TARGET UINT8_C(15)
#define DB_X64_ARM64_HOST_PRIMARY UINT8_C(16)
#define DB_X64_ARM64_HOST_SECONDARY UINT8_C(17)
#define DB_X64_ARM64_HOST_STATE UINT8_C(19)
#define DB_X64_ARM64_HOST_CONTEXT UINT8_C(20)
#define DB_X64_ARM64_HOST_LINK UINT8_C(30)
#define DB_X64_ARM64_HOST_SP_ZR UINT8_C(31)
#define DB_X64_ARM64_HOST_NONE UINT8_C(0xff)
#define DB_X64_ARM64_GUEST_NONE UINT8_C(0xff)

typedef enum db_x64_arm64_plan_opcode {
    DB_X64_ARM64_PLAN_NOP = 0,
    DB_X64_ARM64_PLAN_LOAD_STATE_U64,
    DB_X64_ARM64_PLAN_STORE_STATE_U64,
    DB_X64_ARM64_PLAN_MOVZ_U16,
    DB_X64_ARM64_PLAN_MOVK_U16,
    DB_X64_ARM64_PLAN_EOR_REGISTER,
    DB_X64_ARM64_PLAN_CALL_HELPER,
    DB_X64_ARM64_PLAN_TAIL_HELPER
} db_x64_arm64_plan_opcode;

typedef enum db_x64_arm64_helper {
    DB_X64_ARM64_HELPER_NONE = 0,
    DB_X64_ARM64_HELPER_FORM_GUEST_ADDRESS,
    DB_X64_ARM64_HELPER_READ_GUEST_U64,
    DB_X64_ARM64_HELPER_WRITE_GUEST_U64,
    DB_X64_ARM64_HELPER_PUSH_GUEST_U64,
    DB_X64_ARM64_HELPER_POP_GUEST_U64,
    DB_X64_ARM64_HELPER_UPDATE_LOGIC_FLAGS,
    DB_X64_ARM64_HELPER_TEST_GUEST_CONDITION,
    DB_X64_ARM64_HELPER_DISPATCH_DIRECT,
    DB_X64_ARM64_HELPER_DISPATCH_TARGET,
    DB_X64_ARM64_HELPER_DISPATCH_CONDITIONAL,
    DB_X64_ARM64_HELPER_RAISE_TRAP,
    DB_X64_ARM64_HELPER_SUB_GUEST_IMM8
} db_x64_arm64_helper;

typedef struct db_x64_arm64_plan_record {
    uint64_t source_rip;
    uint64_t value;
    int64_t displacement;
    uint32_t source_select_index;
    uint32_t guest_state_offset;
    db_x64_arm64_plan_opcode opcode;
    db_x64_arm64_helper helper;
    db_ir_width width;
    db_ir_condition condition;
    db_ir_trap trap;
    db_ir_address_mode address_mode;
    uint16_t immediate16;
    uint8_t shift;
    uint8_t host_destination;
    uint8_t host_source;
    uint8_t host_auxiliary;
    uint8_t guest_register;
    uint8_t base_guest_register;
    uint8_t index_guest_register;
    uint8_t scale;
    uint8_t terminal;
    uint8_t reserved[4];
} db_x64_arm64_plan_record;

typedef enum db_x64_arm64_plan_status {
    DB_X64_ARM64_PLAN_OK = 0,
    DB_X64_ARM64_PLAN_NULL_ARGUMENT,
    DB_X64_ARM64_PLAN_BUFFER_OVERLAP,
    DB_X64_ARM64_PLAN_BAD_COUNT,
    DB_X64_ARM64_PLAN_BAD_SELECTION,
    DB_X64_ARM64_PLAN_BAD_GROUPING,
    DB_X64_ARM64_PLAN_TERMINATOR_ORDER,
    DB_X64_ARM64_PLAN_OUTPUT_CAPACITY,
    DB_X64_ARM64_PLAN_COUNT_OVERFLOW,
    DB_X64_ARM64_PLAN_STATE_OFFSET_OVERFLOW
} db_x64_arm64_plan_status;

typedef struct db_x64_arm64_plan_summary {
    uint64_t source_rip;
    uint32_t selection_count;
    uint32_t plan_count;
    uint32_t required_capacity;
    db_x64_arm64_helper terminal_helper;
    uint8_t has_terminal;
    uint8_t reserved[7];
} db_x64_arm64_plan_summary;

db_x64_arm64_plan_status db_x64_arm64_plan_block(
    const db_arm64_select_record *selections,
    uint32_t selection_count,
    db_x64_arm64_plan_record *records,
    uint32_t record_capacity,
    db_x64_arm64_plan_summary *summary);

const char *db_x64_arm64_plan_status_name(db_x64_arm64_plan_status status);
const char *db_x64_arm64_plan_opcode_name(db_x64_arm64_plan_opcode opcode);
const char *db_x64_arm64_helper_name(db_x64_arm64_helper helper);

#endif
