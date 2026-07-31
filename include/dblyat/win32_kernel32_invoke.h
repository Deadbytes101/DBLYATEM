#ifndef DBLYAT_WIN32_KERNEL32_INVOKE_H
#define DBLYAT_WIN32_KERNEL32_INVOKE_H

#include "dblyat/gateway_result.h"
#include "dblyat/win32_kernel32.h"

#include <stdint.h>

typedef enum db_win32_kernel32_invoke_status {
    DB_WIN32_KERNEL32_INVOKE_OK = 0,
    DB_WIN32_KERNEL32_INVOKE_NULL_ARGUMENT,
    DB_WIN32_KERNEL32_INVOKE_BUFFER_OVERLAP,
    DB_WIN32_KERNEL32_INVOKE_BAD_REQUEST,
    DB_WIN32_KERNEL32_INVOKE_PROVIDER_FAILED
} db_win32_kernel32_invoke_status;

db_win32_kernel32_invoke_status db_win32_kernel32_invoke_plan(
    const db_gateway_invoke_request *request,
    const db_win32_kernel32_state *current_state,
    db_win32_kernel32_state *planned_state,
    db_gateway_invoke_result *result);

const char *db_win32_kernel32_invoke_status_name(
    db_win32_kernel32_invoke_status status);

#endif
