#include "dblyat/iat_apply.h"

#include <stdbool.h>
#include <stdint.h>

#define DB_IAT_APPLY_ENTRY_SIZE 8u

static bool db_iat_apply_mul_size(size_t left,
                                  size_t right,
                                  size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_iat_apply_is_power_of_two(size_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static size_t db_iat_apply_page_count(size_t image_size, size_t page_size)
{
    return image_size / page_size +
           (image_size % page_size != 0u ? 1u : 0u);
}

static bool db_iat_apply_span_fits(size_t total,
                                   size_t offset,
                                   size_t length)
{
    return offset <= total && length <= total - offset;
}

static bool db_iat_apply_ranges_overlap(const void *left,
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

static bool db_iat_apply_page_value_is_valid(
    db_map_page_protection protection)
{
    return protection == DB_MAP_PAGE_NONE ||
           protection == DB_MAP_PAGE_R ||
           protection == DB_MAP_PAGE_RW ||
           protection == DB_MAP_PAGE_RX;
}

static bool db_iat_apply_guest_value_is_valid(uint64_t value)
{
    const uint64_t upper = value >> 48u;
    const uint64_t sign = (value >> 47u) & 1u;

    return value != 0u &&
           ((sign == 0u && upper == 0u) ||
            (sign != 0u && upper == UINT64_C(0xffff)));
}

static uint64_t db_iat_apply_read_u64(const uint8_t *bytes, size_t offset)
{
    return (uint64_t)bytes[offset] |
           ((uint64_t)bytes[offset + 1u] << 8u) |
           ((uint64_t)bytes[offset + 2u] << 16u) |
           ((uint64_t)bytes[offset + 3u] << 24u) |
           ((uint64_t)bytes[offset + 4u] << 32u) |
           ((uint64_t)bytes[offset + 5u] << 40u) |
           ((uint64_t)bytes[offset + 6u] << 48u) |
           ((uint64_t)bytes[offset + 7u] << 56u);
}

static void db_iat_apply_write_u64(uint8_t *bytes,
                                   size_t offset,
                                   uint64_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
    bytes[offset + 2u] = (uint8_t)(value >> 16u);
    bytes[offset + 3u] = (uint8_t)(value >> 24u);
    bytes[offset + 4u] = (uint8_t)(value >> 32u);
    bytes[offset + 5u] = (uint8_t)(value >> 40u);
    bytes[offset + 6u] = (uint8_t)(value >> 48u);
    bytes[offset + 7u] = (uint8_t)(value >> 56u);
}

static db_iat_apply_status db_iat_apply_validate_aliases(
    void *mapped_image,
    size_t mapped_size,
    const db_map_page_protection *page_plan,
    size_t page_bytes,
    const db_iat_patch *patches,
    size_t patch_bytes,
    db_iat_apply_summary *summary)
{
    if (db_iat_apply_ranges_overlap(mapped_image, mapped_size,
                                    page_plan, page_bytes) ||
        db_iat_apply_ranges_overlap(mapped_image, mapped_size,
                                    patches, patch_bytes) ||
        db_iat_apply_ranges_overlap(mapped_image, mapped_size,
                                    summary, sizeof(*summary)) ||
        db_iat_apply_ranges_overlap(page_plan, page_bytes,
                                    patches, patch_bytes) ||
        db_iat_apply_ranges_overlap(page_plan, page_bytes,
                                    summary, sizeof(*summary)) ||
        db_iat_apply_ranges_overlap(patches, patch_bytes,
                                    summary, sizeof(*summary))) {
        return DB_IAT_APPLY_BUFFER_OVERLAP;
    }

    return DB_IAT_APPLY_OK;
}

static db_iat_apply_status db_iat_apply_validate_page_plan(
    const db_map_page_protection *page_plan,
    size_t page_count)
{
    size_t page;

    for (page = 0u; page < page_count; ++page) {
        if (!db_iat_apply_page_value_is_valid(page_plan[page])) {
            return DB_IAT_APPLY_BAD_PAGE_PLAN;
        }
    }

    return DB_IAT_APPLY_OK;
}

static db_iat_apply_status db_iat_apply_validate_patch(
    const uint8_t *image,
    size_t mapped_size,
    size_t page_size,
    const db_map_page_protection *page_plan,
    const db_iat_patch *patches,
    size_t index,
    uint32_t *first_rva,
    uint32_t *last_end)
{
    const db_iat_patch *patch = &patches[index];
    const size_t offset = (size_t)patch->iat_rva;
    size_t first_page;
    size_t last_page;
    size_t page;
    size_t other;
    uint32_t end;

    if (patch->record_index != index ||
        patch->provider_id == 0u ||
        patch->symbol_id == 0u ||
        patch->iat_rva == 0u ||
        patch->reserved != 0u ||
        !db_iat_apply_guest_value_is_valid(patch->replacement_value)) {
        return DB_IAT_APPLY_BAD_PATCH;
    }

    if (patch->iat_rva > UINT32_MAX - DB_IAT_APPLY_ENTRY_SIZE ||
        !db_iat_apply_span_fits(mapped_size,
                                offset,
                                DB_IAT_APPLY_ENTRY_SIZE)) {
        return DB_IAT_APPLY_IAT_OUT_OF_RANGE;
    }

    first_page = offset / page_size;
    last_page = (offset + DB_IAT_APPLY_ENTRY_SIZE - 1u) / page_size;

    for (page = first_page; page <= last_page; ++page) {
        if (page_plan[page] != DB_MAP_PAGE_RW) {
            return DB_IAT_APPLY_IAT_PAGE_NOT_WRITABLE;
        }
    }

    for (other = 0u; other < index; ++other) {
        const uint32_t left = patch->iat_rva;
        const uint32_t right = patches[other].iat_rva;
        const uint32_t distance = left >= right
                                      ? left - right
                                      : right - left;

        if (distance < DB_IAT_APPLY_ENTRY_SIZE) {
            return DB_IAT_APPLY_IAT_OVERLAP;
        }
    }

    if (db_iat_apply_read_u64(image, offset) != patch->expected_value) {
        return DB_IAT_APPLY_STALE_IAT;
    }

    end = patch->iat_rva + DB_IAT_APPLY_ENTRY_SIZE;
    if (index == 0u || patch->iat_rva < *first_rva) {
        *first_rva = patch->iat_rva;
    }
    if (index == 0u || end > *last_end) {
        *last_end = end;
    }

    return DB_IAT_APPLY_OK;
}

db_iat_apply_status db_iat_apply_patches(
    void *mapped_image,
    size_t mapped_size,
    size_t page_size,
    const db_map_page_protection *page_plan,
    size_t page_capacity,
    const db_iat_patch *patches,
    size_t patch_count,
    db_iat_apply_summary *summary)
{
    uint8_t *image = (uint8_t *)mapped_image;
    db_iat_apply_summary applied;
    db_iat_apply_status status;
    size_t page_count;
    size_t page_bytes;
    size_t patch_bytes;
    size_t index;
    uint32_t first_rva = 0u;
    uint32_t last_end = 0u;

    if (mapped_image == NULL || summary == NULL ||
        (patch_count != 0u && patches == NULL)) {
        return DB_IAT_APPLY_NULL_ARGUMENT;
    }

    if (!db_iat_apply_is_power_of_two(page_size)) {
        return DB_IAT_APPLY_INVALID_PAGE_SIZE;
    }

    page_count = db_iat_apply_page_count(mapped_size, page_size);
    if (page_capacity != 0u && page_plan == NULL) {
        return DB_IAT_APPLY_NULL_ARGUMENT;
    }

    if (page_capacity < page_count) {
        return DB_IAT_APPLY_PAGE_PLAN_TOO_SMALL;
    }

    if (!db_iat_apply_mul_size(page_capacity,
                               sizeof(*page_plan),
                               &page_bytes) ||
        !db_iat_apply_mul_size(patch_count,
                               sizeof(*patches),
                               &patch_bytes)) {
        return DB_IAT_APPLY_COUNT_OVERFLOW;
    }

    status = db_iat_apply_validate_aliases(mapped_image,
                                           mapped_size,
                                           page_plan,
                                           page_bytes,
                                           patches,
                                           patch_bytes,
                                           summary);
    if (status != DB_IAT_APPLY_OK) {
        return status;
    }

    status = db_iat_apply_validate_page_plan(page_plan, page_count);
    if (status != DB_IAT_APPLY_OK) {
        return status;
    }

    for (index = 0u; index < patch_count; ++index) {
        status = db_iat_apply_validate_patch(image,
                                             mapped_size,
                                             page_size,
                                             page_plan,
                                             patches,
                                             index,
                                             &first_rva,
                                             &last_end);
        if (status != DB_IAT_APPLY_OK) {
            return status;
        }
    }

    for (index = 0u; index < patch_count; ++index) {
        db_iat_apply_write_u64(image,
                               (size_t)patches[index].iat_rva,
                               patches[index].replacement_value);
    }

    applied.patch_count = patch_count;
    applied.bytes_written = patch_count * DB_IAT_APPLY_ENTRY_SIZE;
    applied.first_iat_rva = first_rva;
    applied.last_iat_end = last_end;
    *summary = applied;

    return DB_IAT_APPLY_OK;
}

const char *db_iat_apply_status_name(db_iat_apply_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "invalid host page size",
        "count overflows",
        "buffer overlap",
        "page plan is too small",
        "bad page plan",
        "bad patch",
        "IAT span is out of range",
        "IAT page is not writable",
        "IAT spans overlap",
        "stale IAT value"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown IAT apply status";
    }

    return names[(size_t)status];
}
