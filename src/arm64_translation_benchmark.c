#include "dblyat/arm64_translation_benchmark.h"

#include <stdbool.h>
#include <string.h>

static bool zero_bytes(const void *data, size_t size)
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

static bool add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (UINT64_MAX - left < right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool multiply_size(size_t left, size_t right, size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool regions_overlap(const void *left,
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

static bool phase_index(db_arm64_translation_benchmark_phase phase,
                        size_t *index)
{
    if ((uint32_t)phase <
            (uint32_t)DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_INSERT ||
        (uint32_t)phase >
            (uint32_t)DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_FULL) {
        return false;
    }

    *index = (size_t)((uint32_t)phase - 1u);
    return true;
}

static bool probe_metrics_valid(uint64_t operation_count,
                                uint64_t total_probes,
                                uint64_t maximum_probes)
{
    uint64_t minimum_total;
    uint64_t maximum_total;

    if (operation_count == 0u) {
        return total_probes == 0u && maximum_probes == 0u;
    }

    if (maximum_probes == 0u ||
        !add_u64(maximum_probes,
                 operation_count - UINT64_C(1),
                 &minimum_total) ||
        total_probes < minimum_total) {
        return false;
    }

    if (operation_count > UINT64_MAX / maximum_probes) {
        return true;
    }

    maximum_total = operation_count * maximum_probes;
    return total_probes <= maximum_total;
}

static bool snapshot_valid(
    const db_arm64_translation_reuse_snapshot *snapshot)
{
    uint64_t lookup_total;
    uint64_t publication_accepted;
    uint64_t publication_rejected;
    uint64_t publication_total;
    uint64_t accepted_total;
    uint64_t completed_total;

    return snapshot->format_version ==
               DB_ARM64_TRANSLATION_REUSE_FORMAT_VERSION &&
           zero_bytes(snapshot->reserved, sizeof(snapshot->reserved)) &&
           add_u64(snapshot->hit_count,
                   snapshot->miss_count,
                   &lookup_total) &&
           lookup_total == snapshot->lookup_count &&
           add_u64(snapshot->insert_count,
                   snapshot->duplicate_count,
                   &publication_accepted) &&
           add_u64(snapshot->conflict_count,
                   snapshot->full_count,
                   &publication_rejected) &&
           add_u64(publication_accepted,
                   publication_rejected,
                   &publication_total) &&
           publication_total == snapshot->publication_count &&
           add_u64(snapshot->lookup_count,
                   publication_accepted,
                   &accepted_total) &&
           accepted_total == snapshot->accepted_operation_count &&
           publication_rejected == snapshot->rejected_operation_count &&
           add_u64(snapshot->accepted_operation_count,
                   snapshot->rejected_operation_count,
                   &completed_total) &&
           completed_total == snapshot->completed_sequence &&
           probe_metrics_valid(snapshot->lookup_count,
                               snapshot->total_lookup_probes,
                               snapshot->maximum_lookup_probes) &&
           probe_metrics_valid(snapshot->publication_count,
                               snapshot->total_publication_probes,
                               snapshot->maximum_publication_probes);
}

static unsigned int outcome_flag_count(
    const db_arm64_translation_reuse_operation *operation)
{
    return operation->hit + operation->miss +
           operation->inserted + operation->duplicate +
           operation->conflict + operation->full;
}

static bool operation_matches_phase(
    db_arm64_translation_benchmark_phase phase,
    const db_arm64_translation_reuse_operation *operation)
{
    if (operation->format_version !=
            DB_ARM64_TRANSLATION_REUSE_FORMAT_VERSION ||
        !zero_bytes(operation->reserved, sizeof(operation->reserved)) ||
        operation->sequence_number == 0u ||
        operation->probe_count == 0u ||
        outcome_flag_count(operation) != 1u) {
        return false;
    }

    switch (phase) {
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_INSERT:
        return operation->operation_kind ==
                   (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_PUBLICATION &&
               operation->cache_status ==
                   (uint32_t)DB_ARM64_BLOCK_CACHE_OK &&
               operation->inserted == 1u &&
               operation->slot_index !=
                   DB_ARM64_TRANSLATION_REUSE_NO_SLOT;
    case DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT:
        return operation->operation_kind ==
                   (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_LOOKUP &&
               operation->cache_status ==
                   (uint32_t)DB_ARM64_BLOCK_CACHE_OK &&
               operation->hit == 1u &&
               operation->slot_index !=
                   DB_ARM64_TRANSLATION_REUSE_NO_SLOT;
    case DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS:
        return operation->operation_kind ==
                   (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_LOOKUP &&
               operation->cache_status ==
                   (uint32_t)DB_ARM64_BLOCK_CACHE_NOT_FOUND &&
               operation->miss == 1u;
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_DUPLICATE:
        return operation->operation_kind ==
                   (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_PUBLICATION &&
               operation->cache_status ==
                   (uint32_t)DB_ARM64_BLOCK_CACHE_OK &&
               operation->duplicate == 1u &&
               operation->slot_index !=
                   DB_ARM64_TRANSLATION_REUSE_NO_SLOT;
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_CONFLICT:
        return operation->operation_kind ==
                   (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_PUBLICATION &&
               operation->cache_status ==
                   (uint32_t)DB_ARM64_BLOCK_CACHE_KEY_CONFLICT &&
               operation->conflict == 1u &&
               operation->slot_index !=
                   DB_ARM64_TRANSLATION_REUSE_NO_SLOT;
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_FULL:
        return operation->operation_kind ==
                   (uint32_t)DB_ARM64_TRANSLATION_REUSE_OPERATION_PUBLICATION &&
               operation->cache_status ==
                   (uint32_t)DB_ARM64_BLOCK_CACHE_FULL &&
               operation->full == 1u &&
               operation->slot_index ==
                   DB_ARM64_TRANSLATION_REUSE_NO_SLOT;
    }

    return false;
}

static bool increment(uint64_t *value)
{
    if (*value == UINT64_MAX) {
        return false;
    }

    *value += UINT64_C(1);
    return true;
}

static bool add_probe(uint64_t *total,
                      uint64_t *maximum,
                      uint64_t probe_count)
{
    uint64_t next_total;

    if (!add_u64(*total, probe_count, &next_total)) {
        return false;
    }

    *total = next_total;
    if (probe_count > *maximum) {
        *maximum = probe_count;
    }

    return true;
}

static void initial_snapshot(
    db_arm64_translation_reuse_snapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->format_version =
        DB_ARM64_TRANSLATION_REUSE_FORMAT_VERSION;
}

static bool transition_snapshot(
    const db_arm64_translation_reuse_snapshot *previous,
    db_arm64_translation_benchmark_phase phase,
    const db_arm64_translation_reuse_operation *operation,
    db_arm64_translation_reuse_snapshot *expected)
{
    *expected = *previous;

    if (!increment(&expected->completed_sequence)) {
        return false;
    }

    switch (phase) {
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_INSERT:
        if (!increment(&expected->publication_count) ||
            !increment(&expected->insert_count) ||
            !increment(&expected->accepted_operation_count) ||
            !add_probe(&expected->total_publication_probes,
                       &expected->maximum_publication_probes,
                       operation->probe_count)) {
            return false;
        }
        break;
    case DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT:
        if (!increment(&expected->lookup_count) ||
            !increment(&expected->hit_count) ||
            !increment(&expected->accepted_operation_count) ||
            !add_probe(&expected->total_lookup_probes,
                       &expected->maximum_lookup_probes,
                       operation->probe_count)) {
            return false;
        }
        break;
    case DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS:
        if (!increment(&expected->lookup_count) ||
            !increment(&expected->miss_count) ||
            !increment(&expected->accepted_operation_count) ||
            !add_probe(&expected->total_lookup_probes,
                       &expected->maximum_lookup_probes,
                       operation->probe_count)) {
            return false;
        }
        break;
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_DUPLICATE:
        if (!increment(&expected->publication_count) ||
            !increment(&expected->duplicate_count) ||
            !increment(&expected->accepted_operation_count) ||
            !add_probe(&expected->total_publication_probes,
                       &expected->maximum_publication_probes,
                       operation->probe_count)) {
            return false;
        }
        break;
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_CONFLICT:
        if (!increment(&expected->publication_count) ||
            !increment(&expected->conflict_count) ||
            !increment(&expected->rejected_operation_count) ||
            !add_probe(&expected->total_publication_probes,
                       &expected->maximum_publication_probes,
                       operation->probe_count)) {
            return false;
        }
        break;
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_FULL:
        if (!increment(&expected->publication_count) ||
            !increment(&expected->full_count) ||
            !increment(&expected->rejected_operation_count) ||
            !add_probe(&expected->total_publication_probes,
                       &expected->maximum_publication_probes,
                       operation->probe_count)) {
            return false;
        }
        break;
    }

    return expected->completed_sequence == operation->sequence_number;
}

static bool sample_valid(
    const db_arm64_translation_benchmark_sample *sample,
    uint64_t expected_benchmark_sequence,
    const db_arm64_translation_reuse_snapshot *previous,
    db_arm64_translation_reuse_snapshot *next)
{
    size_t ignored_index;

    if (sample->format_version !=
            DB_ARM64_TRANSLATION_BENCHMARK_FORMAT_VERSION ||
        !zero_bytes(sample->reserved, sizeof(sample->reserved)) ||
        sample->benchmark_sequence != expected_benchmark_sequence ||
        sample->start_ns > sample->end_ns ||
        sample->duration_ns != sample->end_ns - sample->start_ns ||
        !phase_index(
            (db_arm64_translation_benchmark_phase)sample->phase,
            &ignored_index) ||
        !operation_matches_phase(
            (db_arm64_translation_benchmark_phase)sample->phase,
            &sample->operation) ||
        !snapshot_valid(&sample->snapshot) ||
        sample->operation.sequence_number !=
            sample->snapshot.completed_sequence ||
        !transition_snapshot(
            previous,
            (db_arm64_translation_benchmark_phase)sample->phase,
            &sample->operation,
            next) ||
        memcmp(next, &sample->snapshot, sizeof(*next)) != 0) {
        return false;
    }

    return true;
}

static bool session_storage_bounds(
    const db_arm64_translation_benchmark_session *session,
    size_t *sample_bytes)
{
    return session->samples != NULL &&
           session->capacity != 0u &&
           multiply_size(session->capacity,
                         sizeof(*session->samples),
                         sample_bytes);
}

static bool session_valid(
    const db_arm64_translation_benchmark_session *session)
{
    db_arm64_translation_reuse_snapshot previous;
    db_arm64_translation_reuse_snapshot next;
    uint64_t phase_counts[DB_ARM64_TRANSLATION_BENCHMARK_PHASE_COUNT];
    uint64_t phase_durations[DB_ARM64_TRANSLATION_BENCHMARK_PHASE_COUNT];
    uint64_t total_duration = 0u;
    uint64_t minimum_duration = 0u;
    uint64_t maximum_duration = 0u;
    uint64_t expected_next;
    size_t sample_bytes;
    size_t index;

    if (session->format_version !=
            DB_ARM64_TRANSLATION_BENCHMARK_FORMAT_VERSION ||
        !zero_bytes(session->reserved, sizeof(session->reserved)) ||
        !session_storage_bounds(session, &sample_bytes) ||
        regions_overlap(session, sizeof(*session),
                        session->samples, sample_bytes) ||
        session->count > session->capacity ||
        session->has_snapshot > 1u) {
        return false;
    }

    memset(phase_counts, 0, sizeof(phase_counts));
    memset(phase_durations, 0, sizeof(phase_durations));
    initial_snapshot(&previous);

    for (index = 0u; index < session->count; ++index) {
        const db_arm64_translation_benchmark_sample *sample =
            &session->samples[index];
        size_t current_phase;
        uint64_t next_total;

        if (!sample_valid(sample,
                          (uint64_t)index + UINT64_C(1),
                          &previous,
                          &next) ||
            !phase_index(
                (db_arm64_translation_benchmark_phase)sample->phase,
                &current_phase) ||
            !add_u64(total_duration,
                     sample->duration_ns,
                     &next_total) ||
            !increment(&phase_counts[current_phase]) ||
            !add_u64(phase_durations[current_phase],
                     sample->duration_ns,
                     &phase_durations[current_phase])) {
            return false;
        }

        total_duration = next_total;
        if (index == 0u || sample->duration_ns < minimum_duration) {
            minimum_duration = sample->duration_ns;
        }
        if (sample->duration_ns > maximum_duration) {
            maximum_duration = sample->duration_ns;
        }
        previous = next;
    }

    for (index = session->count; index < session->capacity; ++index) {
        if (!zero_bytes(&session->samples[index],
                        sizeof(session->samples[index]))) {
            return false;
        }
    }

    if ((uint64_t)session->count == UINT64_MAX ||
        !add_u64((uint64_t)session->count,
                 UINT64_C(1),
                 &expected_next) ||
        session->next_sequence != expected_next ||
        session->total_duration_ns != total_duration ||
        session->minimum_duration_ns != minimum_duration ||
        session->maximum_duration_ns != maximum_duration ||
        memcmp(session->phase_sample_count,
               phase_counts,
               sizeof(phase_counts)) != 0 ||
        memcmp(session->phase_total_duration_ns,
               phase_durations,
               sizeof(phase_durations)) != 0) {
        return false;
    }

    if (session->count == 0u) {
        return session->has_snapshot == 0u &&
               zero_bytes(&session->last_snapshot,
                          sizeof(session->last_snapshot));
    }

    return session->has_snapshot == 1u &&
           memcmp(&session->last_snapshot,
                  &previous,
                  sizeof(previous)) == 0;
}

db_arm64_translation_benchmark_status
db_arm64_translation_benchmark_init(
    db_arm64_translation_benchmark_session *session,
    db_arm64_translation_benchmark_sample *samples,
    size_t capacity)
{
    db_arm64_translation_benchmark_session initialized;
    size_t sample_bytes;

    if (session == NULL || samples == NULL) {
        return DB_ARM64_TRANSLATION_BENCHMARK_NULL_ARGUMENT;
    }

    if (capacity == 0u ||
        !multiply_size(capacity, sizeof(*samples), &sample_bytes)) {
        return DB_ARM64_TRANSLATION_BENCHMARK_BAD_CAPACITY;
    }

    if (regions_overlap(session, sizeof(*session),
                        samples, sample_bytes)) {
        return DB_ARM64_TRANSLATION_BENCHMARK_BUFFER_OVERLAP;
    }

    memset(&initialized, 0, sizeof(initialized));
    memset(samples, 0, sample_bytes);
    initialized.samples = samples;
    initialized.capacity = capacity;
    initialized.next_sequence = UINT64_C(1);
    initialized.format_version =
        DB_ARM64_TRANSLATION_BENCHMARK_FORMAT_VERSION;
    *session = initialized;
    return DB_ARM64_TRANSLATION_BENCHMARK_OK;
}

db_arm64_translation_benchmark_status
db_arm64_translation_benchmark_record(
    db_arm64_translation_benchmark_session *session,
    db_arm64_translation_benchmark_phase phase,
    uint64_t iteration,
    uint64_t start_ns,
    uint64_t end_ns,
    const db_arm64_translation_reuse_operation *operation,
    const db_arm64_translation_reuse_snapshot *snapshot)
{
    db_arm64_translation_benchmark_sample completed_sample;
    db_arm64_translation_benchmark_session completed_session;
    db_arm64_translation_reuse_snapshot previous;
    db_arm64_translation_reuse_snapshot expected_snapshot;
    uint64_t duration;
    uint64_t next_total_duration;
    uint64_t next_phase_duration;
    uint64_t next_sequence;
    size_t sample_bytes;
    size_t current_phase;

    if (session == NULL || operation == NULL || snapshot == NULL) {
        return DB_ARM64_TRANSLATION_BENCHMARK_NULL_ARGUMENT;
    }

    if (!session_storage_bounds(session, &sample_bytes)) {
        return DB_ARM64_TRANSLATION_BENCHMARK_BAD_SESSION;
    }

    if (regions_overlap(session, sizeof(*session),
                        operation, sizeof(*operation)) ||
        regions_overlap(session, sizeof(*session),
                        snapshot, sizeof(*snapshot)) ||
        regions_overlap(session->samples, sample_bytes,
                        operation, sizeof(*operation)) ||
        regions_overlap(session->samples, sample_bytes,
                        snapshot, sizeof(*snapshot)) ||
        regions_overlap(operation, sizeof(*operation),
                        snapshot, sizeof(*snapshot))) {
        return DB_ARM64_TRANSLATION_BENCHMARK_BUFFER_OVERLAP;
    }

    if (!session_valid(session)) {
        return DB_ARM64_TRANSLATION_BENCHMARK_BAD_SESSION;
    }

    if (!phase_index(phase, &current_phase)) {
        return DB_ARM64_TRANSLATION_BENCHMARK_BAD_PHASE;
    }

    if (start_ns > end_ns) {
        return DB_ARM64_TRANSLATION_BENCHMARK_BAD_TIMESTAMP;
    }

    if (!operation_matches_phase(phase, operation) ||
        operation->sequence_number != session->next_sequence) {
        return DB_ARM64_TRANSLATION_BENCHMARK_BAD_OPERATION;
    }

    if (!snapshot_valid(snapshot) ||
        operation->sequence_number != snapshot->completed_sequence) {
        return DB_ARM64_TRANSLATION_BENCHMARK_BAD_SNAPSHOT;
    }

    if (session->count == 0u) {
        initial_snapshot(&previous);
    } else {
        previous = session->last_snapshot;
    }

    if (!transition_snapshot(&previous,
                             phase,
                             operation,
                             &expected_snapshot) ||
        memcmp(&expected_snapshot,
               snapshot,
               sizeof(expected_snapshot)) != 0) {
        return DB_ARM64_TRANSLATION_BENCHMARK_BAD_SNAPSHOT;
    }

    if (session->count == session->capacity) {
        return DB_ARM64_TRANSLATION_BENCHMARK_FULL;
    }

    duration = end_ns - start_ns;
    next_phase_duration =
        session->phase_total_duration_ns[current_phase];

    if (!add_u64(session->total_duration_ns,
                 duration,
                 &next_total_duration) ||
        !add_u64(next_phase_duration,
                 duration,
                 &next_phase_duration) ||
        session->phase_sample_count[current_phase] == UINT64_MAX ||
        !add_u64(session->next_sequence,
                 UINT64_C(1),
                 &next_sequence)) {
        return DB_ARM64_TRANSLATION_BENCHMARK_COUNTER_OVERFLOW;
    }

    memset(&completed_sample, 0, sizeof(completed_sample));
    completed_sample.benchmark_sequence = session->next_sequence;
    completed_sample.iteration = iteration;
    completed_sample.start_ns = start_ns;
    completed_sample.end_ns = end_ns;
    completed_sample.duration_ns = duration;
    completed_sample.operation = *operation;
    completed_sample.snapshot = *snapshot;
    completed_sample.phase = (uint32_t)phase;
    completed_sample.format_version =
        DB_ARM64_TRANSLATION_BENCHMARK_FORMAT_VERSION;

    completed_session = *session;
    completed_session.count += 1u;
    completed_session.next_sequence = next_sequence;
    completed_session.total_duration_ns = next_total_duration;
    if (session->count == 0u ||
        duration < completed_session.minimum_duration_ns) {
        completed_session.minimum_duration_ns = duration;
    }
    if (duration > completed_session.maximum_duration_ns) {
        completed_session.maximum_duration_ns = duration;
    }
    completed_session.phase_sample_count[current_phase] += UINT64_C(1);
    completed_session.phase_total_duration_ns[current_phase] =
        next_phase_duration;
    completed_session.last_snapshot = *snapshot;
    completed_session.has_snapshot = 1u;

    session->samples[session->count] = completed_sample;
    *session = completed_session;
    return DB_ARM64_TRANSLATION_BENCHMARK_OK;
}

const char *db_arm64_translation_benchmark_status_name(
    db_arm64_translation_benchmark_status status)
{
    switch (status) {
    case DB_ARM64_TRANSLATION_BENCHMARK_OK:
        return "OK";
    case DB_ARM64_TRANSLATION_BENCHMARK_NULL_ARGUMENT:
        return "NULL_ARGUMENT";
    case DB_ARM64_TRANSLATION_BENCHMARK_BUFFER_OVERLAP:
        return "BUFFER_OVERLAP";
    case DB_ARM64_TRANSLATION_BENCHMARK_BAD_CAPACITY:
        return "BAD_CAPACITY";
    case DB_ARM64_TRANSLATION_BENCHMARK_BAD_SESSION:
        return "BAD_SESSION";
    case DB_ARM64_TRANSLATION_BENCHMARK_BAD_PHASE:
        return "BAD_PHASE";
    case DB_ARM64_TRANSLATION_BENCHMARK_BAD_TIMESTAMP:
        return "BAD_TIMESTAMP";
    case DB_ARM64_TRANSLATION_BENCHMARK_BAD_OPERATION:
        return "BAD_OPERATION";
    case DB_ARM64_TRANSLATION_BENCHMARK_BAD_SNAPSHOT:
        return "BAD_SNAPSHOT";
    case DB_ARM64_TRANSLATION_BENCHMARK_FULL:
        return "FULL";
    case DB_ARM64_TRANSLATION_BENCHMARK_COUNTER_OVERFLOW:
        return "COUNTER_OVERFLOW";
    }

    return "UNKNOWN";
}

const char *db_arm64_translation_benchmark_phase_name(
    db_arm64_translation_benchmark_phase phase)
{
    switch (phase) {
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_INSERT:
        return "PUBLICATION_INSERT";
    case DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT:
        return "LOOKUP_HIT";
    case DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS:
        return "LOOKUP_MISS";
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_DUPLICATE:
        return "PUBLICATION_DUPLICATE";
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_CONFLICT:
        return "PUBLICATION_CONFLICT";
    case DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_FULL:
        return "PUBLICATION_FULL";
    }

    return "UNKNOWN";
}
