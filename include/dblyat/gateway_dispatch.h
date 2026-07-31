#ifndef DBLYAT_GATEWAY_DISPATCH_H
#define DBLYAT_GATEWAY_DISPATCH_H

#include "dblyat/gateway_lookup.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_gateway_dispatch_status {
    DB_GATEWAY_DISPATCH_OK = 0,
    DB_GATEWAY_DISPATCH_NULL_ARGUMENT,
    DB_GATEWAY_DISPATCH_COUNT_OVERFLOW,
    DB_GATEWAY_DISPATCH_BUFFER_OVERLAP,
    DB_GATEWAY_DISPATCH_NOT_ENTRY,
    DB_GATEWAY_DISPATCH_BAD_LOOKUP_RESULT,
    DB_GATEWAY_DISPATCH_BAD_HANDLER,
    DB_GATEWAY_DISPATCH_DUPLICATE_HANDLER_KEY,
    DB_GATEWAY_DISPATCH_DUPLICATE_HANDLER_ID,
    DB_GATEWAY_DISPATCH_HANDLER_NOT_FOUND
} db_gateway_dispatch_status;

typedef struct db_gateway_dispatch_handler {
    uint32_t provider_id;
    uint32_t symbol_id;
    uint32_t handler_id;
    uint32_t reserved;
} db_gateway_dispatch_handler;

typedef struct db_gateway_dispatch_plan {
    uint64_t rip;
    uint64_t slot_address;
    size_t slot_index;
    uint32_t provider_id;
    uint32_t symbol_id;
    uint32_t handler_id;
    uint32_t reserved;
} db_gateway_dispatch_plan;

db_gateway_dispatch_status db_gateway_dispatch_plan_entry(
    const db_gateway_lookup_result *lookup,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    db_gateway_dispatch_plan *plan);

const char *db_gateway_dispatch_status_name(
    db_gateway_dispatch_status status);

#endif
