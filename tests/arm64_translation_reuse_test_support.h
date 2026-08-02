#ifndef DBLYAT_ARM64_TRANSLATION_REUSE_TEST_SUPPORT_H
#define DBLYAT_ARM64_TRANSLATION_REUSE_TEST_SUPPORT_H

#include "dblyat/arm64_translation_reuse.h"

#include <stdint.h>

typedef struct arm64_translation_reuse_fixture {
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_translation_reuse_session session;
    db_arm64_published_block block;
    db_arm64_published_block output;
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;
} arm64_translation_reuse_fixture;

db_arm64_published_block arm64_translation_reuse_make_block(
    uint64_t rip,
    uint64_t generation,
    uint64_t translation_key,
    uint64_t mapping);

int arm64_translation_reuse_fixture_init(
    arm64_translation_reuse_fixture *value);

int arm64_translation_reuse_fixture_unchanged(
    const arm64_translation_reuse_fixture *left,
    const arm64_translation_reuse_fixture *right);

db_arm64_translation_reuse_status arm64_translation_reuse_lookup_fixture(
    arm64_translation_reuse_fixture *value);

db_arm64_translation_reuse_status arm64_translation_reuse_publish_fixture(
    arm64_translation_reuse_fixture *value);

#endif
