#include "arm64_translation_benchmark_test_support.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("ARM64 TRANSLATION BENCHMARK VALIDATION TEST: FAIL line=%d\n", \
                   __LINE__);                                                \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void make_next(
    const arm64_translation_benchmark_fixture *fixture,
    db_arm64_translation_benchmark_phase phase,
    uint64_t probe_count,
    uint64_t slot_index,
    db_arm64_translation_reuse_operation *operation,
    db_arm64_translation_reuse_snapshot *snapshot)
{
    const db_arm64_translation_reuse_snapshot *previous =
        fixture->session.has_snapshot != 0u
            ? &fixture->session.last_snapshot
            : NULL;

    arm64_translation_benchmark_make_event(
        previous,
        phase,
        probe_count,
        slot_index,
        operation,
        snapshot);
}

static int commit_one(
    arm64_translation_benchmark_fixture *fixture,
    db_arm64_translation_benchmark_phase phase,
    uint64_t start_ns,
    uint64_t end_ns)
{
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;
    uint64_t slot_index =
        phase == DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_FULL
            ? DB_ARM64_TRANSLATION_REUSE_NO_SLOT
            : 1u;

    make_next(fixture,
              phase,
              1u,
              slot_index,
              &operation,
              &snapshot);
    return db_arm64_translation_benchmark_record(
               &fixture->session,
               phase,
               0u,
               start_ns,
               end_ns,
               &operation,
               &snapshot) ==
           DB_ARM64_TRANSLATION_BENCHMARK_OK;
}

static int test_init_validation(void)
{
    arm64_translation_benchmark_fixture value;
    union {
        max_align_t alignment;
        unsigned char bytes[
            sizeof(db_arm64_translation_benchmark_session) +
            sizeof(db_arm64_translation_benchmark_sample)];
    } overlap;

    memset(&value, 0xa5, sizeof(value));
    CHECK(db_arm64_translation_benchmark_init(
              NULL,
              value.samples,
              1u) ==
          DB_ARM64_TRANSLATION_BENCHMARK_NULL_ARGUMENT);
    CHECK(db_arm64_translation_benchmark_init(
              &value.session,
              NULL,
              1u) ==
          DB_ARM64_TRANSLATION_BENCHMARK_NULL_ARGUMENT);
    CHECK(db_arm64_translation_benchmark_init(
              &value.session,
              value.samples,
              0u) ==
          DB_ARM64_TRANSLATION_BENCHMARK_BAD_CAPACITY);

    memset(&overlap, 0, sizeof(overlap));
    CHECK(db_arm64_translation_benchmark_init(
              (db_arm64_translation_benchmark_session *)overlap.bytes,
              (db_arm64_translation_benchmark_sample *)overlap.bytes,
              1u) ==
          DB_ARM64_TRANSLATION_BENCHMARK_BUFFER_OVERLAP);
    return 0;
}

static int test_argument_and_alias_validation(void)
{
    arm64_translation_benchmark_fixture value;
    arm64_translation_benchmark_fixture before;
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;

    CHECK(arm64_translation_benchmark_fixture_init(&value, 2u));
    make_next(
        &value,
        DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
        1u,
        0u,
        &operation,
        &snapshot);
    before = value;

    CHECK(db_arm64_translation_benchmark_record(
              NULL,
              DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
              0u,
              1u,
              2u,
              &operation,
              &snapshot) ==
          DB_ARM64_TRANSLATION_BENCHMARK_NULL_ARGUMENT);
    CHECK(db_arm64_translation_benchmark_record(
              &value.session,
              DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
              0u,
              1u,
              2u,
              NULL,
              &snapshot) ==
          DB_ARM64_TRANSLATION_BENCHMARK_NULL_ARGUMENT);
    CHECK(db_arm64_translation_benchmark_record(
              &value.session,
              DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
              0u,
              1u,
              2u,
              &operation,
              NULL) ==
          DB_ARM64_TRANSLATION_BENCHMARK_NULL_ARGUMENT);
    CHECK(arm64_translation_benchmark_fixture_unchanged(
        &value, &before));

    CHECK(db_arm64_translation_benchmark_record(
              &value.session,
              DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
              0u,
              1u,
              2u,
              (const db_arm64_translation_reuse_operation *)
                  &value.samples[0],
              &snapshot) ==
          DB_ARM64_TRANSLATION_BENCHMARK_BUFFER_OVERLAP);
    CHECK(arm64_translation_benchmark_fixture_unchanged(
        &value, &before));

    CHECK(db_arm64_translation_benchmark_record(
              &value.session,
              DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
              0u,
              1u,
              2u,
              &operation,
              (const db_arm64_translation_reuse_snapshot *)
                  &operation) ==
          DB_ARM64_TRANSLATION_BENCHMARK_BUFFER_OVERLAP);
    CHECK(arm64_translation_benchmark_fixture_unchanged(
        &value, &before));
    return 0;
}

static int test_bad_record_preservation(void)
{
    arm64_translation_benchmark_fixture value;
    arm64_translation_benchmark_fixture before;
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;

    CHECK(arm64_translation_benchmark_fixture_init(&value, 2u));
    make_next(
        &value,
        DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
        1u,
        1u,
        &operation,
        &snapshot);

    before = value;
    CHECK(db_arm64_translation_benchmark_record(
              &value.session,
              (db_arm64_translation_benchmark_phase)99,
              0u,
              10u,
              11u,
              &operation,
              &snapshot) ==
          DB_ARM64_TRANSLATION_BENCHMARK_BAD_PHASE);
    CHECK(arm64_translation_benchmark_fixture_unchanged(
        &value, &before));

    before = value;
    CHECK(db_arm64_translation_benchmark_record(
              &value.session,
              DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
              0u,
              12u,
              11u,
              &operation,
              &snapshot) ==
          DB_ARM64_TRANSLATION_BENCHMARK_BAD_TIMESTAMP);
    CHECK(arm64_translation_benchmark_fixture_unchanged(
        &value, &before));

    operation.miss = 1u;
    before = value;
    CHECK(db_arm64_translation_benchmark_record(
              &value.session,
              DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
              0u,
              10u,
              11u,
              &operation,
              &snapshot) ==
          DB_ARM64_TRANSLATION_BENCHMARK_BAD_OPERATION);
    CHECK(arm64_translation_benchmark_fixture_unchanged(
        &value, &before));
    operation.miss = 0u;

    operation.sequence_number = 2u;
    before = value;
    CHECK(db_arm64_translation_benchmark_record(
              &value.session,
              DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
              0u,
              10u,
              11u,
              &operation,
              &snapshot) ==
          DB_ARM64_TRANSLATION_BENCHMARK_BAD_OPERATION);
    CHECK(arm64_translation_benchmark_fixture_unchanged(
        &value, &before));
    operation.sequence_number = 1u;

    snapshot.hit_count = 0u;
    snapshot.miss_count = 1u;
    before = value;
    CHECK(db_arm64_translation_benchmark_record(
              &value.session,
              DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
              0u,
              10u,
              11u,
              &operation,
              &snapshot) ==
          DB_ARM64_TRANSLATION_BENCHMARK_BAD_SNAPSHOT);
    CHECK(arm64_translation_benchmark_fixture_unchanged(
        &value, &before));
    return 0;
}

static int test_full_and_corruption(void)
{
    arm64_translation_benchmark_fixture value;
    arm64_translation_benchmark_fixture before;
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;

    CHECK(arm64_translation_benchmark_fixture_init(&value, 1u));
    CHECK(commit_one(
        &value,
        DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_INSERT,
        10u,
        20u));
    make_next(
        &value,
        DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
        1u,
        1u,
        &operation,
        &snapshot);
    before = value;
    CHECK(db_arm64_translation_benchmark_record(
              &value.session,
              DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
              1u,
              30u,
              31u,
              &operation,
              &snapshot) ==
          DB_ARM64_TRANSLATION_BENCHMARK_FULL);
    CHECK(arm64_translation_benchmark_fixture_unchanged(
        &value, &before));

    CHECK(arm64_translation_benchmark_fixture_init(&value, 2u));
    value.samples[1].reserved[0] = 1u;
    make_next(
        &value,
        DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
        1u,
        0u,
        &operation,
        &snapshot);
    before = value;
    CHECK(db_arm64_translation_benchmark_record(
              &value.session,
              DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
              0u,
              1u,
              2u,
              &operation,
              &snapshot) ==
          DB_ARM64_TRANSLATION_BENCHMARK_BAD_SESSION);
    CHECK(arm64_translation_benchmark_fixture_unchanged(
        &value, &before));
    return 0;
}

static int test_duration_overflow(void)
{
    arm64_translation_benchmark_fixture value;
    arm64_translation_benchmark_fixture before;
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;

    CHECK(arm64_translation_benchmark_fixture_init(&value, 2u));
    CHECK(commit_one(
        &value,
        DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
        0u,
        UINT64_MAX));

    make_next(
        &value,
        DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
        1u,
        0u,
        &operation,
        &snapshot);
    before = value;
    CHECK(db_arm64_translation_benchmark_record(
              &value.session,
              DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
              1u,
              0u,
              1u,
              &operation,
              &snapshot) ==
          DB_ARM64_TRANSLATION_BENCHMARK_COUNTER_OVERFLOW);
    CHECK(arm64_translation_benchmark_fixture_unchanged(
        &value, &before));
    return 0;
}

int main(void)
{
    if (test_init_validation() != 0) {
        return 1;
    }
    if (test_argument_and_alias_validation() != 0) {
        return 2;
    }
    if (test_bad_record_preservation() != 0) {
        return 3;
    }
    if (test_full_and_corruption() != 0) {
        return 4;
    }
    if (test_duration_overflow() != 0) {
        return 5;
    }

    printf("ARM64 TRANSLATION BENCHMARK VALIDATION TEST: PASS\n");
    return 0;
}
