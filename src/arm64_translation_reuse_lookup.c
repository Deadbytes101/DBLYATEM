#include "arm64_translation_reuse_internal.h"

#include <string.h>

db_arm64_translation_reuse_status db_arm64_translation_reuse_lookup(
    const db_arm64_block_cache *cache,
    db_arm64_translation_reuse_session *session,
    uint64_t guest_source_rip,
    uint64_t source_generation,
    uint64_t translation_key,
    db_arm64_published_block *published,
    db_arm64_translation_reuse_operation *operation,
    db_arm64_translation_reuse_snapshot *snapshot)
{
    db_arm64_published_block cache_published;
    db_arm64_block_cache_result cache_result;
    db_arm64_translation_reuse_session completed_session;
    db_arm64_translation_reuse_operation completed_operation;
    db_arm64_translation_reuse_snapshot completed_snapshot;
    db_arm64_block_cache_status cache_status;
    db_arm64_translation_reuse_probe_observation observation;
    size_t slot_bytes;
    const bool has_storage_bounds =
        cache != NULL && db_arm64_translation_reuse_internal_cache_storage_bounds(cache, &slot_bytes);

    if (cache == NULL || session == NULL || published == NULL ||
        operation == NULL || snapshot == NULL) {
        return DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT;
    }

    if (db_arm64_translation_reuse_internal_lookup_fixed_overlap(cache,
                             session,
                             published,
                             operation,
                             snapshot) ||
        (has_storage_bounds &&
         db_arm64_translation_reuse_internal_lookup_slot_overlap(cache,
                             slot_bytes,
                             session,
                             published,
                             operation,
                             snapshot))) {
        return DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP;
    }

    if (!db_arm64_translation_reuse_internal_session_valid(session)) {
        return DB_ARM64_TRANSLATION_REUSE_BAD_SESSION;
    }

    memset(&cache_published, 0xa5, sizeof(cache_published));
    memset(&cache_result, 0x5a, sizeof(cache_result));
    cache_status = db_arm64_block_cache_lookup(cache,
                                               guest_source_rip,
                                               source_generation,
                                               translation_key,
                                               &cache_published,
                                               &cache_result);

    if (cache_status != DB_ARM64_BLOCK_CACHE_OK &&
        cache_status != DB_ARM64_BLOCK_CACHE_NOT_FOUND) {
        return DB_ARM64_TRANSLATION_REUSE_CACHE_REJECTED;
    }

    observation = db_arm64_translation_reuse_internal_observe_probe(cache,
                                guest_source_rip,
                                source_generation,
                                translation_key);

    if (cache_status == DB_ARM64_BLOCK_CACHE_OK) {
        if (!observation.found ||
            !db_arm64_translation_reuse_internal_cache_result_matches(&cache_result,
                                  &observation,
                                  0u)) {
            return DB_ARM64_TRANSLATION_REUSE_CACHE_INCONSISTENT;
        }
    } else if (observation.found) {
        return DB_ARM64_TRANSLATION_REUSE_CACHE_INCONSISTENT;
    }

    if (!db_arm64_translation_reuse_internal_complete_lookup_session(session,
                                 observation.probe_count,
                                 cache_status == DB_ARM64_BLOCK_CACHE_OK,
                                 &completed_session)) {
        return DB_ARM64_TRANSLATION_REUSE_COUNTER_OVERFLOW;
    }

    completed_operation = db_arm64_translation_reuse_internal_make_operation(
        session->next_sequence,
        DB_ARM64_TRANSLATION_REUSE_OPERATION_LOOKUP,
        cache_status,
        &observation);

    if (cache_status == DB_ARM64_BLOCK_CACHE_OK) {
        completed_operation.hit = 1u;
    } else {
        completed_operation.miss = 1u;
    }

    db_arm64_translation_reuse_internal_snapshot_from_session(&completed_session, &completed_snapshot);

    if (cache_status == DB_ARM64_BLOCK_CACHE_OK) {
        *published = cache_published;
    }
    *operation = completed_operation;
    *snapshot = completed_snapshot;
    *session = completed_session;
    return DB_ARM64_TRANSLATION_REUSE_OK;
}
