#include "arm64_translation_benchmark_test_support.h"

#include <string.h>

int arm64_translation_benchmark_fixture_init(
    arm64_translation_benchmark_fixture *fixture,
    size_t capacity)
{
    memset(fixture, 0, sizeof(*fixture));
    if (capacity > ARM64_TRANSLATION_BENCHMARK_TEST_CAPACITY) {
        return 0;
    }

    return db_arm64_translation_benchmark_init(
               &fixture->session,
               fixture->samples,
               capacity) ==
           DB_ARM64_TRANSLATION_BENCHMARK_OK;
}

static void increment_probe_metrics(uint64_t *count,
                                    uint64_t *total,
                                    uint64_t *maximum,
                                    uint64_t probe_count)
{
    *count += UINT64_C(1);
    *total += probe_count;
    if (probe_count > *maximum) {
        *maximum = probe_count;
    }
}

void arm64_translation_benchmark_make_event(
    const db_arm64_translation_reuse_snapshot *previous,
    db_arm64_translation_benchmark_phase phase,
    uint64_t probe_count,
    uint64_t slot_index,
    db_arm64_translation_reuse_operation *operation,
    db_arm64_translation_reuse_snapshot *snapshot)
{
    memset(operation, 0, sizeof(*operation));
    if (previous == NULL) {
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->format_version =
            DB_ARM64_TRANSLATION_REUSE_FORMAT_VERSION;
    } else {
        *snapshot = *previous;
    }

    snapshot->completed_sequence += UINT64_C(1);
    operation->sequence_number = snapshot->completed_sequence;
    operation->slot_index = slot_index;
    operation->probe_count = probe_count;
    operation->format_version =
        DB_ARM64_TRANSLATION_REUSE_FORMAT_VERSION;

    switch (phase) {
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_INSERT:
        operation->operation_kind =
            (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_PUBLICATION;
        operation->cache_status = (uint32_t)DB_ARM64_BLOCK_CACHE_OK;
        operation->inserted = 1u;
        increment_probe_metrics(
            &snapshot->publication_count,
            &snapshot->total_publication_probes,
            &snapshot->maximum_publication_probes,
            probe_count);
        snapshot->insert_count += UINT64_C(1);
        snapshot->accepted_operation_count += UINT64_C(1);
        break;
    case DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT:
        operation->operation_kind =
            (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_LOOKUP;
        operation->cache_status = (uint32_t)DB_ARM64_BLOCK_CACHE_OK;
        operation->hit = 1u;
        increment_probe_metrics(
            &snapshot->lookup_count,
            &snapshot->total_lookup_probes,
            &snapshot->maximum_lookup_probes,
            probe_count);
        snapshot->hit_count += UINT64_C(1);
        snapshot->accepted_operation_count += UINT64_C(1);
        break;
    case DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS:
        operation->operation_kind =
            (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_LOOKUP;
        operation->cache_status =
            (uint32_t)DB_ARM64_BLOCK_CACHE_NOT_FOUND;
        operation->miss = 1u;
        increment_probe_metrics(
            &snapshot->lookup_count,
            &snapshot->total_lookup_probes,
            &snapshot->maximum_lookup_probes,
            probe_count);
        snapshot->miss_count += UINT64_C(1);
        snapshot->accepted_operation_count += UINT64_C(1);
        break;
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_DUPLICATE:
        operation->operation_kind =
            (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_PUBLICATION;
        operation->cache_status = (uint32_t)DB_ARM64_BLOCK_CACHE_OK;
        operation->duplicate = 1u;
        increment_probe_metrics(
            &snapshot->publication_count,
            &snapshot->total_publication_probes,
            &snapshot->maximum_publication_probes,
            probe_count);
        snapshot->duplicate_count += UINT64_C(1);
        snapshot->accepted_operation_count += UINT64_C(1);
        break;
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_CONFLICT:
        operation->operation_kind =
            (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_PUBLICATION;
        operation->cache_status =
            (uint32_t)DB_ARM64_BLOCK_CACHE_KEY_CONFLICT;
        operation->conflict = 1u;
        increment_probe_metrics(
            &snapshot->publication_count,
            &snapshot->total_publication_probes,
            &snapshot->maximum_publication_probes,
            probe_count);
        snapshot->conflict_count += UINT64_C(1);
        snapshot->rejected_operation_count += UINT64_C(1);
        break;
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_FULL:
        operation->operation_kind =
            (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_PUBLICATION;
        operation->cache_status = (uint32_t)DB_ARM64_BLOCK_CACHE_FULL;
        operation->full = 1u;
        increment_probe_metrics(
            &snapshot->publication_count,
            &snapshot->total_publication_probes,
            &snapshot->maximum_publication_probes,
            probe_count);
        snapshot->full_count += UINT64_C(1);
        snapshot->rejected_operation_count += UINT64_C(1);
        break;
    }
}

int arm64_translation_benchmark_fixture_unchanged(
    const arm64_translation_benchmark_fixture *left,
    const arm64_translation_benchmark_fixture *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

int arm64_translation_benchmark_session_equivalent(
    const db_arm64_translation_benchmark_session *left,
    const db_arm64_translation_benchmark_session *right)
{
    db_arm64_translation_benchmark_session left_copy = *left;
    db_arm64_translation_benchmark_session right_copy = *right;

    left_copy.samples = NULL;
    right_copy.samples = NULL;
    return memcmp(&left_copy, &right_copy, sizeof(left_copy)) == 0;
}
