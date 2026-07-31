#include "dblyat/gateway_lookup.h"

#include <stdbool.h>
#include <stdint.h>

#define DB_GATEWAY_LOOKUP_MIN_SLOT_SIZE UINT64_C(32)
#define DB_GATEWAY_LOOKUP_LOW_MAX UINT64_C(0x00007fffffffffff)
#define DB_GATEWAY_LOOKUP_HIGH_MIN UINT64_C(0xffff800000000000)

static bool db_gateway_lookup_mul_size(size_t left,
                                       size_t right,
                                       size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_gateway_lookup_ranges_overlap(const void *left,
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

static bool db_gateway_lookup_is_power_of_two(uint64_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool db_gateway_lookup_is_canonical(uint64_t value)
{
    return value <= DB_GATEWAY_LOOKUP_LOW_MAX ||
           value >= DB_GATEWAY_LOOKUP_HIGH_MIN;
}

static bool db_gateway_lookup_region_is_canonical(uint64_t begin,
                                                   uint64_t size)
{
    uint64_t last;

    if (!db_gateway_lookup_is_canonical(begin)) {
        return false;
    }

    if (size == 0u) {
        return true;
    }

    last = begin + size - 1u;
    if (begin <= DB_GATEWAY_LOOKUP_LOW_MAX) {
        return last <= DB_GATEWAY_LOOKUP_LOW_MAX;
    }

    return last >= DB_GATEWAY_LOOKUP_HIGH_MIN;
}

static db_gateway_lookup_status db_gateway_lookup_validate_catalog(
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    uint64_t *used_size)
{
    size_t index;

    if (target_count > UINT64_MAX / slot_size) {
        return DB_GATEWAY_LOOKUP_COUNT_OVERFLOW;
    }

    *used_size = (uint64_t)target_count * slot_size;
    if (*used_size != 0u &&
        gateway_base > UINT64_MAX - (*used_size - 1u)) {
        return DB_GATEWAY_LOOKUP_ADDRESS_OVERFLOW;
    }

    if (!db_gateway_lookup_region_is_canonical(gateway_base, *used_size)) {
        return DB_GATEWAY_LOOKUP_NONCANONICAL_ADDRESS;
    }

    for (index = 0u; index < target_count; ++index) {
        const db_iat_target *target = &targets[index];
        const uint64_t expected =
            gateway_base + (uint64_t)index * slot_size;
        size_t other;

        if (target->provider_id == 0u || target->symbol_id == 0u) {
            return DB_GATEWAY_LOOKUP_BAD_TARGET;
        }

        if (target->guest_value != expected) {
            return DB_GATEWAY_LOOKUP_TARGET_ADDRESS_MISMATCH;
        }

        if (!db_gateway_lookup_is_canonical(target->guest_value)) {
            return DB_GATEWAY_LOOKUP_NONCANONICAL_ADDRESS;
        }

        for (other = 0u; other < index; ++other) {
            if (target->provider_id == targets[other].provider_id &&
                target->symbol_id == targets[other].symbol_id) {
                return DB_GATEWAY_LOOKUP_DUPLICATE_TARGET_KEY;
            }
        }
    }

    return DB_GATEWAY_LOOKUP_OK;
}

db_gateway_lookup_status db_gateway_lookup_rip(
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    uint64_t guest_rip,
    db_gateway_lookup_result *result)
{
    db_gateway_lookup_result planned;
    db_gateway_lookup_status status;
    size_t target_bytes;
    uint64_t used_size;

    if (result == NULL || (target_count != 0u && targets == NULL)) {
        return DB_GATEWAY_LOOKUP_NULL_ARGUMENT;
    }

    if (!db_gateway_lookup_mul_size(target_count,
                                    sizeof(*targets),
                                    &target_bytes)) {
        return DB_GATEWAY_LOOKUP_COUNT_OVERFLOW;
    }

    if (db_gateway_lookup_ranges_overlap(targets,
                                         target_bytes,
                                         result,
                                         sizeof(*result))) {
        return DB_GATEWAY_LOOKUP_BUFFER_OVERLAP;
    }

    if (!db_gateway_lookup_is_power_of_two(slot_size) ||
        slot_size < DB_GATEWAY_LOOKUP_MIN_SLOT_SIZE) {
        return DB_GATEWAY_LOOKUP_INVALID_SLOT_SIZE;
    }

    if (gateway_base == 0u ||
        !db_gateway_lookup_is_canonical(gateway_base) ||
        (gateway_base & (slot_size - 1u)) != 0u) {
        return DB_GATEWAY_LOOKUP_INVALID_GATEWAY_BASE;
    }

    status = db_gateway_lookup_validate_catalog(targets,
                                                target_count,
                                                gateway_base,
                                                slot_size,
                                                &used_size);
    if (status != DB_GATEWAY_LOOKUP_OK) {
        return status;
    }

    if (!db_gateway_lookup_is_canonical(guest_rip)) {
        return DB_GATEWAY_LOOKUP_NONCANONICAL_ADDRESS;
    }

    planned.rip = guest_rip;
    planned.slot_address = 0u;
    planned.slot_index = SIZE_MAX;
    planned.provider_id = 0u;
    planned.symbol_id = 0u;
    planned.kind = DB_GATEWAY_LOOKUP_MISS;
    planned.reserved[0] = 0u;
    planned.reserved[1] = 0u;
    planned.reserved[2] = 0u;
    planned.reserved[3] = 0u;
    planned.reserved[4] = 0u;
    planned.reserved[5] = 0u;
    planned.reserved[6] = 0u;

    if (used_size != 0u &&
        guest_rip >= gateway_base &&
        guest_rip - gateway_base < used_size) {
        const uint64_t offset = guest_rip - gateway_base;
        const size_t slot_index = (size_t)(offset / slot_size);

        planned.slot_index = slot_index;
        planned.slot_address = targets[slot_index].guest_value;

        if ((offset & (slot_size - 1u)) == 0u) {
            planned.kind = DB_GATEWAY_LOOKUP_ENTRY;
            planned.provider_id = targets[slot_index].provider_id;
            planned.symbol_id = targets[slot_index].symbol_id;
        } else {
            planned.kind = DB_GATEWAY_LOOKUP_INTERIOR;
        }
    }

    *result = planned;
    return DB_GATEWAY_LOOKUP_OK;
}

const char *db_gateway_lookup_status_name(db_gateway_lookup_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "invalid gateway slot size",
        "invalid gateway base",
        "bad gateway target",
        "duplicate gateway target key",
        "gateway target address mismatch",
        "gateway address overflows",
        "noncanonical gateway address"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway lookup status";
    }

    return names[(size_t)status];
}
