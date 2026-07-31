#ifndef DBLYAT_GATEWAY_INVOKE_H
#define DBLYAT_GATEWAY_INVOKE_H

#include "dblyat/gateway_enter.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_gateway_invoke_status {
    DB_GATEWAY_INVOKE_OK = 0,
    DB_GATEWAY_INVOKE_NULL_ARGUMENT,
    DB_GATEWAY_INVOKE_COUNT_OVERFLOW,
    DB_GATEWAY_INVOKE_BUFFER_OVERLAP,
    DB_GATEWAY_INVOKE_BAD_ENTRY_SUMMARY,
    DB_GATEWAY_INVOKE_ARGUMENT_COUNT_MISMATCH
} db_gateway_invoke_status;

typedef struct db_gateway_invoke_request {
    uint64_t rip;
    uint64_t slot_address;
    uint64_t guest_rsp;
    uint64_t return_address;
    size_t slot_index;
    size_t argument_count;
    uint32_t provider_id;
    uint32_t symbol_id;
    uint32_t handler_id;
    uint32_t reserved;
    uint64_t arguments[DB_GATEWAY_FRAME_MAX_ARGUMENTS];
} db_gateway_invoke_request;

db_gateway_invoke_status db_gateway_invoke_plan(
    const db_gateway_enter_summary *entry,
    const uint64_t *arguments,
    size_t argument_count,
    db_gateway_invoke_request *request);

const char *db_gateway_invoke_status_name(db_gateway_invoke_status status);

#endif
