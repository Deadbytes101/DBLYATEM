#include "dblyat/gateway_emit.h"

#include <stdbool.h>
#include <stdint.h>

#define DB_GATEWAY_EMIT_MIN_SLOT_SIZE UINT64_C(32)
#define DB_GATEWAY_EMIT_FORMAT_VERSION UINT16_C(1)
#define DB_GATEWAY_EMIT_LOW_MAX UINT64_C(0x00007fffffffffff)
#define DB_GATEWAY_EMIT_HIGH_MIN UINT64_C(0xffff800000000000)
#define DB_GATEWAY_EMIT_INT3 UINT8_C(0xcc)

static bool db_gateway_emit_mul_size(size_t left,
                                     size_t right,
                                     size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_gateway_emit_ranges_overlap(const void *left,
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

static bool db_gateway_emit_is_power_of_two(uint64_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool db_gateway_emit_is_canonical(uint64_t value)
{
    return value <= DB_GATEWAY_EMIT_LOW_MAX ||
           value >= DB_GATEWAY_EMIT_HIGH_MIN;
}

static bool db_gateway_emit_region_is_canonical(uint64_t begin,
                                                uint64_t size)
{
    uint64_t last;

    if (!db_gateway_emit_is_canonical(begin)) {
        return false;
    }

    if (size == 0u) {
        return true;
    }

    last = begin + size - 1u;
    if (begin <= DB_GATEWAY_EMIT_LOW_MAX) {
        return last <= DB_GATEWAY_EMIT_LOW_MAX;
    }

    return last >= DB_GATEWAY_EMIT_HIGH_MIN;
}

static void db_gateway_emit_put_u16(uint8_t *bytes,
                                    size_t offset,
                                    uint16_t value)
{
    bytes[offset] = (uint8_t)(value & UINT16_C(0x00ff));
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
}

static void db_gateway_emit_put_u32(uint8_t *bytes,
                                    size_t offset,
                                    uint32_t value)
{
    bytes[offset] = (uint8_t)(value & UINT32_C(0x000000ff));
    bytes[offset + 1u] = (uint8_t)((value >> 8u) & UINT32_C(0xff));
    bytes[offset + 2u] = (uint8_t)((value >> 16u) & UINT32_C(0xff));
    bytes[offset + 3u] = (uint8_t)(value >> 24u);
}

static void db_gateway_emit_put_u64(uint8_t *bytes,
                                    size_t offset,
                                    uint64_t value)
{
    db_gateway_emit_put_u32(bytes, offset, (uint32_t)value);
    db_gateway_emit_put_u32(bytes, offset + 4u, (uint32_t)(value >> 32u));
}

static void db_gateway_emit_fill(uint8_t *bytes,
                                 size_t size,
                                 uint8_t value)
{
    size_t index;

    for (index = 0u; index < size; ++index) {
        bytes[index] = value;
    }
}

static db_gateway_emit_status db_gateway_emit_validate_targets(
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size)
{
    size_t index;

    for (index = 0u; index < target_count; ++index) {
        const db_iat_target *target = &targets[index];
        const uint64_t expected =
            gateway_base + (uint64_t)index * slot_size;
        size_t other;

        if (target->provider_id == 0u || target->symbol_id == 0u) {
            return DB_GATEWAY_EMIT_BAD_TARGET;
        }

        if (target->guest_value != expected) {
            return DB_GATEWAY_EMIT_TARGET_ADDRESS_MISMATCH;
        }

        if (!db_gateway_emit_is_canonical(target->guest_value)) {
            return DB_GATEWAY_EMIT_NONCANONICAL_ADDRESS;
        }

        for (other = 0u; other < index; ++other) {
            if (target->provider_id == targets[other].provider_id &&
                target->symbol_id == targets[other].symbol_id) {
                return DB_GATEWAY_EMIT_DUPLICATE_TARGET_KEY;
            }
        }
    }

    return DB_GATEWAY_EMIT_OK;
}

db_gateway_emit_status db_gateway_emit_slots(
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    void *gateway_buffer,
    size_t gateway_buffer_size,
    db_gateway_emit_summary *summary)
{
    uint8_t *bytes = (uint8_t *)gateway_buffer;
    db_gateway_emit_summary planned;
    db_gateway_emit_status status;
    uint64_t used_u64;
    size_t used_size;
    size_t target_bytes;
    size_t index;

    if (summary == NULL ||
        (target_count != 0u && targets == NULL) ||
        (gateway_buffer_size != 0u && gateway_buffer == NULL)) {
        return DB_GATEWAY_EMIT_NULL_ARGUMENT;
    }

    if (!db_gateway_emit_is_power_of_two(slot_size) ||
        slot_size < DB_GATEWAY_EMIT_MIN_SLOT_SIZE) {
        return DB_GATEWAY_EMIT_INVALID_SLOT_SIZE;
    }

    if (gateway_base == 0u ||
        !db_gateway_emit_is_canonical(gateway_base) ||
        (gateway_base & (slot_size - 1u)) != 0u) {
        return DB_GATEWAY_EMIT_INVALID_GATEWAY_BASE;
    }

    if (target_count > UINT64_MAX / slot_size) {
        return DB_GATEWAY_EMIT_COUNT_OVERFLOW;
    }

    used_u64 = (uint64_t)target_count * slot_size;
    if (used_u64 > (uint64_t)SIZE_MAX ||
        slot_size > (uint64_t)SIZE_MAX ||
        !db_gateway_emit_mul_size(target_count,
                                  sizeof(*targets),
                                  &target_bytes)) {
        return DB_GATEWAY_EMIT_COUNT_OVERFLOW;
    }
    used_size = (size_t)used_u64;

    if (used_u64 != 0u &&
        gateway_base > UINT64_MAX - (used_u64 - 1u)) {
        return DB_GATEWAY_EMIT_ADDRESS_OVERFLOW;
    }

    if (!db_gateway_emit_region_is_canonical(gateway_base, used_u64)) {
        return DB_GATEWAY_EMIT_NONCANONICAL_ADDRESS;
    }

    if (gateway_buffer_size < used_size) {
        return DB_GATEWAY_EMIT_BUFFER_TOO_SMALL;
    }

    if (used_size != 0u && gateway_buffer == NULL) {
        return DB_GATEWAY_EMIT_NULL_ARGUMENT;
    }

    if (db_gateway_emit_ranges_overlap(targets, target_bytes,
                                       gateway_buffer,
                                       gateway_buffer_size) ||
        db_gateway_emit_ranges_overlap(targets, target_bytes,
                                       summary, sizeof(*summary)) ||
        db_gateway_emit_ranges_overlap(gateway_buffer,
                                       gateway_buffer_size,
                                       summary, sizeof(*summary))) {
        return DB_GATEWAY_EMIT_BUFFER_OVERLAP;
    }

    status = db_gateway_emit_validate_targets(targets,
                                              target_count,
                                              gateway_base,
                                              slot_size);
    if (status != DB_GATEWAY_EMIT_OK) {
        return status;
    }

    for (index = 0u; index < target_count; ++index) {
        const db_iat_target *target = &targets[index];
        const size_t offset = index * (size_t)slot_size;
        uint8_t *slot = bytes + offset;
        size_t reserved;

        db_gateway_emit_fill(slot, (size_t)slot_size,
                             DB_GATEWAY_EMIT_INT3);
        slot[0] = UINT8_C(0x0f);
        slot[1] = UINT8_C(0x0b);
        db_gateway_emit_put_u16(slot, 2u,
                                DB_GATEWAY_EMIT_FORMAT_VERSION);
        slot[4] = (uint8_t)'D';
        slot[5] = (uint8_t)'B';
        slot[6] = (uint8_t)'G';
        slot[7] = (uint8_t)'T';
        db_gateway_emit_put_u32(slot, 8u, target->provider_id);
        db_gateway_emit_put_u32(slot, 12u, target->symbol_id);
        db_gateway_emit_put_u64(slot, 16u, target->guest_value);

        for (reserved = 24u; reserved < 32u; ++reserved) {
            slot[reserved] = 0u;
        }
    }

    planned.target_count = target_count;
    planned.bytes_written = used_size;
    planned.gateway_base = gateway_base;
    planned.slot_size = slot_size;
    planned.format_version = DB_GATEWAY_EMIT_FORMAT_VERSION;
    *summary = planned;

    return DB_GATEWAY_EMIT_OK;
}

const char *db_gateway_emit_status_name(db_gateway_emit_status status)
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
        "noncanonical gateway address",
        "gateway buffer is too small"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway emit status";
    }

    return names[(size_t)status];
}
