#ifndef DBLYAT_GATEWAY_RETURN_APPLY_H
#define DBLYAT_GATEWAY_RETURN_APPLY_H

#include "dblyat/gateway_return.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_gateway_return_apply_status {
    DB_GATEWAY_RETURN_APPLY_OK = 0,
    DB_GATEWAY_RETURN_APPLY_NULL_ARGUMENT,
    DB_GATEWAY_RETURN_APPLY_BUFFER_OVERLAP,
    DB_GATEWAY_RETURN_APPLY_BAD_RETURN_PLAN,
    DB_GATEWAY_RETURN_APPLY_NONCANONICAL_ADDRESS,
    DB_GATEWAY_RETURN_APPLY_ADDRESS_OVERFLOW,
    DB_GATEWAY_RETURN_APPLY_STALE_STATE
} db_gateway_return_apply_status;

typedef struct db_gateway_x64_guest_state {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
} db_gateway_x64_guest_state;

typedef struct db_gateway_return_apply_summary {
    uint64_t source_rip;
    uint64_t next_rip;
    uint64_t previous_rsp;
    uint64_t next_rsp;
    uint64_t previous_rax;
    uint64_t next_rax;
    size_t slot_index;
    size_t argument_count;
    uint32_t provider_id;
    uint32_t symbol_id;
    uint32_t handler_id;
    uint32_t reserved;
} db_gateway_return_apply_summary;

db_gateway_return_apply_status db_gateway_return_apply_plan(
    const db_gateway_return_plan *plan,
    db_gateway_x64_guest_state *state,
    db_gateway_return_apply_summary *summary);

const char *db_gateway_return_apply_status_name(
    db_gateway_return_apply_status status);

#endif
