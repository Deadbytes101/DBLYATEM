#ifndef DBLYAT_GATEWAY_RETURN_H
#define DBLYAT_GATEWAY_RETURN_H

#include "dblyat/gateway_frame.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_gateway_return_status {
    DB_GATEWAY_RETURN_OK = 0,
    DB_GATEWAY_RETURN_NULL_ARGUMENT,
    DB_GATEWAY_RETURN_BUFFER_OVERLAP,
    DB_GATEWAY_RETURN_BAD_FRAME_SUMMARY,
    DB_GATEWAY_RETURN_NONCANONICAL_ADDRESS,
    DB_GATEWAY_RETURN_ADDRESS_OVERFLOW
} db_gateway_return_status;

typedef struct db_gateway_return_plan {
    uint64_t source_rip;
    uint64_t slot_address;
    uint64_t next_rip;
    uint64_t previous_rsp;
    uint64_t next_rsp;
    uint64_t rax;
    size_t slot_index;
    size_t argument_count;
    uint32_t provider_id;
    uint32_t symbol_id;
    uint32_t handler_id;
    uint32_t reserved;
} db_gateway_return_plan;

db_gateway_return_status db_gateway_return_plan_raw(
    const db_gateway_frame_summary *frame,
    uint64_t raw_result,
    db_gateway_return_plan *plan);

const char *db_gateway_return_status_name(
    db_gateway_return_status status);

#endif
