#include "arm64_translation_reuse_internal.h"

#include <string.h>

void db_arm64_translation_reuse_internal_snapshot_from_session(
    const db_arm64_translation_reuse_session *session,
    db_arm64_translation_reuse_snapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->completed_sequence = session->next_sequence - UINT64_C(1);
    snapshot->lookup_count = session->lookup_count;
    snapshot->hit_count = session->hit_count;
    snapshot->miss_count = session->miss_count;
    snapshot->total_lookup_probes = session->total_lookup_probes;
    snapshot->maximum_lookup_probes = session->maximum_lookup_probes;
    snapshot->publication_count = session->publication_count;
    snapshot->insert_count = session->insert_count;
    snapshot->duplicate_count = session->duplicate_count;
    snapshot->conflict_count = session->conflict_count;
    snapshot->full_count = session->full_count;
    snapshot->total_publication_probes =
        session->total_publication_probes;
    snapshot->maximum_publication_probes =
        session->maximum_publication_probes;
    snapshot->accepted_operation_count =
        session->accepted_operation_count;
    snapshot->rejected_operation_count =
        session->rejected_operation_count;
    snapshot->format_version =
        DB_ARM64_TRANSLATION_REUSE_FORMAT_VERSION;
}
db_arm64_translation_reuse_operation db_arm64_translation_reuse_internal_make_operation(
    uint64_t sequence,
    db_arm64_translation_reuse_operation_kind kind,
    db_arm64_block_cache_status cache_status,
    const db_arm64_translation_reuse_probe_observation *observation)
{
    db_arm64_translation_reuse_operation operation;

    memset(&operation, 0, sizeof(operation));
    operation.sequence_number = sequence;
    operation.slot_index = observation->slot_index;
    operation.probe_count = observation->probe_count;
    operation.operation_kind = (uint32_t)kind;
    operation.cache_status = (uint32_t)cache_status;
    operation.format_version =
        DB_ARM64_TRANSLATION_REUSE_FORMAT_VERSION;
    return operation;
}
bool db_arm64_translation_reuse_internal_complete_lookup_session(
    const db_arm64_translation_reuse_session *session,
    uint64_t probe_count,
    bool hit,
    db_arm64_translation_reuse_session *completed)
{
    *completed = *session;

    if (probe_count == 0u ||
        !db_arm64_translation_reuse_internal_increment_u64(&completed->next_sequence) ||
        !db_arm64_translation_reuse_internal_increment_u64(&completed->lookup_count) ||
        !db_arm64_translation_reuse_internal_add_u64(completed->total_lookup_probes,
                 probe_count,
                 &completed->total_lookup_probes) ||
        !db_arm64_translation_reuse_internal_increment_u64(&completed->accepted_operation_count)) {
        return false;
    }

    if (hit) {
        if (!db_arm64_translation_reuse_internal_increment_u64(&completed->hit_count)) {
            return false;
        }
    } else if (!db_arm64_translation_reuse_internal_increment_u64(&completed->miss_count)) {
        return false;
    }

    if (probe_count > completed->maximum_lookup_probes) {
        completed->maximum_lookup_probes = probe_count;
    }

    return true;
}
bool db_arm64_translation_reuse_internal_complete_publication_session(
    const db_arm64_translation_reuse_session *session,
    uint64_t probe_count,
    db_arm64_translation_reuse_publication_outcome outcome,
    db_arm64_translation_reuse_session *completed)
{
    *completed = *session;

    if (probe_count == 0u ||
        !db_arm64_translation_reuse_internal_increment_u64(&completed->next_sequence) ||
        !db_arm64_translation_reuse_internal_increment_u64(&completed->publication_count) ||
        !db_arm64_translation_reuse_internal_add_u64(completed->total_publication_probes,
                 probe_count,
                 &completed->total_publication_probes)) {
        return false;
    }

    switch (outcome) {
    case DB_ARM64_TRANSLATION_REUSE_PUBLICATION_INSERT:
        if (!db_arm64_translation_reuse_internal_increment_u64(&completed->insert_count) ||
            !db_arm64_translation_reuse_internal_increment_u64(&completed->accepted_operation_count)) {
            return false;
        }
        break;
    case DB_ARM64_TRANSLATION_REUSE_PUBLICATION_DUPLICATE:
        if (!db_arm64_translation_reuse_internal_increment_u64(&completed->duplicate_count) ||
            !db_arm64_translation_reuse_internal_increment_u64(&completed->accepted_operation_count)) {
            return false;
        }
        break;
    case DB_ARM64_TRANSLATION_REUSE_PUBLICATION_CONFLICT:
        if (!db_arm64_translation_reuse_internal_increment_u64(&completed->conflict_count) ||
            !db_arm64_translation_reuse_internal_increment_u64(&completed->rejected_operation_count)) {
            return false;
        }
        break;
    case DB_ARM64_TRANSLATION_REUSE_PUBLICATION_FULL:
        if (!db_arm64_translation_reuse_internal_increment_u64(&completed->full_count) ||
            !db_arm64_translation_reuse_internal_increment_u64(&completed->rejected_operation_count)) {
            return false;
        }
        break;
    }

    if (probe_count > completed->maximum_publication_probes) {
        completed->maximum_publication_probes = probe_count;
    }

    return true;
}
db_arm64_translation_reuse_status db_arm64_translation_reuse_init(
    db_arm64_translation_reuse_session *session)
{
    db_arm64_translation_reuse_session completed;

    if (session == NULL) {
        return DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT;
    }

    memset(&completed, 0, sizeof(completed));
    completed.next_sequence = UINT64_C(1);
    completed.format_version =
        DB_ARM64_TRANSLATION_REUSE_FORMAT_VERSION;
    *session = completed;
    return DB_ARM64_TRANSLATION_REUSE_OK;
}
