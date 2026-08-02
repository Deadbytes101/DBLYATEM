#include "arm64_translation_reuse_test_support.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("ARM64 TRANSLATION REUSE REPLAY TEST: FAIL line=%d\n", __LINE__);              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int run_replay(db_arm64_translation_reuse_snapshot *final_snapshot)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[8];
    db_arm64_translation_reuse_session session;
    db_arm64_translation_reuse_operation operation;
    db_arm64_published_block first;
    db_arm64_published_block second;
    db_arm64_published_block output;

    if (db_arm64_block_cache_init(&cache, slots, 8u) !=
            DB_ARM64_BLOCK_CACHE_OK ||
        db_arm64_translation_reuse_init(&session) !=
            DB_ARM64_TRANSLATION_REUSE_OK) {
        return 0;
    }

    first = arm64_translation_reuse_make_block(UINT64_C(0x8000), 3u, 31u,
                       UINT64_C(0x800000));
    second = arm64_translation_reuse_make_block(UINT64_C(0x9000), 3u, 32u,
                        UINT64_C(0x900000));

    if (db_arm64_translation_reuse_lookup(
            &cache, &session,
            first.guest_source_rip,
            first.source_generation,
            first.translation_key,
            &output, &operation, final_snapshot) !=
            DB_ARM64_TRANSLATION_REUSE_OK ||
        db_arm64_translation_reuse_publish(
            &cache, &session, &first,
            &operation, final_snapshot) !=
            DB_ARM64_TRANSLATION_REUSE_OK ||
        db_arm64_translation_reuse_publish(
            &cache, &session, &second,
            &operation, final_snapshot) !=
            DB_ARM64_TRANSLATION_REUSE_OK ||
        db_arm64_translation_reuse_lookup(
            &cache, &session,
            first.guest_source_rip,
            first.source_generation,
            first.translation_key,
            &output, &operation, final_snapshot) !=
            DB_ARM64_TRANSLATION_REUSE_OK ||
        db_arm64_translation_reuse_publish(
            &cache, &session, &first,
            &operation, final_snapshot) !=
            DB_ARM64_TRANSLATION_REUSE_OK) {
        return 0;
    }

    return 1;
}

static int test_deterministic_replay(void)
{
    db_arm64_translation_reuse_snapshot first;
    db_arm64_translation_reuse_snapshot second;

    CHECK(run_replay(&first));
    CHECK(run_replay(&second));
    CHECK(memcmp(&first, &second, sizeof(first)) == 0);
    CHECK(first.completed_sequence == 5u);
    CHECK(first.lookup_count == 2u);
    CHECK(first.hit_count == 1u);
    CHECK(first.miss_count == 1u);
    CHECK(first.publication_count == 3u);
    CHECK(first.insert_count == 2u);
    CHECK(first.duplicate_count == 1u);
    CHECK(first.accepted_operation_count == 5u);
    CHECK(first.rejected_operation_count == 0u);

    return 0;
}

int main(void)
{
    if (test_deterministic_replay() != 0) {
        return 1;
    }

    printf("ARM64 TRANSLATION REUSE REPLAY TEST: PASS\n");
    return 0;
}
