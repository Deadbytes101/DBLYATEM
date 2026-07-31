#ifndef DBLYAT_GATEWAY_FRAME_H
#define DBLYAT_GATEWAY_FRAME_H

#include "dblyat/gateway_dispatch.h"

#include <stddef.h>
#include <stdint.h>

#define DB_GATEWAY_FRAME_MAX_ARGUMENTS 64u

typedef enum db_gateway_frame_status {
    DB_GATEWAY_FRAME_OK = 0,
    DB_GATEWAY_FRAME_NULL_ARGUMENT,
    DB_GATEWAY_FRAME_COUNT_OVERFLOW,
    DB_GATEWAY_FRAME_BUFFER_OVERLAP,
    DB_GATEWAY_FRAME_BAD_DISPATCH_PLAN,
    DB_GATEWAY_FRAME_BAD_SIGNATURE,
    DB_GATEWAY_FRAME_DUPLICATE_HANDLER_ID,
    DB_GATEWAY_FRAME_SIGNATURE_NOT_FOUND,
    DB_GATEWAY_FRAME_INVALID_STACK_WINDOW,
    DB_GATEWAY_FRAME_NONCANONICAL_ADDRESS,
    DB_GATEWAY_FRAME_ADDRESS_OVERFLOW,
    DB_GATEWAY_FRAME_STACK_OUT_OF_RANGE,
    DB_GATEWAY_FRAME_OUTPUT_BUFFER_TOO_SMALL
} db_gateway_frame_status;

typedef struct db_gateway_frame_signature {
    uint32_t handler_id;
    uint16_t argument_count;
    uint16_t reserved;
} db_gateway_frame_signature;

typedef struct db_gateway_guest_registers {
    uint64_t rcx;
    uint64_t rdx;
    uint64_t r8;
    uint64_t r9;
    uint64_t rsp;
} db_gateway_guest_registers;

typedef struct db_gateway_frame_summary {
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
} db_gateway_frame_summary;

db_gateway_frame_status db_gateway_frame_plan_call(
    const db_gateway_dispatch_plan *dispatch,
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    const db_gateway_guest_registers *registers,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t guest_stack_base,
    uint64_t *arguments,
    size_t argument_capacity,
    db_gateway_frame_summary *summary);

const char *db_gateway_frame_status_name(
    db_gateway_frame_status status);

#endif
