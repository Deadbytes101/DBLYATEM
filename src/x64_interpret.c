#include "dblyat/x64_interpret.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DB_X64_ARITH_FLAGS \
    (DB_X64_RFLAGS_CF | DB_X64_RFLAGS_PF | DB_X64_RFLAGS_AF | \
     DB_X64_RFLAGS_ZF | DB_X64_RFLAGS_SF | DB_X64_RFLAGS_OF)

static bool overlap(const void *a, size_t an, const void *b, size_t bn)
{
    const uintptr_t x = (uintptr_t)a;
    const uintptr_t y = (uintptr_t)b;

    if (an == 0u || bn == 0u) {
        return false;
    }
    return x <= y ? y - x < an : x - y < bn;
}

static bool canonical(uint64_t value)
{
    const uint64_t upper = value >> 48u;
    const uint64_t sign = (value >> 47u) & 1u;

    return value != 0u &&
           ((sign == 0u && upper == 0u) ||
            (sign != 0u && upper == UINT64_C(0xffff)));
}

static bool empty_fields(const db_x64_instruction *ins)
{
    return ins->immediate == 0u &&
           ins->destination_register == DB_X64_REGISTER_NONE &&
           ins->source_register == DB_X64_REGISTER_NONE;
}

static bool relative_fits(int64_t value, bool short_form)
{
    return short_form
               ? value >= INT8_MIN && value <= INT8_MAX
               : value >= INT32_MIN && value <= INT32_MAX;
}

static bool instruction_valid(const db_x64_instruction *ins)
{
    if (ins->length == 0u ||
        ins->length > DB_X64_DECODE_MAX_LENGTH ||
        ins->reserved[0] != 0u ||
        ins->reserved[1] != 0u ||
        ins->reserved[2] != 0u ||
        (ins->rex != 0u &&
         (ins->rex < UINT8_C(0x40) || ins->rex > UINT8_C(0x4f)))) {
        return false;
    }

    switch (ins->kind) {
    case DB_X64_INSTRUCTION_NOP:
    case DB_X64_INSTRUCTION_RET:
    case DB_X64_INSTRUCTION_INT3:
        return ins->length == 1u && ins->rex == 0u &&
               ins->relative == 0 &&
               ins->condition == DB_X64_CONDITION_NONE &&
               empty_fields(ins);

    case DB_X64_INSTRUCTION_UD2:
    case DB_X64_INSTRUCTION_SYSCALL:
        return ins->length == 2u && ins->rex == 0u &&
               ins->relative == 0 &&
               ins->condition == DB_X64_CONDITION_NONE &&
               empty_fields(ins);

    case DB_X64_INSTRUCTION_CALL_REL32:
    case DB_X64_INSTRUCTION_JMP_REL32:
        return ins->length == 5u && ins->rex == 0u &&
               relative_fits(ins->relative, false) &&
               ins->condition == DB_X64_CONDITION_NONE &&
               empty_fields(ins);

    case DB_X64_INSTRUCTION_JMP_REL8:
        return ins->length == 2u && ins->rex == 0u &&
               relative_fits(ins->relative, true) &&
               ins->condition == DB_X64_CONDITION_NONE &&
               empty_fields(ins);

    case DB_X64_INSTRUCTION_JCC_REL8:
    case DB_X64_INSTRUCTION_JCC_REL32:
        return ins->length ==
                   (uint8_t)(ins->kind == DB_X64_INSTRUCTION_JCC_REL8
                                 ? 2u
                                 : 6u) &&
               ins->rex == 0u &&
               relative_fits(ins->relative,
                             ins->kind == DB_X64_INSTRUCTION_JCC_REL8) &&
               ins->condition < 16u &&
               empty_fields(ins);

    case DB_X64_INSTRUCTION_MOV_R64_IMM64:
        return ins->length == 10u &&
               (ins->rex & UINT8_C(0x08)) != 0u &&
               ins->relative == 0 &&
               ins->destination_register < 16u &&
               ins->source_register == DB_X64_REGISTER_NONE &&
               ins->condition == DB_X64_CONDITION_NONE;

    case DB_X64_INSTRUCTION_MOV_R64_R64:
    case DB_X64_INSTRUCTION_XOR_R64_R64:
        return ins->length == 3u &&
               (ins->rex & UINT8_C(0x08)) != 0u &&
               ins->immediate == 0u &&
               ins->relative == 0 &&
               ins->destination_register < 16u &&
               ins->source_register < 16u &&
               ins->condition == DB_X64_CONDITION_NONE;

    case DB_X64_INSTRUCTION_XOR_R32_R32:
        return ins->length == (uint8_t)(ins->rex == 0u ? 2u : 3u) &&
               (ins->rex & UINT8_C(0x08)) == 0u &&
               ins->immediate == 0u &&
               ins->relative == 0 &&
               ins->destination_register < 16u &&
               ins->source_register < 16u &&
               ins->condition == DB_X64_CONDITION_NONE;
    }

    return false;
}

static db_x64_interpret_status add_unsigned(uint64_t base,
                                            uint64_t value,
                                            uint64_t *out)
{
    uint64_t planned;

    if (UINT64_MAX - base < value) {
        return DB_X64_INTERPRET_ADDRESS_OVERFLOW;
    }
    planned = base + value;
    if (!canonical(planned)) {
        return DB_X64_INTERPRET_NONCANONICAL_ADDRESS;
    }
    *out = planned;
    return DB_X64_INTERPRET_OK;
}

static db_x64_interpret_status add_relative(uint64_t base,
                                            int64_t value,
                                            uint64_t *out)
{
    uint64_t planned;

    if (value >= 0) {
        return add_unsigned(base, (uint64_t)value, out);
    }

    planned = (uint64_t)(-(value + 1)) + UINT64_C(1);
    if (base < planned) {
        return DB_X64_INTERPRET_ADDRESS_OVERFLOW;
    }
    planned = base - planned;
    if (!canonical(planned)) {
        return DB_X64_INTERPRET_NONCANONICAL_ADDRESS;
    }
    *out = planned;
    return DB_X64_INTERPRET_OK;
}

static bool parity_even(uint8_t value)
{
    value ^= (uint8_t)(value >> 4u);
    value &= UINT8_C(0x0f);
    return ((UINT16_C(0x6996) >> value) & 1u) == 0u;
}

static uint64_t xor_flags(uint64_t old, uint64_t result, bool width64)
{
    uint64_t flags = old & ~DB_X64_ARITH_FLAGS;
    const uint64_t sign =
        width64 ? UINT64_C(0x8000000000000000)
                : UINT64_C(0x80000000);

    if (result == 0u) {
        flags |= DB_X64_RFLAGS_ZF;
    }
    if ((result & sign) != 0u) {
        flags |= DB_X64_RFLAGS_SF;
    }
    if (parity_even((uint8_t)result)) {
        flags |= DB_X64_RFLAGS_PF;
    }
    return flags;
}

static bool condition_true(uint8_t c, uint64_t f)
{
    const bool cf = (f & DB_X64_RFLAGS_CF) != 0u;
    const bool pf = (f & DB_X64_RFLAGS_PF) != 0u;
    const bool zf = (f & DB_X64_RFLAGS_ZF) != 0u;
    const bool sf = (f & DB_X64_RFLAGS_SF) != 0u;
    const bool of = (f & DB_X64_RFLAGS_OF) != 0u;

    switch (c) {
    case 0u: return of;
    case 1u: return !of;
    case 2u: return cf;
    case 3u: return !cf;
    case 4u: return zf;
    case 5u: return !zf;
    case 6u: return cf || zf;
    case 7u: return !cf && !zf;
    case 8u: return sf;
    case 9u: return !sf;
    case 10u: return pf;
    case 11u: return !pf;
    case 12u: return sf != of;
    case 13u: return sf == of;
    case 14u: return zf || sf != of;
    case 15u: return !zf && sf == of;
    }
    return false;
}

db_x64_interpret_status db_x64_interpret_one(
    const db_x64_instruction *ins,
    db_x64_scalar_state *state,
    db_x64_step_summary *summary)
{
    db_x64_scalar_state next;
    db_x64_step_summary done;
    db_x64_interpret_status status;
    uint64_t fallthrough;

    if (ins == NULL || state == NULL || summary == NULL) {
        return DB_X64_INTERPRET_NULL_ARGUMENT;
    }
    if (overlap(ins, sizeof(*ins), state, sizeof(*state)) ||
        overlap(ins, sizeof(*ins), summary, sizeof(*summary)) ||
        overlap(state, sizeof(*state), summary, sizeof(*summary))) {
        return DB_X64_INTERPRET_BUFFER_OVERLAP;
    }
    if (!canonical(state->rip) ||
        (state->rflags & DB_X64_RFLAGS_FIXED_ONE) == 0u) {
        return DB_X64_INTERPRET_BAD_STATE;
    }
    if (!instruction_valid(ins)) {
        return DB_X64_INTERPRET_BAD_INSTRUCTION;
    }

    status = add_unsigned(state->rip, ins->length, &fallthrough);
    if (status != DB_X64_INTERPRET_OK) {
        return status;
    }

    next = *state;
    done.source_rip = state->rip;
    done.fallthrough_rip = fallthrough;
    done.next_rip = fallthrough;
    done.previous_rflags = state->rflags;
    done.next_rflags = state->rflags;
    done.previous_destination = 0u;
    done.next_destination = 0u;
    done.kind = ins->kind;
    done.instruction_length = ins->length;
    done.destination_register = DB_X64_REGISTER_NONE;
    done.branch_taken = 0u;
    done.reserved = 0u;

    switch (ins->kind) {
    case DB_X64_INSTRUCTION_NOP:
        next.rip = fallthrough;
        break;

    case DB_X64_INSTRUCTION_JMP_REL8:
    case DB_X64_INSTRUCTION_JMP_REL32:
        status = add_relative(fallthrough, ins->relative, &next.rip);
        if (status != DB_X64_INTERPRET_OK) {
            return status;
        }
        done.next_rip = next.rip;
        done.branch_taken = 1u;
        break;

    case DB_X64_INSTRUCTION_JCC_REL8:
    case DB_X64_INSTRUCTION_JCC_REL32:
        next.rip = fallthrough;
        if (condition_true(ins->condition, state->rflags)) {
            status = add_relative(fallthrough, ins->relative, &next.rip);
            if (status != DB_X64_INTERPRET_OK) {
                return status;
            }
            done.next_rip = next.rip;
            done.branch_taken = 1u;
        }
        break;

    case DB_X64_INSTRUCTION_MOV_R64_IMM64:
        done.destination_register = ins->destination_register;
        done.previous_destination =
            state->registers[ins->destination_register];
        done.next_destination = ins->immediate;
        next.registers[ins->destination_register] = ins->immediate;
        next.rip = fallthrough;
        break;

    case DB_X64_INSTRUCTION_MOV_R64_R64:
        done.destination_register = ins->destination_register;
        done.previous_destination =
            state->registers[ins->destination_register];
        done.next_destination = state->registers[ins->source_register];
        next.registers[ins->destination_register] =
            state->registers[ins->source_register];
        next.rip = fallthrough;
        break;

    case DB_X64_INSTRUCTION_XOR_R32_R32:
    {
        const uint32_t result =
            (uint32_t)state->registers[ins->destination_register] ^
            (uint32_t)state->registers[ins->source_register];

        done.destination_register = ins->destination_register;
        done.previous_destination =
            state->registers[ins->destination_register];
        done.next_destination = result;
        next.registers[ins->destination_register] = result;
        next.rflags = xor_flags(state->rflags, result, false);
        next.rip = fallthrough;
        done.next_rflags = next.rflags;
        break;
    }

    case DB_X64_INSTRUCTION_XOR_R64_R64:
    {
        const uint64_t result =
            state->registers[ins->destination_register] ^
            state->registers[ins->source_register];

        done.destination_register = ins->destination_register;
        done.previous_destination =
            state->registers[ins->destination_register];
        done.next_destination = result;
        next.registers[ins->destination_register] = result;
        next.rflags = xor_flags(state->rflags, result, true);
        next.rip = fallthrough;
        done.next_rflags = next.rflags;
        break;
    }

    case DB_X64_INSTRUCTION_CALL_REL32:
    case DB_X64_INSTRUCTION_RET:
    case DB_X64_INSTRUCTION_INT3:
    case DB_X64_INSTRUCTION_UD2:
    case DB_X64_INSTRUCTION_SYSCALL:
        return DB_X64_INTERPRET_UNSUPPORTED_INSTRUCTION;
    }

    *state = next;
    *summary = done;
    return DB_X64_INTERPRET_OK;
}

const char *db_x64_interpret_status_name(db_x64_interpret_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad scalar state",
        "bad decoded instruction",
        "noncanonical address",
        "address overflow",
        "unsupported instruction semantics"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 interpret status";
    }
    return names[(size_t)status];
}
