#ifndef DBLYAT_X64_ARM64_RELOCATE_INTERNAL_H
#define DBLYAT_X64_ARM64_RELOCATE_INTERNAL_H

#include "dblyat/x64_arm64_relocate.h"

#include <stdbool.h>

bool db_x64_arm64_relocate_canonical_internal(uint64_t address);

bool db_x64_arm64_relocate_add_scaled_internal(
    uint64_t base,
    uint32_t index,
    uint64_t *result);

bool db_x64_arm64_relocate_branch_word_internal(
    db_x64_arm64_relocation_kind kind,
    uint64_t source,
    uint64_t target,
    uint32_t *word);

uint32_t db_x64_arm64_relocate_materialize_count_internal(
    uint64_t value);

uint32_t db_x64_arm64_relocate_emit_materialize_internal(
    uint64_t value,
    uint32_t *words);

bool db_x64_arm64_relocate_record_valid_internal(
    const db_x64_arm64_relocation *relocation);

bool db_x64_arm64_relocate_target_table_valid_internal(
    const db_x64_arm64_helper_targets *targets);

bool db_x64_arm64_relocate_placeholder_matches_internal(
    uint32_t word,
    db_x64_arm64_relocation_kind kind);

#endif
