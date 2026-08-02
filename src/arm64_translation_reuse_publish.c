#include "arm64_translation_reuse_internal.h"

#include <string.h>

db_arm64_translation_reuse_status db_arm64_translation_reuse_publish(
    db_arm64_block_cache *cache,
    db_arm64_translation_reuse_session *session,
    const db_arm64_published_block *published,
    db_arm64_translation_reuse_operation *operation,
    db_arm64_translation_reuse_snapshot *snapshot)
{
    db_arm64_published_block resident;
    db_arm64_block_cache_result lookup_result;
    db_arm64_block_cache_result insert_result;
    db_arm64_translation_reuse_session completed_session;
    db_arm64_translation_reuse_operation completed_operation;
    db_arm64_translation_reuse_snapshot completed_snapshot;
    db_arm64_block_cache_status lookup_status;
    db_arm64_block_cache_status insert_status;
    db_arm64_block_cache_status expected_status;
    db_arm64_translation_reuse_publication_outcome outcome;
    db_arm64_translation_reuse_probe_observation observation;
    size_t slot_bytes;
    const bool has_storage_bounds =
        cache != NULL && db_arm64_translation_reuse_internal_cache_storage_bounds(cache, &slot_bytes);

    if (cache == NULL || session == NULL || published == NULL ||
        operation == NULL || snapshot == NULL) {
        return DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT;
    }

    if (db_arm64_translation_reuse_internal_publish_fixed_overlap(cache,
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

    if (!db_arm64_translation_reuse_internal_published_block_valid(published)) {
        return DB_ARM64_TRANSLATION_REUSE_CACHE_REJECTED;
    }

    memset(&resident, 0xa5, sizeof(resident));
    memset(&lookup_result, 0x5a, sizeof(lookup_result));
    lookup_status = db_arm64_block_cache_lookup(
        cache,
        published->guest_source_rip,
        published->source_generation,
        published->translation_key,
        &resident,
        &lookup_result);

    if (lookup_status != DB_ARM64_BLOCK_CACHE_OK &&
        lookup_status != DB_ARM64_BLOCK_CACHE_NOT_FOUND) {
        return DB_ARM64_TRANSLATION_REUSE_CACHE_REJECTED;
    }

    observation = db_arm64_translation_reuse_internal_observe_probe(cache,
                                published->guest_source_rip,
                                published->source_generation,
                                published->translation_key);

    if (lookup_status == DB_ARM64_BLOCK_CACHE_OK) {
        if (!observation.found ||
            !db_arm64_translation_reuse_internal_cache_result_matches(&lookup_result,
                                  &observation,
                                  0u)) {
            return DB_ARM64_TRANSLATION_REUSE_CACHE_INCONSISTENT;
        }

        if (memcmp(&resident, published, sizeof(resident)) == 0) {
            outcome = DB_ARM64_TRANSLATION_REUSE_PUBLICATION_DUPLICATE;
            expected_status = DB_ARM64_BLOCK_CACHE_OK;
        } else {
            outcome = DB_ARM64_TRANSLATION_REUSE_PUBLICATION_CONFLICT;
            expected_status = DB_ARM64_BLOCK_CACHE_KEY_CONFLICT;
        }
    } else {
        if (observation.found) {
            return DB_ARM64_TRANSLATION_REUSE_CACHE_INCONSISTENT;
        }

        if (cache->count == cache->capacity) {
            outcome = DB_ARM64_TRANSLATION_REUSE_PUBLICATION_FULL;
            expected_status = DB_ARM64_BLOCK_CACHE_FULL;
        } else {
            outcome = DB_ARM64_TRANSLATION_REUSE_PUBLICATION_INSERT;
            expected_status = DB_ARM64_BLOCK_CACHE_OK;
        }
    }

    if (!db_arm64_translation_reuse_internal_complete_publication_session(session,
                                      observation.probe_count,
                                      outcome,
                                      &completed_session)) {
        return DB_ARM64_TRANSLATION_REUSE_COUNTER_OVERFLOW;
    }

    memset(&insert_result, 0x5a, sizeof(insert_result));
    insert_status = db_arm64_block_cache_insert(cache,
                                                published,
                                                &insert_result);

    if (insert_status != expected_status) {
        return DB_ARM64_TRANSLATION_REUSE_CACHE_INCONSISTENT;
    }

    if ((outcome == DB_ARM64_TRANSLATION_REUSE_PUBLICATION_INSERT &&
         !db_arm64_translation_reuse_internal_cache_result_matches(&insert_result,
                               &observation,
                               1u)) ||
        (outcome == DB_ARM64_TRANSLATION_REUSE_PUBLICATION_DUPLICATE &&
         !db_arm64_translation_reuse_internal_cache_result_matches(&insert_result,
                               &observation,
                               0u))) {
        return DB_ARM64_TRANSLATION_REUSE_CACHE_INCONSISTENT;
    }

    completed_operation = db_arm64_translation_reuse_internal_make_operation(
        session->next_sequence,
        DB_ARM64_TRANSLATION_REUSE_OPERATION_PUBLICATION,
        insert_status,
        &observation);

    switch (outcome) {
    case DB_ARM64_TRANSLATION_REUSE_PUBLICATION_INSERT:
        completed_operation.inserted = 1u;
        break;
    case DB_ARM64_TRANSLATION_REUSE_PUBLICATION_DUPLICATE:
        completed_operation.duplicate = 1u;
        break;
    case DB_ARM64_TRANSLATION_REUSE_PUBLICATION_CONFLICT:
        completed_operation.conflict = 1u;
        break;
    case DB_ARM64_TRANSLATION_REUSE_PUBLICATION_FULL:
        completed_operation.full = 1u;
        break;
    }

    db_arm64_translation_reuse_internal_snapshot_from_session(&completed_session, &completed_snapshot);

    *operation = completed_operation;
    *snapshot = completed_snapshot;
    *session = completed_session;
    return DB_ARM64_TRANSLATION_REUSE_OK;
}
