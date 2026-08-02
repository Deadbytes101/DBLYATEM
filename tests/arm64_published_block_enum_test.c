#include "dblyat/arm64_published_block.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            printf("ARM64 PUBLISHED BLOCK ENUM TEST: FAIL line=%d\n", __LINE__); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

typedef struct fixture {
    db_x64_block_summary block;
    db_arm64_code_image_plan plan;
    db_linux_arm64_wx_handle handle;
    db_linux_arm64_wx_audit audit;
} fixture;

_Alignas(4096) static uint8_t mapping[4096];

static void fixture_init(fixture *value)
{
    const uint64_t base = (uint64_t)(uintptr_t)mapping;

    memset(value, 0, sizeof(*value));

    value->block.source_rip = UINT64_C(0x1000);
    value->block.final_fallthrough_rip = UINT64_C(0x1010);
    value->block.instruction_budget = 8u;
    value->block.byte_budget = 64u;
    value->block.output_capacity = 8u;
    value->block.record_count = 4u;
    value->block.byte_count = 16u;
    value->block.stop_reason = DB_X64_BLOCK_STOP_TERMINATOR;
    value->block.scalar_decode_status = DB_X64_DECODE_OK;
    value->block.memory_decode_status = DB_X64_MEMORY_INSTRUCTION_OK;

    value->plan.code_base = base;
    value->plan.entry_address = base + UINT64_C(4);
    value->plan.image_end_address = base + UINT64_C(15);
    value->plan.allocation_end_address = base + UINT64_C(4095);
    value->plan.page_size = UINT64_C(4096);
    value->plan.allocation_size = UINT64_C(4096);
    value->plan.instruction_cache_size = UINT64_C(16);
    value->plan.format_version = DB_ARM64_CODE_IMAGE_FORMAT_VERSION;
    value->plan.word_count = 4u;
    value->plan.byte_count = UINT64_C(16);
    value->plan.entry_byte_offset = UINT64_C(4);
    value->plan.entry_word_index = 1u;
    value->plan.initial_permissions =
        DB_ARM64_CODE_PERMISSION_READ |
        DB_ARM64_CODE_PERMISSION_WRITE;
    value->plan.final_permissions =
        DB_ARM64_CODE_PERMISSION_READ |
        DB_ARM64_CODE_PERMISSION_EXECUTE;

    value->handle.mapping = mapping;
    value->handle.entry = mapping + 4u;
    value->handle.allocation_size = sizeof(mapping);
    value->handle.state = DB_LINUX_ARM64_WX_PUBLISHED;

    value->audit.requested_base = base;
    value->audit.mapped_base = base;
    value->audit.entry_address = base + UINT64_C(4);
    value->audit.allocation_size = UINT64_C(4096);
    value->audit.image_size = UINT64_C(16);
    value->audit.icache_address = base;
    value->audit.icache_size = UINT64_C(16);
    value->audit.version = DB_LINUX_ARM64_WX_AUDIT_VERSION;
    value->audit.completed_state = DB_LINUX_ARM64_WX_PUBLISHED;
    value->audit.failure_state = DB_LINUX_ARM64_WX_EMPTY;
    value->audit.status = DB_LINUX_ARM64_WX_OK;
    value->audit.primary_status = DB_LINUX_ARM64_WX_OK;
    value->audit.map_observed = 1u;
    value->audit.copy_verified = 1u;
    value->audit.rx_observed = 1u;
    value->audit.icache_observed = 1u;
}

static int expect_bad_block(const fixture *value)
{
    db_arm64_published_block published;
    db_arm64_published_block before;

    memset(&published, 0xa5, sizeof(published));
    before = published;

    CHECK(db_arm64_published_block_build(
              &value->block,
              &value->plan,
              &value->handle,
              &value->audit,
              UINT64_C(1),
              UINT64_C(0),
              &published) == DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK);
    CHECK(memcmp(&published, &before, sizeof(published)) == 0);
    return 0;
}

int main(void)
{
    fixture value;

    fixture_init(&value);
    value.block.stop_reason = (db_x64_block_stop_reason)-1;
    CHECK(expect_bad_block(&value) == 0);

    fixture_init(&value);
    value.block.scalar_decode_status = (db_x64_decode_status)-1;
    CHECK(expect_bad_block(&value) == 0);

    fixture_init(&value);
    value.block.memory_decode_status =
        (db_x64_memory_instruction_status)-1;
    CHECK(expect_bad_block(&value) == 0);

    printf("%s\n", "ARM64 PUBLISHED BLOCK ENUM TEST: PASS");
    return 0;
}
