#include "dblyat/x64_address.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_x64_address_overlap(const void *left,
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

static bool db_x64_address_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return upper == (sign != 0u ? UINT64_C(0xffff) : UINT64_C(0));
}

static bool db_x64_address_register(uint8_t register_id)
{
    return register_id < UINT8_C(16) ||
           register_id == DB_X64_REGISTER_NONE;
}

static bool db_x64_address_scale(uint8_t scale)
{
    return scale == UINT8_C(1) ||
           scale == UINT8_C(2) ||
           scale == UINT8_C(4) ||
           scale == UINT8_C(8);
}

static bool db_x64_address_reserved_zero(
    const db_x64_address_expression *expression)
{
    size_t index;

    for (index = 0u; index < sizeof(expression->reserved); ++index) {
        if (expression->reserved[index] != 0u) {
            return false;
        }
    }

    return true;
}

static bool db_x64_address_expression_valid(
    const db_x64_address_expression *expression)
{
    if ((size_t)expression->mode > (size_t)DB_X64_ADDRESS_ABSOLUTE ||
        !db_x64_address_register(expression->base_register) ||
        !db_x64_address_register(expression->index_register) ||
        !db_x64_address_scale(expression->scale) ||
        !db_x64_address_reserved_zero(expression)) {
        return false;
    }

    if (expression->index_register == DB_X64_REGISTER_NONE &&
        expression->scale != UINT8_C(1)) {
        return false;
    }

    switch (expression->mode) {
    case DB_X64_ADDRESS_BASE_INDEX:
        if (expression->base_register == DB_X64_REGISTER_NONE &&
            expression->index_register == DB_X64_REGISTER_NONE) {
            return false;
        }
        return expression->instruction_length == 0u;

    case DB_X64_ADDRESS_RIP_RELATIVE:
        return expression->base_register == DB_X64_REGISTER_NONE &&
               expression->index_register == DB_X64_REGISTER_NONE &&
               expression->scale == UINT8_C(1) &&
               expression->instruction_length >= UINT8_C(1) &&
               expression->instruction_length <=
                   (uint8_t)DB_X64_DECODE_MAX_LENGTH;

    case DB_X64_ADDRESS_ABSOLUTE:
        return expression->base_register == DB_X64_REGISTER_NONE &&
               expression->index_register == DB_X64_REGISTER_NONE &&
               expression->scale == UINT8_C(1) &&
               expression->instruction_length == 0u;

    default:
        return false;
    }
}

db_x64_address_status db_x64_address_plan(
    const db_x64_address_expression *expression,
    const db_x64_scalar_state *state,
    db_x64_address_summary *summary)
{
    db_x64_address_summary planned;
    uint64_t base_value = 0u;
    uint64_t index_value = 0u;
    uint64_t scaled_index = 0u;
    uint64_t linear_address;

    if (expression == NULL || state == NULL || summary == NULL) {
        return DB_X64_ADDRESS_NULL_ARGUMENT;
    }

    if (db_x64_address_overlap(expression, sizeof(*expression),
                               state, sizeof(*state)) ||
        db_x64_address_overlap(expression, sizeof(*expression),
                               summary, sizeof(*summary)) ||
        db_x64_address_overlap(state, sizeof(*state),
                               summary, sizeof(*summary))) {
        return DB_X64_ADDRESS_BUFFER_OVERLAP;
    }

    if (!db_x64_address_expression_valid(expression)) {
        return DB_X64_ADDRESS_BAD_EXPRESSION;
    }

    if (expression->mode == DB_X64_ADDRESS_RIP_RELATIVE) {
        if (!db_x64_address_canonical(state->rip)) {
            return DB_X64_ADDRESS_NONCANONICAL_ADDRESS;
        }

        base_value = state->rip + (uint64_t)expression->instruction_length;
        if (!db_x64_address_canonical(base_value)) {
            return DB_X64_ADDRESS_NONCANONICAL_ADDRESS;
        }
    } else if (expression->mode == DB_X64_ADDRESS_BASE_INDEX &&
               expression->base_register != DB_X64_REGISTER_NONE) {
        base_value = state->registers[expression->base_register];
    }

    if (expression->mode == DB_X64_ADDRESS_BASE_INDEX &&
        expression->index_register != DB_X64_REGISTER_NONE) {
        index_value = state->registers[expression->index_register];
        scaled_index = index_value * (uint64_t)expression->scale;
    }

    linear_address = base_value + scaled_index +
                     (uint64_t)expression->displacement;
    if (!db_x64_address_canonical(linear_address)) {
        return DB_X64_ADDRESS_NONCANONICAL_ADDRESS;
    }

    planned.base_value = base_value;
    planned.index_value = index_value;
    planned.scaled_index = scaled_index;
    planned.linear_address = linear_address;
    planned.displacement = expression->displacement;
    planned.mode = expression->mode;
    planned.base_register = expression->base_register;
    planned.index_register = expression->index_register;
    planned.scale = expression->scale;
    planned.instruction_length = expression->instruction_length;
    planned.reserved[0] = 0u;
    planned.reserved[1] = 0u;
    planned.reserved[2] = 0u;
    planned.reserved[3] = 0u;

    *summary = planned;
    return DB_X64_ADDRESS_OK;
}

const char *db_x64_address_status_name(db_x64_address_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad address expression",
        "noncanonical address"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 address status";
    }

    return names[(size_t)status];
}
