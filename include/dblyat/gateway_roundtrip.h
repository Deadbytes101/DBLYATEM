#ifndef DBLYAT_GATEWAY_ROUNDTRIP_H
#define DBLYAT_GATEWAY_ROUNDTRIP_H

#include "dblyat/gateway_complete.h"
#include "dblyat/gateway_enter.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_gateway_roundtrip_status {
    DB_GATEWAY_ROUNDTRIP_OK = 0,
    DB_GATEWAY_ROUNDTRIP_NULL_ARGUMENT,
    DB_GATEWAY_ROUNDTRIP_COUNT_OVERFLOW,
    DB_GATEWAY_ROUNDTRIP_BUFFER_OVERLAP,
    DB_GATEWAY_ROUNDTRIP_ENTRY_FAILED,
    DB_GATEWAY_ROUNDTRIP_ARGUMENT_BUFFER_TOO_SMALL,
    DB_GATEWAY_ROUNDTRIP_COMPLETION_FAILED
} db_gateway_roundtrip_status;

typedef struct db_gateway_roundtrip_summary {
    db_gateway_enter_summary enter;
    db_gateway_complete_summary complete;
} db_gateway_roundtrip_summary;

db_gateway_roundtrip_status db_gateway_roundtrip_raw(
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    db_gateway_x64_guest_state *state,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t guest_stack_base,
    uint64_t raw_result,
    uint64_t *arguments,
    size_t argument_capacity,
    db_gateway_roundtrip_summary *summary);

const char *db_gateway_roundtrip_status_name(
    db_gateway_roundtrip_status status);

#endif
