#ifndef DBLYAT_X64_STACK_EXECUTE_H
#define DBLYAT_X64_STACK_EXECUTE_H

#include "dblyat/x64_interpret.h"
#include "dblyat/x64_memory_view.h"

#include <stddef.h>
#include <stdint.h>

#define DB_X64_STACK_POINTER_REGISTER UINT8_C(4)

typedef enum db_x64_stack_execute_status {
    DB_X64_STACK_EXECUTE_OK = 0,
    DB_X64_STACK_EXECUTE_NULL_ARGUMENT,
    DB_X64_STACK_EXECUTE_BUFFER_OVERLAP,
    DB_X64_STACK_EXECUTE_BAD_STATE,
    DB_X64_STACK_EXECUTE_DECODE_FAILED,
    DB_X64_STACK_EXECUTE_UNSUPPORTED_INSTRUCTION,
    DB_X64_STACK_EXECUTE_NONCANONICAL_ADDRESS,
    DB_X64_STACK_EXECUTE_ADDRESS_OVERFLOW,
    DB_X64_STACK_EXECUTE_MEMORY_FAILED
} db_x64_stack_execute_status;

typedef struct db_x64_stack_execute_summary {
    db_x64_instruction instruction;
    db_x64_memory_access_summary access;
    uint64_t source_rip;
    uint64_t fallthrough_rip;
    uint64_t next_rip;
    uint64_t previous_rsp;
    uint64_t next_rsp;
    uint64_t stack_value;
    uint64_t previous_rflags;
    uint64_t next_rflags;
    uint8_t memory_written;
    uint8_t reserved[7];
} db_x64_stack_execute_summary;

db_x64_stack_execute_status db_x64_stack_execute_one(
    const uint8_t *bytes,
    size_t available_size,
    db_x64_scalar_state *state,
    db_x64_memory_view *view,
    db_x64_stack_execute_summary *summary);

const char *db_x64_stack_execute_status_name(
    db_x64_stack_execute_status status);

#endif
