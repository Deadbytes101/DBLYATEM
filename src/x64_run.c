#include "dblyat/x64_run.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_x64_run_overlap(const void *left,
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

static bool db_x64_run_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return upper == (sign != 0u ? UINT64_C(0xffff) : UINT64_C(0));
}

static bool db_x64_run_code_reserved_zero(const db_x64_code_view *code)
{
    size_t index;

    for (index = 0u; index < sizeof(code->reserved); ++index) {
        if (code->reserved[index] != 0u) {
            return false;
        }
    }

    return true;
}

static bool db_x64_run_code_valid(const db_x64_code_view *code)
{
    uint64_t last_address;

    if (code->bytes == NULL ||
        code->size == 0u ||
        !db_x64_run_canonical(code->guest_base) ||
        !db_x64_run_code_reserved_zero(code) ||
        db_x64_run_overlap(code->bytes, code->size,
                           code, sizeof(*code))) {
        return false;
    }

    if (code->size - 1u > (size_t)(UINT64_MAX - code->guest_base)) {
        return false;
    }

    last_address = code->guest_base + (uint64_t)(code->size - 1u);
    return db_x64_run_canonical(last_address);
}

static bool db_x64_run_aliases(const db_x64_code_view *code,
                               const db_x64_scalar_state *state,
                               const db_x64_memory_view *memory,
                               const db_x64_run_summary *summary)
{
    if (db_x64_run_overlap(code, sizeof(*code),
                           state, sizeof(*state)) ||
        db_x64_run_overlap(code, sizeof(*code),
                           memory, sizeof(*memory)) ||
        db_x64_run_overlap(code, sizeof(*code),
                           summary, sizeof(*summary)) ||
        db_x64_run_overlap(state, sizeof(*state),
                           memory, sizeof(*memory)) ||
        db_x64_run_overlap(state, sizeof(*state),
                           summary, sizeof(*summary)) ||
        db_x64_run_overlap(memory, sizeof(*memory),
                           summary, sizeof(*summary))) {
        return true;
    }

    if (db_x64_run_overlap(code->bytes, code->size,
                           state, sizeof(*state)) ||
        db_x64_run_overlap(code->bytes, code->size,
                           memory, sizeof(*memory)) ||
        db_x64_run_overlap(code->bytes, code->size,
                           summary, sizeof(*summary))) {
        return true;
    }

    return db_x64_run_overlap(memory->bytes, memory->size,
                              code, sizeof(*code)) ||
           db_x64_run_overlap(memory->bytes, memory->size,
                              state, sizeof(*state)) ||
           db_x64_run_overlap(memory->bytes, memory->size,
                              memory, sizeof(*memory)) ||
           db_x64_run_overlap(memory->bytes, memory->size,
                              summary, sizeof(*summary));
}

static bool db_x64_run_locate(const db_x64_code_view *code,
                              uint64_t rip,
                              const uint8_t **bytes,
                              size_t *available_size)
{
    uint64_t delta;
    size_t offset;
    size_t remaining;

    if (!db_x64_run_canonical(rip) || rip < code->guest_base) {
        return false;
    }

    delta = rip - code->guest_base;
    if (delta > (uint64_t)SIZE_MAX || (size_t)delta >= code->size) {
        return false;
    }

    offset = (size_t)delta;
    remaining = code->size - offset;
    *bytes = code->bytes + offset;
    *available_size =
        remaining < (size_t)DB_X64_DECODE_MAX_LENGTH
            ? remaining
            : (size_t)DB_X64_DECODE_MAX_LENGTH;
    return true;
}

static void db_x64_run_reset_step_status(db_x64_run_summary *completed)
{
    completed->attempted_kind = DB_X64_RUN_STEP_NONE;
    completed->scalar_decode_status = DB_X64_DECODE_OK;
    completed->memory_decode_status = DB_X64_MEMORY_INSTRUCTION_OK;
    completed->scalar_execute_status = DB_X64_EXECUTE_OK;
    completed->memory_execute_status = DB_X64_MEMORY_EXECUTE_OK;
    completed->stack_execute_status = DB_X64_STACK_EXECUTE_OK;
}

static bool db_x64_run_scalar_is_stack(
    const db_x64_instruction *instruction)
{
    return instruction->kind == DB_X64_INSTRUCTION_CALL_REL32 ||
           instruction->kind == DB_X64_INSTRUCTION_RET;
}

db_x64_run_status db_x64_run_steps(
    const db_x64_code_view *code,
    uint32_t step_budget,
    db_x64_scalar_state *state,
    db_x64_memory_view *memory,
    db_x64_run_summary *summary)
{
    db_x64_run_summary completed = {0};
    db_x64_execute_summary scalar_summary;
    db_x64_memory_execute_summary memory_summary;
    db_x64_stack_execute_summary stack_summary;
    db_x64_instruction scalar_instruction;
    db_x64_memory_instruction memory_instruction;
    const uint8_t *instruction_bytes;
    size_t available_size;
    uint32_t step;

    if (code == NULL || state == NULL || memory == NULL || summary == NULL) {
        return DB_X64_RUN_NULL_ARGUMENT;
    }

    if (db_x64_run_aliases(code, state, memory, summary)) {
        return DB_X64_RUN_BUFFER_OVERLAP;
    }

    if (!db_x64_run_code_valid(code)) {
        return DB_X64_RUN_BAD_CODE_VIEW;
    }

    if (step_budget == 0u) {
        return DB_X64_RUN_BAD_STEP_BUDGET;
    }

    completed.source_rip = state->rip;
    completed.final_rip = state->rip;
    completed.step_budget = step_budget;
    completed.completed_steps = 0u;
    completed.stop_reason = DB_X64_RUN_STOP_STEP_BUDGET;
    completed.last_completed_kind = DB_X64_RUN_STEP_NONE;
    db_x64_run_reset_step_status(&completed);

    for (step = 0u; step < step_budget; ++step) {
        db_x64_run_reset_step_status(&completed);

        if (!db_x64_run_locate(code,
                               state->rip,
                               &instruction_bytes,
                               &available_size)) {
            completed.stop_reason = DB_X64_RUN_STOP_CODE_OUT_OF_BOUNDS;
            break;
        }

        completed.scalar_decode_status =
            db_x64_decode_one(instruction_bytes,
                              available_size,
                              &scalar_instruction);

        if (completed.scalar_decode_status == DB_X64_DECODE_OK) {
            if (db_x64_run_scalar_is_stack(&scalar_instruction)) {
                completed.attempted_kind = DB_X64_RUN_STEP_STACK;
                completed.stack_execute_status =
                    db_x64_stack_execute_one(instruction_bytes,
                                             available_size,
                                             state,
                                             memory,
                                             &stack_summary);
                if (completed.stack_execute_status !=
                    DB_X64_STACK_EXECUTE_OK) {
                    completed.stop_reason = DB_X64_RUN_STOP_EXECUTE_FAILED;
                    break;
                }
            } else {
                completed.attempted_kind = DB_X64_RUN_STEP_SCALAR;
                completed.scalar_execute_status =
                    db_x64_execute_one(instruction_bytes,
                                       available_size,
                                       state,
                                       &scalar_summary);
                if (completed.scalar_execute_status != DB_X64_EXECUTE_OK) {
                    completed.stop_reason = DB_X64_RUN_STOP_EXECUTE_FAILED;
                    break;
                }
            }
        } else {
            completed.memory_decode_status =
                db_x64_memory_instruction_decode_one(instruction_bytes,
                                                     available_size,
                                                     &memory_instruction);
            if (completed.memory_decode_status !=
                DB_X64_MEMORY_INSTRUCTION_OK) {
                completed.stop_reason = DB_X64_RUN_STOP_DECODE_FAILED;
                break;
            }

            completed.attempted_kind = DB_X64_RUN_STEP_MEMORY;
            completed.memory_execute_status =
                db_x64_memory_execute_one(instruction_bytes,
                                          available_size,
                                          state,
                                          memory,
                                          &memory_summary);
            if (completed.memory_execute_status !=
                DB_X64_MEMORY_EXECUTE_OK) {
                completed.stop_reason = DB_X64_RUN_STOP_EXECUTE_FAILED;
                break;
            }
        }

        ++completed.completed_steps;
        completed.last_completed_kind = completed.attempted_kind;
        completed.attempted_kind = DB_X64_RUN_STEP_NONE;
        completed.final_rip = state->rip;
    }

    completed.final_rip = state->rip;
    *summary = completed;
    return DB_X64_RUN_OK;
}

const char *db_x64_run_status_name(db_x64_run_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad code view",
        "bad step budget"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 run status";
    }

    return names[(size_t)status];
}

const char *db_x64_run_stop_reason_name(db_x64_run_stop_reason reason)
{
    static const char *const names[] = {
        "step budget exhausted",
        "code address out of bounds",
        "instruction decode failed",
        "instruction execution failed"
    };

    if ((size_t)reason >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 run stop reason";
    }

    return names[(size_t)reason];
}

const char *db_x64_run_step_kind_name(db_x64_run_step_kind kind)
{
    static const char *const names[] = {
        "none",
        "scalar",
        "memory",
        "stack"
    };

    if ((size_t)kind >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 run step kind";
    }

    return names[(size_t)kind];
}
