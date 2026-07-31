#ifndef DBLYAT_GATEWAY_AUTH_COMPLETE_H
#define DBLYAT_GATEWAY_AUTH_COMPLETE_H

#include "dblyat/gateway_complete.h"
#include "dblyat/gateway_result.h"

typedef enum db_gateway_auth_complete_status {
    DB_GATEWAY_AUTH_COMPLETE_OK = 0,
    DB_GATEWAY_AUTH_COMPLETE_NULL_ARGUMENT,
    DB_GATEWAY_AUTH_COMPLETE_BUFFER_OVERLAP,
    DB_GATEWAY_AUTH_COMPLETE_RESULT_FAILED,
    DB_GATEWAY_AUTH_COMPLETE_COMPLETION_FAILED
} db_gateway_auth_complete_status;

typedef struct db_gateway_auth_complete_summary {
    db_gateway_validated_result validated;
    db_gateway_complete_summary complete;
} db_gateway_auth_complete_summary;

db_gateway_auth_complete_status db_gateway_auth_complete(
    const db_gateway_invoke_request *request,
    const db_gateway_invoke_result *result,
    db_gateway_x64_guest_state *state,
    db_gateway_auth_complete_summary *summary);

const char *db_gateway_auth_complete_status_name(
    db_gateway_auth_complete_status status);

#endif
