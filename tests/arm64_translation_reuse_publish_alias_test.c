#include "arm64_translation_reuse_test_support.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("ARM64 TRANSLATION REUSE PUBLISH ALIAS TEST: FAIL line=%d\n", __LINE__);              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int test_publish_alias_matrix(void)
{
    arm64_translation_reuse_fixture value;
    arm64_translation_reuse_fixture before;

    CHECK(arm64_translation_reuse_fixture_init(&value));
    before = value;

    CHECK(db_arm64_translation_reuse_publish(
              &value.cache,
              (db_arm64_translation_reuse_session *)(void *)&value.cache,
              &value.block, &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session,
              (const db_arm64_published_block *)(const void *)&value.cache,
              &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session, &value.block,
              (db_arm64_translation_reuse_operation *)(void *)&value.cache,
              &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session, &value.block,
              &value.operation,
              (db_arm64_translation_reuse_snapshot *)(void *)&value.cache) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);

    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session,
              (const db_arm64_published_block *)(const void *)&value.session,
              &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session, &value.block,
              (db_arm64_translation_reuse_operation *)(void *)&value.session,
              &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session, &value.block,
              &value.operation,
              (db_arm64_translation_reuse_snapshot *)(void *)&value.session) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);

    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session, &value.block,
              &value.operation,
              (db_arm64_translation_reuse_snapshot *)(void *)&value.operation) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session,
              (const db_arm64_published_block *)(const void *)&value.operation,
              &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session,
              (const db_arm64_published_block *)(const void *)&value.snapshot,
              &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);

    CHECK(db_arm64_translation_reuse_publish(
              &value.cache,
              (db_arm64_translation_reuse_session *)(void *)&value.slots[0],
              &value.block, &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session,
              (const db_arm64_published_block *)(const void *)&value.slots[0],
              &value.operation, &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session, &value.block,
              (db_arm64_translation_reuse_operation *)(void *)&value.slots[0],
              &value.snapshot) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);
    CHECK(db_arm64_translation_reuse_publish(
              &value.cache, &value.session, &value.block,
              &value.operation,
              (db_arm64_translation_reuse_snapshot *)(void *)&value.slots[0]) ==
          DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP);

    CHECK(arm64_translation_reuse_fixture_unchanged(&value, &before));
    return 0;
}

int main(void)
{
    if (test_publish_alias_matrix() != 0) {
        return 1;
    }

    printf("ARM64 TRANSLATION REUSE PUBLISH ALIAS TEST: PASS\n");
    return 0;
}
