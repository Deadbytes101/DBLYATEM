#ifndef DBLYAT_GATEWAY_REGISTRY_H
#define DBLYAT_GATEWAY_REGISTRY_H

#include "dblyat/gateway_executor.h"

#include <stddef.h>
#include <stdint.h>

typedef struct db_gateway_executor_entry {
    uint32_t handler_id;
    uint32_t reserved;
    db_gateway_executor_callback callback;
    void *context;
} db_gateway_executor_entry;

typedef enum db_gateway_registry_status {
    DB_GATEWAY_REGISTRY_OK = 0,
    DB_GATEWAY_REGISTRY_NULL_ARGUMENT,
    DB_GATEWAY_REGISTRY_COUNT_OVERFLOW,
    DB_GATEWAY_REGISTRY_BUFFER_OVERLAP,
    DB_GATEWAY_REGISTRY_BAD_REQUEST,
    DB_GATEWAY_REGISTRY_BAD_ENTRY,
    DB_GATEWAY_REGISTRY_DUPLICATE_HANDLER_ID,
    DB_GATEWAY_REGISTRY_HANDLER_NOT_FOUND,
    DB_GATEWAY_REGISTRY_EXECUTOR_FAILED
} db_gateway_registry_status;

typedef struct db_gateway_registry_summary {
    db_gateway_executor_summary executor;
    size_t entry_index;
    uint32_t handler_id;
    uint32_t reserved;
} db_gateway_registry_summary;

db_gateway_registry_status db_gateway_registry_run(
    const db_gateway_invoke_request *request,
    const db_gateway_executor_entry *entries,
    size_t entry_count,
    db_gateway_x64_guest_state *state,
    db_gateway_registry_summary *summary);

const char *db_gateway_registry_status_name(db_gateway_registry_status status);

#endif
