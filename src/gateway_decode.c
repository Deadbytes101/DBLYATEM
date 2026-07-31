#include "dblyat/gateway_decode.h"

#include <stdbool.h>
#include <stdint.h>

#define DB_GATEWAY_DECODE_MIN_SLOT_SIZE UINT64_C(32)
#define DB_GATEWAY_DECODE_FORMAT_VERSION UINT16_C(1)
#define DB_GATEWAY_DECODE_LOW_MAX UINT64_C(0x00007fffffffffff)
#define DB_GATEWAY_DECODE_HIGH_MIN UINT64_C(0xffff800000000000)

static bool db_gateway_decode_mul_size(size_t left,
                                       size_t right,
                                       size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_gateway_decode_ranges_overlap(const void *left,
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

static bool db_gateway_decode_is_power_of_two(uint64_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool db_gateway_decode_is_canonical(uint64_t value)
{
    return value <= DB_GATEWAY_DECODE_LOW_MAX ||
           value >= DB_GATEWAY_DECODE_HIGH_MIN;
}

static bool db_gateway_decode_region_is_canonical(uint64_t begin,
                                                  uint64_t size)
{
    uint64_t last;

    if (!db_gateway_decode_is_canonical(begin)) {
        return false;
    }

    if (size == 0u) {
        return true;
    }

    last = begin + size - 1u;
    if (begin <= DB_GATEWAY_DECODE_LOW_MAX) {
        return last <= DB_GATEWAY_DECODE_LOW_MAX;
    }

    return last >= DB_GATEWAY_DECODE_HIGH_MIN;
}

static uint16_t db_gateway_decode_read_u16(const uint8_t *bytes,
                                           size_t offset)
{
    return (uint16_t)((uint16_t)bytes[offset] |
                      ((uint16_t)bytes[offset + 1u] << 8u));
}

static uint32_t db_gateway_decode_read_u32(const uint8_t *bytes,
                                           size_t offset)
{
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t db_gateway_decode_read_u64(const uint8_t *bytes,
                                           size_t offset)
{
    return (uint64_t)db_gateway_decode_read_u32(bytes, offset) |
           ((uint64_t)db_gateway_decode_read_u32(bytes, offset + 4u) << 32u);
}

static db_gateway_decode_status db_gateway_decode_validate_slot(
    const uint8_t *slot,
    size_t slot_size,
    uint64_t expected_address,
    const uint8_t *region,
    size_t index)
{
    const uint32_t provider_id = db_gateway_decode_read_u32(slot, 8u);
    const uint32_t symbol_id = db_gateway_decode_read_u32(slot, 12u);
    const uint64_t guest_value = db_gateway_decode_read_u64(slot, 16u);
    size_t cursor;
    size_t other;

    if (slot[0] != UINT8_C(0x0f) || slot[1] != UINT8_C(0x0b)) {
        return DB_GATEWAY_DECODE_BAD_OPCODE;
    }

    if (db_gateway_decode_read_u16(slot, 2u) !=
        DB_GATEWAY_DECODE_FORMAT_VERSION) {
        return DB_GATEWAY_DECODE_UNSUPPORTED_VERSION;
    }

    if (slot[4] != (uint8_t)'D' || slot[5] != (uint8_t)'B' ||
        slot[6] != (uint8_t)'G' || slot[7] != (uint8_t)'T') {
        return DB_GATEWAY_DECODE_BAD_MAGIC;
    }

    if (provider_id == 0u || symbol_id == 0u) {
        return DB_GATEWAY_DECODE_BAD_TARGET;
    }

    if (guest_value != expected_address) {
        return DB_GATEWAY_DECODE_TARGET_ADDRESS_MISMATCH;
    }

    for (cursor = 24u; cursor < 32u; ++cursor) {
        if (slot[cursor] != 0u) {
            return DB_GATEWAY_DECODE_RESERVED_NONZERO;
        }
    }

    for (cursor = 32u; cursor < slot_size; ++cursor) {
        if (slot[cursor] != UINT8_C(0xcc)) {
            return DB_GATEWAY_DECODE_BAD_PADDING;
        }
    }

    for (other = 0u; other < index; ++other) {
        const uint8_t *prior = region + other * slot_size;

        if (provider_id == db_gateway_decode_read_u32(prior, 8u) &&
            symbol_id == db_gateway_decode_read_u32(prior, 12u)) {
            return DB_GATEWAY_DECODE_DUPLICATE_TARGET_KEY;
        }
    }

    return DB_GATEWAY_DECODE_OK;
}

db_gateway_decode_status db_gateway_decode_slots(
    const void *gateway_buffer,
    size_t gateway_buffer_size,
    size_t slot_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    db_iat_target *targets,
    size_t target_capacity,
    db_gateway_decode_summary *summary)
{
    const uint8_t *bytes = (const uint8_t *)gateway_buffer;
    db_gateway_decode_summary planned;
    uint64_t used_u64;
    size_t used_size;
    size_t target_bytes;
    size_t index;

    if (summary == NULL ||
        (gateway_buffer_size != 0u && gateway_buffer == NULL) ||
        (target_capacity != 0u && targets == NULL)) {
        return DB_GATEWAY_DECODE_NULL_ARGUMENT;
    }

    if (!db_gateway_decode_is_power_of_two(slot_size) ||
        slot_size < DB_GATEWAY_DECODE_MIN_SLOT_SIZE) {
        return DB_GATEWAY_DECODE_INVALID_SLOT_SIZE;
    }

    if (gateway_base == 0u ||
        !db_gateway_decode_is_canonical(gateway_base) ||
        (gateway_base & (slot_size - 1u)) != 0u) {
        return DB_GATEWAY_DECODE_INVALID_GATEWAY_BASE;
    }

    if (slot_count > UINT64_MAX / slot_size) {
        return DB_GATEWAY_DECODE_COUNT_OVERFLOW;
    }

    used_u64 = (uint64_t)slot_count * slot_size;
    if (used_u64 > (uint64_t)SIZE_MAX ||
        slot_size > (uint64_t)SIZE_MAX ||
        !db_gateway_decode_mul_size(target_capacity,
                                    sizeof(*targets),
                                    &target_bytes)) {
        return DB_GATEWAY_DECODE_COUNT_OVERFLOW;
    }
    used_size = (size_t)used_u64;

    if (used_u64 != 0u &&
        gateway_base > UINT64_MAX - (used_u64 - 1u)) {
        return DB_GATEWAY_DECODE_ADDRESS_OVERFLOW;
    }

    if (!db_gateway_decode_region_is_canonical(gateway_base, used_u64)) {
        return DB_GATEWAY_DECODE_NONCANONICAL_ADDRESS;
    }

    if (gateway_buffer_size < used_size) {
        return DB_GATEWAY_DECODE_BUFFER_TOO_SMALL;
    }

    if (used_size != 0u && gateway_buffer == NULL) {
        return DB_GATEWAY_DECODE_NULL_ARGUMENT;
    }

    if (db_gateway_decode_ranges_overlap(gateway_buffer,
                                         gateway_buffer_size,
                                         targets,
                                         target_bytes) ||
        db_gateway_decode_ranges_overlap(gateway_buffer,
                                         gateway_buffer_size,
                                         summary,
                                         sizeof(*summary)) ||
        db_gateway_decode_ranges_overlap(targets,
                                         target_bytes,
                                         summary,
                                         sizeof(*summary))) {
        return DB_GATEWAY_DECODE_BUFFER_OVERLAP;
    }

    for (index = 0u; index < slot_count; ++index) {
        const size_t offset = index * (size_t)slot_size;
        const uint64_t expected = gateway_base + (uint64_t)offset;
        const db_gateway_decode_status status =
            db_gateway_decode_validate_slot(bytes + offset,
                                            (size_t)slot_size,
                                            expected,
                                            bytes,
                                            index);

        if (status != DB_GATEWAY_DECODE_OK) {
            return status;
        }
    }

    if (target_capacity < slot_count) {
        return DB_GATEWAY_DECODE_OUTPUT_BUFFER_TOO_SMALL;
    }

    for (index = 0u; index < slot_count; ++index) {
        const size_t offset = index * (size_t)slot_size;
        const uint8_t *slot = bytes + offset;
        db_iat_target target;

        target.provider_id = db_gateway_decode_read_u32(slot, 8u);
        target.symbol_id = db_gateway_decode_read_u32(slot, 12u);
        target.guest_value = db_gateway_decode_read_u64(slot, 16u);
        targets[index] = target;
    }

    planned.slot_count = slot_count;
    planned.bytes_consumed = used_size;
    planned.gateway_base = gateway_base;
    planned.slot_size = slot_size;
    planned.format_version = DB_GATEWAY_DECODE_FORMAT_VERSION;
    *summary = planned;

    return DB_GATEWAY_DECODE_OK;
}

const char *db_gateway_decode_status_name(db_gateway_decode_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "invalid gateway slot size",
        "invalid gateway base",
        "gateway buffer is too small",
        "bad gateway opcode",
        "unsupported gateway format version",
        "bad gateway magic",
        "bad gateway target",
        "duplicate gateway target key",
        "gateway target address mismatch",
        "gateway reserved bytes are nonzero",
        "bad gateway padding",
        "gateway address overflows",
        "noncanonical gateway address",
        "output buffer is too small"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway decode status";
    }

    return names[(size_t)status];
}
