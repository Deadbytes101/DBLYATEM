#include "dblyat/gateway_executor.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_gateway_executor_overlap(const void *left,
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

static db_gateway_result_status db_gateway_executor_validate_request(
    const db_gateway_invoke_request *request)
{
    db_gateway_invoke_result probe = {0};
    db_gateway_validated_result validated;

    probe.rip = request->rip;
    probe.slot_address = request->slot_address;
    probe.slot_index = request->slot_index;
    probe.provider_id = request->provider_id;
    probe.symbol_id = request->symbol_id;
    probe.handler_id = request->handler_id;

    return db_gateway_result_validate(request, &probe, &validated);
}

db_gateway_executor_status db_gateway_executor_run(
    const db_gateway_invoke_request *request,
    db_gateway_executor_callback callback,
    void *context,
    db_gateway_x64_guest_state *state,
    db_gateway_executor_summary *summary)
{
    db_gateway_executor_summary completed;
    db_gateway_x64_guest_state planned_state;
    db_gateway_executor_callback_status callback_status;
    db_gateway_result_status request_status;
    db_gateway_auth_complete_status completion_status;

    if (request == NULL || callback == NULL || state == NULL || summary == NULL) {
        return DB_GATEWAY_EXECUTOR_NULL_ARGUMENT;
    }

    if (db_gateway_executor_overlap(request, sizeof(*request),
                                    state, sizeof(*state)) ||
        db_gateway_executor_overlap(request, sizeof(*request),
                                    summary, sizeof(*summary)) ||
        db_gateway_executor_overlap(state, sizeof(*state),
                                    summary, sizeof(*summary)) ||
        context == (void *)request ||
        context == (void *)state ||
        context == (void *)summary) {
        return DB_GATEWAY_EXECUTOR_BUFFER_OVERLAP;
    }

    request_status = db_gateway_executor_validate_request(request);
    if (request_status != DB_GATEWAY_RESULT_OK) {
        return DB_GATEWAY_EXECUTOR_BAD_REQUEST;
    }

    completed.result = (db_gateway_invoke_result){0};
    callback_status = callback(context, request, &completed.result);
    if (callback_status != DB_GATEWAY_EXECUTOR_CALLBACK_OK) {
        return DB_GATEWAY_EXECUTOR_CALLBACK_FAILED;
    }

    planned_state = *state;
    completion_status = db_gateway_auth_complete(request,
                                                 &completed.result,
                                                 &planned_state,
                                                 &completed.completion);
    if (completion_status != DB_GATEWAY_AUTH_COMPLETE_OK) {
        return DB_GATEWAY_EXECUTOR_COMPLETION_FAILED;
    }

    completed.callback_status = callback_status;
    completed.reserved = 0u;
    *state = planned_state;
    *summary = completed;
    return DB_GATEWAY_EXECUTOR_OK;
}

const char *db_gateway_executor_status_name(db_gateway_executor_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad invocation request",
        "executor callback failed",
        "authenticated completion failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway executor status";
    }

    return names[(size_t)status];
}
