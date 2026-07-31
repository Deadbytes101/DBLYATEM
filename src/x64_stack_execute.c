#include "dblyat/x64_stack_execute.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_x64_stack_execute_overlap(const void *left,
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

static bool db_x64_stack_execute_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return upper == (sign != 0u ? UINT64_C(0xffff) : UINT64_C(0));
}

static bool db_x64_stack_execute_canonical_nonzero(uint64_t address)
{
    return address != 0u && db_x64_stack_execute_canonical(address);
}

static bool db_x64_stack_execute_state_valid(
    const db_x64_scalar_state *state)
{
    return db_x64_stack_execute_canonical_nonzero(state->rip) &&
           db_x64_stack_execute_canonical_nonzero(
               state->registers[DB_X64_STACK_POINTER_REGISTER]) &&
           (state->rflags & DB_X64_RFLAGS_FIXED_ONE) != 0u;
}

static bool db_x64_stack_execute_aliases(
    const uint8_t *bytes,
    size_t available_size,
    const db_x64_scalar_state *state,
    const db_x64_memory_view *view,
    const db_x64_stack_execute_summary *summary)
{
    if (db_x64_stack_execute_overlap(bytes, available_size,
                                     state, sizeof(*state)) ||
        db_x64_stack_execute_overlap(bytes, available_size,
                                     view, sizeof(*view)) ||
        db_x64_stack_execute_overlap(bytes, available_size,
                                     summary, sizeof(*summary)) ||
        db_x64_stack_execute_overlap(state, sizeof(*state),
                                     view, sizeof(*view)) ||
        db_x64_stack_execute_overlap(state, sizeof(*state),
                                     summary, sizeof(*summary)) ||
        db_x64_stack_execute_overlap(view, sizeof(*view),
                                     summary, sizeof(*summary))) {
        return true;
    }

    return db_x64_stack_execute_overlap(view->bytes, view->size,
                                        state, sizeof(*state)) ||
           db_x64_stack_execute_overlap(view->bytes, view->size,
                                        view, sizeof(*view)) ||
           db_x64_stack_execute_overlap(view->bytes, view->size,
                                        summary, sizeof(*summary));
}

static db_x64_stack_execute_status db_x64_stack_execute_add_unsigned(
    uint64_t base,
    uint64_t value,
    uint64_t *result)
{
    uint64_t planned;

    if (UINT64_MAX - base < value) {
        return DB_X64_STACK_EXECUTE_ADDRESS_OVERFLOW;
    }

    planned = base + value;
    if (!db_x64_stack_execute_canonical_nonzero(planned)) {
        return DB_X64_STACK_EXECUTE_NONCANONICAL_ADDRESS;
    }

    *result = planned;
    return DB_X64_STACK_EXECUTE_OK;
}

static db_x64_stack_execute_status db_x64_stack_execute_add_relative(
    uint64_t base,
    int64_t relative,
    uint64_t *result)
{
    uint64_t magnitude;
    uint64_t planned;

    if (relative >= 0) {
        return db_x64_stack_execute_add_unsigned(base,
                                                 (uint64_t)relative,
                                                 result);
    }

    magnitude = (uint64_t)(-(relative + 1)) + UINT64_C(1);
    if (base < magnitude) {
        return DB_X64_STACK_EXECUTE_ADDRESS_OVERFLOW;
    }

    planned = base - magnitude;
    if (!db_x64_stack_execute_canonical_nonzero(planned)) {
        return DB_X64_STACK_EXECUTE_NONCANONICAL_ADDRESS;
    }

    *result = planned;
    return DB_X64_STACK_EXECUTE_OK;
}

static void db_x64_stack_execute_finish_summary(
    db_x64_stack_execute_summary *completed,
    const db_x64_instruction *instruction,
    const db_x64_memory_access_summary *access,
    const db_x64_scalar_state *state,
    uint64_t fallthrough_rip,
    uint64_t next_rip,
    uint64_t previous_rsp,
    uint64_t next_rsp,
    uint64_t stack_value,
    bool memory_written)
{
    size_t index;

    completed->instruction = *instruction;
    completed->access = *access;
    completed->source_rip = state->rip;
    completed->fallthrough_rip = fallthrough_rip;
    completed->next_rip = next_rip;
    completed->previous_rsp = previous_rsp;
    completed->next_rsp = next_rsp;
    completed->stack_value = stack_value;
    completed->previous_rflags = state->rflags;
    completed->next_rflags = state->rflags;
    completed->memory_written = memory_written ? UINT8_C(1) : UINT8_C(0);

    for (index = 0u; index < sizeof(completed->reserved); ++index) {
        completed->reserved[index] = 0u;
    }
}

db_x64_stack_execute_status db_x64_stack_execute_one(
    const uint8_t *bytes,
    size_t available_size,
    db_x64_scalar_state *state,
    db_x64_memory_view *view,
    db_x64_stack_execute_summary *summary)
{
    db_x64_stack_execute_summary completed;
    db_x64_memory_access_summary access;
    db_x64_scalar_state planned_state;
    db_x64_instruction instruction;
    db_x64_decode_status decode_status;
    db_x64_memory_view_status memory_status;
    db_x64_stack_execute_status status;
    uint64_t fallthrough_rip;
    uint64_t next_rip;
    uint64_t previous_rsp;
    uint64_t next_rsp;
    uint64_t stack_value;

    if (bytes == NULL || state == NULL || view == NULL || summary == NULL) {
        return DB_X64_STACK_EXECUTE_NULL_ARGUMENT;
    }

    if (db_x64_stack_execute_aliases(bytes,
                                     available_size,
                                     state,
                                     view,
                                     summary)) {
        return DB_X64_STACK_EXECUTE_BUFFER_OVERLAP;
    }

    if (!db_x64_stack_execute_state_valid(state)) {
        return DB_X64_STACK_EXECUTE_BAD_STATE;
    }

    decode_status = db_x64_decode_one(bytes, available_size, &instruction);
    if (decode_status != DB_X64_DECODE_OK) {
        return DB_X64_STACK_EXECUTE_DECODE_FAILED;
    }

    if (instruction.kind != DB_X64_INSTRUCTION_CALL_REL32 &&
        instruction.kind != DB_X64_INSTRUCTION_RET) {
        return DB_X64_STACK_EXECUTE_UNSUPPORTED_INSTRUCTION;
    }

    status = db_x64_stack_execute_add_unsigned(state->rip,
                                               instruction.length,
                                               &fallthrough_rip);
    if (status != DB_X64_STACK_EXECUTE_OK) {
        return status;
    }

    previous_rsp = state->registers[DB_X64_STACK_POINTER_REGISTER];
    planned_state = *state;

    if (instruction.kind == DB_X64_INSTRUCTION_CALL_REL32) {
        status = db_x64_stack_execute_add_relative(fallthrough_rip,
                                                   instruction.relative,
                                                   &next_rip);
        if (status != DB_X64_STACK_EXECUTE_OK) {
            return status;
        }

        if (previous_rsp < UINT64_C(8)) {
            return DB_X64_STACK_EXECUTE_ADDRESS_OVERFLOW;
        }

        next_rsp = previous_rsp - UINT64_C(8);
        if (!db_x64_stack_execute_canonical_nonzero(next_rsp)) {
            return DB_X64_STACK_EXECUTE_NONCANONICAL_ADDRESS;
        }

        stack_value = fallthrough_rip;
        memory_status = db_x64_memory_view_write_u64(view,
                                                     next_rsp,
                                                     stack_value,
                                                     &access);
        if (memory_status != DB_X64_MEMORY_VIEW_OK) {
            return DB_X64_STACK_EXECUTE_MEMORY_FAILED;
        }

        planned_state.registers[DB_X64_STACK_POINTER_REGISTER] = next_rsp;
        planned_state.rip = next_rip;
        db_x64_stack_execute_finish_summary(&completed,
                                            &instruction,
                                            &access,
                                            state,
                                            fallthrough_rip,
                                            next_rip,
                                            previous_rsp,
                                            next_rsp,
                                            stack_value,
                                            true);
    } else {
        status = db_x64_stack_execute_add_unsigned(previous_rsp,
                                                   UINT64_C(8),
                                                   &next_rsp);
        if (status != DB_X64_STACK_EXECUTE_OK) {
            return status;
        }

        memory_status = db_x64_memory_view_read_u64(view,
                                                    previous_rsp,
                                                    &stack_value,
                                                    &access);
        if (memory_status != DB_X64_MEMORY_VIEW_OK) {
            return DB_X64_STACK_EXECUTE_MEMORY_FAILED;
        }

        if (!db_x64_stack_execute_canonical_nonzero(stack_value)) {
            return DB_X64_STACK_EXECUTE_NONCANONICAL_ADDRESS;
        }

        next_rip = stack_value;
        planned_state.registers[DB_X64_STACK_POINTER_REGISTER] = next_rsp;
        planned_state.rip = next_rip;
        db_x64_stack_execute_finish_summary(&completed,
                                            &instruction,
                                            &access,
                                            state,
                                            fallthrough_rip,
                                            next_rip,
                                            previous_rsp,
                                            next_rsp,
                                            stack_value,
                                            false);
    }

    *state = planned_state;
    *summary = completed;
    return DB_X64_STACK_EXECUTE_OK;
}

const char *db_x64_stack_execute_status_name(
    db_x64_stack_execute_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad scalar state",
        "x86-64 decode failed",
        "unsupported stack instruction",
        "noncanonical address",
        "address overflow",
        "guest stack memory access failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 stack execute status";
    }

    return names[(size_t)status];
}
