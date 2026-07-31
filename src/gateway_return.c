#include "dblyat/gateway_return.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_gateway_return_ranges_overlap(const void *left,
                                             size_t left_size,
                                             const void *right,
                                             size_t right_size)
{
    const uintptr_t left_address = (uintptr_t)left;
    const uintptr_t right_address = (uintptr_t)right;

    if (left_size == 0u || right_size == 0u) {
        return false;
    }

    if (left_address <= right_address) {
        return right_address - left_address < left_size;
    }

    return left_address - right_address < right_size;
}

static bool db_gateway_return_address_is_canonical(uint64_t value)
{
    const uint64_t upper = value >> 48u;
    const uint64_t sign = (value >> 47u) & 1u;

    return (sign == 0u && upper == 0u) ||
           (sign != 0u && upper == UINT64_C(0xffff));
}

static bool db_gateway_return_add_u64(uint64_t left,
                                      uint64_t right,
                                      uint64_t *result)
{
    if (left > UINT64_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static db_gateway_return_status db_gateway_return_validate_frame(
    const db_gateway_frame_summary *frame)
{
    if (frame->rip == 0u ||
        frame->slot_address != frame->rip ||
        frame->slot_index == SIZE_MAX ||
        frame->argument_count > DB_GATEWAY_FRAME_MAX_ARGUMENTS ||
        frame->provider_id == 0u ||
        frame->symbol_id == 0u ||
        frame->handler_id == 0u ||
        frame->reserved != 0u) {
        return DB_GATEWAY_RETURN_BAD_FRAME_SUMMARY;
    }

    if (!db_gateway_return_address_is_canonical(frame->rip) ||
        frame->guest_rsp == 0u ||
        frame->return_address == 0u ||
        !db_gateway_return_address_is_canonical(frame->guest_rsp) ||
        !db_gateway_return_address_is_canonical(
            frame->return_address)) {
        return DB_GATEWAY_RETURN_NONCANONICAL_ADDRESS;
    }

    return DB_GATEWAY_RETURN_OK;
}

db_gateway_return_status db_gateway_return_plan_raw(
    const db_gateway_frame_summary *frame,
    uint64_t raw_result,
    db_gateway_return_plan *plan)
{
    db_gateway_return_plan planned;
    db_gateway_return_status status;
    uint64_t next_rsp;

    if (frame == NULL || plan == NULL) {
        return DB_GATEWAY_RETURN_NULL_ARGUMENT;
    }

    if (db_gateway_return_ranges_overlap(frame,
                                         sizeof(*frame),
                                         plan,
                                         sizeof(*plan))) {
        return DB_GATEWAY_RETURN_BUFFER_OVERLAP;
    }

    status = db_gateway_return_validate_frame(frame);
    if (status != DB_GATEWAY_RETURN_OK) {
        return status;
    }

    if (!db_gateway_return_add_u64(frame->guest_rsp,
                                   UINT64_C(8),
                                   &next_rsp)) {
        return DB_GATEWAY_RETURN_ADDRESS_OVERFLOW;
    }

    if (!db_gateway_return_address_is_canonical(next_rsp)) {
        return DB_GATEWAY_RETURN_NONCANONICAL_ADDRESS;
    }

    planned.source_rip = frame->rip;
    planned.slot_address = frame->slot_address;
    planned.next_rip = frame->return_address;
    planned.previous_rsp = frame->guest_rsp;
    planned.next_rsp = next_rsp;
    planned.rax = raw_result;
    planned.slot_index = frame->slot_index;
    planned.argument_count = frame->argument_count;
    planned.provider_id = frame->provider_id;
    planned.symbol_id = frame->symbol_id;
    planned.handler_id = frame->handler_id;
    planned.reserved = 0u;
    *plan = planned;

    return DB_GATEWAY_RETURN_OK;
}

const char *db_gateway_return_status_name(
    db_gateway_return_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad frame summary",
        "noncanonical guest address",
        "guest address overflows"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway return status";
    }

    return names[(size_t)status];
}
