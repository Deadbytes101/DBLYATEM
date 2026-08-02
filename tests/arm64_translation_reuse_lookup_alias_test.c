#include "arm64_translation_reuse_test_support.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("ARM64 TRANSLATION REUSE LOOKUP ALIAS TEST: FAIL line=%d\n", __LINE__);              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int test_lookup_alias_matrix(void)
{
    arm64_translation_reuse_fixture value;
    arm64_translation_reuse_fixture before;

    CHECK(arm64_translation_reuse_fixture_init(&value));
    before = value;

    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache,
              (db_arm64_translation_reuse_session *)(void *)&value.cache,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output, &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              (db_arm64_published_block *)(void *)&value.cache,
              &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output,
              (db_arm64_translation_reuse_operation *)(void *)&value.cache,
              &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output, &value.operation,
              (db_arm64_translation_reuse_snapshot *)(void *)&value.cache) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);

    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              (db_arm64_published_block *)(void *)&value.session,
              &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output,
              (db_arm64_translation_reuse_operation *)(void *)&value.session,
              &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output, &value.operation,
              (db_arm64_translation_reuse_snapshot *)(void *)&value.session) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);

    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output,
              (db_arm64_translation_reuse_operation *)(void *)&value.output,
              &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output, &value.operation,
              (db_arm64_translation_reuse_snapshot *)(void *)&value.output) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output, &value.operation,
              (db_arm64_translation_reuse_snapshot *)(void *)&value.operation) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);

    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache,
              (db_arm64_translation_reuse_session *)(void *)&value.slots[0],
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output, &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              (db_arm64_published_block *)(void *)&value.slots[0],
              &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output,
              (db_arm64_translation_reuse_operation *)(void *)&value.slots[0],
              &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_lookup(
              &value.cache, &value.session,
              value.block.guest_source_rip,
              value.block.source_generation,
              value.block.translation_key,
              &value.output, &value.operation,
              (db_arm64_translation_reuse_snapshot *)(void *)&value.slots[0]) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);

    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));
    return 0;
}

int main(void)
{
    if (test_lookup_alias_matrix() != 0) {
        return 1;
    }

    printf("ARM64 TRANSLATION REUSE LOOKUP ALIAS TEST: PASS\n");
    return 0;
}
