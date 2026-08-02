#include "arm64_translation_reuse_test_support.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("ARM64 TRANSLATION REUSE OUTCOME TEST: FAIL line=%d\n", __LINE__);              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int test_conflict_and_full(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[2];
    db_arm64_translation_reuse_session session;
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;
    db_arm64_published_block first;
    db_arm64_published_block second;
    db_arm64_published_block conflict;
    db_arm64_published_block third;
    db_arm64_block_cache cache_before;
    db_arm64_block_cache_slot slots_before[2];

    CHECK(db_arm64_block_cache_init(&cache, slots, 2u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(db_arm64_translation_reuse_init(&session) ==
          DB_ARM64_TRANSLATION_REUSE_OK);

    first = arm64_translation_reuse_make_block(UINT64_C(0x2000), 2u, 21u,
                       UINT64_C(0x200000));
    second = arm64_translation_reuse_make_block(UINT64_C(0x3000), 2u, 22u,
                        UINT64_C(0x300000));
    third = arm64_translation_reuse_make_block(UINT64_C(0x4000), 2u, 23u,
                       UINT64_C(0x400000));
    conflict = arm64_translation_reuse_make_block(first.guest_source_rip,
                          first.source_generation,
                          first.translation_key,
                          UINT64_C(0x500000));

    CHECK(db_arm64_translation_reuse_publish(
              &cache, &session, &first,
              &operation, &snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_OK);
    CHECK(db_arm64_translation_reuse_publish(
              &cache, &session, &second,
              &operation, &snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_OK);
    CHECK(cache.count == 2u);

    cache_before = cache;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(db_arm64_translation_reuse_publish(
              &cache, &session, &conflict,
              &operation, &snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_OK);
    CHECK(operation.cache_status ==
          (uint32_t)DB_ARM64_BLOCK_CACHE_KEY_CONFLICT);
    CHECK(operation.conflict == 1u);
    CHECK(operation.full == 0u);
    CHECK(memcmp(&cache, &cache_before, sizeof(cache)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(snapshot.conflict_count == 1u);
    CHECK(snapshot.rejected_operation_count == 1u);

    cache_before = cache;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(db_arm64_translation_reuse_publish(
              &cache, &session, &third,
              &operation, &snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_OK);
    CHECK(operation.cache_status ==
          (uint32_t)DB_ARM64_BLOCK_CACHE_FULL);
    CHECK(operation.full == 1u);
    CHECK(operation.slot_index ==
          DB_ARM64_TRANSLATION_REUSE_NO_SLOT);
    CHECK(operation.probe_count == cache.capacity);
    CHECK(memcmp(&cache, &cache_before, sizeof(cache)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(snapshot.publication_count == 4u);
    CHECK(snapshot.insert_count == 2u);
    CHECK(snapshot.conflict_count == 1u);
    CHECK(snapshot.full_count == 1u);
    CHECK(snapshot.accepted_operation_count == 2u);
    CHECK(snapshot.rejected_operation_count == 2u);

    return 0;
}

static int home_for_key(uint64_t key, uint64_t *home)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_block_cache_result result;
    db_arm64_published_block block;

    if (db_arm64_block_cache_init(&cache, slots, 4u) !=
        DB_ARM64_BLOCK_CACHE_OK) {
        return 0;
    }

    block = arm64_translation_reuse_make_block(UINT64_C(0x6000), 9u, key,
                       UINT64_C(0x600000));
    if (db_arm64_block_cache_insert(&cache, &block, &result) !=
        DB_ARM64_BLOCK_CACHE_OK) {
        return 0;
    }

    *home = result.slot_index;
    return 1;
}

static int find_collision(uint64_t *first_key,
                          uint64_t *second_key)
{
    uint64_t first_home;
    uint64_t candidate;

    *first_key = 1u;
    if (!home_for_key(*first_key, &first_home)) {
        return 0;
    }

    for (candidate = 2u; candidate < UINT64_C(10000); ++candidate) {
        uint64_t candidate_home;

        if (!home_for_key(candidate, &candidate_home)) {
            return 0;
        }

        if (candidate_home == first_home) {
            *second_key = candidate;
            return 1;
        }
    }

    return 0;
}

static int test_collision_probe_accounting(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_translation_reuse_session session;
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;
    db_arm64_published_block first;
    db_arm64_published_block second;
    db_arm64_published_block output;
    uint64_t first_key;
    uint64_t second_key;

    CHECK(find_collision(&first_key, &second_key));
    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(db_arm64_translation_reuse_init(&session) ==
          DB_ARM64_TRANSLATION_REUSE_OK);

    first = arm64_translation_reuse_make_block(UINT64_C(0x6000), 9u, first_key,
                       UINT64_C(0x600000));
    second = arm64_translation_reuse_make_block(UINT64_C(0x6000), 9u, second_key,
                        UINT64_C(0x700000));

    CHECK(db_arm64_translation_reuse_publish(
              &cache, &session, &first,
              &operation, &snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_OK);
    CHECK(operation.probe_count == 1u);

    CHECK(db_arm64_translation_reuse_publish(
              &cache, &session, &second,
              &operation, &snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_OK);
    CHECK(operation.probe_count == 2u);
    CHECK(snapshot.total_publication_probes == 3u);
    CHECK(snapshot.maximum_publication_probes == 2u);

    CHECK(db_arm64_translation_reuse_lookup(
              &cache, &session,
              second.guest_source_rip,
              second.source_generation,
              second.translation_key,
              &output, &operation, &snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_OK);
    CHECK(operation.hit == 1u);
    CHECK(operation.probe_count == 2u);
    CHECK(snapshot.total_lookup_probes == 2u);
    CHECK(snapshot.maximum_lookup_probes == 2u);

    return 0;
}

int main(void)
{
    if (test_conflict_and_full() != 0) {
        return 1;
    }
    if (test_collision_probe_accounting() != 0) {
        return 2;
    }

    printf("ARM64 TRANSLATION REUSE OUTCOME TEST: PASS\n");
    return 0;
}
