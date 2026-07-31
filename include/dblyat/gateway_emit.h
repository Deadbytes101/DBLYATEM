#ifndef DBLYAT_GATEWAY_EMIT_H
#define DBLYAT_GATEWAY_EMIT_H

#include "dblyat/gateway_plan.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_gateway_emit_status {
    DB_GATEWAY_EMIT_OK = 0,
    DB_GATEWAY_EMIT_NULL_ARGUMENT,
    DB_GATEWAY_EMIT_COUNT_OVERFLOW,
    DB_GATEWAY_EMIT_BUFFER_OVERLAP,
    DB_GATEWAY_EMIT_INVALID_SLOT_SIZE,
    DB_GATEWAY_EMIT_INVALID_GATEWAY_BASE,
    DB_GATEWAY_EMIT_BAD_TARGET,
    DB_GATEWAY_EMIT_DUPLICATE_TARGET_KEY,
    DB_GATEWAY_EMIT_TARGET_ADDRESS_MISMATCH,
    DB_GATEWAY_EMIT_ADDRESS_OVERFLOW,
    DB_GATEWAY_EMIT_NONCANONICAL_ADDRESS,
    DB_GATEWAY_EMIT_BUFFER_TOO_SMALL
} db_gateway_emit_status;

typedef struct db_gateway_emit_summary {
    size_t target_count;
    size_t bytes_written;
    uint64_t gateway_base;
    uint64_t slot_size;
    uint16_t format_version;
} db_gateway_emit_summary;

db_gateway_emit_status db_gateway_emit_slots(
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    void *gateway_buffer,
    size_t gateway_buffer_size,
    db_gateway_emit_summary *summary);

const char *db_gateway_emit_status_name(db_gateway_emit_status status);

#endif
