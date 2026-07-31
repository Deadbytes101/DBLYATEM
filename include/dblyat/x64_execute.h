#ifndef DBLYAT_X64_EXECUTE_H
#define DBLYAT_X64_EXECUTE_H

#include "dblyat/x64_interpret.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_x64_execute_status {
    DB_X64_EXECUTE_OK = 0,
    DB_X64_EXECUTE_NULL_ARGUMENT,
    DB_X64_EXECUTE_BUFFER_OVERLAP,
    DB_X64_EXECUTE_DECODE_FAILED,
    DB_X64_EXECUTE_INTERPRET_FAILED
} db_x64_execute_status;

typedef struct db_x64_execute_summary {
    db_x64_instruction instruction;
    db_x64_step_summary step;
    uint32_t reserved;
} db_x64_execute_summary;

db_x64_execute_status db_x64_execute_one(
    const uint8_t *bytes,
    size_t available_size,
    db_x64_scalar_state *state,
    db_x64_execute_summary *summary);

const char *db_x64_execute_status_name(db_x64_execute_status status);

#endif
