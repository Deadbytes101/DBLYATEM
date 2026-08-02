#ifndef DBLYAT_ARM64_TRANSLATION_REUSE_INTERNAL_H
#define DBLYAT_ARM64_TRANSLATION_REUSE_INTERNAL_H

#include "dblyat/arm64_translation_reuse.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct db_arm64_translation_reuse_probe_observation {
    uint64_t slot_index;
    uint64_t probe_count;
    bool found;
} db_arm64_translation_reuse_probe_observation;

typedef enum db_arm64_translation_reuse_publication_outcome {
    DB_ARM64_TRANSLATION_REUSE_PUBLICATION_INSERT = 0,
    DB_ARM64_TRANSLATION_REUSE_PUBLICATION_DUPLICATE,
    DB_ARM64_TRANSLATION_REUSE_PUBLICATION_CONFLICT,
    DB_ARM64_TRANSLATION_REUSE_PUBLICATION_FULL
} db_arm64_translation_reuse_publication_outcome;

bool db_arm64_translation_reuse_internal_zero_bytes(const void *data, size_t size);

bool db_arm64_translation_reuse_internal_regions_overlap(const void *left,
                            size_t left_size,
                            const void *right,
                            size_t right_size);

bool db_arm64_translation_reuse_internal_multiply_size(size_t left, size_t right, size_t *result);

bool db_arm64_translation_reuse_internal_add_u64(uint64_t left, uint64_t right, uint64_t *result);

bool db_arm64_translation_reuse_internal_increment_u64(uint64_t *value);

bool db_arm64_translation_reuse_internal_session_valid(
    const db_arm64_translation_reuse_session *session);

bool db_arm64_translation_reuse_internal_cache_storage_bounds(const db_arm64_block_cache *cache,
                                 size_t *slot_bytes);

bool db_arm64_translation_reuse_internal_lookup_fixed_overlap(
    const db_arm64_block_cache *cache,
    const db_arm64_translation_reuse_session *session,
    const db_arm64_published_block *published,
    const db_arm64_translation_reuse_operation *operation,
    const db_arm64_translation_reuse_snapshot *snapshot);

bool db_arm64_translation_reuse_internal_publish_fixed_overlap(
    const db_arm64_block_cache *cache,
    const db_arm64_translation_reuse_session *session,
    const db_arm64_published_block *published,
    const db_arm64_translation_reuse_operation *operation,
    const db_arm64_translation_reuse_snapshot *snapshot);

bool db_arm64_translation_reuse_internal_lookup_slot_overlap(
    const db_arm64_block_cache *cache,
    size_t slot_bytes,
    const db_arm64_translation_reuse_session *session,
    const db_arm64_published_block *published,
    const db_arm64_translation_reuse_operation *operation,
    const db_arm64_translation_reuse_snapshot *snapshot);

uint64_t db_arm64_translation_reuse_internal_mix_u64(uint64_t value);

uint64_t db_arm64_translation_reuse_internal_rotate_left(uint64_t value, unsigned int amount);

size_t db_arm64_translation_reuse_internal_key_home(uint64_t guest_source_rip,
                       uint64_t source_generation,
                       uint64_t translation_key,
                       size_t capacity);

bool db_arm64_translation_reuse_internal_key_equal(const db_arm64_published_block *block,
                      uint64_t guest_source_rip,
                      uint64_t source_generation,
                      uint64_t translation_key);

db_arm64_translation_reuse_probe_observation db_arm64_translation_reuse_internal_observe_probe(
    const db_arm64_block_cache *cache,
    uint64_t guest_source_rip,
    uint64_t source_generation,
    uint64_t translation_key);

bool db_arm64_translation_reuse_internal_cache_result_matches(
    const db_arm64_block_cache_result *result,
    const db_arm64_translation_reuse_probe_observation *observation,
    uint32_t inserted);

bool db_arm64_translation_reuse_internal_published_block_valid(
    const db_arm64_published_block *published);

void db_arm64_translation_reuse_internal_snapshot_from_session(
    const db_arm64_translation_reuse_session *session,
    db_arm64_translation_reuse_snapshot *snapshot);

db_arm64_translation_reuse_operation db_arm64_translation_reuse_internal_make_operation(
    uint64_t sequence,
    db_arm64_translation_reuse_operation_kind kind,
    db_arm64_block_cache_status cache_status,
    const db_arm64_translation_reuse_probe_observation *observation);

bool db_arm64_translation_reuse_internal_complete_lookup_session(
    const db_arm64_translation_reuse_session *session,
    uint64_t probe_count,
    bool hit,
    db_arm64_translation_reuse_session *completed);

bool db_arm64_translation_reuse_internal_complete_publication_session(
    const db_arm64_translation_reuse_session *session,
    uint64_t probe_count,
    db_arm64_translation_reuse_publication_outcome outcome,
    db_arm64_translation_reuse_session *completed);

#endif
