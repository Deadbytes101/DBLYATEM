#include "dblyat/arm64_code_image.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_arm64_code_image_overlap(const void *left,
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

static bool db_arm64_code_image_canonical_nonzero(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return address != 0u &&
           (upper == UINT64_C(0) ||
            (upper == UINT64_C(0xffff) && sign != 0u));
}

static bool db_arm64_code_image_page_size_valid(uint64_t page_size)
{
    return page_size >= UINT64_C(4096) &&
           (page_size & (page_size - UINT64_C(1))) == 0u;
}

static bool db_arm64_code_image_add(uint64_t left,
                                    uint64_t right,
                                    uint64_t *result)
{
    if (UINT64_MAX - left < right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool db_arm64_code_image_round_up(uint64_t value,
                                         uint64_t alignment,
                                         uint64_t *result)
{
    const uint64_t mask = alignment - UINT64_C(1);

    if (UINT64_MAX - value < mask) {
        return false;
    }

    *result = (value + mask) & ~mask;
    return true;
}

db_arm64_code_image_status db_arm64_code_image_build(
    const uint32_t *words,
    uint32_t word_count,
    uint64_t code_base,
    uint32_t entry_word_index,
    uint64_t page_size,
    uint8_t *output_bytes,
    size_t output_capacity,
    db_arm64_code_image_plan *plan)
{
    db_arm64_code_image_plan completed = {0};
    uint64_t byte_count_u64;
    uint64_t entry_offset;
    uint64_t allocation_size;
    uint64_t image_end;
    uint64_t allocation_end;
    uint64_t entry_address;
    size_t word_bytes;
    size_t byte_count;
    uint32_t index;

    if (words == NULL || output_bytes == NULL || plan == NULL) {
        return DB_ARM64_CODE_IMAGE_NULL_ARGUMENT;
    }

    if (word_count == 0u) {
        return DB_ARM64_CODE_IMAGE_BAD_WORD_COUNT;
    }

#if SIZE_MAX < UINT64_MAX
    if ((uint64_t)word_count >
        (uint64_t)(SIZE_MAX / sizeof(*words))) {
        return DB_ARM64_CODE_IMAGE_SIZE_OVERFLOW;
    }
#endif

    word_bytes = (size_t)word_count * sizeof(*words);
    byte_count = word_bytes;
    byte_count_u64 = (uint64_t)word_count * UINT64_C(4);

    if (!db_arm64_code_image_canonical_nonzero(code_base) ||
        (code_base & UINT64_C(3)) != 0u) {
        return DB_ARM64_CODE_IMAGE_BAD_CODE_BASE;
    }

    if (entry_word_index >= word_count) {
        return DB_ARM64_CODE_IMAGE_BAD_ENTRY_INDEX;
    }

    if (!db_arm64_code_image_page_size_valid(page_size)) {
        return DB_ARM64_CODE_IMAGE_BAD_PAGE_SIZE;
    }

    if ((code_base & (page_size - UINT64_C(1))) != 0u) {
        return DB_ARM64_CODE_IMAGE_BAD_CODE_BASE;
    }

    if (output_capacity != byte_count) {
        return DB_ARM64_CODE_IMAGE_OUTPUT_CAPACITY;
    }

    if (db_arm64_code_image_overlap(words, word_bytes,
                                    output_bytes, output_capacity) ||
        db_arm64_code_image_overlap(words, word_bytes,
                                    plan, sizeof(*plan)) ||
        db_arm64_code_image_overlap(output_bytes, output_capacity,
                                    plan, sizeof(*plan))) {
        return DB_ARM64_CODE_IMAGE_BUFFER_OVERLAP;
    }

    entry_offset = (uint64_t)entry_word_index * UINT64_C(4);

    if (!db_arm64_code_image_round_up(byte_count_u64,
                                      page_size,
                                      &allocation_size) ||
        allocation_size == 0u ||
        !db_arm64_code_image_add(code_base,
                                 byte_count_u64 - UINT64_C(1),
                                 &image_end) ||
        !db_arm64_code_image_add(code_base,
                                 allocation_size - UINT64_C(1),
                                 &allocation_end) ||
        !db_arm64_code_image_add(code_base,
                                 entry_offset,
                                 &entry_address) ||
        !db_arm64_code_image_canonical_nonzero(image_end) ||
        !db_arm64_code_image_canonical_nonzero(allocation_end) ||
        !db_arm64_code_image_canonical_nonzero(entry_address)) {
        return DB_ARM64_CODE_IMAGE_SIZE_OVERFLOW;
    }

    completed.code_base = code_base;
    completed.entry_address = entry_address;
    completed.image_end_address = image_end;
    completed.allocation_end_address = allocation_end;
    completed.page_size = page_size;
    completed.allocation_size = allocation_size;
    completed.instruction_cache_offset = 0u;
    completed.instruction_cache_size = byte_count_u64;
    completed.format_version = DB_ARM64_CODE_IMAGE_FORMAT_VERSION;
    completed.word_count = word_count;
    completed.byte_count = byte_count_u64;
    completed.entry_byte_offset = entry_offset;
    completed.entry_word_index = entry_word_index;
    completed.initial_permissions =
        DB_ARM64_CODE_PERMISSION_READ |
        DB_ARM64_CODE_PERMISSION_WRITE;
    completed.final_permissions =
        DB_ARM64_CODE_PERMISSION_READ |
        DB_ARM64_CODE_PERMISSION_EXECUTE;

    for (index = 0u; index < word_count; ++index) {
        const uint32_t word = words[index];
        const size_t offset = (size_t)index * sizeof(word);

        output_bytes[offset] = (uint8_t)word;
        output_bytes[offset + 1u] = (uint8_t)(word >> 8u);
        output_bytes[offset + 2u] = (uint8_t)(word >> 16u);
        output_bytes[offset + 3u] = (uint8_t)(word >> 24u);
    }

    *plan = completed;
    return DB_ARM64_CODE_IMAGE_OK;
}

const char *db_arm64_code_image_status_name(
    db_arm64_code_image_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad word count",
        "bad code base",
        "bad entry index",
        "bad page size",
        "size overflow",
        "output capacity mismatch"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown ARM64 code image status";
    }

    return names[(size_t)status];
}
