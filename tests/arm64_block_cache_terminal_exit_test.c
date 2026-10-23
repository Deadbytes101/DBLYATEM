#include "dblyat/arm64_block_cache.h"
#include "dblyat/translated_terminal_exit.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            printf("ARM64 BLOCK CACHE TERMINAL EXIT TEST: FAIL line=%d\n",     \
                   __LINE__);                                                   \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static db_arm64_published_block make_block(void)
{
    db_arm64_published_block block;

    memset(&block, 0, sizeof(block));
    block.guest_source_rip = UINT64_C(0x1000);
    block.guest_end_rip = UINT64_C(0x1010);
    block.guest_fallthrough_rip = UINT64_C(0x1010);
    block.source_generation = UINT64_C(7);
    block.translation_key = UINT64_C(0x11223344);
    block.host_mapping_address = UINT64_C(0x100000);
    block.host_entry_address = UINT64_C(0x100004);
    block.host_allocation_end = UINT64_C(0x100fff);
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

static int expect_bad_block(const db_arm64_published_block *input)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache cache_before;
    db_arm64_block_cache_slot slots[4];
    db_arm64_block_cache_slot slots_before[4];
    db_arm64_block_cache_result result;
    db_arm64_block_cache_result result_before;

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    cache_before = cache;
    memcpy(slots_before, slots, sizeof(slots));
    memset(&result, 0xa5, sizeof(result));
    result_before = result;

    CHECK(db_arm64_block_cache_insert(&cache, input, &result) ==
          DB_ARM64_BLOCK_CACHE_BAD_BLOCK);
    CHECK(memcmp(&cache, &cache_before, sizeof(cache)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(memcmp(&result, &result_before, sizeof(result)) == 0);
    return 0;
}

static int test_bound_direct_roundtrip(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_block_cache_result result;
    db_arm64_published_block input = make_block();
    db_arm64_published_block output;

    CHECK(db_translated_terminal_exit_plan_direct(
              UINT64_C(0x1008),
              UINT64_C(0x2000),
              &input.terminal_exit) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(db_arm64_block_cache_insert(&cache, &input, &result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(db_arm64_block_cache_lookup(
              &cache,
              input.guest_source_rip,
              input.source_generation,
              input.translation_key,
              &output,
              &result) == DB_ARM64_BLOCK_CACHE_OK);
    CHECK(memcmp(&output, &input, sizeof(output)) == 0);
    CHECK(memcmp(&output.terminal_exit,
                 &input.terminal_exit,
                 sizeof(output.terminal_exit)) == 0);
    return 0;
}

static int test_foreign_source_rejected(void)
{
    db_arm64_published_block block = make_block();

    CHECK(db_translated_terminal_exit_plan_direct(
              UINT64_C(0x2000),
              UINT64_C(0x3000),
              &block.terminal_exit) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    CHECK(expect_bad_block(&block) == 0);
    return 0;
}

static int test_conditional_fallthrough_mismatch_rejected(void)
{
    db_arm64_published_block block = make_block();

    CHECK(db_translated_terminal_exit_plan_conditional(
              UINT64_C(0x1008),
              UINT64_C(0x2000),
              block.guest_fallthrough_rip,
              UINT32_C(4),
              &block.terminal_exit) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    block.terminal_exit.fallthrough_target -= UINT64_C(1);
    CHECK(db_translated_terminal_exit_validate_planned(
              &block.terminal_exit) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    CHECK(expect_bad_block(&block) == 0);
    return 0;
}

int main(void)
{
    CHECK(test_bound_direct_roundtrip() == 0);
    CHECK(test_foreign_source_rejected() == 0);
    CHECK(test_conditional_fallthrough_mismatch_rejected() == 0);

    printf("ARM64 BLOCK CACHE TERMINAL EXIT TEST: PASS\n");
    return 0;
}
