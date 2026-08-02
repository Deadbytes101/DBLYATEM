#include "arm64_translation_benchmark_test_support.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("ARM64 TRANSLATION BENCHMARK TEST: FAIL line=%d\n",       \
                   __LINE__);                                                \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int record_event(
    arm64_translation_benchmark_fixture *fixture,
    db_arm64_translation_benchmark_phase phase,
    uint64_t iteration,
    uint64_t start_ns,
    uint64_t end_ns,
    uint64_t probe_count,
    uint64_t slot_index)
{
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;
    const db_arm64_translation_reuse_snapshot *previous =
        fixture->session.has_snapshot != 0u
            ? &fixture->session.last_snapshot
            : NULL;

    arm64_translation_benchmark_make_event(
        previous,
        phase,
        probe_count,
        slot_index,
        &operation,
        &snapshot);

    return db_arm64_translation_benchmark_record(
               &fixture->session,
               phase,
               iteration,
               start_ns,
               end_ns,
               &operation,
               &snapshot) ==
           DB_ARM64_TRANSLATION_BENCHMARK_OK;
}

static int test_exact_metrics(void)
{
    arm64_translation_benchmark_fixture value;

    CHECK(arm64_translation_benchmark_fixture_init(&value, 6u));
    CHECK(record_event(
        &value,
        DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_INSERT,
        0u,
        100u,
        110u,
        1u,
        2u));
    CHECK(record_event(
        &value,
        DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
        0u,
        200u,
        200u,
        2u,
        2u));
    CHECK(record_event(
        &value,
        DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
        0u,
        300u,
        315u,
        3u,
        4u));
    CHECK(record_event(
        &value,
        DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_DUPLICATE,
        0u,
        400u,
        405u,
        2u,
        2u));

    CHECK(value.session.count == 4u);
    CHECK(value.session.next_sequence == 5u);
    CHECK(value.session.total_duration_ns == 30u);
    CHECK(value.session.minimum_duration_ns == 0u);
    CHECK(value.session.maximum_duration_ns == 15u);
    CHECK(value.session.phase_sample_count[0] == 1u);
    CHECK(value.session.phase_sample_count[1] == 1u);
    CHECK(value.session.phase_sample_count[2] == 1u);
    CHECK(value.session.phase_sample_count[3] == 1u);
    CHECK(value.session.phase_total_duration_ns[0] == 10u);
    CHECK(value.session.phase_total_duration_ns[1] == 0u);
    CHECK(value.session.phase_total_duration_ns[2] == 15u);
    CHECK(value.session.phase_total_duration_ns[3] == 5u);
    CHECK(value.samples[0].benchmark_sequence == 1u);
    CHECK(value.samples[3].benchmark_sequence == 4u);
    CHECK(value.samples[1].duration_ns == 0u);
    CHECK(value.samples[2].operation.miss == 1u);
    CHECK(value.samples[3].snapshot.duplicate_count == 1u);
    CHECK(value.session.last_snapshot.completed_sequence == 4u);
    CHECK(value.session.last_snapshot.lookup_count == 2u);
    CHECK(value.session.last_snapshot.hit_count == 1u);
    CHECK(value.session.last_snapshot.miss_count == 1u);
    CHECK(value.session.last_snapshot.publication_count == 2u);
    CHECK(value.session.last_snapshot.insert_count == 1u);
    CHECK(value.session.last_snapshot.duplicate_count == 1u);

    return 0;
}

static int test_deterministic_samples(void)
{
    arm64_translation_benchmark_fixture first;
    arm64_translation_benchmark_fixture second;

    CHECK(arm64_translation_benchmark_fixture_init(&first, 4u));
    CHECK(arm64_translation_benchmark_fixture_init(&second, 4u));

    CHECK(record_event(
        &first,
        DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_INSERT,
        7u,
        1000u,
        1010u,
        1u,
        1u));
    CHECK(record_event(
        &first,
        DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
        8u,
        2000u,
        2003u,
        1u,
        1u));

    CHECK(record_event(
        &second,
        DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_INSERT,
        7u,
        1000u,
        1010u,
        1u,
        1u));
    CHECK(record_event(
        &second,
        DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
        8u,
        2000u,
        2003u,
        1u,
        1u));

    CHECK(memcmp(first.samples,
                 second.samples,
                 sizeof(first.samples)) == 0);
    CHECK(arm64_translation_benchmark_session_equivalent(
        &first.session,
        &second.session));
    return 0;
}

static int test_rejected_phases(void)
{
    arm64_translation_benchmark_fixture value;

    CHECK(arm64_translation_benchmark_fixture_init(&value, 2u));
    CHECK(record_event(
        &value,
        DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_CONFLICT,
        1u,
        10u,
        12u,
        2u,
        3u));
    CHECK(record_event(
        &value,
        DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_FULL,
        2u,
        20u,
        24u,
        4u,
        DB_ARM64_TRANSLATION_REUSE_NO_SLOT));

    CHECK(value.session.last_snapshot.publication_count == 2u);
    CHECK(value.session.last_snapshot.conflict_count == 1u);
    CHECK(value.session.last_snapshot.full_count == 1u);
    CHECK(value.session.last_snapshot.accepted_operation_count == 0u);
    CHECK(value.session.last_snapshot.rejected_operation_count == 2u);
    CHECK(value.session.phase_sample_count[4] == 1u);
    CHECK(value.session.phase_sample_count[5] == 1u);
    return 0;
}

static int test_names(void)
{
    CHECK(strcmp(
              db_arm64_translation_benchmark_status_name(
                  DB_ARM64_TRANSLATION_BENCHMARK_BAD_TIMESTAMP),
              "BAD_TIMESTAMP") == 0);
    CHECK(strcmp(
              db_arm64_translation_benchmark_phase_name(
                  DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT),
              "LOOKUP_HIT") == 0);
    CHECK(strcmp(
              db_arm64_translation_benchmark_status_name(
                  (db_arm64_translation_benchmark_status)999),
              "UNKNOWN") == 0);
    CHECK(strcmp(
              db_arm64_translation_benchmark_phase_name(
                  (db_arm64_translation_benchmark_phase)999),
              "UNKNOWN") == 0);
    return 0;
}

int main(void)
{
    if (test_exact_metrics() != 0) {
        return 1;
    }
    if (test_deterministic_samples() != 0) {
        return 2;
    }
    if (test_rejected_phases() != 0) {
        return 3;
    }
    if (test_names() != 0) {
        return 4;
    }

    printf("ARM64 TRANSLATION BENCHMARK TEST: PASS\n");
    return 0;
}
