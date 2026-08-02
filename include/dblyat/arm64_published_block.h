#ifndef DBLYAT_ARM64_PUBLISHED_BLOCK_H
#define DBLYAT_ARM64_PUBLISHED_BLOCK_H

#include "dblyat/arm64_code_image.h"
#include "dblyat/linux_arm64_wx_provider.h"
#include "dblyat/x64_block.h"

#include <stdint.h>

#define DB_ARM64_PUBLISHED_BLOCK_FORMAT_VERSION UINT32_C(1)

typedef enum db_arm64_published_block_status {
    DB_ARM64_PUBLISHED_BLOCK_OK = 0,
    DB_ARM64_PUBLISHED_BLOCK_NULL_ARGUMENT,
    DB_ARM64_PUBLISHED_BLOCK_BUFFER_OVERLAP,
    DB_ARM64_PUBLISHED_BLOCK_BAD_SOURCE_GENERATION,
    DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK,
    DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN,
    DB_ARM64_PUBLISHED_BLOCK_BAD_HANDLE,
    DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT
} db_arm64_published_block_status;

typedef struct db_arm64_published_block {
    uint64_t guest_source_rip;
    uint64_t guest_end_rip;
    uint64_t guest_fallthrough_rip;
    uint64_t source_generation;
    uint64_t translation_key;
    uint64_t host_mapping_address;
    uint64_t host_entry_address;
    uint64_t host_allocation_end;
    uint64_t host_allocation_size;
    uint64_t arm64_image_byte_count;
    uint32_t guest_byte_count;
    uint32_t guest_instruction_count;
    uint32_t arm64_word_count;
    uint32_t block_stop_reason;
    uint32_t format_version;
    uint8_t reserved[12];
} db_arm64_published_block;

db_arm64_published_block_status db_arm64_published_block_build(
    const db_x64_block_summary *block,
    const db_arm64_code_image_plan *plan,
    const db_linux_arm64_wx_handle *handle,
    const db_linux_arm64_wx_audit *audit,
    uint64_t source_generation,
    uint64_t translation_key,
    db_arm64_published_block *published);

const char *db_arm64_published_block_status_name(
    db_arm64_published_block_status status);

#endif
