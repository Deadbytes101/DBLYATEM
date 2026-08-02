#include "arm64_translation_reuse_internal.h"

#include <string.h>

bool db_arm64_translation_reuse_internal_zero_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (bytes[index] != 0u) {
            return false;
        }
    }

    return true;
}
bool db_arm64_translation_reuse_internal_regions_overlap(const void *left,
                            size_t left_size,
                            const void *right,
                            size_t right_size)
{
    const uintptr_t left_address = (uintptr_t)left;
    const uintptr_t right_address = (uintptr_t)right;

    if (left_size == 0u || right_size == 0u) {
        return false;
    }

    if (left_address <= right_address) {
        return right_address - left_address < left_size;
    }

    return left_address - right_address < right_size;
}
bool db_arm64_translation_reuse_internal_multiply_size(size_t left, size_t right, size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}
bool db_arm64_translation_reuse_internal_add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (UINT64_MAX - left < right) {
        return false;
    }

    *result = left + right;
    return true;
}
bool db_arm64_translation_reuse_internal_increment_u64(uint64_t *value)
{
    if (*value == UINT64_MAX) {
        return false;
    }

    *value += UINT64_C(1);
    return true;
}
bool db_arm64_translation_reuse_internal_session_valid(
    const db_arm64_translation_reuse_session *session)
{
    uint64_t lookup_total;
    uint64_t publication_accepted;
    uint64_t publication_rejected;
    uint64_t publication_total;
    uint64_t accepted_total;
    uint64_t completed_total;
    uint64_t expected_next;

    if (session->format_version !=
            DB_ARM64_TRANSLATION_REUSE_FORMAT_VERSION ||
        !db_arm64_translation_reuse_internal_zero_bytes(session->reserved, sizeof(session->reserved)) ||
        session->next_sequence == 0u ||
        !db_arm64_translation_reuse_internal_add_u64(session->hit_count,
                 session->miss_count,
                 &lookup_total) ||
        lookup_total != session->lookup_count ||
        !db_arm64_translation_reuse_internal_add_u64(session->insert_count,
                 session->duplicate_count,
                 &publication_accepted) ||
        !db_arm64_translation_reuse_internal_add_u64(session->conflict_count,
                 session->full_count,
                 &publication_rejected) ||
        !db_arm64_translation_reuse_internal_add_u64(publication_accepted,
                 publication_rejected,
                 &publication_total) ||
        publication_total != session->publication_count ||
        !db_arm64_translation_reuse_internal_add_u64(session->lookup_count,
                 publication_accepted,
                 &accepted_total) ||
        accepted_total != session->accepted_operation_count ||
        publication_rejected != session->rejected_operation_count ||
        !db_arm64_translation_reuse_internal_add_u64(session->accepted_operation_count,
                 session->rejected_operation_count,
                 &completed_total) ||
        !db_arm64_translation_reuse_internal_add_u64(completed_total, UINT64_C(1), &expected_next) ||
        expected_next != session->next_sequence) {
        return false;
    }

    if (session->lookup_count == 0u) {
        if (session->total_lookup_probes != 0u ||
            session->maximum_lookup_probes != 0u) {
            return false;
        }
    } else if (session->total_lookup_probes < session->lookup_count ||
               session->maximum_lookup_probes == 0u ||
               session->maximum_lookup_probes >
                   session->total_lookup_probes) {
        return false;
    }

    if (session->publication_count == 0u) {
        if (session->total_publication_probes != 0u ||
            session->maximum_publication_probes != 0u) {
            return false;
        }
    } else if (session->total_publication_probes <
                   session->publication_count ||
               session->maximum_publication_probes == 0u ||
               session->maximum_publication_probes >
                   session->total_publication_probes) {
        return false;
    }

    return true;
}
bool db_arm64_translation_reuse_internal_cache_storage_bounds(const db_arm64_block_cache *cache,
                                 size_t *slot_bytes)
{
    return cache->slots != NULL &&
           cache->capacity != 0u &&
           (cache->capacity & (cache->capacity - 1u)) == 0u &&
           db_arm64_translation_reuse_internal_multiply_size(cache->capacity,
                         sizeof(*cache->slots),
                         slot_bytes);
}
bool db_arm64_translation_reuse_internal_lookup_fixed_overlap(
    const db_arm64_block_cache *cache,
    const db_arm64_translation_reuse_session *session,
    const db_arm64_published_block *published,
    const db_arm64_translation_reuse_operation *operation,
    const db_arm64_translation_reuse_snapshot *snapshot)
{
    return db_arm64_translation_reuse_internal_regions_overlap(cache, sizeof(*cache),
                           session, sizeof(*session)) ||
           db_arm64_translation_reuse_internal_regions_overlap(cache, sizeof(*cache),
                           published, sizeof(*published)) ||
           db_arm64_translation_reuse_internal_regions_overlap(cache, sizeof(*cache),
                           operation, sizeof(*operation)) ||
           db_arm64_translation_reuse_internal_regions_overlap(cache, sizeof(*cache),
                           snapshot, sizeof(*snapshot)) ||
           db_arm64_translation_reuse_internal_regions_overlap(session, sizeof(*session),
                           published, sizeof(*published)) ||
           db_arm64_translation_reuse_internal_regions_overlap(session, sizeof(*session),
                           operation, sizeof(*operation)) ||
           db_arm64_translation_reuse_internal_regions_overlap(session, sizeof(*session),
                           snapshot, sizeof(*snapshot)) ||
           db_arm64_translation_reuse_internal_regions_overlap(published, sizeof(*published),
                           operation, sizeof(*operation)) ||
           db_arm64_translation_reuse_internal_regions_overlap(published, sizeof(*published),
                           snapshot, sizeof(*snapshot)) ||
           db_arm64_translation_reuse_internal_regions_overlap(operation, sizeof(*operation),
                           snapshot, sizeof(*snapshot));
}
bool db_arm64_translation_reuse_internal_publish_fixed_overlap(
    const db_arm64_block_cache *cache,
    const db_arm64_translation_reuse_session *session,
    const db_arm64_published_block *published,
    const db_arm64_translation_reuse_operation *operation,
    const db_arm64_translation_reuse_snapshot *snapshot)
{
    return db_arm64_translation_reuse_internal_lookup_fixed_overlap(cache,
                                session,
                                published,
                                operation,
                                snapshot);
}
bool db_arm64_translation_reuse_internal_lookup_slot_overlap(
    const db_arm64_block_cache *cache,
    size_t slot_bytes,
    const db_arm64_translation_reuse_session *session,
    const db_arm64_published_block *published,
    const db_arm64_translation_reuse_operation *operation,
    const db_arm64_translation_reuse_snapshot *snapshot)
{
    return db_arm64_translation_reuse_internal_regions_overlap(cache->slots, slot_bytes,
                           session, sizeof(*session)) ||
           db_arm64_translation_reuse_internal_regions_overlap(cache->slots, slot_bytes,
                           published, sizeof(*published)) ||
           db_arm64_translation_reuse_internal_regions_overlap(cache->slots, slot_bytes,
                           operation, sizeof(*operation)) ||
           db_arm64_translation_reuse_internal_regions_overlap(cache->slots, slot_bytes,
                           snapshot, sizeof(*snapshot));
}
