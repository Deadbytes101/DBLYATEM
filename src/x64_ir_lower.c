#include "x64_ir_lower_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_x64_ir_lower_overlap(const void *left,
                                    size_t left_size,
                                    const void *right,
                                    size_t right_size)
{
    const uintptr_t left_address = (uintptr_t)left;
    const uintptr_t right_address = (uintptr_t)right;

    if (left_size == 0u || right_size == 0u) {
        return false;
    }

    if (left_address <= right_address) {
        return right_address - left_address < left_size;
    }

    return left_address - right_address < right_size;
}

db_x64_ir_lower_status db_x64_ir_lower_block(
    const db_x64_block_record *records,
    uint32_t record_count,
    db_ir_instruction *instructions,
    uint32_t instruction_capacity,
    db_x64_ir_lower_summary *summary)
{
    db_x64_ir_lower_summary completed;
    db_x64_ir_lower_status status;
    db_ir_instruction planned;
    size_t record_bytes;
    size_t instruction_bytes;
    uint32_t index;
    bool has_terminator = false;
    db_ir_opcode terminator_opcode = DB_IR_OPCODE_NOP;

    if (records == NULL || instructions == NULL || summary == NULL) {
        return DB_X64_IR_LOWER_NULL_ARGUMENT;
    }

    if (record_count == 0u) {
        return DB_X64_IR_LOWER_BAD_COUNT;
    }

    if (instruction_capacity < record_count) {
        return DB_X64_IR_LOWER_OUTPUT_CAPACITY;
    }

#if SIZE_MAX < UINT32_MAX
    if (record_count > SIZE_MAX / sizeof(*records) ||
        instruction_capacity > SIZE_MAX / sizeof(*instructions)) {
        return DB_X64_IR_LOWER_BAD_COUNT;
    }
#endif

    record_bytes = (size_t)record_count * sizeof(*records);
    instruction_bytes =
        (size_t)instruction_capacity * sizeof(*instructions);

    if (db_x64_ir_lower_overlap(records, record_bytes,
                                instructions, instruction_bytes) ||
        db_x64_ir_lower_overlap(records, record_bytes,
                                summary, sizeof(*summary)) ||
        db_x64_ir_lower_overlap(instructions, instruction_bytes,
                                summary, sizeof(*summary))) {
        return DB_X64_IR_LOWER_BUFFER_OVERLAP;
    }

    for (index = 0u; index < record_count; ++index) {
        status = db_x64_ir_lower_validate_record_internal(&records[index]);
        if (status != DB_X64_IR_LOWER_OK) {
            return status;
        }

        if (index != 0u &&
            records[index].rip != records[index - 1u].fallthrough_rip) {
            return DB_X64_IR_LOWER_DISCONTINUOUS_BLOCK;
        }

        if (db_x64_ir_lower_control_is_terminator_internal(
                records[index].control)) {
            if (has_terminator || index + 1u != record_count) {
                return DB_X64_IR_LOWER_TERMINATOR_ORDER;
            }
            has_terminator = true;
        }

        status = db_x64_ir_lower_emit_record_internal(&records[index],
                                                       &planned);
        if (status != DB_X64_IR_LOWER_OK) {
            return status;
        }
        if (has_terminator) {
            terminator_opcode = planned.opcode;
        }
    }

    for (index = 0u; index < record_count; ++index) {
        status = db_x64_ir_lower_emit_record_internal(&records[index],
                                                       &planned);
        if (status != DB_X64_IR_LOWER_OK) {
            return status;
        }
        instructions[index] = planned;
    }

    completed.source_rip = records[0].rip;
    completed.final_fallthrough_rip =
        records[record_count - 1u].fallthrough_rip;
    completed.record_count = record_count;
    completed.instruction_count = record_count;
    completed.terminator_opcode = terminator_opcode;
    completed.has_terminator = has_terminator ? UINT8_C(1) : UINT8_C(0);
    for (index = 0u; index < sizeof(completed.reserved); ++index) {
        completed.reserved[index] = 0u;
    }

    *summary = completed;
    return DB_X64_IR_LOWER_OK;
}

const char *db_x64_ir_lower_status_name(db_x64_ir_lower_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad record count",
        "insufficient output capacity",
        "bad block record",
        "discontinuous block",
        "terminator order invalid",
        "unsupported block record"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 IR lower status";
    }

    return names[(size_t)status];
}
