#include "dblyat/gateway_dispatch.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_gateway_dispatch_mul_size(size_t left,
                                         size_t right,
                                         size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_gateway_dispatch_ranges_overlap(const void *left,
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

static bool db_gateway_dispatch_address_is_canonical(uint64_t value)
{
    const uint64_t upper = value >> 48u;
    const uint64_t sign = (value >> 47u) & 1u;

    return (sign == 0u && upper == 0u) ||
           (sign != 0u && upper == UINT64_C(0xffff));
}

static bool db_gateway_dispatch_lookup_reserved_is_zero(
    const db_gateway_lookup_result *lookup)
{
    size_t index;

    for (index = 0u; index < sizeof(lookup->reserved); ++index) {
        if (lookup->reserved[index] != 0u) {
            return false;
        }
    }

    return true;
}

static db_gateway_dispatch_status db_gateway_dispatch_validate_lookup(
    const db_gateway_lookup_result *lookup)
{
    if (lookup->kind != DB_GATEWAY_LOOKUP_ENTRY) {
        if (lookup->kind == DB_GATEWAY_LOOKUP_MISS ||
            lookup->kind == DB_GATEWAY_LOOKUP_INTERIOR) {
            return DB_GATEWAY_DISPATCH_NOT_ENTRY;
        }

        return DB_GATEWAY_DISPATCH_BAD_LOOKUP_RESULT;
    }

    if (lookup->rip == 0u ||
        lookup->slot_address == 0u ||
        lookup->rip != lookup->slot_address ||
        !db_gateway_dispatch_address_is_canonical(lookup->rip) ||
        lookup->slot_index == SIZE_MAX ||
        lookup->provider_id == 0u ||
        lookup->symbol_id == 0u ||
        !db_gateway_dispatch_lookup_reserved_is_zero(lookup)) {
        return DB_GATEWAY_DISPATCH_BAD_LOOKUP_RESULT;
    }

    return DB_GATEWAY_DISPATCH_OK;
}

static db_gateway_dispatch_status db_gateway_dispatch_validate_catalog(
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count)
{
    size_t index;

    for (index = 0u; index < handler_count; ++index) {
        const db_gateway_dispatch_handler *handler = &handlers[index];
        size_t other;

        if (handler->provider_id == 0u ||
            handler->symbol_id == 0u ||
            handler->handler_id == 0u ||
            handler->reserved != 0u) {
            return DB_GATEWAY_DISPATCH_BAD_HANDLER;
        }

        for (other = 0u; other < index; ++other) {
            if (handler->provider_id == handlers[other].provider_id &&
                handler->symbol_id == handlers[other].symbol_id) {
                return DB_GATEWAY_DISPATCH_DUPLICATE_HANDLER_KEY;
            }

            if (handler->handler_id == handlers[other].handler_id) {
                return DB_GATEWAY_DISPATCH_DUPLICATE_HANDLER_ID;
            }
        }
    }

    return DB_GATEWAY_DISPATCH_OK;
}

static const db_gateway_dispatch_handler *db_gateway_dispatch_find_handler(
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    const db_gateway_lookup_result *lookup)
{
    size_t index;

    for (index = 0u; index < handler_count; ++index) {
        if (handlers[index].provider_id == lookup->provider_id &&
            handlers[index].symbol_id == lookup->symbol_id) {
            return &handlers[index];
        }
    }

    return NULL;
}

db_gateway_dispatch_status db_gateway_dispatch_plan_entry(
    const db_gateway_lookup_result *lookup,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    db_gateway_dispatch_plan *plan)
{
    db_gateway_dispatch_plan planned;
    db_gateway_dispatch_status status;
    const db_gateway_dispatch_handler *handler;
    size_t handler_bytes;

    if (lookup == NULL || plan == NULL ||
        (handler_count != 0u && handlers == NULL)) {
        return DB_GATEWAY_DISPATCH_NULL_ARGUMENT;
    }

    if (!db_gateway_dispatch_mul_size(handler_count,
                                      sizeof(*handlers),
                                      &handler_bytes)) {
        return DB_GATEWAY_DISPATCH_COUNT_OVERFLOW;
    }

    if (db_gateway_dispatch_ranges_overlap(lookup,
                                           sizeof(*lookup),
                                           plan,
                                           sizeof(*plan)) ||
        db_gateway_dispatch_ranges_overlap(handlers,
                                           handler_bytes,
                                           plan,
                                           sizeof(*plan)) ||
        db_gateway_dispatch_ranges_overlap(lookup,
                                           sizeof(*lookup),
                                           handlers,
                                           handler_bytes)) {
        return DB_GATEWAY_DISPATCH_BUFFER_OVERLAP;
    }

    status = db_gateway_dispatch_validate_lookup(lookup);
    if (status != DB_GATEWAY_DISPATCH_OK) {
        return status;
    }

    status = db_gateway_dispatch_validate_catalog(handlers, handler_count);
    if (status != DB_GATEWAY_DISPATCH_OK) {
        return status;
    }

    handler = db_gateway_dispatch_find_handler(handlers,
                                               handler_count,
                                               lookup);
    if (handler == NULL) {
        return DB_GATEWAY_DISPATCH_HANDLER_NOT_FOUND;
    }

    planned.rip = lookup->rip;
    planned.slot_address = lookup->slot_address;
    planned.slot_index = lookup->slot_index;
    planned.provider_id = lookup->provider_id;
    planned.symbol_id = lookup->symbol_id;
    planned.handler_id = handler->handler_id;
    planned.reserved = 0u;
    *plan = planned;

    return DB_GATEWAY_DISPATCH_OK;
}

const char *db_gateway_dispatch_status_name(
    db_gateway_dispatch_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "gateway result is not an entry",
        "bad gateway lookup result",
        "bad dispatch handler",
        "duplicate dispatch handler key",
        "duplicate dispatch handler id",
        "dispatch handler not found"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway dispatch status";
    }

    return names[(size_t)status];
}
