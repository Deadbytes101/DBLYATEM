#ifndef ARM64_TRANSLATION_BENCHMARK_TEST_SUPPORT_H
#define ARM64_TRANSLATION_BENCHMARK_TEST_SUPPORT_H

#include "dblyat/arm64_translation_benchmark.h"

#include <stdint.h>

#define ARM64_TRANSLATION_BENCHMARK_TEST_CAPACITY 8u

typedef struct arm64_translation_benchmark_fixture {
    db_arm64_translation_benchmark_session session;
    db_arm64_translation_benchmark_sample
        samples[ARM64_TRANSLATION_BENCHMARK_TEST_CAPACITY];
} arm64_translation_benchmark_fixture;

int arm64_translation_benchmark_fixture_init(
    arm64_translation_benchmark_fixture *fixture,
    size_t capacity);

void arm64_translation_benchmark_make_event(
    const db_arm64_translation_reuse_snapshot *previous,
    db_arm64_translation_benchmark_phase phase,
    uint64_t probe_count,
    uint64_t slot_index,
    db_arm64_translation_reuse_operation *operation,
    db_arm64_translation_reuse_snapshot *snapshot);

int arm64_translation_benchmark_fixture_unchanged(
    const arm64_translation_benchmark_fixture *left,
    const arm64_translation_benchmark_fixture *right);

int arm64_translation_benchmark_session_equivalent(
    const db_arm64_translation_benchmark_session *left,
    const db_arm64_translation_benchmark_session *right);

#endif
