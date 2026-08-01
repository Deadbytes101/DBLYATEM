#ifndef DBLYAT_ARM64_CODE_IMAGE_H
#define DBLYAT_ARM64_CODE_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define DB_ARM64_CODE_IMAGE_FORMAT_VERSION UINT32_C(1)

#define DB_ARM64_CODE_PERMISSION_READ UINT8_C(0x01)
#define DB_ARM64_CODE_PERMISSION_WRITE UINT8_C(0x02)
#define DB_ARM64_CODE_PERMISSION_EXECUTE UINT8_C(0x04)

typedef enum db_arm64_code_image_status {
    DB_ARM64_CODE_IMAGE_OK = 0,
    DB_ARM64_CODE_IMAGE_NULL_ARGUMENT,
    DB_ARM64_CODE_IMAGE_BUFFER_OVERLAP,
    DB_ARM64_CODE_IMAGE_BAD_WORD_COUNT,
    DB_ARM64_CODE_IMAGE_BAD_CODE_BASE,
    DB_ARM64_CODE_IMAGE_BAD_ENTRY_INDEX,
    DB_ARM64_CODE_IMAGE_BAD_PAGE_SIZE,
    DB_ARM64_CODE_IMAGE_SIZE_OVERFLOW,
    DB_ARM64_CODE_IMAGE_OUTPUT_CAPACITY
} db_arm64_code_image_status;

typedef struct db_arm64_code_image_plan {
    uint64_t code_base;
    uint64_t entry_address;
    uint64_t image_end_address;
    uint64_t allocation_end_address;
    uint64_t page_size;
    uint64_t allocation_size;
    uint64_t instruction_cache_offset;
    uint64_t instruction_cache_size;
    uint32_t format_version;
    uint32_t word_count;
    uint64_t byte_count;
    uint64_t entry_byte_offset;
    uint32_t entry_word_index;
    uint8_t initial_permissions;
    uint8_t final_permissions;
    uint8_t reserved[6];
} db_arm64_code_image_plan;

db_arm64_code_image_status db_arm64_code_image_build(
    const uint32_t *words,
    uint32_t word_count,
    uint64_t code_base,
    uint32_t entry_word_index,
    uint64_t page_size,
    uint8_t *output_bytes,
    size_t output_capacity,
    db_arm64_code_image_plan *plan);

const char *db_arm64_code_image_status_name(
    db_arm64_code_image_status status);

#endif
