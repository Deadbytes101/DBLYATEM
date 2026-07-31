#include "dblyat/gateway_auth_complete.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_gateway_auth_complete_overlap(const void *left,
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

db_gateway_auth_complete_status db_gateway_auth_complete(
    const db_gateway_invoke_request *request,
    const db_gateway_invoke_result *result,
    db_gateway_x64_guest_state *state,
    db_gateway_auth_complete_summary *summary)
{
    db_gateway_auth_complete_summary completed;
    db_gateway_frame_summary frame;
    db_gateway_x64_guest_state planned_state;
    db_gateway_result_status result_status;
    db_gateway_complete_status complete_status;

    if (request == NULL || result == NULL || state == NULL || summary == NULL) {
        return DB_GATEWAY_AUTH_COMPLETE_NULL_ARGUMENT;
    }

    if (db_gateway_auth_complete_overlap(request, sizeof(*request),
                                         result, sizeof(*result)) ||
        db_gateway_auth_complete_overlap(request, sizeof(*request),
                                         state, sizeof(*state)) ||
        db_gateway_auth_complete_overlap(request, sizeof(*request),
                                         summary, sizeof(*summary)) ||
        db_gateway_auth_complete_overlap(result, sizeof(*result),
                                         state, sizeof(*state)) ||
        db_gateway_auth_complete_overlap(result, sizeof(*result),
                                         summary, sizeof(*summary)) ||
        db_gateway_auth_complete_overlap(state, sizeof(*state),
                                         summary, sizeof(*summary))) {
        return DB_GATEWAY_AUTH_COMPLETE_BUFFER_OVERLAP;
    }

    result_status = db_gateway_result_validate(request,
                                               result,
                                               &completed.validated);
    if (result_status != DB_GATEWAY_RESULT_OK) {
        return DB_GATEWAY_AUTH_COMPLETE_RESULT_FAILED;
    }

    frame.rip = completed.validated.rip;
    frame.slot_address = completed.validated.slot_address;
    frame.guest_rsp = completed.validated.guest_rsp;
    frame.return_address = completed.validated.return_address;
    frame.slot_index = completed.validated.slot_index;
    frame.argument_count = completed.validated.argument_count;
    frame.provider_id = completed.validated.provider_id;
    frame.symbol_id = completed.validated.symbol_id;
    frame.handler_id = completed.validated.handler_id;
    frame.reserved = 0u;

    planned_state = *state;
    complete_status = db_gateway_complete_raw(&frame,
                                              completed.validated.raw_result,
                                              &planned_state,
                                              &completed.complete);
    if (complete_status != DB_GATEWAY_COMPLETE_OK) {
        return DB_GATEWAY_AUTH_COMPLETE_COMPLETION_FAILED;
    }

    *state = planned_state;
    *summary = completed;
    return DB_GATEWAY_AUTH_COMPLETE_OK;
}

const char *db_gateway_auth_complete_status_name(
    db_gateway_auth_complete_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "result validation failed",
        "gateway completion failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown authenticated completion status";
    }

    return names[(size_t)status];
}
