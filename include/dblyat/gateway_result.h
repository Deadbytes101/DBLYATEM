#ifndef DBLYAT_GATEWAY_RESULT_H
#define DBLYAT_GATEWAY_RESULT_H

#include "dblyat/gateway_invoke.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_gateway_result_status {
    DB_GATEWAY_RESULT_OK = 0,
    DB_GATEWAY_RESULT_NULL_ARGUMENT,
    DB_GATEWAY_RESULT_BUFFER_OVERLAP,
    DB_GATEWAY_RESULT_BAD_REQUEST,
    DB_GATEWAY_RESULT_BAD_RESULT,
    DB_GATEWAY_RESULT_IDENTITY_MISMATCH
} db_gateway_result_status;

typedef struct db_gateway_invoke_result {
    uint64_t rip;
    uint64_t slot_address;
    uint64_t raw_result;
    size_t slot_index;
    uint32_t provider_id;
    uint32_t symbol_id;
    uint32_t handler_id;
    uint32_t reserved;
} db_gateway_invoke_result;

typedef struct db_gateway_validated_result {
    uint64_t rip;
    uint64_t slot_address;
    uint64_t guest_rsp;
    uint64_t return_address;
    uint64_t raw_result;
    size_t slot_index;
    size_t argument_count;
    uint32_t provider_id;
    uint32_t symbol_id;
    uint32_t handler_id;
    uint32_t reserved;
} db_gateway_validated_result;

db_gateway_result_status db_gateway_result_validate(
    const db_gateway_invoke_request *request,
    const db_gateway_invoke_result *result,
    db_gateway_validated_result *validated);

const char *db_gateway_result_status_name(db_gateway_result_status status);

#endif
