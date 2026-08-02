#include "dblyat/arm64_published_block.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("ARM64 PUBLISHED BLOCK TEST: FAIL line=%d\n", __LINE__); \
            return 1;                                                        \
        }                                                                    \
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
    value->block.memory_decode_status =
        DB_X64_MEMORY_INSTRUCTION_OK;

    value->plan.code_base = base;
    value->plan.entry_address = base + UINT64_C(4);
    value->plan.image_end_address = base + UINT64_C(15);
    value->plan.allocation_end_address = base + UINT64_C(4095);
    value->plan.page_size = UINT64_C(4096);
    value->plan.allocation_size = UINT64_C(4096);
    value->plan.instruction_cache_offset = 0u;
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

static int expect_failure(const fixture *value,
                          uint64_t generation,
                          db_arm64_published_block_status expected)
{
    db_arm64_published_block published;
    db_arm64_published_block before;
    db_arm64_published_block_status status;

    memset(&published, 0xa5, sizeof(published));
    before = published;

    status = db_arm64_published_block_build(
        &value->block,
        &value->plan,
        &value->handle,
        &value->audit,
        generation,
        UINT64_C(0x1122334455667788),
        &published);

    CHECK(status == expected);
    CHECK(memcmp(&published, &before, sizeof(published)) == 0);
    return 0;
}

static int test_valid(void)
{
    fixture value;
    db_arm64_published_block published;
    db_arm64_published_block_status status;

    fixture_init(&value);
    memset(&published, 0xa5, sizeof(published));

    status = db_arm64_published_block_build(
        &value.block,
        &value.plan,
        &value.handle,
        &value.audit,
        UINT64_C(7),
        UINT64_C(0x1122334455667788),
        &published);

    CHECK(status == DB_ARM64_PUBLISHED_BLOCK_OK);
    CHECK(published.guest_source_rip == value.block.source_rip);
    CHECK(published.guest_end_rip ==
          value.block.source_rip + value.block.byte_count);
    CHECK(published.guest_fallthrough_rip ==
          value.block.final_fallthrough_rip);
    CHECK(published.source_generation == UINT64_C(7));
    CHECK(published.translation_key ==
          UINT64_C(0x1122334455667788));
    CHECK(published.host_mapping_address == value.plan.code_base);
    CHECK(published.host_entry_address == value.plan.entry_address);
    CHECK(published.host_allocation_end ==
          value.plan.allocation_end_address);
    CHECK(published.host_allocation_size ==
          value.plan.allocation_size);
    CHECK(published.arm64_image_byte_count == value.plan.byte_count);
    CHECK(published.guest_byte_count == value.block.byte_count);
    CHECK(published.guest_instruction_count ==
          value.block.record_count);
    CHECK(published.arm64_word_count == value.plan.word_count);
    CHECK(published.block_stop_reason ==
          (uint32_t)value.block.stop_reason);
    CHECK(published.format_version ==
          DB_ARM64_PUBLISHED_BLOCK_FORMAT_VERSION);
    CHECK(memcmp(published.reserved,
                 (uint8_t[12]){0},
                 sizeof(published.reserved)) == 0);

    status = db_arm64_published_block_build(
        &value.block,
        &value.plan,
        &value.handle,
        &value.audit,
        UINT64_C(8),
        UINT64_C(0),
        &published);
    CHECK(status == DB_ARM64_PUBLISHED_BLOCK_OK);
    CHECK(published.translation_key == 0u);

    return 0;
}

static int test_null_arguments(void)
{
    fixture value;
    db_arm64_published_block published;

    fixture_init(&value);

    CHECK(db_arm64_published_block_build(
              NULL, &value.plan, &value.handle, &value.audit,
              1u, 0u, &published) ==
          DB_ARM64_PUBLISHED_BLOCK_NULL_ARGUMENT);
    CHECK(db_arm64_published_block_build(
              &value.block, NULL, &value.handle, &value.audit,
              1u, 0u, &published) ==
          DB_ARM64_PUBLISHED_BLOCK_NULL_ARGUMENT);
    CHECK(db_arm64_published_block_build(
              &value.block, &value.plan, NULL, &value.audit,
              1u, 0u, &published) ==
          DB_ARM64_PUBLISHED_BLOCK_NULL_ARGUMENT);
    CHECK(db_arm64_published_block_build(
              &value.block, &value.plan, &value.handle, NULL,
              1u, 0u, &published) ==
          DB_ARM64_PUBLISHED_BLOCK_NULL_ARGUMENT);
    CHECK(db_arm64_published_block_build(
              &value.block, &value.plan, &value.handle, &value.audit,
              1u, 0u, NULL) ==
          DB_ARM64_PUBLISHED_BLOCK_NULL_ARGUMENT);

    return 0;
}

static int test_block_failures(void)
{
    fixture value;

    fixture_init(&value);
    CHECK(expect_failure(&value, 0u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_SOURCE_GENERATION) == 0);

    fixture_init(&value);
    value.block.source_rip = UINT64_C(0x0000800000000000);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK) == 0);

    fixture_init(&value);
    value.block.source_rip = UINT64_MAX - UINT64_C(7);
    value.block.byte_count = 16u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK) == 0);

    fixture_init(&value);
    value.block.source_rip = UINT64_C(0x00007ffffffffff8);
    value.block.byte_count = 16u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK) == 0);

    fixture_init(&value);
    value.block.byte_count = 0u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK) == 0);

    fixture_init(&value);
    value.block.record_count = 0u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK) == 0);

    fixture_init(&value);
    value.block.byte_budget = 15u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK) == 0);

    fixture_init(&value);
    value.block.instruction_budget = 3u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK) == 0);

    fixture_init(&value);
    value.block.output_capacity = 3u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK) == 0);

    fixture_init(&value);
    value.block.stop_reason =
        (db_x64_block_stop_reason)
        (DB_X64_BLOCK_STOP_ADDRESS_INVALID + 1);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK) == 0);

    fixture_init(&value);
    value.block.scalar_decode_status =
        (db_x64_decode_status)(DB_X64_DECODE_INVALID_ENCODING + 1);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK) == 0);

    fixture_init(&value);
    value.block.memory_decode_status =
        (db_x64_memory_instruction_status)
        (DB_X64_MEMORY_INSTRUCTION_INVALID_ENCODING + 1);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK) == 0);

    fixture_init(&value);
    value.block.final_fallthrough_rip =
        UINT64_C(0x0000800000000000);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK) == 0);

    fixture_init(&value);
    value.block.reserved[0] = 1u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK) == 0);

    return 0;
}

static int test_plan_failures(void)
{
    fixture value;

    fixture_init(&value);
    value.plan.format_version += 1u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.word_count = 0u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.byte_count += UINT64_C(4);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.page_size = UINT64_C(3000);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.code_base += UINT64_C(4);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.entry_word_index = value.plan.word_count;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.entry_byte_offset += UINT64_C(4);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.entry_address += UINT64_C(4);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.image_end_address += UINT64_C(1);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.allocation_size += UINT64_C(4096);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.allocation_end_address -= UINT64_C(1);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.instruction_cache_offset = UINT64_C(4);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.instruction_cache_size += UINT64_C(4);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.initial_permissions = DB_ARM64_CODE_PERMISSION_READ;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.final_permissions =
        DB_ARM64_CODE_PERMISSION_READ |
        DB_ARM64_CODE_PERMISSION_WRITE |
        DB_ARM64_CODE_PERMISSION_EXECUTE;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    fixture_init(&value);
    value.plan.reserved[0] = 1u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN) == 0);

    return 0;
}

static int test_handle_failures(void)
{
    fixture value;

    fixture_init(&value);
    value.handle.state = DB_LINUX_ARM64_WX_EMPTY;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_HANDLE) == 0);

    fixture_init(&value);
    value.handle.state = DB_LINUX_ARM64_WX_ROLLBACK_PENDING;
    value.handle.entry = NULL;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_HANDLE) == 0);

    fixture_init(&value);
    value.handle.mapping = NULL;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_HANDLE) == 0);

    fixture_init(&value);
    value.handle.entry = NULL;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_HANDLE) == 0);

    fixture_init(&value);
    value.handle.mapping = mapping + 16u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_HANDLE) == 0);

    fixture_init(&value);
    value.handle.entry = mapping + 8u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_HANDLE) == 0);

    fixture_init(&value);
    value.handle.allocation_size -= 1u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_HANDLE) == 0);

    fixture_init(&value);
    value.handle.reserved = 1u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_HANDLE) == 0);

    return 0;
}

static int test_audit_failures(void)
{
    fixture value;

    fixture_init(&value);
    value.audit.version += 1u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.completed_state = DB_LINUX_ARM64_WX_RX_SEALED;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.failure_state = DB_LINUX_ARM64_WX_IMAGE_COPIED;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.status = DB_LINUX_ARM64_WX_COPY_FAILED;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.primary_status = DB_LINUX_ARM64_WX_COPY_FAILED;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.requested_base += UINT64_C(4096);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.mapped_base += UINT64_C(4096);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.entry_address += UINT64_C(4);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.allocation_size -= UINT64_C(1);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.image_size += UINT64_C(4);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.icache_address += UINT64_C(4);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.icache_size += UINT64_C(4);
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.map_observed = 0u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.copy_verified = 0u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.rx_observed = 0u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.icache_observed = 0u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.rollback_observed = 1u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.release_observed = 1u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    fixture_init(&value);
    value.audit.reserved[0] = 1u;
    CHECK(expect_failure(&value, 1u,
                         DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT) == 0);

    return 0;
}

static int test_aliases(void)
{
    fixture value;
    db_arm64_published_block published;

    union block_alias {
        db_x64_block_summary block;
        db_arm64_published_block published;
    } block_alias;

    union plan_alias {
        db_arm64_code_image_plan plan;
        db_arm64_published_block published;
    } plan_alias;

    union handle_alias {
        db_linux_arm64_wx_handle handle;
        db_arm64_published_block published;
    } handle_alias;

    union audit_alias {
        db_linux_arm64_wx_audit audit;
        db_arm64_published_block published;
    } audit_alias;

    fixture_init(&value);
    memset(&block_alias, 0, sizeof(block_alias));
    block_alias.block = value.block;
    CHECK(db_arm64_published_block_build(
              &block_alias.block, &value.plan, &value.handle, &value.audit,
              1u, 0u, &block_alias.published) ==
          DB_ARM64_PUBLISHED_BLOCK_BUFFER_OVERLAP);

    fixture_init(&value);
    memset(&plan_alias, 0, sizeof(plan_alias));
    plan_alias.plan = value.plan;
    CHECK(db_arm64_published_block_build(
              &value.block, &plan_alias.plan, &value.handle, &value.audit,
              1u, 0u, &plan_alias.published) ==
          DB_ARM64_PUBLISHED_BLOCK_BUFFER_OVERLAP);

    fixture_init(&value);
    memset(&handle_alias, 0, sizeof(handle_alias));
    handle_alias.handle = value.handle;
    CHECK(db_arm64_published_block_build(
              &value.block, &value.plan, &handle_alias.handle, &value.audit,
              1u, 0u, &handle_alias.published) ==
          DB_ARM64_PUBLISHED_BLOCK_BUFFER_OVERLAP);

    fixture_init(&value);
    memset(&audit_alias, 0, sizeof(audit_alias));
    audit_alias.audit = value.audit;
    CHECK(db_arm64_published_block_build(
              &value.block, &value.plan, &value.handle, &audit_alias.audit,
              1u, 0u, &audit_alias.published) ==
          DB_ARM64_PUBLISHED_BLOCK_BUFFER_OVERLAP);

    fixture_init(&value);
    CHECK(db_arm64_published_block_build(
              &value.block, &value.plan, &value.handle, &value.audit,
              1u, 0u, &published) ==
          DB_ARM64_PUBLISHED_BLOCK_OK);

    return 0;
}

static int test_status_names(void)
{
    size_t index;

    for (index = 0u;
         index <= (size_t)DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT;
         ++index) {
        CHECK(strcmp(
                  db_arm64_published_block_status_name(
                      (db_arm64_published_block_status)index),
                  "unknown ARM64 published block status") != 0);
    }

    CHECK(strcmp(
              db_arm64_published_block_status_name(
                  (db_arm64_published_block_status)UINT32_C(0xffffffff)),
              "unknown ARM64 published block status") == 0);

    return 0;
}

int main(void)
{
    CHECK(test_valid() == 0);
    CHECK(test_null_arguments() == 0);
    CHECK(test_block_failures() == 0);
    CHECK(test_plan_failures() == 0);
    CHECK(test_handle_failures() == 0);
    CHECK(test_audit_failures() == 0);
    CHECK(test_aliases() == 0);
    CHECK(test_status_names() == 0);

    printf("%s\n", "ARM64 PUBLISHED BLOCK TEST: PASS");
    return 0;
}
