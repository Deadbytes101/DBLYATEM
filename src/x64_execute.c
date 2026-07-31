#include "dblyat/x64_execute.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_x64_execute_overlap(const void *left,
                                   size_t left_size,
                                   const void *right,
                                   size_t right_size)
{
    const uintptr_t left_address = (uintptr_t)left;
    const uintptr_t right_address = (uintptr_t)right;

    if (left_size == 0u || right_size == 0u) {
        return false;
    }

    if (left_address <= right_address) {
        return right_address - left_address < left_size;
    }

    return left_address - right_address < right_size;
}

db_x64_execute_status db_x64_execute_one(
    const uint8_t *bytes,
    size_t available_size,
    db_x64_scalar_state *state,
    db_x64_execute_summary *summary)
{
    db_x64_execute_summary completed;
    db_x64_scalar_state planned_state;
    db_x64_decode_status decode_status;
    db_x64_interpret_status interpret_status;

    if (bytes == NULL || state == NULL || summary == NULL) {
        return DB_X64_EXECUTE_NULL_ARGUMENT;
    }

    if (db_x64_execute_overlap(bytes,
                               available_size,
                               state,
                               sizeof(*state)) ||
        db_x64_execute_overlap(bytes,
                               available_size,
                               summary,
                               sizeof(*summary)) ||
        db_x64_execute_overlap(state,
                               sizeof(*state),
                               summary,
                               sizeof(*summary))) {
        return DB_X64_EXECUTE_BUFFER_OVERLAP;
    }

    decode_status = db_x64_decode_one(bytes,
                                      available_size,
                                      &completed.instruction);
    if (decode_status != DB_X64_DECODE_OK) {
        return DB_X64_EXECUTE_DECODE_FAILED;
    }

    planned_state = *state;
    interpret_status = db_x64_interpret_one(&completed.instruction,
                                            &planned_state,
                                            &completed.step);
    if (interpret_status != DB_X64_INTERPRET_OK) {
        return DB_X64_EXECUTE_INTERPRET_FAILED;
    }

    completed.reserved = 0u;
    *state = planned_state;
    *summary = completed;
    return DB_X64_EXECUTE_OK;
}

const char *db_x64_execute_status_name(db_x64_execute_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "x86-64 decode failed",
        "x86-64 interpretation failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 execute status";
    }

    return names[(size_t)status];
}
