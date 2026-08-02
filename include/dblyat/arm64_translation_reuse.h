#ifndef DBLYAT_ARM64_TRANSLATION_REUSE_H
#define DBLYAT_ARM64_TRANSLATION_REUSE_H

#include "dblyat/arm64_block_cache.h"

#include <stdint.h>

#define DB_ARM64_TRANSLATION_REUSE_FORMAT_VERSION UINT32_C(1)
#define DB_ARM64_TRANSLATION_REUSE_NO_SLOT UINT64_MAX

typedef enum db_arm64_translation_reuse_status {
    DB_ARM64_TRANSLATION_REUSE_OK = 0,
    DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT,
    DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP,
    DB_ARM64_TRANSLATION_REUSE_BAD_SESSION,
    DB_ARM64_TRANSLATION_REUSE_CACHE_REJECTED,
    DB_ARM64_TRANSLATION_REUSE_COUNTER_OVERFLOW,
    DB_ARM64_TRANSLATION_REUSE_CACHE_INCONSISTENT
} db_arm64_translation_reuse_status;

typedef enum db_arm64_translation_reuse_operation_kind {
    DB_ARM64_TRANSLATION_REUSE_OPERATION_LOOKUP = 1,
    DB_ARM64_TRANSLATION_REUSE_OPERATION_PUBLICATION
} db_arm64_translation_reuse_operation_kind;

typedef struct db_arm64_translation_reuse_session {
    uint64_t next_sequence;
    uint64_t lookup_count;
    uint64_t hit_count;
    uint64_t miss_count;
    uint64_t total_lookup_probes;
    uint64_t maximum_lookup_probes;
    uint64_t publication_count;
    uint64_t insert_count;
    uint64_t duplicate_count;
    uint64_t conflict_count;
    uint64_t full_count;
    uint64_t total_publication_probes;
    uint64_t maximum_publication_probes;
    uint64_t accepted_operation_count;
    uint64_t rejected_operation_count;
    uint32_t format_version;
    uint8_t reserved[12];
} db_arm64_translation_reuse_session;

typedef struct db_arm64_translation_reuse_snapshot {
    uint64_t completed_sequence;
    uint64_t lookup_count;
    uint64_t hit_count;
    uint64_t miss_count;
    uint64_t total_lookup_probes;
    uint64_t maximum_lookup_probes;
    uint64_t publication_count;
    uint64_t insert_count;
    uint64_t duplicate_count;
    uint64_t conflict_count;
    uint64_t full_count;
    uint64_t total_publication_probes;
    uint64_t maximum_publication_probes;
    uint64_t accepted_operation_count;
    uint64_t rejected_operation_count;
    uint32_t format_version;
    uint8_t reserved[12];
} db_arm64_translation_reuse_snapshot;

typedef struct db_arm64_translation_reuse_operation {
    uint64_t sequence_number;
    uint64_t slot_index;
    uint64_t probe_count;
    uint32_t operation_kind;
    uint32_t cache_status;
    uint32_t hit;
    uint32_t miss;
    uint32_t inserted;
    uint32_t duplicate;
    uint32_t conflict;
    uint32_t full;
    uint32_t format_version;
    uint8_t reserved[4];
} db_arm64_translation_reuse_operation;

db_arm64_translation_reuse_status db_arm64_translation_reuse_init(
    db_arm64_translation_reuse_session *session);

db_arm64_translation_reuse_status db_arm64_translation_reuse_lookup(
    const db_arm64_block_cache *cache,
    db_arm64_translation_reuse_session *session,
    uint64_t guest_source_rip,
    uint64_t source_generation,
    uint64_t translation_key,
    db_arm64_published_block *published,
    db_arm64_translation_reuse_operation *operation,
    db_arm64_translation_reuse_snapshot *snapshot);

db_arm64_translation_reuse_status db_arm64_translation_reuse_publish(
    db_arm64_block_cache *cache,
    db_arm64_translation_reuse_session *session,
    const db_arm64_published_block *published,
    db_arm64_translation_reuse_operation *operation,
    db_arm64_translation_reuse_snapshot *snapshot);

const char *db_arm64_translation_reuse_status_name(
    db_arm64_translation_reuse_status status);

const char *db_arm64_translation_reuse_operation_kind_name(
    db_arm64_translation_reuse_operation_kind kind);

#endif
