#ifndef DBLYAT_X64_ARM64_RELOCATE_H
#define DBLYAT_X64_ARM64_RELOCATE_H

#include "dblyat/x64_arm64_encode.h"

#include <stdint.h>

#define DB_X64_ARM64_HELPER_TARGET_COUNT \
    ((uint32_t)DB_X64_ARM64_HELPER_SUB_GUEST_IMM8 + UINT32_C(1))

typedef struct db_x64_arm64_helper_targets {
    uint64_t addresses[DB_X64_ARM64_HELPER_TARGET_COUNT];
    uint8_t reserved[8];
} db_x64_arm64_helper_targets;

typedef enum db_x64_arm64_resolution_kind {
    DB_X64_ARM64_RESOLUTION_DIRECT = 0,
    DB_X64_ARM64_RESOLUTION_FAR_THUNK
} db_x64_arm64_resolution_kind;

typedef struct db_x64_arm64_resolution {
    uint64_t source_address;
    uint64_t helper_target;
    uint64_t thunk_address;
    uint32_t relocation_index;
    uint32_t source_word_index;
    uint32_t patched_word;
    uint32_t thunk_word_index;
    uint32_t thunk_word_count;
    db_x64_arm64_resolution_kind resolution;
    db_x64_arm64_relocation_kind relocation_kind;
    db_x64_arm64_helper helper;
    uint8_t reserved[4];
} db_x64_arm64_resolution;

typedef enum db_x64_arm64_relocate_status {
    DB_X64_ARM64_RELOCATE_OK = 0,
    DB_X64_ARM64_RELOCATE_NULL_ARGUMENT,
    DB_X64_ARM64_RELOCATE_BUFFER_OVERLAP,
    DB_X64_ARM64_RELOCATE_BAD_WORD_COUNT,
    DB_X64_ARM64_RELOCATE_BAD_CODE_BASE,
    DB_X64_ARM64_RELOCATE_BAD_TARGET_TABLE,
    DB_X64_ARM64_RELOCATE_BAD_RELOCATION,
    DB_X64_ARM64_RELOCATE_RELOCATION_ORDER,
    DB_X64_ARM64_RELOCATE_PLACEHOLDER_MISMATCH,
    DB_X64_ARM64_RELOCATE_ADDRESS_OVERFLOW,
    DB_X64_ARM64_RELOCATE_THUNK_OUT_OF_RANGE,
    DB_X64_ARM64_RELOCATE_WORD_CAPACITY,
    DB_X64_ARM64_RELOCATE_DECISION_CAPACITY
} db_x64_arm64_relocate_status;

typedef struct db_x64_arm64_relocate_summary {
    uint64_t code_base;
    uint32_t source_word_count;
    uint32_t output_word_count;
    uint32_t relocation_count;
    uint32_t direct_count;
    uint32_t thunk_count;
    uint32_t thunk_word_count;
    uint8_t reserved[4];
} db_x64_arm64_relocate_summary;

db_x64_arm64_relocate_status db_x64_arm64_resolve_helpers(
    const uint32_t *source_words,
    uint32_t source_word_count,
    const db_x64_arm64_relocation *relocations,
    uint32_t relocation_count,
    const db_x64_arm64_helper_targets *targets,
    uint64_t code_base,
    uint32_t *output_words,
    uint32_t output_word_capacity,
    db_x64_arm64_resolution *decisions,
    uint32_t decision_capacity,
    db_x64_arm64_relocate_summary *summary);

const char *db_x64_arm64_relocate_status_name(
    db_x64_arm64_relocate_status status);

#endif
