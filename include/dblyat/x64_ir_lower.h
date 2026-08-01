#ifndef DBLYAT_X64_IR_LOWER_H
#define DBLYAT_X64_IR_LOWER_H

#include "dblyat/ir.h"
#include "dblyat/x64_block.h"

#include <stdint.h>

typedef enum db_x64_ir_lower_status {
    DB_X64_IR_LOWER_OK = 0,
    DB_X64_IR_LOWER_NULL_ARGUMENT,
    DB_X64_IR_LOWER_BUFFER_OVERLAP,
    DB_X64_IR_LOWER_BAD_COUNT,
    DB_X64_IR_LOWER_OUTPUT_CAPACITY,
    DB_X64_IR_LOWER_BAD_RECORD,
    DB_X64_IR_LOWER_DISCONTINUOUS_BLOCK,
    DB_X64_IR_LOWER_TERMINATOR_ORDER,
    DB_X64_IR_LOWER_UNSUPPORTED_RECORD
} db_x64_ir_lower_status;

typedef struct db_x64_ir_lower_summary {
    uint64_t source_rip;
    uint64_t final_fallthrough_rip;
    uint32_t record_count;
    uint32_t instruction_count;
    db_ir_opcode terminator_opcode;
    uint8_t has_terminator;
    uint8_t reserved[7];
} db_x64_ir_lower_summary;

db_x64_ir_lower_status db_x64_ir_lower_block(
    const db_x64_block_record *records,
    uint32_t record_count,
    db_ir_instruction *instructions,
    uint32_t instruction_capacity,
    db_x64_ir_lower_summary *summary);

const char *db_x64_ir_lower_status_name(db_x64_ir_lower_status status);

#endif
