#include "dblyat/win32_kernel32.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_win32_kernel32_mul_size(size_t left,
                                       size_t right,
                                       size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_win32_kernel32_overlap(const void *left,
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

static bool db_win32_kernel32_state_valid(
    const db_win32_kernel32_state *state)
{
    return state->process_id != 0u &&
           state->thread_id != 0u &&
           state->reserved == 0u;
}

static bool db_win32_kernel32_handler_known(uint32_t handler_id,
                                            size_t *expected_arguments)
{
    switch (handler_id) {
    case DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_PROCESS_ID:
    case DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_THREAD_ID:
    case DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR:
        *expected_arguments = 0u;
        return true;

    case DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR:
    case DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS:
        *expected_arguments = 1u;
        return true;

    default:
        return false;
    }
}

db_win32_kernel32_status db_win32_kernel32_execute(
    db_win32_kernel32_state *state,
    uint32_t handler_id,
    const uint64_t *arguments,
    size_t argument_count,
    db_win32_kernel32_result *result)
{
    db_win32_kernel32_state planned_state;
    db_win32_kernel32_result planned_result;
    size_t argument_bytes;
    size_t expected_arguments;

    if (state == NULL || result == NULL ||
        (argument_count != 0u && arguments == NULL)) {
        return DB_WIN32_KERNEL32_NULL_ARGUMENT;
    }

    if (!db_win32_kernel32_mul_size(argument_count,
                                    sizeof(*arguments),
                                    &argument_bytes)) {
        return DB_WIN32_KERNEL32_COUNT_OVERFLOW;
    }

    if (db_win32_kernel32_overlap(state, sizeof(*state),
                                  arguments, argument_bytes) ||
        db_win32_kernel32_overlap(state, sizeof(*state),
                                  result, sizeof(*result)) ||
        db_win32_kernel32_overlap(arguments, argument_bytes,
                                  result, sizeof(*result))) {
        return DB_WIN32_KERNEL32_BUFFER_OVERLAP;
    }

    if (!db_win32_kernel32_state_valid(state)) {
        return DB_WIN32_KERNEL32_BAD_STATE;
    }

    if (!db_win32_kernel32_handler_known(handler_id,
                                         &expected_arguments)) {
        return DB_WIN32_KERNEL32_BAD_HANDLER;
    }

    if (argument_count != expected_arguments) {
        return DB_WIN32_KERNEL32_ARGUMENT_COUNT_MISMATCH;
    }

    planned_state = *state;
    planned_result.raw_result = 0u;
    planned_result.handler_id = handler_id;
    planned_result.reserved = 0u;

    switch (handler_id) {
    case DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_PROCESS_ID:
        planned_result.raw_result = state->process_id;
        break;

    case DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_THREAD_ID:
        planned_result.raw_result = state->thread_id;
        break;

    case DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR:
        planned_result.raw_result = state->last_error;
        break;

    case DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR:
        if (arguments[0] > UINT32_MAX) {
            return DB_WIN32_KERNEL32_ARGUMENT_OUT_OF_RANGE;
        }
        planned_state.last_error = (uint32_t)arguments[0];
        break;

    case DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS:
        if (arguments[0] > UINT32_MAX) {
            return DB_WIN32_KERNEL32_ARGUMENT_OUT_OF_RANGE;
        }
        planned_result.raw_result = (uint32_t)arguments[0];
        break;

    default:
        return DB_WIN32_KERNEL32_BAD_HANDLER;
    }

    *state = planned_state;
    *result = planned_result;
    return DB_WIN32_KERNEL32_OK;
}

const char *db_win32_kernel32_status_name(db_win32_kernel32_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "bad KERNEL32 state",
        "bad KERNEL32 handler",
        "argument count mismatch",
        "argument out of range"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown KERNEL32 status";
    }

    return names[(size_t)status];
}
