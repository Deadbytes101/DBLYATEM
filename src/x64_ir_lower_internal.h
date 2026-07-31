#ifndef DBLYAT_X64_IR_LOWER_INTERNAL_H
#define DBLYAT_X64_IR_LOWER_INTERNAL_H

#include "dblyat/x64_ir_lower.h"

#include <stdbool.h>

db_x64_ir_lower_status db_x64_ir_lower_validate_record_internal(
    const db_x64_block_record *record);

bool db_x64_ir_lower_control_is_terminator_internal(
    db_x64_block_control control);

db_x64_ir_lower_status db_x64_ir_lower_emit_record_internal(
    const db_x64_block_record *record,
    db_ir_instruction *instruction);

#endif
