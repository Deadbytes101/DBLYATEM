#include "dblyat/arm64_block_cache.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("ARM64 BLOCK CACHE LOOKUP VALIDATION TEST: FAIL line=%d\n", \
                   __LINE__);                                              \
            return 1;                                                      \
        }                                                                  \
    } while (0)

static db_arm64_published_block make_block(uint64_t translation_key)
{
    db_arm64_published_block block;

    memset(&block, 0, sizeof(block));
    block.guest_source_rip = UINT64_C(0x4000);
    block.guest_end_rip = UINT64_C(0x4010);
    block.guest_fallthrough_rip = UINT64_C(0x4010);
    block.source_generation = UINT64_C(9);
    block.translation_key = translation_key;
    block.host_mapping_address = UINT64_C(0x400000);
    block.host_entry_address = UINT64_C(0x400004);
    block.host_allocation_end = UINT64_C(0x400fff);
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

static int home_for_key(uint64_t translation_key,
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

    block = make_block(translation_key);
    if (db_arm64_block_cache_insert(&cache, &block, &result) !=
        DB_ARM64_BLOCK_CACHE_OK) {
        return 0;
    }

    *home = result.slot_index;
    return 1;
}

int main(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_published_block output;
    db_arm64_published_block output_before;
    db_arm64_block_cache_result result;
    db_arm64_block_cache_result result_before;
    uint64_t home;

    CHECK(home_for_key(UINT64_C(77), &home));
    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);

    slots[(home + UINT64_C(1)) & UINT64_C(3)].state =
        UINT32_C(99);
    memset(&output, 0xa5, sizeof(output));
    memset(&result, 0x5a, sizeof(result));
    output_before = output;
    result_before = result;

    CHECK(db_arm64_block_cache_lookup(
              &cache,
              UINT64_C(0x4000),
              UINT64_C(9),
              UINT64_C(77),
              &output,
              &result) == DB_ARM64_BLOCK_CACHE_BAD_CACHE);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(&result, &result_before, sizeof(result)) == 0);

    printf("ARM64 BLOCK CACHE LOOKUP VALIDATION TEST: PASS\n");
    return 0;
}
