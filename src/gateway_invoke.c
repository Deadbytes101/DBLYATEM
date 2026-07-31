#include "dblyat/gateway_invoke.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_gateway_invoke_mul_size(size_t left,
                                       size_t right,
                                       size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_gateway_invoke_overlap(const void *left,
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

static bool db_gateway_invoke_canonical(uint64_t value)
{
    const uint64_t upper = value >> 48u;
    const uint64_t sign = (value >> 47u) & 1u;

    return value != 0u &&
           ((sign == 0u && upper == 0u) ||
            (sign != 0u && upper == UINT64_C(0xffff)));
}

static bool db_gateway_invoke_reserved_zero(const uint8_t reserved[7])
{
    size_t index;

    for (index = 0u; index < 7u; ++index) {
        if (reserved[index] != 0u) {
            return false;
        }
    }

    return true;
}

static bool db_gateway_invoke_entry_valid(
    const db_gateway_enter_summary *entry)
{
    const db_gateway_lookup_result *lookup = &entry->lookup;
    const db_gateway_dispatch_plan *dispatch = &entry->dispatch;
    const db_gateway_frame_summary *frame = &entry->frame;

    if (lookup->kind != DB_GATEWAY_LOOKUP_ENTRY ||
        !db_gateway_invoke_reserved_zero(lookup->reserved) ||
        !db_gateway_invoke_canonical(lookup->rip) ||
        lookup->rip != lookup->slot_address ||
        lookup->provider_id == 0u || lookup->symbol_id == 0u) {
        return false;
    }

    if (dispatch->reserved != 0u || dispatch->handler_id == 0u ||
        dispatch->rip != lookup->rip ||
        dispatch->slot_address != lookup->slot_address ||
        dispatch->slot_index != lookup->slot_index ||
        dispatch->provider_id != lookup->provider_id ||
        dispatch->symbol_id != lookup->symbol_id) {
        return false;
    }

    if (frame->reserved != 0u ||
        frame->argument_count > DB_GATEWAY_FRAME_MAX_ARGUMENTS ||
        !db_gateway_invoke_canonical(frame->rip) ||
        !db_gateway_invoke_canonical(frame->slot_address) ||
        !db_gateway_invoke_canonical(frame->guest_rsp) ||
        !db_gateway_invoke_canonical(frame->return_address) ||
        frame->rip != dispatch->rip ||
        frame->slot_address != dispatch->slot_address ||
        frame->slot_index != dispatch->slot_index ||
        frame->provider_id != dispatch->provider_id ||
        frame->symbol_id != dispatch->symbol_id ||
        frame->handler_id != dispatch->handler_id) {
        return false;
    }

    return true;
}

db_gateway_invoke_status db_gateway_invoke_plan(
    const db_gateway_enter_summary *entry,
    const uint64_t *arguments,
    size_t argument_count,
    db_gateway_invoke_request *request)
{
    db_gateway_invoke_request planned = {0};
    size_t argument_bytes;
    size_t index;

    if (entry == NULL || request == NULL ||
        (argument_count != 0u && arguments == NULL)) {
        return DB_GATEWAY_INVOKE_NULL_ARGUMENT;
    }

    if (!db_gateway_invoke_mul_size(argument_count,
                                    sizeof(*arguments),
                                    &argument_bytes)) {
        return DB_GATEWAY_INVOKE_COUNT_OVERFLOW;
    }

    if (db_gateway_invoke_overlap(request, sizeof(*request),
                                  entry, sizeof(*entry)) ||
        db_gateway_invoke_overlap(request, sizeof(*request),
                                  arguments, argument_bytes)) {
        return DB_GATEWAY_INVOKE_BUFFER_OVERLAP;
    }

    if (!db_gateway_invoke_entry_valid(entry)) {
        return DB_GATEWAY_INVOKE_BAD_ENTRY_SUMMARY;
    }

    if (argument_count != entry->frame.argument_count) {
        return DB_GATEWAY_INVOKE_ARGUMENT_COUNT_MISMATCH;
    }

    planned.rip = entry->frame.rip;
    planned.slot_address = entry->frame.slot_address;
    planned.guest_rsp = entry->frame.guest_rsp;
    planned.return_address = entry->frame.return_address;
    planned.slot_index = entry->frame.slot_index;
    planned.argument_count = argument_count;
    planned.provider_id = entry->frame.provider_id;
    planned.symbol_id = entry->frame.symbol_id;
    planned.handler_id = entry->frame.handler_id;
    planned.reserved = 0u;

    for (index = 0u; index < argument_count; ++index) {
        planned.arguments[index] = arguments[index];
    }

    *request = planned;
    return DB_GATEWAY_INVOKE_OK;
}

const char *db_gateway_invoke_status_name(db_gateway_invoke_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "bad gateway entry summary",
        "argument count mismatch"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway invocation status";
    }

    return names[(size_t)status];
}
