#ifndef DBLYAT_GATEWAY_PLAN_H
#define DBLYAT_GATEWAY_PLAN_H

#include "dblyat/iat_plan.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_gateway_plan_status {
    DB_GATEWAY_PLAN_OK = 0,
    DB_GATEWAY_PLAN_NULL_ARGUMENT,
    DB_GATEWAY_PLAN_COUNT_OVERFLOW,
    DB_GATEWAY_PLAN_BUFFER_OVERLAP,
    DB_GATEWAY_PLAN_BAD_BINDING,
    DB_GATEWAY_PLAN_INVALID_SLOT_SIZE,
    DB_GATEWAY_PLAN_INVALID_GATEWAY_BASE,
    DB_GATEWAY_PLAN_INVALID_REGION,
    DB_GATEWAY_PLAN_ADDRESS_OVERFLOW,
    DB_GATEWAY_PLAN_NONCANONICAL_ADDRESS,
    DB_GATEWAY_PLAN_OUTPUT_BUFFER_TOO_SMALL
} db_gateway_plan_status;

typedef struct db_gateway_plan_summary {
    size_t binding_count;
    size_t target_count;
    uint64_t gateway_base;
    uint64_t region_size;
    uint64_t slot_size;
    uint64_t used_size;
} db_gateway_plan_summary;

db_gateway_plan_status db_gateway_plan_targets(
    const db_import_binding *bindings,
    size_t binding_count,
    uint64_t gateway_base,
    uint64_t region_size,
    uint64_t slot_size,
    db_iat_target *targets,
    size_t target_capacity,
    db_gateway_plan_summary *summary);

const char *db_gateway_plan_status_name(db_gateway_plan_status status);

#endif
