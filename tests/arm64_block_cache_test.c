#include "dblyat/arm64_block_cache.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            printf("ARM64 BLOCK CACHE TEST: FAIL line=%d\n", __LINE__); \
            return 1;                                                     \
        }                                                                 \
    } while (0)

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

static int test_init(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_block_cache before;
    db_arm64_block_cache_slot slots_before[4];
    const db_arm64_block_cache_slot zero_slots[4] = {0};

    memset(&cache, 0xa5, sizeof(cache));
    memset(slots, 0xa5, sizeof(slots));

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(cache.slots == slots);
    CHECK(cache.capacity == 4u);
    CHECK(cache.count == 0u);
    CHECK(cache.format_version ==
          DB_ARM64_BLOCK_CACHE_FORMAT_VERSION);
    CHECK(memcmp(cache.reserved,
                 (uint8_t[12]){0},
                 sizeof(cache.reserved)) == 0);
    CHECK(memcmp(slots, zero_slots, sizeof(slots)) == 0);

    memset(&cache, 0xa5, sizeof(cache));
    memset(slots, 0x5a, sizeof(slots));
    before = cache;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(db_arm64_block_cache_init(&cache, slots, 0u) ==
          DB_ARM64_BLOCK_CACHE_BAD_CAPACITY);
    CHECK(memcmp(&cache, &before, sizeof(cache)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);

    CHECK(db_arm64_block_cache_init(NULL, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT);
    CHECK(db_arm64_block_cache_init(&cache, NULL, 4u) ==
          DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT);
    CHECK(db_arm64_block_cache_init(&cache, slots, 3u) ==
          DB_ARM64_BLOCK_CACHE_BAD_CAPACITY);

    return 0;
}

static int test_insert_lookup(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[8];
    db_arm64_published_block input;
    db_arm64_published_block output;
    db_arm64_block_cache_result insert_result;
    db_arm64_block_cache_result lookup_result;

    CHECK(db_arm64_block_cache_init(&cache, slots, 8u) ==
          DB_ARM64_BLOCK_CACHE_OK);

    input = make_block(UINT64_C(0x1000),
                       UINT64_C(7),
                       UINT64_C(0x1122),
                       UINT64_C(0x100000));
    memset(&insert_result, 0xa5, sizeof(insert_result));

    CHECK(db_arm64_block_cache_insert(
              &cache, &input, &insert_result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(cache.count == 1u);
    CHECK(insert_result.found == 1u);
    CHECK(insert_result.inserted == 1u);
    CHECK(insert_result.probe_count == 1u);
    CHECK(insert_result.slot_index < cache.capacity);
    CHECK(memcmp(insert_result.reserved,
                 (uint8_t[8]){0},
                 sizeof(insert_result.reserved)) == 0);

    memset(&output, 0xa5, sizeof(output));
    memset(&lookup_result, 0xa5, sizeof(lookup_result));
    CHECK(db_arm64_block_cache_lookup(
              &cache,
              input.guest_source_rip,
              input.source_generation,
              input.translation_key,
              &output,
              &lookup_result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(memcmp(&output, &input, sizeof(output)) == 0);
    CHECK(lookup_result.found == 1u);
    CHECK(lookup_result.inserted == 0u);
    CHECK(lookup_result.slot_index == insert_result.slot_index);
    CHECK(lookup_result.probe_count == insert_result.probe_count);

    return 0;
}

static int test_key_separation_and_miss_preservation(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[8];
    db_arm64_published_block first;
    db_arm64_published_block second;
    db_arm64_published_block third;
    db_arm64_published_block output;
    db_arm64_published_block output_before;
    db_arm64_block_cache_result result;
    db_arm64_block_cache_result result_before;

    CHECK(db_arm64_block_cache_init(&cache, slots, 8u) ==
          DB_ARM64_BLOCK_CACHE_OK);

    first = make_block(UINT64_C(0x1000), 1u, 11u,
                       UINT64_C(0x100000));
    second = make_block(UINT64_C(0x1000), 2u, 11u,
                        UINT64_C(0x200000));
    third = make_block(UINT64_C(0x1000), 1u, 12u,
                       UINT64_C(0x300000));

    CHECK(db_arm64_block_cache_insert(&cache, &first, &result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(db_arm64_block_cache_insert(&cache, &second, &result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(db_arm64_block_cache_insert(&cache, &third, &result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(cache.count == 3u);

    CHECK(db_arm64_block_cache_lookup(
              &cache, second.guest_source_rip,
              second.source_generation,
              second.translation_key,
              &output, &result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(memcmp(&output, &second, sizeof(output)) == 0);

    CHECK(db_arm64_block_cache_lookup(
              &cache, third.guest_source_rip,
              third.source_generation,
              third.translation_key,
              &output, &result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(memcmp(&output, &third, sizeof(output)) == 0);

    memset(&output, 0xa5, sizeof(output));
    memset(&result, 0x5a, sizeof(result));
    output_before = output;
    result_before = result;
    CHECK(db_arm64_block_cache_lookup(
              &cache, UINT64_C(0x2000), 1u, 11u,
              &output, &result) ==
          DB_ARM64_BLOCK_CACHE_NOT_FOUND);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
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

    block = make_block(UINT64_C(0x4000), 9u,
                       translation_key,
                       UINT64_C(0x400000));
    if (db_arm64_block_cache_insert(&cache, &block, &result) !=
        DB_ARM64_BLOCK_CACHE_OK) {
        return 0;
    }

    *home = result.slot_index;
    return 1;
}

static int test_collision_and_duplicate(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache cache_before;
    db_arm64_block_cache_slot slots[4];
    db_arm64_block_cache_slot slots_before[4];
    db_arm64_published_block first;
    db_arm64_published_block second;
    db_arm64_published_block conflict;
    db_arm64_block_cache_result first_result;
    db_arm64_block_cache_result second_result;
    db_arm64_block_cache_result duplicate_result;
    db_arm64_block_cache_result conflict_result;
    db_arm64_block_cache_result conflict_before;
    uint64_t first_home;
    uint64_t candidate_home;
    uint64_t candidate;

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
    CHECK(first_result.slot_index == first_home);
    CHECK(first_result.probe_count == 1u);
    CHECK(second_result.slot_index ==
          ((first_result.slot_index + 1u) & UINT64_C(3)));
    CHECK(second_result.probe_count == 2u);

    cache_before = cache;
    memcpy(slots_before, slots, sizeof(slots));
    memset(&duplicate_result, 0xa5, sizeof(duplicate_result));
    CHECK(db_arm64_block_cache_insert(
              &cache, &first, &duplicate_result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(memcmp(&cache, &cache_before, sizeof(cache)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(duplicate_result.inserted == 0u);
    CHECK(duplicate_result.slot_index == first_result.slot_index);

    conflict = first;
    conflict.host_entry_address += UINT64_C(4);
    cache_before = cache;
    memcpy(slots_before, slots, sizeof(slots));
    memset(&conflict_result, 0x5a, sizeof(conflict_result));
    conflict_before = conflict_result;
    CHECK(db_arm64_block_cache_insert(
              &cache, &conflict, &conflict_result) ==
          DB_ARM64_BLOCK_CACHE_KEY_CONFLICT);
    CHECK(memcmp(&cache, &cache_before, sizeof(cache)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(memcmp(&conflict_result,
                 &conflict_before,
                 sizeof(conflict_result)) == 0);

    return 0;
}

static int test_full(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache cache_before;
    db_arm64_block_cache_slot slots[2];
    db_arm64_block_cache_slot slots_before[2];
    db_arm64_published_block block;
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
    CHECK(cache.count == 2u);

    block = make_block(UINT64_C(0x3000), 1u, 1u,
                       UINT64_C(0x300000));
    cache_before = cache;
    memcpy(slots_before, slots, sizeof(slots));
    memset(&result, 0xa5, sizeof(result));
    result_before = result;
    CHECK(db_arm64_block_cache_insert(&cache, &block, &result) ==
          DB_ARM64_BLOCK_CACHE_FULL);
    CHECK(memcmp(&cache, &cache_before, sizeof(cache)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
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
    db_arm64_block_cache_result before;

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    cache_before = cache;
    memcpy(slots_before, slots, sizeof(slots));
    memset(&result, 0xa5, sizeof(result));
    before = result;

    CHECK(db_arm64_block_cache_insert(&cache, &block, &result) ==
          DB_ARM64_BLOCK_CACHE_BAD_BLOCK);
    CHECK(memcmp(&cache, &cache_before, sizeof(cache)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(memcmp(&result, &before, sizeof(result)) == 0);
    return 0;
}

static int test_bad_blocks(void)
{
    db_arm64_published_block block;

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.source_generation = 0u;
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.format_version += 1u;
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.reserved[0] = 1u;
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.guest_source_rip = UINT64_C(0x0000800000000000);
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.guest_end_rip += 1u;
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.host_allocation_end -= 1u;
    CHECK(expect_bad_block(block) == 0);

    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));
    block.host_entry_address = block.host_allocation_end;
    CHECK(expect_bad_block(block) == 0);

    return 0;
}

static int test_bad_cache_preservation(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache cache_before;
    db_arm64_block_cache_slot slots[4];
    db_arm64_block_cache_slot slots_before[4];
    db_arm64_published_block block;
    db_arm64_block_cache_result result;
    db_arm64_block_cache_result result_before;

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    block = make_block(UINT64_C(0x1000), 1u, 1u,
                       UINT64_C(0x100000));

    cache.count = 5u;
    cache_before = cache;
    memcpy(slots_before, slots, sizeof(slots));
    memset(&result, 0xa5, sizeof(result));
    result_before = result;
    CHECK(db_arm64_block_cache_insert(&cache, &block, &result) ==
          DB_ARM64_BLOCK_CACHE_BAD_CACHE);
    CHECK(memcmp(&cache, &cache_before, sizeof(cache)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(memcmp(&result, &result_before, sizeof(result)) == 0);

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    slots[0].reserved[0] = 1u;
    cache_before = cache;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(db_arm64_block_cache_insert(&cache, &block, &result) ==
          DB_ARM64_BLOCK_CACHE_BAD_CACHE);
    CHECK(memcmp(&cache, &cache_before, sizeof(cache)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    slots[0].state = UINT32_C(99);
    cache_before = cache;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(db_arm64_block_cache_insert(&cache, &block, &result) ==
          DB_ARM64_BLOCK_CACHE_BAD_CACHE);
    CHECK(memcmp(&cache, &cache_before, sizeof(cache)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);

    return 0;
}

static int test_alias_and_bad_key(void)
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

    CHECK(db_arm64_block_cache_insert(
              &cache,
              (const db_arm64_published_block *)(const void *)slots,
              &result) ==
          DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);

    CHECK(db_arm64_block_cache_insert(
              &cache, &block,
              (db_arm64_block_cache_result *)(void *)slots) ==
          DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);

    CHECK(db_arm64_block_cache_lookup(
              &cache, block.guest_source_rip,
              block.source_generation,
              block.translation_key,
              (db_arm64_published_block *)(void *)slots,
              &result) ==
          DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP);

    CHECK(db_arm64_block_cache_lookup(
              &cache, 0u, 1u, 0u,
              &output, &result) ==
          DB_ARM64_BLOCK_CACHE_BAD_KEY);
    CHECK(db_arm64_block_cache_lookup(
              &cache, UINT64_C(0x1000), 0u, 0u,
              &output, &result) ==
          DB_ARM64_BLOCK_CACHE_BAD_KEY);

    return 0;
}

static int test_names(void)
{
    CHECK(strcmp(db_arm64_block_cache_status_name(
                     DB_ARM64_BLOCK_CACHE_OK),
                 "ok") == 0);
    CHECK(strcmp(db_arm64_block_cache_status_name(
                     DB_ARM64_BLOCK_CACHE_NOT_FOUND),
                 "not found") == 0);
    CHECK(strcmp(db_arm64_block_cache_status_name(
                     (db_arm64_block_cache_status)99),
                 "unknown ARM64 block cache status") == 0);
    CHECK(strcmp(db_arm64_block_cache_slot_state_name(
                     DB_ARM64_BLOCK_CACHE_SLOT_EMPTY),
                 "empty") == 0);
    CHECK(strcmp(db_arm64_block_cache_slot_state_name(
                     DB_ARM64_BLOCK_CACHE_SLOT_OCCUPIED),
                 "occupied") == 0);
    CHECK(strcmp(db_arm64_block_cache_slot_state_name(
                     (db_arm64_block_cache_slot_state)99),
                 "unknown ARM64 block cache slot state") == 0);
    return 0;
}

int main(void)
{
    CHECK(test_init() == 0);
    CHECK(test_insert_lookup() == 0);
    CHECK(test_key_separation_and_miss_preservation() == 0);
    CHECK(test_collision_and_duplicate() == 0);
    CHECK(test_full() == 0);
    CHECK(test_bad_blocks() == 0);
    CHECK(test_bad_cache_preservation() == 0);
    CHECK(test_alias_and_bad_key() == 0);
    CHECK(test_names() == 0);

    printf("ARM64 BLOCK CACHE TEST: PASS\n");
    return 0;
}
