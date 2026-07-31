#include "x64_arm64_plan_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool overlap(const void *a, size_t an, const void *b, size_t bn)
{
    const uintptr_t x = (uintptr_t)a;
    const uintptr_t y = (uintptr_t)b;
    if (an == 0u || bn == 0u) return false;
    return x <= y ? y - x < an : x - y < bn;
}

db_x64_arm64_plan_status db_x64_arm64_plan_block(
    const db_arm64_select_record *selections,
    uint32_t selection_count,
    db_x64_arm64_plan_record *records,
    uint32_t record_capacity,
    db_x64_arm64_plan_summary *summary)
{
    db_x64_arm64_plan_summary completed;
    uint64_t required = 0u;
    size_t selection_bytes;
    size_t record_bytes;
    uint32_t index;
    uint32_t cursor = 0u;
    bool has_terminal = false;
    db_x64_arm64_helper terminal_helper = DB_X64_ARM64_HELPER_NONE;

    if (selections == NULL || records == NULL || summary == NULL) {
        return DB_X64_ARM64_PLAN_NULL_ARGUMENT;
    }
    if (selection_count == 0u) return DB_X64_ARM64_PLAN_BAD_COUNT;

#if SIZE_MAX < UINT64_MAX
    if ((uint64_t)selection_count >
            (uint64_t)(SIZE_MAX / sizeof(*selections)) ||
        (uint64_t)record_capacity >
            (uint64_t)(SIZE_MAX / sizeof(*records))) {
        return DB_X64_ARM64_PLAN_COUNT_OVERFLOW;
    }
#endif

    selection_bytes = (size_t)selection_count * sizeof(*selections);
    record_bytes = (size_t)record_capacity * sizeof(*records);
    if (overlap(selections, selection_bytes, records, record_bytes) ||
        overlap(selections, selection_bytes, summary, sizeof(*summary)) ||
        overlap(records, record_bytes, summary, sizeof(*summary))) {
        return DB_X64_ARM64_PLAN_BUFFER_OVERLAP;
    }

    if (offsetof(db_x64_scalar_state, registers) > UINT32_MAX ||
        offsetof(db_x64_scalar_state, rip) > UINT32_MAX ||
        offsetof(db_x64_scalar_state, rflags) > UINT32_MAX) {
        return DB_X64_ARM64_PLAN_STATE_OFFSET_OVERFLOW;
    }

    for (index = 0u; index < selection_count; ++index) {
        const db_arm64_select_record *current = &selections[index];
        const uint32_t expansion =
            db_x64_arm64_plan_expansion_internal(current);
        uint32_t offset;

        if (!db_x64_arm64_plan_selection_valid_internal(current) ||
            expansion == 0u) {
            return DB_X64_ARM64_PLAN_BAD_SELECTION;
        }
        if (current->destination_register < UINT8_C(16) &&
            !db_x64_arm64_plan_state_offset_internal(
                current->destination_register, &offset)) {
            return DB_X64_ARM64_PLAN_STATE_OFFSET_OVERFLOW;
        }
        if (current->source_register < UINT8_C(16) &&
            !db_x64_arm64_plan_state_offset_internal(
                current->source_register, &offset)) {
            return DB_X64_ARM64_PLAN_STATE_OFFSET_OVERFLOW;
        }

        if (index == 0u) {
            if (current->source_ir_index != 0u ||
                db_x64_arm64_plan_is_consumer_internal(current->opcode)) {
                return DB_X64_ARM64_PLAN_BAD_GROUPING;
            }
        } else {
            const db_arm64_select_record *previous = &selections[index - 1u];
            if (current->source_ir_index == previous->source_ir_index) {
                if (current->source_rip != previous->source_rip ||
                    !db_x64_arm64_plan_group_pair_internal(
                        previous->opcode, current->opcode)) {
                    return DB_X64_ARM64_PLAN_BAD_GROUPING;
                }
            } else if (current->source_ir_index ==
                       previous->source_ir_index + 1u) {
                if (db_x64_arm64_plan_requires_consumer_internal(
                        previous->opcode) ||
                    db_x64_arm64_plan_is_consumer_internal(current->opcode)) {
                    return DB_X64_ARM64_PLAN_BAD_GROUPING;
                }
            } else {
                return DB_X64_ARM64_PLAN_BAD_GROUPING;
            }
        }

        if (db_x64_arm64_plan_requires_consumer_internal(current->opcode)) {
            if (index + 1u >= selection_count ||
                selections[index + 1u].source_ir_index !=
                    current->source_ir_index ||
                !db_x64_arm64_plan_group_pair_internal(
                    current->opcode, selections[index + 1u].opcode)) {
                return DB_X64_ARM64_PLAN_BAD_GROUPING;
            }
        }

        if (db_x64_arm64_plan_terminal_internal(current->opcode)) {
            if (has_terminal || index + 1u != selection_count) {
                return DB_X64_ARM64_PLAN_TERMINATOR_ORDER;
            }
            has_terminal = true;
        }

        required += expansion;
        if (required > UINT32_MAX) {
            return DB_X64_ARM64_PLAN_COUNT_OVERFLOW;
        }
    }

    if ((uint64_t)record_capacity < required) {
        return DB_X64_ARM64_PLAN_OUTPUT_CAPACITY;
    }

    for (index = 0u; index < selection_count; ++index) {
        const uint32_t expansion =
            db_x64_arm64_plan_expansion_internal(&selections[index]);
        db_x64_arm64_plan_emit_internal(&selections[index], index,
                                        &records[cursor]);
        cursor += expansion;
    }

    if (has_terminal) terminal_helper = records[cursor - 1u].helper;
    completed.source_rip = selections[0].source_rip;
    completed.selection_count = selection_count;
    completed.plan_count = cursor;
    completed.required_capacity = (uint32_t)required;
    completed.terminal_helper = terminal_helper;
    completed.has_terminal = has_terminal ? UINT8_C(1) : UINT8_C(0);
    for (index = 0u; index < sizeof(completed.reserved); ++index) {
        completed.reserved[index] = 0u;
    }
    *summary = completed;
    return DB_X64_ARM64_PLAN_OK;
}

const char *db_x64_arm64_plan_status_name(db_x64_arm64_plan_status status)
{
    static const char *const names[] = {
        "ok", "null argument", "buffer overlap", "bad selection count",
        "bad selection record", "bad selection grouping",
        "terminator order invalid", "insufficient output capacity",
        "plan count overflow", "guest state offset overflow"
    };
    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 ARM64 plan status";
    }
    return names[(size_t)status];
}

const char *db_x64_arm64_plan_opcode_name(db_x64_arm64_plan_opcode opcode)
{
    static const char *const names[] = {
        "nop", "load state u64", "store state u64", "movz u16",
        "movk u16", "eor register", "call helper", "tail helper"
    };
    if ((size_t)opcode >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 ARM64 plan opcode";
    }
    return names[(size_t)opcode];
}

const char *db_x64_arm64_helper_name(db_x64_arm64_helper helper)
{
    static const char *const names[] = {
        "none", "form guest address", "read guest u64", "write guest u64",
        "push guest u64", "pop guest u64", "update logic flags",
        "test guest condition", "dispatch direct", "dispatch target",
        "dispatch conditional", "raise trap"
    };
    if ((size_t)helper >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 ARM64 helper";
    }
    return names[(size_t)helper];
}
