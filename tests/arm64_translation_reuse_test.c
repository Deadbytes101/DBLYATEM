#include "arm64_translation_reuse_test_support.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("ARM64 TRANSLATION REUSE TEST: FAIL line=%d\n", __LINE__);              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int test_basic_session(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[8];
    db_arm64_translation_reuse_session session;
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;
    db_arm64_published_block block;
    db_arm64_published_block output;
    db_arm64_published_block output_before;
    uint64_t miss_slot;
    uint64_t miss_probes;

    CHECK(db_arm64_block_cache_init(&cache, slots, 8u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(db_arm64_translation_reuse_init(&session) ==
          DB_ARM64_TRANSLATION_REUSE_OK);
    CHECK(session.next_sequence == 1u);
    CHECK(session.format_version ==
          DB_ARM64_TRANSLATION_REUSE_FORMAT_VERSION);

    block = arm64_translation_reuse_make_block(UINT64_C(0x1000), 1u, 11u,
                       UINT64_C(0x100000));
    memset(&output, 0xa5, sizeof(output));
    output_before = output;
    memset(&operation, 0x5a, sizeof(operation));
    memset(&snapshot, 0x3c, sizeof(snapshot));

    CHECK(db_arm64_translation_reuse_lookup(
              &cache,
              &session,
              block.guest_source_rip,
              block.source_generation,
              block.translation_key,
              &output,
              &operation,
              &snapshot) == DB_ARM64_TRANSLATION_REUSE_OK);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(operation.sequence_number == 1u);
    CHECK(operation.operation_kind ==
          (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_LOOKUP);
    CHECK(operation.cache_status ==
          (uint32_t)DB_ARM64_BLOCK_CACHE_NOT_FOUND);
    CHECK(operation.hit == 0u);
    CHECK(operation.miss == 1u);
    CHECK(operation.inserted == 0u);
    CHECK(operation.duplicate == 0u);
    CHECK(operation.conflict == 0u);
    CHECK(operation.full == 0u);
    CHECK(operation.probe_count == 1u);
    CHECK(operation.slot_index < cache.capacity);
    miss_slot = operation.slot_index;
    miss_probes = operation.probe_count;
    CHECK(snapshot.completed_sequence == 1u);
    CHECK(snapshot.lookup_count == 1u);
    CHECK(snapshot.hit_count == 0u);
    CHECK(snapshot.miss_count == 1u);
    CHECK(snapshot.total_lookup_probes == 1u);
    CHECK(snapshot.maximum_lookup_probes == 1u);
    CHECK(snapshot.accepted_operation_count == 1u);
    CHECK(snapshot.rejected_operation_count == 0u);

    CHECK(db_arm64_translation_reuse_publish(
              &cache,
              &session,
              &block,
              &operation,
              &snapshot) == DB_ARM64_TRANSLATION_REUSE_OK);
    CHECK(operation.sequence_number == 2u);
    CHECK(operation.operation_kind ==
          (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_PUBLICATION);
    CHECK(operation.cache_status ==
          (uint32_t)DB_ARM64_BLOCK_CACHE_OK);
    CHECK(operation.inserted == 1u);
    CHECK(operation.slot_index == miss_slot);
    CHECK(operation.probe_count == miss_probes);
    CHECK(snapshot.publication_count == 1u);
    CHECK(snapshot.insert_count == 1u);
    CHECK(snapshot.duplicate_count == 0u);
    CHECK(snapshot.accepted_operation_count == 2u);
    CHECK(cache.count == 1u);

    memset(&output, 0xa5, sizeof(output));
    CHECK(db_arm64_translation_reuse_lookup(
              &cache,
              &session,
              block.guest_source_rip,
              block.source_generation,
              block.translation_key,
              &output,
              &operation,
              &snapshot) == DB_ARM64_TRANSLATION_REUSE_OK);
    CHECK(operation.sequence_number == 3u);
    CHECK(operation.cache_status ==
          (uint32_t)DB_ARM64_BLOCK_CACHE_OK);
    CHECK(operation.hit == 1u);
    CHECK(operation.miss == 0u);
    CHECK(memcmp(&output, &block, sizeof(output)) == 0);
    CHECK(snapshot.lookup_count == 2u);
    CHECK(snapshot.hit_count == 1u);
    CHECK(snapshot.miss_count == 1u);

    CHECK(db_arm64_translation_reuse_publish(
              &cache,
              &session,
              &block,
              &operation,
              &snapshot) == DB_ARM64_TRANSLATION_REUSE_OK);
    CHECK(operation.sequence_number == 4u);
    CHECK(operation.cache_status ==
          (uint32_t)DB_ARM64_BLOCK_CACHE_OK);
    CHECK(operation.inserted == 0u);
    CHECK(operation.duplicate == 1u);
    CHECK(cache.count == 1u);
    CHECK(snapshot.publication_count == 2u);
    CHECK(snapshot.insert_count == 1u);
    CHECK(snapshot.duplicate_count == 1u);
    CHECK(snapshot.accepted_operation_count == 4u);
    CHECK(snapshot.rejected_operation_count == 0u);
    CHECK(session.next_sequence == 5u);

    return 0;
}

static int test_status_names(void)
{
    CHECK(strcmp(db_arm64_translation_reuse_status_name(
                     DB_ARM64_TRANSLATION_REUSE_OK),
                 "ok") == 0);
    CHECK(strcmp(db_arm64_translation_reuse_status_name(
                     DB_ARM64_TRANSLATION_REUSE_COUNTER_OVERFLOW),
                 "counter overflow") == 0);
    CHECK(strcmp(db_arm64_translation_reuse_status_name(
                     (db_arm64_translation_reuse_status)99),
                 "unknown") == 0);
    CHECK(strcmp(db_arm64_translation_reuse_operation_kind_name(
                     DB_ARM64_TRANSLATION_REUSE_OPERATION_LOOKUP),
                 "lookup") == 0);
    CHECK(strcmp(db_arm64_translation_reuse_operation_kind_name(
                     DB_ARM64_TRANSLATION_REUSE_OPERATION_PUBLICATION),
                 "publication") == 0);
    CHECK(strcmp(db_arm64_translation_reuse_operation_kind_name(
                     (db_arm64_translation_reuse_operation_kind)99),
                 "unknown") == 0);

    return 0;
}

int main(void)
{
    if (test_basic_session() != 0) {
        return 1;
    }
    if (test_status_names() != 0) {
        return 2;
    }

    printf("ARM64 TRANSLATION REUSE TEST: PASS\n");
    return 0;
}
