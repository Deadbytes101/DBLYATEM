#ifndef DBLYAT_GATEWAY_DECODE_H
#define DBLYAT_GATEWAY_DECODE_H

#include "dblyat/gateway_emit.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_gateway_decode_status {
    DB_GATEWAY_DECODE_OK = 0,
    DB_GATEWAY_DECODE_NULL_ARGUMENT,
    DB_GATEWAY_DECODE_COUNT_OVERFLOW,
    DB_GATEWAY_DECODE_BUFFER_OVERLAP,
    DB_GATEWAY_DECODE_INVALID_SLOT_SIZE,
    DB_GATEWAY_DECODE_INVALID_GATEWAY_BASE,
    DB_GATEWAY_DECODE_BUFFER_TOO_SMALL,
    DB_GATEWAY_DECODE_BAD_OPCODE,
    DB_GATEWAY_DECODE_UNSUPPORTED_VERSION,
    DB_GATEWAY_DECODE_BAD_MAGIC,
    DB_GATEWAY_DECODE_BAD_TARGET,
    DB_GATEWAY_DECODE_DUPLICATE_TARGET_KEY,
    DB_GATEWAY_DECODE_TARGET_ADDRESS_MISMATCH,
    DB_GATEWAY_DECODE_RESERVED_NONZERO,
    DB_GATEWAY_DECODE_BAD_PADDING,
    DB_GATEWAY_DECODE_ADDRESS_OVERFLOW,
    DB_GATEWAY_DECODE_NONCANONICAL_ADDRESS,
    DB_GATEWAY_DECODE_OUTPUT_BUFFER_TOO_SMALL
} db_gateway_decode_status;

typedef struct db_gateway_decode_summary {
    size_t slot_count;
    size_t bytes_consumed;
    uint64_t gateway_base;
    uint64_t slot_size;
    uint16_t format_version;
} db_gateway_decode_summary;

db_gateway_decode_status db_gateway_decode_slots(
    const void *gateway_buffer,
    size_t gateway_buffer_size,
    size_t slot_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    db_iat_target *targets,
    size_t target_capacity,
    db_gateway_decode_summary *summary);

const char *db_gateway_decode_status_name(db_gateway_decode_status status);

#endif
