#ifndef DBLYAT_X64_BLOCK_H
#define DBLYAT_X64_BLOCK_H

#include "dblyat/x64_decode.h"
#include "dblyat/x64_memory_instruction.h"
#include "dblyat/x64_run.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_x64_block_status {
    DB_X64_BLOCK_OK = 0,
    DB_X64_BLOCK_NULL_ARGUMENT,
    DB_X64_BLOCK_BUFFER_OVERLAP,
    DB_X64_BLOCK_BAD_CODE_VIEW,
    DB_X64_BLOCK_BAD_START_RIP,
    DB_X64_BLOCK_BAD_LIMIT
} db_x64_block_status;

typedef enum db_x64_block_stop_reason {
    DB_X64_BLOCK_STOP_TERMINATOR = 0,
    DB_X64_BLOCK_STOP_INSTRUCTION_BUDGET,
    DB_X64_BLOCK_STOP_BYTE_BUDGET,
    DB_X64_BLOCK_STOP_OUTPUT_CAPACITY,
    DB_X64_BLOCK_STOP_CODE_OUT_OF_BOUNDS,
    DB_X64_BLOCK_STOP_DECODE_FAILED,
    DB_X64_BLOCK_STOP_ADDRESS_INVALID
} db_x64_block_stop_reason;

typedef enum db_x64_block_decode_path {
    DB_X64_BLOCK_PATH_SCALAR = 0,
    DB_X64_BLOCK_PATH_MEMORY
} db_x64_block_decode_path;

typedef enum db_x64_block_control {
    DB_X64_BLOCK_CONTROL_LINEAR = 0,
    DB_X64_BLOCK_CONTROL_RETURN,
    DB_X64_BLOCK_CONTROL_DIRECT_CALL,
    DB_X64_BLOCK_CONTROL_DIRECT_JUMP,
    DB_X64_BLOCK_CONTROL_CONDITIONAL_JUMP,
    DB_X64_BLOCK_CONTROL_TRAP
} db_x64_block_control;

typedef struct db_x64_block_record {
    uint64_t rip;
    uint64_t fallthrough_rip;
    uint64_t direct_target;
    db_x64_block_decode_path path;
    db_x64_block_control control;
    db_x64_instruction scalar;
    db_x64_memory_instruction memory;
    uint8_t has_direct_target;
    uint8_t reserved[7];
} db_x64_block_record;

typedef struct db_x64_block_summary {
    uint64_t source_rip;
    uint64_t final_fallthrough_rip;
    uint32_t instruction_budget;
    uint32_t byte_budget;
    uint32_t output_capacity;
    uint32_t record_count;
    uint32_t byte_count;
    db_x64_block_stop_reason stop_reason;
    db_x64_decode_status scalar_decode_status;
    db_x64_memory_instruction_status memory_decode_status;
    uint8_t reserved[8];
} db_x64_block_summary;

db_x64_block_status db_x64_block_build(
    const db_x64_code_view *code,
    uint64_t start_rip,
    uint32_t instruction_budget,
    uint32_t byte_budget,
    db_x64_block_record *records,
    uint32_t record_capacity,
    db_x64_block_summary *summary);

const char *db_x64_block_status_name(db_x64_block_status status);
const char *db_x64_block_stop_reason_name(db_x64_block_stop_reason reason);
const char *db_x64_block_decode_path_name(db_x64_block_decode_path path);
const char *db_x64_block_control_name(db_x64_block_control control);

#endif
