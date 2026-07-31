#ifndef DBLYAT_GATEWAY_ENTER_H
#define DBLYAT_GATEWAY_ENTER_H

#include "dblyat/gateway_return_apply.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_gateway_enter_status {
    DB_GATEWAY_ENTER_OK = 0,
    DB_GATEWAY_ENTER_NULL_ARGUMENT,
    DB_GATEWAY_ENTER_COUNT_OVERFLOW,
    DB_GATEWAY_ENTER_BUFFER_OVERLAP,
    DB_GATEWAY_ENTER_LOOKUP_FAILED,
    DB_GATEWAY_ENTER_NOT_ENTRY,
    DB_GATEWAY_ENTER_DISPATCH_FAILED,
    DB_GATEWAY_ENTER_FRAME_FAILED
} db_gateway_enter_status;

typedef struct db_gateway_enter_summary {
    db_gateway_lookup_result lookup;
    db_gateway_dispatch_plan dispatch;
    db_gateway_frame_summary frame;
} db_gateway_enter_summary;

db_gateway_enter_status db_gateway_enter_plan(
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    const db_gateway_x64_guest_state *state,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t guest_stack_base,
    uint64_t *arguments,
    size_t argument_capacity,
    db_gateway_enter_summary *summary);

const char *db_gateway_enter_status_name(db_gateway_enter_status status);

#endif
