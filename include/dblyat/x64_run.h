#ifndef DBLYAT_X64_RUN_H
#define DBLYAT_X64_RUN_H

#include "dblyat/x64_execute.h"
#include "dblyat/x64_memory_execute.h"
#include "dblyat/x64_stack_execute.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_x64_run_status {
    DB_X64_RUN_OK = 0,
    DB_X64_RUN_NULL_ARGUMENT,
    DB_X64_RUN_BUFFER_OVERLAP,
    DB_X64_RUN_BAD_CODE_VIEW,
    DB_X64_RUN_BAD_STEP_BUDGET
} db_x64_run_status;

typedef enum db_x64_run_stop_reason {
    DB_X64_RUN_STOP_STEP_BUDGET = 0,
    DB_X64_RUN_STOP_CODE_OUT_OF_BOUNDS,
    DB_X64_RUN_STOP_DECODE_FAILED,
    DB_X64_RUN_STOP_EXECUTE_FAILED
} db_x64_run_stop_reason;

typedef enum db_x64_run_step_kind {
    DB_X64_RUN_STEP_NONE = 0,
    DB_X64_RUN_STEP_SCALAR,
    DB_X64_RUN_STEP_MEMORY,
    DB_X64_RUN_STEP_STACK
} db_x64_run_step_kind;

typedef struct db_x64_code_view {
    const uint8_t *bytes;
    size_t size;
    uint64_t guest_base;
    uint8_t reserved[8];
} db_x64_code_view;

typedef struct db_x64_run_summary {
    uint64_t source_rip;
    uint64_t final_rip;
    uint32_t step_budget;
    uint32_t completed_steps;
    db_x64_run_stop_reason stop_reason;
    db_x64_run_step_kind last_completed_kind;
    db_x64_run_step_kind attempted_kind;
    db_x64_decode_status scalar_decode_status;
    db_x64_memory_instruction_status memory_decode_status;
    db_x64_execute_status scalar_execute_status;
    db_x64_memory_execute_status memory_execute_status;
    db_x64_stack_execute_status stack_execute_status;
    uint8_t reserved[8];
} db_x64_run_summary;

db_x64_run_status db_x64_run_steps(
    const db_x64_code_view *code,
    uint32_t step_budget,
    db_x64_scalar_state *state,
    db_x64_memory_view *memory,
    db_x64_run_summary *summary);

const char *db_x64_run_status_name(db_x64_run_status status);
const char *db_x64_run_stop_reason_name(db_x64_run_stop_reason reason);
const char *db_x64_run_step_kind_name(db_x64_run_step_kind kind);

#endif
