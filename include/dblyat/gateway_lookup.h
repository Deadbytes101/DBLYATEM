#ifndef DBLYAT_GATEWAY_LOOKUP_H
#define DBLYAT_GATEWAY_LOOKUP_H

#include "dblyat/gateway_decode.h"

#include <stddef.h>
#include <stdint.h>

typedef uint8_t db_gateway_lookup_kind;

enum {
    DB_GATEWAY_LOOKUP_MISS = 0u,
    DB_GATEWAY_LOOKUP_ENTRY = 1u,
    DB_GATEWAY_LOOKUP_INTERIOR = 2u
};

typedef enum db_gateway_lookup_status {
    DB_GATEWAY_LOOKUP_OK = 0,
    DB_GATEWAY_LOOKUP_NULL_ARGUMENT,
    DB_GATEWAY_LOOKUP_COUNT_OVERFLOW,
    DB_GATEWAY_LOOKUP_BUFFER_OVERLAP,
    DB_GATEWAY_LOOKUP_INVALID_SLOT_SIZE,
    DB_GATEWAY_LOOKUP_INVALID_GATEWAY_BASE,
    DB_GATEWAY_LOOKUP_BAD_TARGET,
    DB_GATEWAY_LOOKUP_DUPLICATE_TARGET_KEY,
    DB_GATEWAY_LOOKUP_TARGET_ADDRESS_MISMATCH,
    DB_GATEWAY_LOOKUP_ADDRESS_OVERFLOW,
    DB_GATEWAY_LOOKUP_NONCANONICAL_ADDRESS
} db_gateway_lookup_status;

typedef struct db_gateway_lookup_result {
    uint64_t rip;
    uint64_t slot_address;
    size_t slot_index;
    uint32_t provider_id;
    uint32_t symbol_id;
    db_gateway_lookup_kind kind;
    uint8_t reserved[7];
} db_gateway_lookup_result;

db_gateway_lookup_status db_gateway_lookup_rip(
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    uint64_t guest_rip,
    db_gateway_lookup_result *result);

const char *db_gateway_lookup_status_name(db_gateway_lookup_status status);

#endif
