#ifndef DBLYAT_GATEWAY_COMPLETE_H
#define DBLYAT_GATEWAY_COMPLETE_H

#include "dblyat/gateway_return_apply.h"

#include <stdint.h>

typedef enum db_gateway_complete_status {
    DB_GATEWAY_COMPLETE_OK = 0,
    DB_GATEWAY_COMPLETE_NULL_ARGUMENT,
    DB_GATEWAY_COMPLETE_BUFFER_OVERLAP,
    DB_GATEWAY_COMPLETE_RETURN_PLAN_FAILED,
    DB_GATEWAY_COMPLETE_RETURN_APPLY_FAILED
} db_gateway_complete_status;

typedef struct db_gateway_complete_summary {
    db_gateway_return_plan return_plan;
    db_gateway_return_apply_summary apply_summary;
} db_gateway_complete_summary;

db_gateway_complete_status db_gateway_complete_raw(
    const db_gateway_frame_summary *frame,
    uint64_t raw_result,
    db_gateway_x64_guest_state *state,
    db_gateway_complete_summary *summary);

const char *db_gateway_complete_status_name(
    db_gateway_complete_status status);

#endif
