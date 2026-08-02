#define _GNU_SOURCE
#include "dblyat/arm64_translation_benchmark.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef CLOCK_MONOTONIC_RAW
#error CLOCK_MONOTONIC_RAW is required
#endif

#define BENCHMARK_MAX_ITERATIONS 32u
#define BENCHMARK_CACHE_CAPACITY 64u
#define BENCHMARK_SAMPLE_CAPACITY (BENCHMARK_MAX_ITERATIONS * 4u)

static int token_valid(const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;

    if (*cursor == '\0') {
        return 0;
    }

    while (*cursor != '\0') {
        const unsigned char current = *cursor;

        if (!((current >= (unsigned char)'a' &&
               current <= (unsigned char)'z') ||
              (current >= (unsigned char)'A' &&
               current <= (unsigned char)'Z') ||
              (current >= (unsigned char)'0' &&
               current <= (unsigned char)'9') ||
              current == (unsigned char)'.' ||
              current == (unsigned char)'_' ||
              current == (unsigned char)'-')) {
            return 0;
        }
        ++cursor;
    }

    return 1;
}

static int parse_iterations(const char *text, uint64_t *iterations)
{
    char *end = NULL;
    unsigned long long parsed;

    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed == 0u ||
        parsed > (unsigned long long)BENCHMARK_MAX_ITERATIONS) {
        return 0;
    }

    *iterations = (uint64_t)parsed;
    return 1;
}

static const char *host_architecture(void)
{
#if defined(__aarch64__)
    return "aarch64";
#elif defined(__x86_64__)
    return "x86_64";
#else
    return "other";
#endif
}

static int monotonic_raw_ns(uint64_t *value)
{
    struct timespec now;
    uint64_t seconds;

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &now) != 0 ||
        now.tv_sec < 0 ||
        now.tv_nsec < 0 ||
        now.tv_nsec >= 1000000000L) {
        return 0;
    }

    seconds = (uint64_t)now.tv_sec;
    if (seconds > (UINT64_MAX - (uint64_t)now.tv_nsec) /
                      UINT64_C(1000000000)) {
        return 0;
    }

    *value = seconds * UINT64_C(1000000000) +
             (uint64_t)now.tv_nsec;
    return 1;
}

static db_arm64_published_block make_block(uint64_t iteration)
{
    db_arm64_published_block block;
    const uint64_t rip =
        UINT64_C(0x100000) + iteration * UINT64_C(0x100);
    const uint64_t mapping =
        UINT64_C(0x10000000) + iteration * UINT64_C(0x10000);

    memset(&block, 0, sizeof(block));
    block.guest_source_rip = rip;
    block.guest_end_rip = rip + UINT64_C(16);
    block.guest_fallthrough_rip = rip + UINT64_C(16);
    block.source_generation = UINT64_C(1);
    block.translation_key = iteration + UINT64_C(1);
    block.host_mapping_address = mapping;
    block.host_entry_address = mapping + UINT64_C(4);
    block.host_allocation_end = mapping + UINT64_C(4095);
    block.host_allocation_size = UINT64_C(4096);
    block.arm64_image_byte_count = UINT64_C(16);
    block.guest_byte_count = 16u;
    block.guest_instruction_count = 4u;
    block.arm64_word_count = 4u;
    block.block_stop_reason =
        (uint32_t)DB_X64_BLOCK_STOP_TERMINATOR;
    block.format_version =
        DB_ARM64_PUBLISHED_BLOCK_FORMAT_VERSION;
    return block;
}

static int record_publish(
    db_arm64_block_cache *cache,
    db_arm64_translation_reuse_session *reuse,
    db_arm64_translation_benchmark_session *benchmark,
    db_arm64_translation_benchmark_phase phase,
    uint64_t iteration,
    const db_arm64_published_block *block)
{
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;
    db_arm64_translation_reuse_status reuse_status;
    db_arm64_translation_benchmark_status benchmark_status;
    uint64_t start_ns;
    uint64_t end_ns;

    if (!monotonic_raw_ns(&start_ns)) {
        return 0;
    }
    reuse_status = db_arm64_translation_reuse_publish(
        cache,
        reuse,
        block,
        &operation,
        &snapshot);
    if (!monotonic_raw_ns(&end_ns) ||
        reuse_status != DB_ARM64_TRANSLATION_REUSE_OK) {
        return 0;
    }

    benchmark_status = db_arm64_translation_benchmark_record(
        benchmark,
        phase,
        iteration,
        start_ns,
        end_ns,
        &operation,
        &snapshot);
    return benchmark_status == DB_ARM64_TRANSLATION_BENCHMARK_OK;
}

static int record_lookup(
    const db_arm64_block_cache *cache,
    db_arm64_translation_reuse_session *reuse,
    db_arm64_translation_benchmark_session *benchmark,
    db_arm64_translation_benchmark_phase phase,
    uint64_t iteration,
    uint64_t guest_source_rip,
    uint64_t source_generation,
    uint64_t translation_key)
{
    db_arm64_published_block output;
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;
    db_arm64_translation_reuse_status reuse_status;
    db_arm64_translation_benchmark_status benchmark_status;
    uint64_t start_ns;
    uint64_t end_ns;

    memset(&output, 0xa5, sizeof(output));
    if (!monotonic_raw_ns(&start_ns)) {
        return 0;
    }
    reuse_status = db_arm64_translation_reuse_lookup(
        cache,
        reuse,
        guest_source_rip,
        source_generation,
        translation_key,
        &output,
        &operation,
        &snapshot);
    if (!monotonic_raw_ns(&end_ns) ||
        reuse_status != DB_ARM64_TRANSLATION_REUSE_OK) {
        return 0;
    }

    benchmark_status = db_arm64_translation_benchmark_record(
        benchmark,
        phase,
        iteration,
        start_ns,
        end_ns,
        &operation,
        &snapshot);
    return benchmark_status == DB_ARM64_TRANSLATION_BENCHMARK_OK;
}

static FILE *create_output_file(const char *path)
{
    const int descriptor = open(
        path,
        O_WRONLY | O_CREAT | O_EXCL,
        (mode_t)0600);
    FILE *file;

    if (descriptor < 0) {
        return NULL;
    }

    file = fdopen(descriptor, "wb");
    if (file == NULL) {
        (void)close(descriptor);
        (void)remove(path);
        return NULL;
    }

    return file;
}

static int finish_output_file(FILE *file, const char *path, int failed)
{
    failed |= fflush(file) != 0;
    failed |= fclose(file) != 0;
    if (failed != 0) {
        (void)remove(path);
        return 0;
    }

    return 1;
}

static int write_manifest(const char *path,
                          const char *runtime_id,
                          const char *runtime_build,
                          const char *device_id,
                          const char *platform_build,
                          const char *driver_build,
                          const char *power_mode,
                          const char *thermal_start,
                          uint64_t iterations,
                          size_t sample_count)
{
    FILE *file = create_output_file(path);
    int failed = 0;

    if (file == NULL) {
        return 0;
    }

    failed |= fprintf(file, "%s\n",
                      "DBLYATEM_ARM64_TRANSLATION_BENCHMARK_MANIFEST_V1") < 0;
    failed |= fprintf(file, "FORMAT_VERSION=1\n") < 0;
    failed |= fprintf(file,
                      "BENCHMARK=arm64-translation-reuse\n") < 0;
    failed |= fprintf(file, "RUNTIME_ID=%s\n", runtime_id) < 0;
    failed |= fprintf(file, "RUNTIME_BUILD=%s\n", runtime_build) < 0;
    failed |= fprintf(file, "DEVICE_ID=%s\n", device_id) < 0;
    failed |= fprintf(file,
                      "PLATFORM_BUILD=%s\n",
                      platform_build) < 0;
    failed |= fprintf(file,
                      "DRIVER_BUILD=%s\n",
                      driver_build) < 0;
    failed |= fprintf(file,
                      "POWER_MODE=%s\n",
                      power_mode) < 0;
    failed |= fprintf(file,
                      "THERMAL_START=%s\n",
                      thermal_start) < 0;
    failed |= fprintf(file,
                      "HOST_ARCH=%s\n",
                      host_architecture()) < 0;
    failed |= fprintf(file,
                      "WORKLOAD_ID=synthetic-publish-hit-miss-duplicate-v1\n") < 0;
    failed |= fprintf(file,
                      "CLOCK=CLOCK_MONOTONIC_RAW\n") < 0;
    failed |= fprintf(file, "CLOCK_UNIT=nanoseconds\n") < 0;
    failed |= fprintf(file,
                      "ITERATIONS=%" PRIu64 "\n",
                      iterations) < 0;
    failed |= fprintf(file,
                      "SAMPLE_COUNT=%zu\n",
                      sample_count) < 0;
    failed |= fprintf(file,
                      "PUBLISHED_BLOCK_EXECUTION=NO\n") < 0;
    failed |= fprintf(file,
                      "WINLATOR_COMPARISON=NOT_RUN\n") < 0;
    return finish_output_file(file, path, failed);
}

static int write_csv(
    const char *path,
    const db_arm64_translation_benchmark_session *benchmark)
{
    static const char header[] =
        "format_version,benchmark_sequence,iteration,phase,start_ns,end_ns,"
        "duration_ns,operation_sequence,operation_kind,cache_status,"
        "slot_index,probe_count,hit,miss,inserted,duplicate,conflict,full,"
        "lookup_count,hit_count,miss_count,total_lookup_probes,"
        "maximum_lookup_probes,publication_count,insert_count,"
        "duplicate_count,conflict_count,full_count,"
        "total_publication_probes,maximum_publication_probes,"
        "accepted_operation_count,rejected_operation_count\n";
    FILE *file = create_output_file(path);
    size_t index;
    int failed = 0;

    if (file == NULL) {
        return 0;
    }

    failed |= fputs(header, file) == EOF;

    for (index = 0u; index < benchmark->count && failed == 0; ++index) {
        const db_arm64_translation_benchmark_sample *sample =
            &benchmark->samples[index];
        const db_arm64_translation_reuse_operation *operation =
            &sample->operation;
        const db_arm64_translation_reuse_snapshot *snapshot =
            &sample->snapshot;

        failed |= fprintf(
            file,
            "%" PRIu32 ",%" PRIu64 ",%" PRIu64 ",%s,"
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu32 ",%" PRIu32 ","
            "%" PRIu64 ",%" PRIu64 ","
            "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ","
            "%" PRIu32 ",%" PRIu32 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 "\n",
            sample->format_version,
            sample->benchmark_sequence,
            sample->iteration,
            db_arm64_translation_benchmark_phase_name(
                (db_arm64_translation_benchmark_phase)sample->phase),
            sample->start_ns,
            sample->end_ns,
            sample->duration_ns,
            operation->sequence_number,
            operation->operation_kind,
            operation->cache_status,
            operation->slot_index,
            operation->probe_count,
            operation->hit,
            operation->miss,
            operation->inserted,
            operation->duplicate,
            operation->conflict,
            operation->full,
            snapshot->lookup_count,
            snapshot->hit_count,
            snapshot->miss_count,
            snapshot->total_lookup_probes,
            snapshot->maximum_lookup_probes,
            snapshot->publication_count,
            snapshot->insert_count,
            snapshot->duplicate_count,
            snapshot->conflict_count,
            snapshot->full_count,
            snapshot->total_publication_probes,
            snapshot->maximum_publication_probes,
            snapshot->accepted_operation_count,
            snapshot->rejected_operation_count) < 0;
    }

    return finish_output_file(file, path, failed);
}

int main(int argc, char **argv)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[BENCHMARK_CACHE_CAPACITY];
    db_arm64_translation_reuse_session reuse;
    db_arm64_translation_benchmark_session benchmark;
    db_arm64_translation_benchmark_sample
        samples[BENCHMARK_SAMPLE_CAPACITY];
    uint64_t iterations;
    uint64_t iteration;
    size_t sample_capacity;

    if (argc != 11 ||
        !parse_iterations(argv[3], &iterations) ||
        !token_valid(argv[4]) ||
        !token_valid(argv[5]) ||
        !token_valid(argv[6]) ||
        !token_valid(argv[7]) ||
        !token_valid(argv[8]) ||
        !token_valid(argv[9]) ||
        !token_valid(argv[10])) {
        fprintf(stderr,
                "usage: %s MANIFEST CSV ITERATIONS RUNTIME_ID "
                "RUNTIME_BUILD DEVICE_ID PLATFORM_BUILD DRIVER_BUILD "
                "POWER_MODE THERMAL_START\n",
                argc > 0 ? argv[0] : "benchmark");
        return 2;
    }

    sample_capacity = (size_t)(iterations * UINT64_C(4));
    if (db_arm64_block_cache_init(
            &cache,
            slots,
            BENCHMARK_CACHE_CAPACITY) != DB_ARM64_BLOCK_CACHE_OK ||
        db_arm64_translation_reuse_init(&reuse) !=
            DB_ARM64_TRANSLATION_REUSE_OK ||
        db_arm64_translation_benchmark_init(
            &benchmark,
            samples,
            sample_capacity) !=
            DB_ARM64_TRANSLATION_BENCHMARK_OK) {
        fprintf(stderr, "benchmark initialization failed\n");
        return 3;
    }

    for (iteration = 0u; iteration < iterations; ++iteration) {
        const db_arm64_published_block block = make_block(iteration);

        if (!record_publish(
                &cache,
                &reuse,
                &benchmark,
                DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_INSERT,
                iteration,
                &block) ||
            !record_lookup(
                &cache,
                &reuse,
                &benchmark,
                DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
                iteration,
                block.guest_source_rip,
                block.source_generation,
                block.translation_key) ||
            !record_lookup(
                &cache,
                &reuse,
                &benchmark,
                DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_MISS,
                iteration,
                block.guest_source_rip + UINT64_C(0x800000),
                block.source_generation,
                block.translation_key + UINT64_C(0x400000)) ||
            !record_publish(
                &cache,
                &reuse,
                &benchmark,
                DB_ARM64_TRANSLATION_BENCHMARK_PUBLICATION_DUPLICATE,
                iteration,
                &block)) {
            fprintf(stderr,
                    "benchmark workload failed at iteration=%" PRIu64 "\n",
                    iteration);
            return 4;
        }
    }

    if (benchmark.count != sample_capacity ||
        cache.count != (size_t)iterations ||
        reuse.next_sequence != (uint64_t)sample_capacity + UINT64_C(1) ||
        reuse.lookup_count != iterations * UINT64_C(2) ||
        reuse.hit_count != iterations ||
        reuse.miss_count != iterations ||
        reuse.publication_count != iterations * UINT64_C(2) ||
        reuse.insert_count != iterations ||
        reuse.duplicate_count != iterations ||
        reuse.conflict_count != 0u ||
        reuse.full_count != 0u ||
        reuse.accepted_operation_count !=
            iterations * UINT64_C(4) ||
        reuse.rejected_operation_count != 0u) {
        fprintf(stderr, "benchmark final state mismatch\n");
        return 5;
    }

    if (!write_manifest(
            argv[1],
            argv[4],
            argv[5],
            argv[6],
            argv[7],
            argv[8],
            argv[9],
            argv[10],
            iterations,
            benchmark.count)) {
        fprintf(stderr, "benchmark manifest output failed\n");
        return 6;
    }

    if (!write_csv(argv[2], &benchmark)) {
        (void)remove(argv[1]);
        fprintf(stderr, "benchmark CSV output failed\n");
        return 6;
    }

    printf("ARM64 TRANSLATION BENCHMARK RUNNER: PASS\n");
    printf("CLOCK_MONOTONIC_RAW=PASS\n");
    printf("MANIFEST_V1=PASS\n");
    printf("RAW_CSV_V1=PASS\n");
    printf("SAMPLE_COUNT=%zu\n", benchmark.count);
    printf("PUBLISHED_BLOCK_EXECUTION=NO\n");
    printf("WINLATOR_COMPARISON=NOT_RUN\n");
    return 0;
}
