#include "dblyat/win32_kernel32_invoke.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_win32_kernel32_invoke_overlap(const void *left,
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

static bool db_win32_kernel32_invoke_canonical(uint64_t value)
{
    const uint64_t upper = value >> 48u;
    const uint64_t sign = (value >> 47u) & 1u;

    return value != 0u &&
           ((sign == 0u && upper == 0u) ||
            (sign != 0u && upper == UINT64_C(0xffff)));
}

static bool db_win32_kernel32_invoke_identity_valid(
    const db_gateway_invoke_request *request)
{
    size_t expected_arguments;
    uint32_t expected_symbol;
    size_t index;

    if (!db_win32_kernel32_invoke_canonical(request->rip) ||
        request->slot_address != request->rip ||
        !db_win32_kernel32_invoke_canonical(request->guest_rsp) ||
        !db_win32_kernel32_invoke_canonical(request->return_address) ||
        request->slot_index == SIZE_MAX ||
        request->provider_id != DB_WIN32_PROVIDER_KERNEL32 ||
        request->reserved != 0u ||
        request->argument_count > DB_GATEWAY_FRAME_MAX_ARGUMENTS) {
        return false;
    }

    switch (request->handler_id) {
    case DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_PROCESS_ID:
        expected_symbol = DB_WIN32_SYMBOL_KERNEL32_GET_CURRENT_PROCESS_ID;
        expected_arguments = 0u;
        break;
    case DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_THREAD_ID:
        expected_symbol = DB_WIN32_SYMBOL_KERNEL32_GET_CURRENT_THREAD_ID;
        expected_arguments = 0u;
        break;
    case DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR:
        expected_symbol = DB_WIN32_SYMBOL_KERNEL32_GET_LAST_ERROR;
        expected_arguments = 0u;
        break;
    case DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR:
        expected_symbol = DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR;
        expected_arguments = 1u;
        break;
    case DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS:
        expected_symbol = DB_WIN32_SYMBOL_KERNEL32_EXIT_PROCESS;
        expected_arguments = 1u;
        break;
    default:
        return false;
    }

    if (request->symbol_id != expected_symbol ||
        request->argument_count != expected_arguments) {
        return false;
    }

    for (index = request->argument_count;
         index < DB_GATEWAY_FRAME_MAX_ARGUMENTS;
         ++index) {
        if (request->arguments[index] != 0u) {
            return false;
        }
    }

    return true;
}

db_win32_kernel32_invoke_status db_win32_kernel32_invoke_plan(
    const db_gateway_invoke_request *request,
    const db_win32_kernel32_state *current_state,
    db_win32_kernel32_state *planned_state,
    db_gateway_invoke_result *result)
{
    db_win32_kernel32_state provider_state;
    db_win32_kernel32_result provider_result;
    db_gateway_invoke_result planned_result;
    db_win32_kernel32_status provider_status;

    if (request == NULL || current_state == NULL ||
        planned_state == NULL || result == NULL) {
        return DB_WIN32_KERNEL32_INVOKE_NULL_ARGUMENT;
    }

    if (db_win32_kernel32_invoke_overlap(request, sizeof(*request),
                                         current_state, sizeof(*current_state)) ||
        db_win32_kernel32_invoke_overlap(request, sizeof(*request),
                                         planned_state, sizeof(*planned_state)) ||
        db_win32_kernel32_invoke_overlap(request, sizeof(*request),
                                         result, sizeof(*result)) ||
        db_win32_kernel32_invoke_overlap(current_state, sizeof(*current_state),
                                         planned_state, sizeof(*planned_state)) ||
        db_win32_kernel32_invoke_overlap(current_state, sizeof(*current_state),
                                         result, sizeof(*result)) ||
        db_win32_kernel32_invoke_overlap(planned_state, sizeof(*planned_state),
                                         result, sizeof(*result))) {
        return DB_WIN32_KERNEL32_INVOKE_BUFFER_OVERLAP;
    }

    if (!db_win32_kernel32_invoke_identity_valid(request)) {
        return DB_WIN32_KERNEL32_INVOKE_BAD_REQUEST;
    }

    provider_state = *current_state;
    provider_status = db_win32_kernel32_execute(&provider_state,
                                                request->handler_id,
                                                request->arguments,
                                                request->argument_count,
                                                &provider_result);
    if (provider_status != DB_WIN32_KERNEL32_OK ||
        provider_result.handler_id != request->handler_id ||
        provider_result.reserved != 0u) {
        return DB_WIN32_KERNEL32_INVOKE_PROVIDER_FAILED;
    }

    planned_result.rip = request->rip;
    planned_result.slot_address = request->slot_address;
    planned_result.raw_result = provider_result.raw_result;
    planned_result.slot_index = request->slot_index;
    planned_result.provider_id = request->provider_id;
    planned_result.symbol_id = request->symbol_id;
    planned_result.handler_id = request->handler_id;
    planned_result.reserved = 0u;

    *planned_state = provider_state;
    *result = planned_result;
    return DB_WIN32_KERNEL32_INVOKE_OK;
}

const char *db_win32_kernel32_invoke_status_name(
    db_win32_kernel32_invoke_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad sealed KERNEL32 request",
        "KERNEL32 provider planning failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown KERNEL32 invocation status";
    }

    return names[(size_t)status];
}
