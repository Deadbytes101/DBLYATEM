#include "dblyat/win32_runtime_dispatch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_win32_runtime_dispatch_overlap(const void *left,
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

db_win32_runtime_dispatch_status db_win32_runtime_dispatch_run(
    const db_gateway_invoke_request *request,
    db_win32_kernel32_state *kernel32_state,
    db_gateway_x64_guest_state *guest_state,
    db_win32_runtime_dispatch_summary *summary)
{
    db_win32_runtime_dispatch_summary completed = {0};
    db_win32_kernel32_state planned_kernel32;
    db_gateway_x64_guest_state planned_guest;
    db_win32_kernel32_gateway_status gateway_status;

    if (request == NULL || kernel32_state == NULL ||
        guest_state == NULL || summary == NULL) {
        return DB_WIN32_RUNTIME_DISPATCH_NULL_ARGUMENT;
    }

    if (db_win32_runtime_dispatch_overlap(request, sizeof(*request),
                                          kernel32_state,
                                          sizeof(*kernel32_state)) ||
        db_win32_runtime_dispatch_overlap(request, sizeof(*request),
                                          guest_state,
                                          sizeof(*guest_state)) ||
        db_win32_runtime_dispatch_overlap(request, sizeof(*request),
                                          summary, sizeof(*summary)) ||
        db_win32_runtime_dispatch_overlap(kernel32_state,
                                          sizeof(*kernel32_state),
                                          guest_state,
                                          sizeof(*guest_state)) ||
        db_win32_runtime_dispatch_overlap(kernel32_state,
                                          sizeof(*kernel32_state),
                                          summary, sizeof(*summary)) ||
        db_win32_runtime_dispatch_overlap(guest_state,
                                          sizeof(*guest_state),
                                          summary, sizeof(*summary))) {
        return DB_WIN32_RUNTIME_DISPATCH_BUFFER_OVERLAP;
    }

    if (request->provider_id != DB_WIN32_PROVIDER_KERNEL32) {
        return DB_WIN32_RUNTIME_DISPATCH_UNSUPPORTED_PROVIDER;
    }

    planned_kernel32 = *kernel32_state;
    planned_guest = *guest_state;

    gateway_status = db_win32_kernel32_gateway_run(request,
                                                   &planned_kernel32,
                                                   &planned_guest,
                                                   &completed.gateway);

    switch (gateway_status) {
    case DB_WIN32_KERNEL32_GATEWAY_OK:
        completed.provider_id = request->provider_id;
        completed.outcome = DB_WIN32_RUNTIME_OUTCOME_CONTINUE;
        completed.exit_code = 0u;
        completed.reserved = 0u;
        break;

    case DB_WIN32_KERNEL32_GATEWAY_GUEST_TERMINATED:
        if (completed.gateway.result.raw_result > UINT32_MAX) {
            return DB_WIN32_RUNTIME_DISPATCH_GATEWAY_FAILED;
        }
        completed.provider_id = request->provider_id;
        completed.outcome = DB_WIN32_RUNTIME_OUTCOME_GUEST_TERMINATED;
        completed.exit_code =
            (uint32_t)completed.gateway.result.raw_result;
        completed.reserved = 0u;
        break;

    default:
        return DB_WIN32_RUNTIME_DISPATCH_GATEWAY_FAILED;
    }

    *kernel32_state = planned_kernel32;
    *guest_state = planned_guest;
    *summary = completed;
    return DB_WIN32_RUNTIME_DISPATCH_OK;
}

const char *db_win32_runtime_dispatch_status_name(
    db_win32_runtime_dispatch_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "unsupported Win32 provider",
        "provider gateway failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown Win32 runtime dispatch status";
    }

    return names[(size_t)status];
}

const char *db_win32_runtime_outcome_name(
    db_win32_runtime_outcome outcome)
{
    static const char *const names[] = {
        "continue guest execution",
        "guest process terminated"
    };

    if ((size_t)outcome >= sizeof(names) / sizeof(names[0])) {
        return "unknown Win32 runtime outcome";
    }

    return names[(size_t)outcome];
}
