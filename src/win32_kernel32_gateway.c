#include "dblyat/win32_kernel32_gateway.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_win32_kernel32_gateway_overlap(const void *left,
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

db_win32_kernel32_gateway_status db_win32_kernel32_gateway_run(
    const db_gateway_invoke_request *request,
    db_win32_kernel32_state *provider_state,
    db_gateway_x64_guest_state *guest_state,
    db_win32_kernel32_gateway_summary *summary)
{
    db_win32_kernel32_gateway_summary completed;
    db_win32_kernel32_state planned_provider;
    db_gateway_x64_guest_state planned_guest;
    db_win32_kernel32_invoke_status invoke_status;
    db_gateway_auth_complete_status completion_status;

    if (request == NULL || provider_state == NULL ||
        guest_state == NULL || summary == NULL) {
        return DB_WIN32_KERNEL32_GATEWAY_NULL_ARGUMENT;
    }

    if (db_win32_kernel32_gateway_overlap(request, sizeof(*request),
                                          provider_state,
                                          sizeof(*provider_state)) ||
        db_win32_kernel32_gateway_overlap(request, sizeof(*request),
                                          guest_state,
                                          sizeof(*guest_state)) ||
        db_win32_kernel32_gateway_overlap(request, sizeof(*request),
                                          summary, sizeof(*summary)) ||
        db_win32_kernel32_gateway_overlap(provider_state,
                                          sizeof(*provider_state),
                                          guest_state,
                                          sizeof(*guest_state)) ||
        db_win32_kernel32_gateway_overlap(provider_state,
                                          sizeof(*provider_state),
                                          summary, sizeof(*summary)) ||
        db_win32_kernel32_gateway_overlap(guest_state,
                                          sizeof(*guest_state),
                                          summary, sizeof(*summary))) {
        return DB_WIN32_KERNEL32_GATEWAY_BUFFER_OVERLAP;
    }

    invoke_status = db_win32_kernel32_invoke_plan(request,
                                                  provider_state,
                                                  &planned_provider,
                                                  &completed.result);
    if (invoke_status != DB_WIN32_KERNEL32_INVOKE_OK) {
        return DB_WIN32_KERNEL32_GATEWAY_INVOCATION_FAILED;
    }

    planned_guest = *guest_state;
    completion_status = db_gateway_auth_complete(request,
                                                 &completed.result,
                                                 &planned_guest,
                                                 &completed.completion);
    if (completion_status != DB_GATEWAY_AUTH_COMPLETE_OK) {
        return DB_WIN32_KERNEL32_GATEWAY_COMPLETION_FAILED;
    }

    completed.reserved = 0u;
    *provider_state = planned_provider;
    *guest_state = planned_guest;
    *summary = completed;
    return DB_WIN32_KERNEL32_GATEWAY_OK;
}

const char *db_win32_kernel32_gateway_status_name(
    db_win32_kernel32_gateway_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "KERNEL32 invocation planning failed",
        "authenticated gateway completion failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown KERNEL32 gateway status";
    }

    return names[(size_t)status];
}
