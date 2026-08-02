#include "dblyat/arm64_block_cache.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("ARM64 BLOCK CACHE ADVERSARIAL TEST: FAIL line=%d\n",       \
                   __LINE__);                                                  \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef union cache_output_alias {
    db_arm64_published_block block;
    db_arm64_block_cache_result result;
} cache_output_alias;

typedef union cache_init_alias {
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
} cache_init_alias;

static db_arm64_published_block make_block(uint64_t rip,
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

static int expect_lookup_bad_cache(
    const db_arm64_block_cache *cache,
    uint64_t rip,
    uint64_t generation,
    uint64_t translation_key)
{
    db_arm64_published_block output;
    db_arm64_published_block output_before;
    db_arm64_block_cache_result result;
    db_arm64_block_cache_result result_before;

    memset(&output, 0xa5, sizeof(output));
    memset(&result, 0x5a, sizeof(result));
    output_before = output;
    result_before = result;

    CHECK(db_arm64_block_cache_lookup(
              cache,
              rip,
              generation,
              translation_key,
              &output,
              &result) == DB_ARM64_BLOCK_CACHE_BAD_CACHE);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(&result, &result_before, sizeof(result)) == 0);
    return 0;
}

static int expect_bad_block(db_arm64_published_block block)
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

    CHECK(db_arm64_block_cache_insert(
              &cache, &block, &result) ==
          DB_ARM64_BLOCK_CACHE_BAD_BLOCK);
    CHECK(memcmp(&cache, &cache_before, sizeof(cache)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(memcmp(&result, &result_before, sizeof(result)) == 0);
    return 0;
}

static int home_for_translation_key(uint64_t translation_key,
                                    uint64_t *home)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_published_block block;
    db_arm64_block_cache_result result;

    if (db_arm64_block_cache_init(&cache, slots, 4u) !=
        DB_ARM64_BLOCK_CACHE_OK) {
        return 0;
    }

    block = make_block(UINT64_C(0x4000),
                       UINT64_C(9),
                       translation_key,
                       UINT64_C(0x400000));
    if (db_arm64_block_cache_insert(&cache, &block, &result) !=
        DB_ARM64_BLOCK_CACHE_OK) {
        return 0;
    }

    *home = result.slot_index;
    return 1;
}

static int test_null_arguments(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_published_block block;
    db_arm64_published_block output;
    db_arm64_block_cache_result result;

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));

    CHECK(db_arm64_block_cache_init(NULL, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT);
    CHECK(db_arm64_block_cache_init(&cache, NULL, 4u) ==
          DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT);

    CHECK(db_arm64_block_cache_lookup(
              NULL, block.guest_source_rip,
              block.source_generation,
              block.translation_key,
              &output, &result) ==
          DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT);
    CHECK(db_arm64_block_cache_lookup(
              &cache, block.guest_source_rip,
              block.source_generation,
              block.translation_key,
              NULL, &result) ==
          DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT);
    CHECK(db_arm64_block_cache_lookup(
              &cache, block.guest_source_rip,
              block.source_generation,
              block.translation_key,
              &output, NULL) ==
          DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT);

    CHECK(db_arm64_block_cache_insert(
              NULL, &block, &result) ==
          DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT);
    CHECK(db_arm64_block_cache_insert(
              &cache, NULL, &result) ==
          DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT);
    CHECK(db_arm64_block_cache_insert(
              &cache, &block, NULL) ==
          DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT);
    return 0;
}

static int test_alias_matrix(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_published_block block;
    db_arm64_published_block output;
    db_arm64_block_cache_result result;
    cache_output_alias alias;
    cache_init_alias init_alias;
    unsigned char init_before[sizeof(init_alias)];

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));

    memset(&init_alias, 0xa5, sizeof(init_alias));
    memcpy(init_before, &init_alias, sizeof(init_alias));
    CHECK(db_arm64_block_cache_init(
              &init_alias.cache,
              init_alias.slots,
              4u) == DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);
    CHECK(memcmp(&init_alias, init_before, sizeof(init_alias)) == 0);

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);

    CHECK(db_arm64_block_cache_lookup(
              &cache,
              block.guest_source_rip,
              block.source_generation,
              block.translation_key,
              &alias.block,
              &alias.result) ==
          DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);
    CHECK(db_arm64_block_cache_lookup(
              &cache,
              block.guest_source_rip,
              block.source_generation,
              block.translation_key,
              (db_arm64_published_block *)(void *)&cache,
              &result) ==
          DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);
    CHECK(db_arm64_block_cache_lookup(
              &cache,
              block.guest_source_rip,
              block.source_generation,
              block.translation_key,
              &output,
              (db_arm64_block_cache_result *)(void *)&cache) ==
          DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);
    CHECK(db_arm64_block_cache_lookup(
              &cache,
              block.guest_source_rip,
              block.source_generation,
              block.translation_key,
              (db_arm64_published_block *)(void *)slots,
              &result) ==
          DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);
    CHECK(db_arm64_block_cache_lookup(
              &cache,
              block.guest_source_rip,
              block.source_generation,
              block.translation_key,
              &output,
              (db_arm64_block_cache_result *)(void *)slots) ==
          DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);

    CHECK(db_arm64_block_cache_insert(
              &cache,
              &alias.block,
              &alias.result) ==
          DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);
    CHECK(db_arm64_block_cache_insert(
              &cache,
              (const db_arm64_published_block *)(const void *)&cache,
              &result) ==
          DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);
    CHECK(db_arm64_block_cache_insert(
              &cache,
              &block,
              (db_arm64_block_cache_result *)(void *)&cache) ==
          DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);
    CHECK(db_arm64_block_cache_insert(
              &cache,
              (const db_arm64_published_block *)(const void *)slots,
              &result) ==
          DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);
    CHECK(db_arm64_block_cache_insert(
              &cache,
              &block,
              (db_arm64_block_cache_result *)(void *)slots) ==
          DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);
    return 0;
}

static int test_bad_key_preservation(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_published_block output;
    db_arm64_published_block output_before;
    db_arm64_block_cache_result result;
    db_arm64_block_cache_result result_before;

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    memset(&output, 0xa5, sizeof(output));
    memset(&result, 0x5a, sizeof(result));
    output_before = output;
    result_before = result;

    CHECK(db_arm64_block_cache_lookup(
              &cache, 0u, 1u, 0u,
              &output, &result) ==
          DB_ARM64_BLOCK_CACHE_BAD_KEY);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(&result, &result_before, sizeof(result)) == 0);

    CHECK(db_arm64_block_cache_lookup(
              &cache, UINT64_C(0x1000), 0u, 0u,
              &output, &result) ==
          DB_ARM64_BLOCK_CACHE_BAD_KEY);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(&result, &result_before, sizeof(result)) == 0);

    CHECK(db_arm64_block_cache_lookup(
              &cache, UINT64_C(0x0000800000000000),
              1u, 0u, &output, &result) ==
          DB_ARM64_BLOCK_CACHE_BAD_KEY);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(&result, &result_before, sizeof(result)) == 0);
    return 0;
}

static int test_bad_block_matrix(void)
{
    db_arm64_published_block block;

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.guest_fallthrough_rip =
        UINT64_C(0x0000800000000000);
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.guest_source_rip = UINT64_MAX - UINT64_C(7);
    block.guest_end_rip = UINT64_C(8);
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.guest_source_rip =
        UINT64_C(0x00007ffffffffff8);
    block.guest_end_rip =
        UINT64_C(0x0000800000000008);
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.guest_instruction_count = 17u;
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.block_stop_reason =
        (uint32_t)DB_X64_BLOCK_STOP_ADDRESS_INVALID + 1u;
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.host_mapping_address += UINT64_C(4);
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.host_entry_address += UINT64_C(2);
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.host_allocation_size = UINT64_C(2048);
    block.host_allocation_end =
        block.host_mapping_address + UINT64_C(2047);
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.arm64_image_byte_count += UINT64_C(4);
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.host_allocation_end += UINT64_C(1);
    CHECK(expect_bad_block(block) == 0);
    return 0;
}

static int test_malformed_resident_entries(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_published_block first;
    db_arm64_published_block second;
    db_arm64_block_cache_result first_result;
    db_arm64_block_cache_result second_result;
    uint64_t first_home;
    uint64_t candidate_home;
    uint64_t candidate;

    first = make_block(UINT64_C(0x4000), 9u, 1u,
                       UINT64_C(0x400000));

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(db_arm64_block_cache_insert(
              &cache, &first, &first_result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    slots[first_result.slot_index].format_version = 0u;
    CHECK(expect_lookup_bad_cache(
              &cache,
              first.guest_source_rip,
              first.source_generation,
              first.translation_key) == 0);

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(db_arm64_block_cache_insert(
              &cache, &first, &first_result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    slots[first_result.slot_index].block.reserved[0] = 1u;
    CHECK(expect_lookup_bad_cache(
              &cache,
              first.guest_source_rip,
              first.source_generation,
              first.translation_key) == 0);

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    second = make_block(UINT64_C(0x4000), 9u, 2u,
                        UINT64_C(0x500000));
    CHECK(db_arm64_block_cache_insert(
              &cache, &first, &first_result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(db_arm64_block_cache_insert(
              &cache, &second, &second_result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    slots[second_result.slot_index].block.translation_key =
        first.translation_key;
    CHECK(expect_lookup_bad_cache(
              &cache,
              first.guest_source_rip,
              first.source_generation,
              first.translation_key) == 0);

    CHECK(home_for_translation_key(1u, &first_home));
    candidate = 2u;
    for (;;) {
        CHECK(home_for_translation_key(candidate, &candidate_home));
        if (candidate_home == first_home) {
            break;
        }
        ++candidate;
        CHECK(candidate < UINT64_C(1000));
    }

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    first = make_block(UINT64_C(0x4000), 9u, 1u,
                       UINT64_C(0x400000));
    second = make_block(UINT64_C(0x4000), 9u, candidate,
                        UINT64_C(0x500000));
    CHECK(db_arm64_block_cache_insert(
              &cache, &first, &first_result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(db_arm64_block_cache_insert(
              &cache, &second, &second_result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(second_result.probe_count == 2u);
    memset(&slots[first_result.slot_index], 0,
           sizeof(slots[first_result.slot_index]));
    cache.count = 1u;
    CHECK(expect_lookup_bad_cache(
              &cache,
              second.guest_source_rip,
              second.source_generation,
              second.translation_key) == 0);

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(db_arm64_block_cache_insert(
              &cache, &first, &first_result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    cache.count = 0u;
    CHECK(expect_lookup_bad_cache(
              &cache,
              first.guest_source_rip,
              first.source_generation,
              first.translation_key) == 0);

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    cache.reserved[0] = 1u;
    CHECK(expect_lookup_bad_cache(
              &cache,
              first.guest_source_rip,
              first.source_generation,
              first.translation_key) == 0);

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    cache.format_version += 1u;
    CHECK(expect_lookup_bad_cache(
              &cache,
              first.guest_source_rip,
              first.source_generation,
              first.translation_key) == 0);
    return 0;
}

static int test_full_table_miss_preservation(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[2];
    db_arm64_published_block block;
    db_arm64_published_block output;
    db_arm64_published_block output_before;
    db_arm64_block_cache_result result;
    db_arm64_block_cache_result result_before;

    CHECK(db_arm64_block_cache_init(&cache, slots, 2u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    CHECK(db_arm64_block_cache_insert(&cache, &block, &result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    block = make_block(UINT64_C(0x2000), 1u, 1u,
                       UINT64_C(0x200000));
    CHECK(db_arm64_block_cache_insert(&cache, &block, &result) ==
          DB_ARM64_BLOCK_CACHE_OK);

    memset(&output, 0xa5, sizeof(output));
    memset(&result, 0x5a, sizeof(result));
    output_before = output;
    result_before = result;
    CHECK(db_arm64_block_cache_lookup(
              &cache, UINT64_C(0x3000), 1u, 1u,
              &output, &result) ==
          DB_ARM64_BLOCK_CACHE_NOT_FOUND);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(&result, &result_before, sizeof(result)) == 0);
    return 0;
}

int main(void)
{
    CHECK(test_null_arguments() == 0);
    CHECK(test_alias_matrix() == 0);
    CHECK(test_bad_key_preservation() == 0);
    CHECK(test_bad_block_matrix() == 0);
    CHECK(test_malformed_resident_entries() == 0);
    CHECK(test_full_table_miss_preservation() == 0);

    printf("ARM64 BLOCK CACHE ADVERSARIAL TEST: PASS\n");
    return 0;
}
