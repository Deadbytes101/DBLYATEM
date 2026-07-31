#ifndef DBLYAT_ARM64_SELECT_INTERNAL_H
#define DBLYAT_ARM64_SELECT_INTERNAL_H

#include "dblyat/arm64_select.h"

#include <stdbool.h>

bool db_arm64_select_ir_valid_internal(
    const db_ir_instruction *instruction);

bool db_arm64_select_terminator_internal(db_ir_opcode opcode);

uint32_t db_arm64_select_expansion_internal(db_ir_opcode opcode);

void db_arm64_select_emit_instruction_internal(
    const db_ir_instruction *instruction,
    uint32_t source_ir_index,
    db_arm64_select_record *records);

#endif
