#ifndef DBLYAT_GATEWAY_EXECUTOR_H
#define DBLYAT_GATEWAY_EXECUTOR_H

#include "dblyat/gateway_auth_complete.h"

#include <stdint.h>

typedef uint32_t db_gateway_executor_callback_status;

enum {
    DB_GATEWAY_EXECUTOR_CALLBACK_OK = 0u
};

typedef db_gateway_executor_callback_status (*db_gateway_executor_callback)(
    void *context,
    const db_gateway_invoke_request *request,
    db_gateway_invoke_result *result);

typedef enum db_gateway_executor_status {
    DB_GATEWAY_EXECUTOR_OK = 0,
    DB_GATEWAY_EXECUTOR_NULL_ARGUMENT,
    DB_GATEWAY_EXECUTOR_BUFFER_OVERLAP,
    DB_GATEWAY_EXECUTOR_BAD_REQUEST,
    DB_GATEWAY_EXECUTOR_CALLBACK_FAILED,
    DB_GATEWAY_EXECUTOR_COMPLETION_FAILED
} db_gateway_executor_status;

typedef struct db_gateway_executor_summary {
    db_gateway_invoke_result result;
    db_gateway_auth_complete_summary completion;
    db_gateway_executor_callback_status callback_status;
    uint32_t reserved;
} db_gateway_executor_summary;

db_gateway_executor_status db_gateway_executor_run(
    const db_gateway_invoke_request *request,
    db_gateway_executor_callback callback,
    void *context,
    db_gateway_x64_guest_state *state,
    db_gateway_executor_summary *summary);

const char *db_gateway_executor_status_name(db_gateway_executor_status status);

#endif
