#ifndef DBLYAT_WIN32_RUNTIME_DISPATCH_H
#define DBLYAT_WIN32_RUNTIME_DISPATCH_H

#include "dblyat/win32_kernel32_gateway.h"

#include <stdint.h>

typedef enum db_win32_runtime_dispatch_status {
    DB_WIN32_RUNTIME_DISPATCH_OK = 0,
    DB_WIN32_RUNTIME_DISPATCH_NULL_ARGUMENT,
    DB_WIN32_RUNTIME_DISPATCH_BUFFER_OVERLAP,
    DB_WIN32_RUNTIME_DISPATCH_UNSUPPORTED_PROVIDER,
    DB_WIN32_RUNTIME_DISPATCH_GATEWAY_FAILED
} db_win32_runtime_dispatch_status;

typedef enum db_win32_runtime_outcome {
    DB_WIN32_RUNTIME_OUTCOME_CONTINUE = 0,
    DB_WIN32_RUNTIME_OUTCOME_GUEST_TERMINATED
} db_win32_runtime_outcome;

typedef struct db_win32_runtime_dispatch_summary {
    db_win32_kernel32_gateway_summary gateway;
    uint32_t provider_id;
    uint32_t outcome;
    uint32_t exit_code;
    uint32_t reserved;
} db_win32_runtime_dispatch_summary;

db_win32_runtime_dispatch_status db_win32_runtime_dispatch_run(
    const db_gateway_invoke_request *request,
    db_win32_kernel32_state *kernel32_state,
    db_gateway_x64_guest_state *guest_state,
    db_win32_runtime_dispatch_summary *summary);

const char *db_win32_runtime_dispatch_status_name(
    db_win32_runtime_dispatch_status status);

const char *db_win32_runtime_outcome_name(
    db_win32_runtime_outcome outcome);

#endif
