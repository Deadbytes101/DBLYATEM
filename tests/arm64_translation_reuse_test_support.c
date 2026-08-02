#include "arm64_translation_reuse_test_support.h"

#include <string.h>

db_arm64_published_block arm64_translation_reuse_make_block(uint64_t rip,
                                           uint64_t generation,
                                           uint64_t translation_key,
                                           uint64_t mapping)
{
    db_arm64_published_block block;

    memset(&block, 0, sizeof(block));
    block.guest_source_rip = rip;
    block.guest_end_rip = rip + UINT64_C(16);
    block.guest_fallthrough_rip = rip + UINT64_C(16);
    block.source_generation = generation;
    block.translation_key = translation_key;
    block.host_mapping_address = mapping;
    block.host_entry_address = mapping + UINT64_C(4);
    block.host_allocation_end = mapping + UINT64_C(4095);
    block.host_allocation_size = UINT64_C(4096);
    block.arm64_image_byte_count = UINT64_C(16);
    block.guest_byte_count = 16u;
    block.guest_instruction_count = 4u;
    block.arm64_word_count = 4u;
    block.block_stop_reason =
        (uint32_t)DB_X64_BLOCK_STOP_TERMINATOR;
    block.format_version =
        DB_ARM64_PUBLISHED_BLOCK_FORMAT_VERSION;
    return block;
}
int arm64_translation_reuse_fixture_init(arm64_translation_reuse_fixture *value)
{
    db_arm64_block_cache_result result;

    memset(value, 0, sizeof(*value));
    if (db_arm64_block_cache_init(&value->cache,
                                  value->slots,
                                  4u) != DB_ARM64_BLOCK_CACHE_OK ||
        db_arm64_translation_reuse_init(&value->session) !=
            DB_ARM64_TRANSLATION_REUSE_OK) {
        return 0;
    }

    value->block = arm64_translation_reuse_make_block(UINT64_C(0x1000),
                              1u,
                              1u,
                              UINT64_C(0x100000));
    if (db_arm64_block_cache_insert(&value->cache,
                                    &value->block,
                                    &result) != DB_ARM64_BLOCK_CACHE_OK) {
        return 0;
    }

    memset(&value->output, 0xa5, sizeof(value->output));
    memset(&value->operation, 0x5a, sizeof(value->operation));
    memset(&value->snapshot, 0x3c, sizeof(value->snapshot));
    return 1;
}
int arm64_translation_reuse_fixture_unchanged(const arm64_translation_reuse_fixture *left,
                             const arm64_translation_reuse_fixture *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}
db_arm64_translation_reuse_status arm64_translation_reuse_lookup_fixture(
    arm64_translation_reuse_fixture *value)
{
    return db_arm64_translation_reuse_lookup(
        &value->cache,
        &value->session,
        value->block.guest_source_rip,
        value->block.source_generation,
        value->block.translation_key,
        &value->output,
        &value->operation,
        &value->snapshot);
}
db_arm64_translation_reuse_status arm64_translation_reuse_publish_fixture(
    arm64_translation_reuse_fixture *value)
{
    return db_arm64_translation_reuse_publish(
        &value->cache,
        &value->session,
        &value->block,
        &value->operation,
        &value->snapshot);
}
