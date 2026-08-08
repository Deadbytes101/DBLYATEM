#ifndef DBLYAT_X64_ARM64_ENCODE_H
#define DBLYAT_X64_ARM64_ENCODE_H
#include "dblyat/x64_arm64_plan.h"
#include <stdint.h>
typedef enum db_x64_arm64_relocation_kind {
    DB_X64_ARM64_RELOCATION_CALL26 = 0, DB_X64_ARM64_RELOCATION_JUMP26
}
db_x64_arm64_relocation_kind;
typedef struct db_x64_arm64_relocation {
    uint64_t source_rip;
    uint64_t value;
    int64_t displacement;
    uint32_t word_index;
    uint32_t source_plan_index;
    db_x64_arm64_relocation_kind kind;
    db_x64_arm64_helper helper;
    db_ir_width width;
    db_ir_condition condition;
    db_ir_trap trap;
    db_ir_address_mode address_mode;
    uint8_t host_destination;
    uint8_t host_source;
    uint8_t host_auxiliary;
    uint8_t guest_register;
    uint8_t base_guest_register;
    uint8_t index_guest_register;
    uint8_t scale;
    uint8_t reserved[5];
}
db_x64_arm64_relocation;
typedef enum db_x64_arm64_encode_status {
    DB_X64_ARM64_ENCODE_OK = 0,
    DB_X64_ARM64_ENCODE_NULL_ARGUMENT,
    DB_X64_ARM64_ENCODE_BUFFER_OVERLAP,
    DB_X64_ARM64_ENCODE_BAD_COUNT,
    DB_X64_ARM64_ENCODE_BAD_PLAN,
    DB_X64_ARM64_ENCODE_BAD_GROUPING,
    DB_X64_ARM64_ENCODE_TERMINATOR_ORDER,
    DB_X64_ARM64_ENCODE_WORD_CAPACITY,
    DB_X64_ARM64_ENCODE_RELOCATION_CAPACITY,
    DB_X64_ARM64_ENCODE_BAD_HELPER_ABI
}
db_x64_arm64_encode_status;
typedef struct db_x64_arm64_encode_summary {
    uint64_t source_rip;
    uint32_t plan_count;
    uint32_t word_count;
    uint32_t relocation_count;
    db_x64_arm64_helper terminal_helper;
    uint8_t has_terminal;
    uint8_t reserved[7];
}
db_x64_arm64_encode_summary;
db_x64_arm64_encode_status db_x64_arm64_encode_block(const db_x64_arm64_plan_record *plans, uint32_t plan_count, uint32_t *words, uint32_t word_capacity, db_x64_arm64_relocation *relocations, uint32_t relocation_capacity, db_x64_arm64_encode_summary *summary);
const char *db_x64_arm64_encode_status_name(db_x64_arm64_encode_status status);
#endif
