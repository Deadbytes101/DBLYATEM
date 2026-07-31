#include "dblyat/x64_address.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define CHECK(value) do { if (!(value)) return 0; } while (0)

static void fill(void *data, size_t size, uint8_t value)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        bytes[index] = value;
    }
}

static int same(const void *left, const void *right, size_t size)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (a[index] != b[index]) {
            return 0;
        }
    }

    return 1;
}

static db_x64_scalar_state make_state(void)
{
    db_x64_scalar_state state;
    size_t index;

    for (index = 0u; index < 16u; ++index) {
        state.registers[index] = UINT64_C(0x1000) +
                                 (uint64_t)index * UINT64_C(0x100);
    }
    state.rip = UINT64_C(0x400000);
    state.rflags = DB_X64_RFLAGS_FIXED_ONE;
    return state;
}

static db_x64_address_expression base_expression(void)
{
    db_x64_address_expression expression;

    fill(&expression, sizeof(expression), 0u);
    expression.mode = DB_X64_ADDRESS_BASE_INDEX;
    expression.base_register = 3u;
    expression.index_register = DB_X64_REGISTER_NONE;
    expression.scale = 1u;
    return expression;
}

static int preserved(const db_x64_address_expression *expression,
                     const db_x64_scalar_state *state,
                     db_x64_address_status expected)
{
    db_x64_address_summary summary;
    db_x64_address_summary before;

    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    before = summary;
    CHECK(db_x64_address_plan(expression, state, &summary) == expected);
    CHECK(same(&summary, &before, sizeof(summary)));
    return 1;
}

static int test_base_index(void)
{
    db_x64_scalar_state state = make_state();
    db_x64_address_expression expression = base_expression();
    db_x64_address_summary summary;

    CHECK(db_x64_address_plan(&expression, &state, &summary) ==
          DB_X64_ADDRESS_OK);
    CHECK(summary.base_value == UINT64_C(0x1300));
    CHECK(summary.index_value == 0u);
    CHECK(summary.scaled_index == 0u);
    CHECK(summary.linear_address == UINT64_C(0x1300));

    expression.displacement = -UINT64_C(0x300);
    CHECK(db_x64_address_plan(&expression, &state, &summary) ==
          DB_X64_ADDRESS_OK);
    CHECK(summary.linear_address == UINT64_C(0x1000));

    expression.index_register = 5u;
    expression.scale = 4u;
    expression.displacement = INT64_C(0x20);
    CHECK(db_x64_address_plan(&expression, &state, &summary) ==
          DB_X64_ADDRESS_OK);
    CHECK(summary.index_value == UINT64_C(0x1500));
    CHECK(summary.scaled_index == UINT64_C(0x5400));
    CHECK(summary.linear_address == UINT64_C(0x6720));

    expression.base_register = DB_X64_REGISTER_NONE;
    expression.index_register = 2u;
    expression.scale = 8u;
    expression.displacement = INT64_C(0x10);
    CHECK(db_x64_address_plan(&expression, &state, &summary) ==
          DB_X64_ADDRESS_OK);
    CHECK(summary.base_value == 0u);
    CHECK(summary.linear_address == UINT64_C(0x9010));

    state.registers[0] = UINT64_MAX;
    expression.base_register = 0u;
    expression.index_register = DB_X64_REGISTER_NONE;
    expression.scale = 1u;
    expression.displacement = 1;
    CHECK(db_x64_address_plan(&expression, &state, &summary) ==
          DB_X64_ADDRESS_OK);
    CHECK(summary.linear_address == 0u);
    return 1;
}

static int test_rip_and_absolute(void)
{
    db_x64_scalar_state state = make_state();
    db_x64_address_expression expression;
    db_x64_address_summary summary;

    fill(&expression, sizeof(expression), 0u);
    expression.mode = DB_X64_ADDRESS_RIP_RELATIVE;
    expression.base_register = DB_X64_REGISTER_NONE;
    expression.index_register = DB_X64_REGISTER_NONE;
    expression.scale = 1u;
    expression.instruction_length = 5u;
    expression.displacement = -5;

    CHECK(db_x64_address_plan(&expression, &state, &summary) ==
          DB_X64_ADDRESS_OK);
    CHECK(summary.base_value == UINT64_C(0x400005));
    CHECK(summary.linear_address == UINT64_C(0x400000));

    expression.mode = DB_X64_ADDRESS_ABSOLUTE;
    expression.instruction_length = 0u;
    expression.displacement = 0;
    CHECK(db_x64_address_plan(&expression, &state, &summary) ==
          DB_X64_ADDRESS_OK);
    CHECK(summary.linear_address == 0u);

    expression.displacement = -1;
    CHECK(db_x64_address_plan(&expression, &state, &summary) ==
          DB_X64_ADDRESS_OK);
    CHECK(summary.linear_address == UINT64_MAX);
    return 1;
}

static int test_bad_expressions(void)
{
    db_x64_scalar_state state = make_state();
    db_x64_address_expression expression = base_expression();

    expression.mode = (db_x64_address_mode)99;
    CHECK(preserved(&expression, &state, DB_X64_ADDRESS_BAD_EXPRESSION));

    expression = base_expression();
    expression.base_register = 16u;
    CHECK(preserved(&expression, &state, DB_X64_ADDRESS_BAD_EXPRESSION));

    expression = base_expression();
    expression.index_register = 16u;
    CHECK(preserved(&expression, &state, DB_X64_ADDRESS_BAD_EXPRESSION));

    expression = base_expression();
    expression.index_register = 1u;
    expression.scale = 3u;
    CHECK(preserved(&expression, &state, DB_X64_ADDRESS_BAD_EXPRESSION));

    expression = base_expression();
    expression.scale = 2u;
    CHECK(preserved(&expression, &state, DB_X64_ADDRESS_BAD_EXPRESSION));

    expression = base_expression();
    expression.base_register = DB_X64_REGISTER_NONE;
    CHECK(preserved(&expression, &state, DB_X64_ADDRESS_BAD_EXPRESSION));

    expression = base_expression();
    expression.instruction_length = 1u;
    CHECK(preserved(&expression, &state, DB_X64_ADDRESS_BAD_EXPRESSION));

    expression = base_expression();
    expression.reserved[2] = 1u;
    CHECK(preserved(&expression, &state, DB_X64_ADDRESS_BAD_EXPRESSION));

    expression = base_expression();
    expression.mode = DB_X64_ADDRESS_RIP_RELATIVE;
    expression.instruction_length = 5u;
    CHECK(preserved(&expression, &state, DB_X64_ADDRESS_BAD_EXPRESSION));

    expression.base_register = DB_X64_REGISTER_NONE;
    expression.index_register = 1u;
    CHECK(preserved(&expression, &state, DB_X64_ADDRESS_BAD_EXPRESSION));

    expression.index_register = DB_X64_REGISTER_NONE;
    expression.instruction_length = 0u;
    CHECK(preserved(&expression, &state, DB_X64_ADDRESS_BAD_EXPRESSION));

    expression.instruction_length = 16u;
    CHECK(preserved(&expression, &state, DB_X64_ADDRESS_BAD_EXPRESSION));

    expression.mode = DB_X64_ADDRESS_ABSOLUTE;
    expression.instruction_length = 0u;
    expression.base_register = 1u;
    CHECK(preserved(&expression, &state, DB_X64_ADDRESS_BAD_EXPRESSION));
    return 1;
}

static int test_noncanonical(void)
{
    db_x64_scalar_state state = make_state();
    db_x64_address_expression expression = base_expression();

    state.registers[3] = UINT64_C(0x0000800000000000);
    CHECK(preserved(&expression, &state,
                    DB_X64_ADDRESS_NONCANONICAL_ADDRESS));

    expression.mode = DB_X64_ADDRESS_RIP_RELATIVE;
    expression.base_register = DB_X64_REGISTER_NONE;
    expression.index_register = DB_X64_REGISTER_NONE;
    expression.scale = 1u;
    expression.instruction_length = 1u;
    expression.displacement = 0;
    state.rip = UINT64_C(0x0000800000000000);
    CHECK(preserved(&expression, &state,
                    DB_X64_ADDRESS_NONCANONICAL_ADDRESS));

    state.rip = UINT64_C(0x00007fffffffffff);
    CHECK(preserved(&expression, &state,
                    DB_X64_ADDRESS_NONCANONICAL_ADDRESS));
    return 1;
}

static int test_guards_and_names(void)
{
    union expression_summary_alias {
        db_x64_address_expression expression;
        db_x64_address_summary summary;
    } expression_alias;
    union state_summary_alias {
        db_x64_scalar_state state;
        db_x64_address_summary summary;
    } state_alias;
    db_x64_scalar_state state = make_state();
    db_x64_address_expression expression = base_expression();
    db_x64_address_summary summary;

    expression_alias.expression = expression;
    CHECK(db_x64_address_plan(&expression_alias.expression,
                              &state,
                              &expression_alias.summary) ==
          DB_X64_ADDRESS_BUFFER_OVERLAP);

    state_alias.state = state;
    CHECK(db_x64_address_plan(&expression,
                              &state_alias.state,
                              &state_alias.summary) ==
          DB_X64_ADDRESS_BUFFER_OVERLAP);

    CHECK(db_x64_address_plan(NULL, &state, &summary) ==
          DB_X64_ADDRESS_NULL_ARGUMENT);
    CHECK(db_x64_address_plan(&expression, NULL, &summary) ==
          DB_X64_ADDRESS_NULL_ARGUMENT);
    CHECK(db_x64_address_plan(&expression, &state, NULL) ==
          DB_X64_ADDRESS_NULL_ARGUMENT);

    CHECK(db_x64_address_status_name(DB_X64_ADDRESS_OK)[0] == 'o');
    CHECK(db_x64_address_status_name(
              DB_X64_ADDRESS_NONCANONICAL_ADDRESS)[0] == 'n');
    CHECK(db_x64_address_status_name((db_x64_address_status)99)[0] == 'u');
    return 1;
}

int main(void)
{
    static const char pass[] = "X64 ADDRESS TEST: PASS\n";
    static const char fail[] = "X64 ADDRESS TEST: FAIL\n";

    if (test_base_index() &&
        test_rip_and_absolute() &&
        test_bad_expressions() &&
        test_noncanonical() &&
        test_guards_and_names()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
