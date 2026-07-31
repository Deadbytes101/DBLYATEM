#include "dblyat/x64_memory_execute.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_x64_memory_execute_overlap(const void *left,
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

static bool db_x64_memory_execute_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return upper == (sign != 0u ? UINT64_C(0xffff) : UINT64_C(0));
}

static bool db_x64_memory_execute_state_valid(
    const db_x64_scalar_state *state)
{
    return state->rip != 0u &&
           db_x64_memory_execute_canonical(state->rip) &&
           (state->rflags & DB_X64_RFLAGS_FIXED_ONE) != 0u;
}

static bool db_x64_memory_execute_reserved_zero(
    const db_x64_memory_instruction *instruction)
{
    size_t index;

    for (index = 0u; index < sizeof(instruction->reserved); ++index) {
        if (instruction->reserved[index] != 0u) {
            return false;
        }
    }

    for (index = 0u; index < sizeof(instruction->memory.reserved); ++index) {
        if (instruction->memory.reserved[index] != 0u) {
            return false;
        }
    }

    for (index = 0u;
         index < sizeof(instruction->memory.address.reserved);
         ++index) {
        if (instruction->memory.address.reserved[index] != 0u) {
            return false;
        }
    }

    return true;
}

static bool db_x64_memory_execute_instruction_valid(
    const db_x64_memory_instruction *instruction)
{
    if ((size_t)instruction->kind >
            (size_t)DB_X64_MEMORY_INSTRUCTION_STORE_R64 ||
        instruction->register_operand >= UINT8_C(16) ||
        instruction->register_operand !=
            instruction->memory.register_operand ||
        instruction->instruction_length < UINT8_C(3) ||
        instruction->instruction_length >
            (uint8_t)DB_X64_DECODE_MAX_LENGTH ||
        instruction->instruction_length !=
            instruction->memory.instruction_length ||
        (instruction->rex & UINT8_C(0x08)) == 0u ||
        !db_x64_memory_execute_reserved_zero(instruction)) {
        return false;
    }

    if (instruction->kind == DB_X64_MEMORY_INSTRUCTION_LOAD_R64) {
        return instruction->opcode == UINT8_C(0x8b);
    }

    return instruction->opcode == UINT8_C(0x89);
}

static bool db_x64_memory_execute_aliases(
    const uint8_t *bytes,
    size_t available_size,
    const db_x64_scalar_state *state,
    const db_x64_memory_view *view,
    const db_x64_memory_execute_summary *summary)
{
    if (db_x64_memory_execute_overlap(bytes, available_size,
                                      state, sizeof(*state)) ||
        db_x64_memory_execute_overlap(bytes, available_size,
                                      view, sizeof(*view)) ||
        db_x64_memory_execute_overlap(bytes, available_size,
                                      summary, sizeof(*summary)) ||
        db_x64_memory_execute_overlap(state, sizeof(*state),
                                      view, sizeof(*view)) ||
        db_x64_memory_execute_overlap(state, sizeof(*state),
                                      summary, sizeof(*summary)) ||
        db_x64_memory_execute_overlap(view, sizeof(*view),
                                      summary, sizeof(*summary))) {
        return true;
    }

    return db_x64_memory_execute_overlap(view->bytes, view->size,
                                         state, sizeof(*state)) ||
           db_x64_memory_execute_overlap(view->bytes, view->size,
                                         view, sizeof(*view)) ||
           db_x64_memory_execute_overlap(view->bytes, view->size,
                                         summary, sizeof(*summary));
}

db_x64_memory_execute_status db_x64_memory_execute_one(
    const uint8_t *bytes,
    size_t available_size,
    db_x64_scalar_state *state,
    db_x64_memory_view *view,
    db_x64_memory_execute_summary *summary)
{
    db_x64_memory_execute_summary completed;
    db_x64_memory_instruction instruction;
    db_x64_address_summary address;
    db_x64_memory_access_summary access;
    db_x64_scalar_state planned_state;
    db_x64_memory_instruction_status decode_status;
    db_x64_address_status address_status;
    db_x64_memory_view_status memory_status;
    uint64_t next_rip;
    uint64_t previous_register;
    uint64_t next_register;
    uint64_t value;

    if (bytes == NULL || state == NULL || view == NULL || summary == NULL) {
        return DB_X64_MEMORY_EXECUTE_NULL_ARGUMENT;
    }

    if (db_x64_memory_execute_aliases(bytes,
                                      available_size,
                                      state,
                                      view,
                                      summary)) {
        return DB_X64_MEMORY_EXECUTE_BUFFER_OVERLAP;
    }

    if (!db_x64_memory_execute_state_valid(state)) {
        return DB_X64_MEMORY_EXECUTE_BAD_STATE;
    }

    decode_status = db_x64_memory_instruction_decode_one(
        bytes,
        available_size,
        &instruction);
    if (decode_status != DB_X64_MEMORY_INSTRUCTION_OK ||
        !db_x64_memory_execute_instruction_valid(&instruction)) {
        return DB_X64_MEMORY_EXECUTE_DECODE_FAILED;
    }

    if (state->rip >
        UINT64_MAX - (uint64_t)instruction.instruction_length) {
        return DB_X64_MEMORY_EXECUTE_ADDRESS_OVERFLOW;
    }

    next_rip = state->rip + (uint64_t)instruction.instruction_length;
    if (!db_x64_memory_execute_canonical(next_rip)) {
        return DB_X64_MEMORY_EXECUTE_NONCANONICAL_ADDRESS;
    }

    address_status = db_x64_address_plan(&instruction.memory.address,
                                         state,
                                         &address);
    if (address_status != DB_X64_ADDRESS_OK) {
        return DB_X64_MEMORY_EXECUTE_ADDRESS_FAILED;
    }

    planned_state = *state;
    previous_register = state->registers[instruction.register_operand];
    next_register = previous_register;

    if (instruction.kind == DB_X64_MEMORY_INSTRUCTION_LOAD_R64) {
        memory_status = db_x64_memory_view_read_u64(view,
                                                    address.linear_address,
                                                    &value,
                                                    &access);
        if (memory_status != DB_X64_MEMORY_VIEW_OK) {
            return DB_X64_MEMORY_EXECUTE_MEMORY_FAILED;
        }

        planned_state.registers[instruction.register_operand] = value;
        next_register = value;
    } else {
        value = previous_register;
        memory_status = db_x64_memory_view_write_u64(view,
                                                     address.linear_address,
                                                     value,
                                                     &access);
        if (memory_status != DB_X64_MEMORY_VIEW_OK) {
            return DB_X64_MEMORY_EXECUTE_MEMORY_FAILED;
        }
    }

    planned_state.rip = next_rip;

    completed.instruction = instruction;
    completed.address = address;
    completed.access = access;
    completed.source_rip = state->rip;
    completed.next_rip = next_rip;
    completed.previous_register = previous_register;
    completed.next_register = next_register;
    completed.previous_rflags = state->rflags;
    completed.next_rflags = state->rflags;
    completed.register_operand = instruction.register_operand;
    completed.memory_written =
        instruction.kind == DB_X64_MEMORY_INSTRUCTION_STORE_R64
            ? UINT8_C(1)
            : UINT8_C(0);
    completed.reserved[0] = 0u;
    completed.reserved[1] = 0u;
    completed.reserved[2] = 0u;
    completed.reserved[3] = 0u;
    completed.reserved[4] = 0u;
    completed.reserved[5] = 0u;

    *state = planned_state;
    *summary = completed;
    return DB_X64_MEMORY_EXECUTE_OK;
}

const char *db_x64_memory_execute_status_name(
    db_x64_memory_execute_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad scalar state",
        "memory instruction decode failed",
        "effective address planning failed",
        "guest memory access failed",
        "noncanonical next instruction address",
        "next instruction address overflow"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 memory execute status";
    }

    return names[(size_t)status];
}
