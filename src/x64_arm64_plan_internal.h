#ifndef DBLYAT_X64_ARM64_PLAN_INTERNAL_H
#define DBLYAT_X64_ARM64_PLAN_INTERNAL_H

#include "dblyat/x64_arm64_plan.h"

#include <stdbool.h>

bool db_x64_arm64_plan_selection_valid_internal(
    const db_arm64_select_record *record);

bool db_x64_arm64_plan_terminal_internal(
    db_arm64_select_opcode opcode);

bool db_x64_arm64_plan_group_pair_internal(
    db_arm64_select_opcode producer,
    db_arm64_select_opcode consumer);

bool db_x64_arm64_plan_requires_consumer_internal(
    db_arm64_select_opcode opcode);

bool db_x64_arm64_plan_is_consumer_internal(
    db_arm64_select_opcode opcode);

uint32_t db_x64_arm64_plan_expansion_internal(
    const db_arm64_select_record *record);

void db_x64_arm64_plan_emit_internal(
    const db_arm64_select_record *selection,
    uint32_t source_select_index,
    db_x64_arm64_plan_record *records);

bool db_x64_arm64_plan_state_offset_internal(
    uint8_t guest_register,
    uint32_t *offset);

#endif
