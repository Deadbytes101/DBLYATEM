#ifndef DBLYAT_WIN32_KERNEL32_GATEWAY_H
#define DBLYAT_WIN32_KERNEL32_GATEWAY_H

#include "dblyat/gateway_auth_complete.h"
#include "dblyat/win32_kernel32_invoke.h"

#include <stdint.h>

typedef enum db_win32_kernel32_gateway_status {
    DB_WIN32_KERNEL32_GATEWAY_OK = 0,
    DB_WIN32_KERNEL32_GATEWAY_NULL_ARGUMENT,
    DB_WIN32_KERNEL32_GATEWAY_BUFFER_OVERLAP,
    DB_WIN32_KERNEL32_GATEWAY_INVOCATION_FAILED,
    DB_WIN32_KERNEL32_GATEWAY_COMPLETION_FAILED,
    DB_WIN32_KERNEL32_GATEWAY_GUEST_TERMINATED
} db_win32_kernel32_gateway_status;

typedef struct db_win32_kernel32_gateway_summary {
    db_gateway_invoke_result result;
    db_gateway_auth_complete_summary completion;
    uint32_t reserved;
} db_win32_kernel32_gateway_summary;

db_win32_kernel32_gateway_status db_win32_kernel32_gateway_run(
    const db_gateway_invoke_request *request,
    db_win32_kernel32_state *provider_state,
    db_gateway_x64_guest_state *guest_state,
    db_win32_kernel32_gateway_summary *summary);

const char *db_win32_kernel32_gateway_status_name(
    db_win32_kernel32_gateway_status status);

#endif
