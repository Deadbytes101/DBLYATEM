#include "arm64_select_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_arm64_select_overlap(const void *left,
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

db_arm64_select_status db_arm64_select_block(
    const db_ir_instruction *instructions,
    uint32_t instruction_count,
    db_arm64_select_record *records,
    uint32_t record_capacity,
    db_arm64_select_summary *summary)
{
    db_arm64_select_summary completed;
    uint64_t required = 0u;
    size_t instruction_bytes;
    size_t record_bytes;
    uint32_t index;
    uint32_t cursor = 0u;
    bool has_terminator = false;
    db_arm64_select_opcode terminator_opcode = DB_ARM64_SELECT_NOP;

    if (instructions == NULL || records == NULL || summary == NULL) {
        return DB_ARM64_SELECT_NULL_ARGUMENT;
    }

    if (instruction_count == 0u) {
        return DB_ARM64_SELECT_BAD_COUNT;
    }

#if SIZE_MAX < UINT64_MAX
    if ((uint64_t)instruction_count >
            (uint64_t)(SIZE_MAX / sizeof(*instructions)) ||
        (uint64_t)record_capacity >
            (uint64_t)(SIZE_MAX / sizeof(*records))) {
        return DB_ARM64_SELECT_COUNT_OVERFLOW;
    }
#endif

    instruction_bytes =
        (size_t)instruction_count * sizeof(*instructions);
    record_bytes = (size_t)record_capacity * sizeof(*records);

    if (db_arm64_select_overlap(instructions, instruction_bytes,
                                records, record_bytes) ||
        db_arm64_select_overlap(instructions, instruction_bytes,
                                summary, sizeof(*summary)) ||
        db_arm64_select_overlap(records, record_bytes,
                                summary, sizeof(*summary))) {
        return DB_ARM64_SELECT_BUFFER_OVERLAP;
    }

    for (index = 0u; index < instruction_count; ++index) {
        const uint32_t expansion =
            db_arm64_select_expansion_internal(instructions[index].opcode);

        if (!db_arm64_select_ir_valid_internal(&instructions[index]) ||
            expansion == 0u) {
            return DB_ARM64_SELECT_BAD_IR;
        }

        if (index != 0u &&
            instructions[index].source_rip !=
                instructions[index - 1u].fallthrough_rip) {
            return DB_ARM64_SELECT_DISCONTINUOUS_IR;
        }

        if (db_arm64_select_terminator_internal(
                instructions[index].opcode)) {
            if (has_terminator || index + 1u != instruction_count) {
                return DB_ARM64_SELECT_TERMINATOR_ORDER;
            }
            has_terminator = true;
        }

        required += expansion;
        if (required > UINT32_MAX) {
            return DB_ARM64_SELECT_COUNT_OVERFLOW;
        }
    }

    if ((uint64_t)record_capacity < required) {
        return DB_ARM64_SELECT_OUTPUT_CAPACITY;
    }

    for (index = 0u; index < instruction_count; ++index) {
        const uint32_t expansion =
            db_arm64_select_expansion_internal(instructions[index].opcode);
        db_arm64_select_emit_instruction_internal(&instructions[index],
                                                  index,
                                                  &records[cursor]);
        cursor += expansion;
    }

    if (has_terminator) {
        terminator_opcode = records[cursor - 1u].opcode;
    }

    completed.source_rip = instructions[0].source_rip;
    completed.final_fallthrough_rip =
        instructions[instruction_count - 1u].fallthrough_rip;
    completed.ir_count = instruction_count;
    completed.operation_count = cursor;
    completed.required_capacity = (uint32_t)required;
    completed.terminator_opcode = terminator_opcode;
    completed.has_terminator = has_terminator ? UINT8_C(1) : UINT8_C(0);
    for (index = 0u; index < sizeof(completed.reserved); ++index) {
        completed.reserved[index] = 0u;
    }

    *summary = completed;
    return DB_ARM64_SELECT_OK;
}

const char *db_arm64_select_status_name(db_arm64_select_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad IR count",
        "bad IR instruction",
        "discontinuous IR stream",
        "terminator order invalid",
        "insufficient output capacity",
        "selection count overflow"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown ARM64 select status";
    }

    return names[(size_t)status];
}

const char *db_arm64_select_opcode_name(db_arm64_select_opcode opcode)
{
    static const char *const names[] = {
        "nop",
        "materialize u64",
        "move guest gpr",
        "eor guest gpr",
        "update logic flags",
        "form guest address",
        "load u64",
        "load u64 target",
        "store u64",
        "push guest u64",
        "pop guest u64",
        "branch guest direct",
        "branch guest target",
        "test guest condition",
        "branch if true",
        "raise trap",
        "sub guest imm8"
    };

    if ((size_t)opcode >= sizeof(names) / sizeof(names[0])) {
        return "unknown ARM64 select opcode";
    }

    return names[(size_t)opcode];
}
