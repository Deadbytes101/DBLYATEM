#ifndef DBLYAT_ARM64_TRANSLATION_BENCHMARK_H
#define DBLYAT_ARM64_TRANSLATION_BENCHMARK_H

#include "dblyat/arm64_translation_reuse.h"

#include <stddef.h>
#include <stdint.h>

#define DB_ARM64_TRANSLATION_BENCHMARK_FORMAT_VERSION UINT32_C(1)
#define DB_ARM64_TRANSLATION_BENCHMARK_PHASE_COUNT 6u

typedef enum db_arm64_translation_benchmark_status {
    DB_ARM64_TRANSLATION_BENCHMARK_OK = 0,
    DB_ARM64_TRANSLATION_BENCHMARK_NULL_ARGUMENT,
    DB_ARM64_TRANSLATION_BENCHMARK_BUFFER_OVERLAP,
    DB_ARM64_TRANSLATION_BENCHMARK_BAD_CAPACITY,
    DB_ARM64_TRANSLATION_BENCHMARK_BAD_SESSION,
    DB_ARM64_TRANSLATION_BENCHMARK_BAD_PHASE,
    DB_ARM64_TRANSLATION_BENCHMARK_BAD_TIMESTAMP,
    DB_ARM64_TRANSLATION_BENCHMARK_BAD_OPERATION,
    DB_ARM64_TRANSLATION_BENCHMARK_BAD_SNAPSHOT,
    DB_ARM64_TRANSLATION_BENCHMARK_FULL,
    DB_ARM64_TRANSLATION_BENCHMARK_COUNTER_OVERFLOW
} db_arm64_translation_benchmark_status;

typedef enum db_arm64_translation_benchmark_phase {
    DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_INSERT = 1,
    DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
    DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
    DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_DUPLICATE,
    DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_CONFLICT,
    DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_FULL
} db_arm64_translation_benchmark_phase;

typedef struct db_arm64_translation_benchmark_sample {
    uint64_t benchmark_sequence;
    uint64_t iteration;
    uint64_t start_ns;
    uint64_t end_ns;
    uint64_t duration_ns;
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;
    uint32_t phase;
    uint32_t format_version;
    uint8_t reserved[8];
} db_arm64_translation_benchmark_sample;

typedef struct db_arm64_translation_benchmark_session {
    db_arm64_translation_benchmark_sample *samples;
    size_t capacity;
    size_t count;
    uint64_t next_sequence;
    uint64_t total_duration_ns;
    uint64_t minimum_duration_ns;
    uint64_t maximum_duration_ns;
    uint64_t phase_sample_count[DB_ARM64_TRANSLATION_BENCHMARK_PHASE_COUNT];
    uint64_t phase_total_duration_ns[DB_ARM64_TRANSLATION_BENCHMARK_PHASE_COUNT];
    db_arm64_translation_reuse_snapshot last_snapshot;
    uint32_t has_snapshot;
    uint32_t format_version;
    uint8_t reserved[8];
} db_arm64_translation_benchmark_session;

db_arm64_translation_benchmark_status
db_arm64_translation_benchmark_init(
    db_arm64_translation_benchmark_session *session,
    db_arm64_translation_benchmark_sample *samples,
    size_t capacity);

db_arm64_translation_benchmark_status
db_arm64_translation_benchmark_record(
    db_arm64_translation_benchmark_session *session,
    db_arm64_translation_benchmark_phase phase,
    uint64_t iteration,
    uint64_t start_ns,
    uint64_t end_ns,
    const db_arm64_translation_reuse_operation *operation,
    const db_arm64_translation_reuse_snapshot *snapshot);

const char *db_arm64_translation_benchmark_status_name(
    db_arm64_translation_benchmark_status status);

const char *db_arm64_translation_benchmark_phase_name(
    db_arm64_translation_benchmark_phase phase);

#endif
