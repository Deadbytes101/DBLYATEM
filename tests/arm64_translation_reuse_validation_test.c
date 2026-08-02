#include "arm64_translation_reuse_test_support.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("ARM64 TRANSLATION REUSE VALIDATION TEST: FAIL line=%d\n", __LINE__);              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int test_null_arguments(void)
{
    arm64_translation_reuse_fixture value;

    CHECK(arm64_translation_reuse_fixture_init(&value));
    CHECK(db_arm64_translation_reuse_init(NULL) ==
          DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT);

    CHECK(db_arm64_translation_reuse_lookup(
              NULL, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output, &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, NULL,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output, &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              NULL, &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output, NULL, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output, &value.operation, NULL) ==
          DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT);

    CHECK(db_arm64_translation_reuse_publish(
              NULL, &value.session, &value.block,
              &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, NULL, &value.block,
              &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session, NULL,
              &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session, &value.block,
              NULL, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session, &value.block,
              &value.operation, NULL) ==
          DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT);

    return 0;
}

static int test_malformed_session(void)
{
    arm64_translation_reuse_fixture value;
    arm64_translation_reuse_fixture before;

    CHECK(arm64_translation_reuse_fixture_init(&value));
    value.session.format_version = 0u;
    before = value;
    CHECK(arm64_translation_reuse_lookup_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_BAD_SESSION);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    CHECK(arm64_translation_reuse_fixture_init(&value));
    value.session.reserved[0] = 1u;
    before = value;
    CHECK(arm64_translation_reuse_publish_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_BAD_SESSION);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    CHECK(arm64_translation_reuse_fixture_init(&value));
    value.session.lookup_count = 1u;
    before = value;
    CHECK(arm64_translation_reuse_lookup_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_BAD_SESSION);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    CHECK(arm64_translation_reuse_fixture_init(&value));
    value.session.next_sequence = 2u;
    before = value;
    CHECK(arm64_translation_reuse_publish_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_BAD_SESSION);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    return 0;
}

static int test_impossible_probe_metrics(void)
{
    arm64_translation_reuse_fixture value;
    arm64_translation_reuse_fixture before;

    CHECK(arm64_translation_reuse_fixture_init(&value));
    value.session.next_sequence = 3u;
    value.session.lookup_count = 2u;
    value.session.hit_count = 2u;
    value.session.total_lookup_probes = 2u;
    value.session.maximum_lookup_probes = 2u;
    value.session.accepted_operation_count = 2u;
    before = value;
    CHECK(arm64_translation_reuse_lookup_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_BAD_SESSION);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    CHECK(arm64_translation_reuse_fixture_init(&value));
    value.session.next_sequence = 3u;
    value.session.lookup_count = 2u;
    value.session.hit_count = 2u;
    value.session.total_lookup_probes = 5u;
    value.session.maximum_lookup_probes = 2u;
    value.session.accepted_operation_count = 2u;
    before = value;
    CHECK(arm64_translation_reuse_lookup_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_BAD_SESSION);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    CHECK(arm64_translation_reuse_fixture_init(&value));
    value.session.next_sequence = 3u;
    value.session.publication_count = 2u;
    value.session.duplicate_count = 2u;
    value.session.total_publication_probes = 2u;
    value.session.maximum_publication_probes = 2u;
    value.session.accepted_operation_count = 2u;
    before = value;
    CHECK(arm64_translation_reuse_publish_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_BAD_SESSION);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    CHECK(arm64_translation_reuse_fixture_init(&value));
    value.session.next_sequence = 3u;
    value.session.publication_count = 2u;
    value.session.duplicate_count = 2u;
    value.session.total_publication_probes = 5u;
    value.session.maximum_publication_probes = 2u;
    value.session.accepted_operation_count = 2u;
    before = value;
    CHECK(arm64_translation_reuse_publish_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_BAD_SESSION);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    return 0;
}

static int test_failure_preservation(void)
{
    arm64_translation_reuse_fixture value;
    arm64_translation_reuse_fixture before;

    CHECK(arm64_translation_reuse_fixture_init(&value));
    before = value;
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              0u,
              value.block.source_generation,
              value.block.translation_key,
              &value.output, &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_CACHE_REJECTED);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    CHECK(arm64_translation_reuse_fixture_init(&value));
    value.cache.format_version = 0u;
    before = value;
    CHECK(arm64_translation_reuse_lookup_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_CACHE_REJECTED);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    CHECK(arm64_translation_reuse_fixture_init(&value));
    value.cache.count = value.cache.capacity + 1u;
    before = value;
    CHECK(arm64_translation_reuse_publish_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_CACHE_REJECTED);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    CHECK(arm64_translation_reuse_fixture_init(&value));
    value.block.format_version = 0u;
    before = value;
    CHECK(arm64_translation_reuse_publish_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_CACHE_REJECTED);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    CHECK(arm64_translation_reuse_fixture_init(&value));
    value.slots[0].state = UINT32_C(99);
    before = value;
    CHECK(arm64_translation_reuse_lookup_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_CACHE_REJECTED);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    return 0;
}

static int test_counter_overflow(void)
{
    arm64_translation_reuse_fixture value;
    arm64_translation_reuse_fixture before;

    CHECK(arm64_translation_reuse_fixture_init(&value));
    memset(&value.session, 0, sizeof(value.session));
    value.session.next_sequence = UINT64_MAX;
    value.session.lookup_count = UINT64_MAX - UINT64_C(1);
    value.session.hit_count = UINT64_MAX - UINT64_C(1);
    value.session.total_lookup_probes = UINT64_MAX - UINT64_C(1);
    value.session.maximum_lookup_probes = 1u;
    value.session.accepted_operation_count =
        UINT64_MAX - UINT64_C(1);
    value.session.format_version =
        DB_ARM64_TRANSLATION_REUSE_FORMAT_VERSION;
    before = value;
    CHECK(arm64_translation_reuse_lookup_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_COUNTER_OVERFLOW);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    CHECK(arm64_translation_reuse_fixture_init(&value));
    memset(&value.session, 0, sizeof(value.session));
    value.session.next_sequence = UINT64_MAX;
    value.session.publication_count = UINT64_MAX - UINT64_C(1);
    value.session.duplicate_count = UINT64_MAX - UINT64_C(1);
    value.session.total_publication_probes =
        UINT64_MAX - UINT64_C(1);
    value.session.maximum_publication_probes = 1u;
    value.session.accepted_operation_count =
        UINT64_MAX - UINT64_C(1);
    value.session.format_version =
        DB_ARM64_TRANSLATION_REUSE_FORMAT_VERSION;
    before = value;
    CHECK(arm64_translation_reuse_publish_fixture(&value) ==
          DB_ARM64_TRANSLATION_REUSE_COUNTER_OVERFLOW);
    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));

    return 0;
}

int main(void)
{
    if (test_null_arguments() != 0) {
        return 1;
    }
    if (test_malformed_session() != 0) {
        return 2;
    }
    if (test_impossible_probe_metrics() != 0) {
        return 3;
    }
    if (test_failure_preservation() != 0) {
        return 4;
    }
    if (test_counter_overflow() != 0) {
        return 5;
    }

    printf("ARM64 TRANSLATION REUSE VALIDATION TEST: PASS\n");
    return 0;
}
