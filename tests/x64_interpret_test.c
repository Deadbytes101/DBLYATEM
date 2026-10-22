#include "dblyat/x64_interpret.h"

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
        state.registers[index] = UINT64_C(0x1000) + index;
    }
    state.rip = UINT64_C(0x400000);
    state.rflags = DB_X64_RFLAGS_FIXED_ONE;
    return state;
}

static db_x64_instruction make_instruction(db_x64_instruction_kind kind,
                                           uint8_t length)
{
    db_x64_instruction instruction;

    fill(&instruction, sizeof(instruction), 0u);
    instruction.kind = kind;
    instruction.length = length;
    instruction.destination_register = DB_X64_REGISTER_NONE;
    instruction.source_register = DB_X64_REGISTER_NONE;
    instruction.condition = DB_X64_CONDITION_NONE;
    return instruction;
}

static int preserved(const db_x64_instruction *instruction,
                     db_x64_scalar_state *state,
                     db_x64_interpret_status expected)
{
    db_x64_scalar_state state_before = *state;
    db_x64_step_summary summary;
    db_x64_step_summary summary_before;

    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    summary_before = summary;

    return db_x64_interpret_one(instruction, state, &summary) == expected &&
           same(state, &state_before, sizeof(*state)) &&
           same(&summary, &summary_before, sizeof(summary));
}

static int test_nop_and_moves(void)
{
    db_x64_scalar_state state = make_state();
    db_x64_step_summary summary;
    db_x64_instruction instruction =
        make_instruction(DB_X64_INSTRUCTION_NOP, 1u);
    db_x64_scalar_state before = state;

    CHECK(db_x64_interpret_one(&instruction, &state, &summary) ==
          DB_X64_INTERPRET_OK);
    CHECK(state.rip == before.rip + 1u);
    CHECK(state.rflags == before.rflags);
    CHECK(same(state.registers,
               before.registers,
               sizeof(state.registers)));
    CHECK(summary.source_rip == before.rip);
    CHECK(summary.fallthrough_rip == before.rip + 1u);
    CHECK(summary.next_rip == before.rip + 1u);
    CHECK(summary.destination_register == DB_X64_REGISTER_NONE);
    CHECK(summary.branch_taken == 0u);
    CHECK(summary.reserved == 0u);

    instruction =
        make_instruction(DB_X64_INSTRUCTION_MOV_R64_IMM64, 10u);
    instruction.rex = UINT8_C(0x49);
    instruction.destination_register = 9u;
    instruction.immediate = UINT64_C(0x1122334455667788);
    before = state;

    CHECK(db_x64_interpret_one(&instruction, &state, &summary) ==
          DB_X64_INTERPRET_OK);
    CHECK(state.registers[9] == instruction.immediate);
    CHECK(state.rip == before.rip + 10u);
    CHECK(summary.destination_register == 9u);
    CHECK(summary.previous_destination == before.registers[9]);
    CHECK(summary.next_destination == instruction.immediate);

    instruction =
        make_instruction(DB_X64_INSTRUCTION_MOV_R32_IMM32, 6u);
    instruction.rex = UINT8_C(0x41);
    instruction.destination_register = 9u;
    instruction.immediate = UINT64_C(0x80000000);
    before = state;

    CHECK(db_x64_interpret_one(&instruction, &state, &summary) ==
          DB_X64_INTERPRET_OK);
    CHECK(before.registers[9] == UINT64_C(0x1122334455667788));
    CHECK(state.registers[9] == UINT64_C(0x0000000080000000));
    CHECK(state.rip == before.rip + 6u);
    CHECK(summary.previous_destination == before.registers[9]);
    CHECK(summary.next_destination == UINT64_C(0x80000000));

    instruction =
        make_instruction(DB_X64_INSTRUCTION_MOV_R64_R64, 3u);
    instruction.rex = UINT8_C(0x4c);
    instruction.destination_register = 2u;
    instruction.source_register = 10u;
    before = state;

    CHECK(db_x64_interpret_one(&instruction, &state, &summary) ==
          DB_X64_INTERPRET_OK);
    CHECK(state.registers[2] == before.registers[10]);
    CHECK(state.rip == before.rip + 3u);
    CHECK(summary.previous_destination == before.registers[2]);
    CHECK(summary.next_destination == before.registers[10]);
    return 1;
}

static int test_xor_semantics(void)
{
    db_x64_scalar_state state = make_state();
    db_x64_step_summary summary;
    db_x64_instruction instruction =
        make_instruction(DB_X64_INSTRUCTION_XOR_R32_R32, 2u);

    state.registers[0] = UINT64_C(0xffffffffffffffff);
    state.rflags = DB_X64_RFLAGS_FIXED_ONE |
                   DB_X64_RFLAGS_CF |
                   DB_X64_RFLAGS_AF |
                   DB_X64_RFLAGS_OF;
    instruction.destination_register = 0u;
    instruction.source_register = 0u;

    CHECK(db_x64_interpret_one(&instruction, &state, &summary) ==
          DB_X64_INTERPRET_OK);
    CHECK(state.registers[0] == 0u);
    CHECK((state.rflags & DB_X64_RFLAGS_ZF) != 0u);
    CHECK((state.rflags & DB_X64_RFLAGS_PF) != 0u);
    CHECK((state.rflags & (DB_X64_RFLAGS_CF |
                           DB_X64_RFLAGS_AF |
                           DB_X64_RFLAGS_SF |
                           DB_X64_RFLAGS_OF)) == 0u);
    CHECK((state.rflags & DB_X64_RFLAGS_FIXED_ONE) != 0u);
    CHECK(summary.next_destination == 0u);
    CHECK(summary.next_rflags == state.rflags);

    instruction =
        make_instruction(DB_X64_INSTRUCTION_XOR_R64_R64, 3u);
    instruction.rex = UINT8_C(0x48);
    instruction.destination_register = 3u;
    instruction.source_register = 4u;
    state.registers[3] = UINT64_C(0x8000000000000000);
    state.registers[4] = 0u;
    state.rflags = DB_X64_RFLAGS_FIXED_ONE;

    CHECK(db_x64_interpret_one(&instruction, &state, &summary) ==
          DB_X64_INTERPRET_OK);
    CHECK(state.registers[3] == UINT64_C(0x8000000000000000));
    CHECK((state.rflags & DB_X64_RFLAGS_SF) != 0u);
    CHECK((state.rflags & DB_X64_RFLAGS_PF) != 0u);
    CHECK((state.rflags & DB_X64_RFLAGS_ZF) == 0u);
    return 1;
}

static int test_branches(void)
{
    db_x64_scalar_state state = make_state();
    db_x64_step_summary summary;
    db_x64_instruction instruction =
        make_instruction(DB_X64_INSTRUCTION_JMP_REL8, 2u);

    instruction.relative = -2;
    CHECK(db_x64_interpret_one(&instruction, &state, &summary) ==
          DB_X64_INTERPRET_OK);
    CHECK(state.rip == UINT64_C(0x400000));
    CHECK(summary.fallthrough_rip == UINT64_C(0x400002));
    CHECK(summary.next_rip == UINT64_C(0x400000));
    CHECK(summary.branch_taken == 1u);

    state = make_state();
    state.rflags |= DB_X64_RFLAGS_ZF;
    instruction =
        make_instruction(DB_X64_INSTRUCTION_JCC_REL8, 2u);
    instruction.condition = 4u;
    instruction.relative = 5;

    CHECK(db_x64_interpret_one(&instruction, &state, &summary) ==
          DB_X64_INTERPRET_OK);
    CHECK(state.rip == UINT64_C(0x400007));
    CHECK(summary.branch_taken == 1u);

    state = make_state();
    instruction =
        make_instruction(DB_X64_INSTRUCTION_JCC_REL32, 6u);
    instruction.condition = 4u;
    instruction.relative = 16;

    CHECK(db_x64_interpret_one(&instruction, &state, &summary) ==
          DB_X64_INTERPRET_OK);
    CHECK(state.rip == UINT64_C(0x400006));
    CHECK(summary.branch_taken == 0u);

    state = make_state();
    state.rflags |= DB_X64_RFLAGS_SF;
    instruction.condition = 12u;
    instruction.relative = -6;

    CHECK(db_x64_interpret_one(&instruction, &state, &summary) ==
          DB_X64_INTERPRET_OK);
    CHECK(state.rip == UINT64_C(0x400000));
    CHECK(summary.branch_taken == 1u);
    return 1;
}

static int test_failures(void)
{
    db_x64_scalar_state state = make_state();
    db_x64_instruction instruction =
        make_instruction(DB_X64_INSTRUCTION_CALL_REL32, 5u);

    instruction.relative = 1;
    CHECK(preserved(&instruction,
                    &state,
                    DB_X64_INTERPRET_UNSUPPORTED_INSTRUCTION));

    instruction =
        make_instruction(DB_X64_INSTRUCTION_MOV_R64_R64, 3u);
    instruction.rex = UINT8_C(0x48);
    instruction.destination_register = 16u;
    instruction.source_register = 0u;
    CHECK(preserved(&instruction,
                    &state,
                    DB_X64_INTERPRET_BAD_INSTRUCTION));

    instruction.destination_register = 1u;
    instruction.reserved[0] = 1u;
    CHECK(preserved(&instruction,
                    &state,
                    DB_X64_INTERPRET_BAD_INSTRUCTION));

    instruction =
        make_instruction(DB_X64_INSTRUCTION_JMP_REL8, 2u);
    instruction.relative = 128;
    CHECK(preserved(&instruction,
                    &state,
                    DB_X64_INTERPRET_BAD_INSTRUCTION));

    state = make_state();
    state.rflags = 0u;
    instruction =
        make_instruction(DB_X64_INSTRUCTION_NOP, 1u);
    CHECK(preserved(&instruction,
                    &state,
                    DB_X64_INTERPRET_BAD_STATE));

    state = make_state();
    state.rip = UINT64_C(0x00007fffffffffff);
    CHECK(preserved(&instruction,
                    &state,
                    DB_X64_INTERPRET_NONCANONICAL_ADDRESS));

    state = make_state();
    state.rip = UINT64_C(0xffffffffffffffff);
    CHECK(preserved(&instruction,
                    &state,
                    DB_X64_INTERPRET_ADDRESS_OVERFLOW));
    return 1;
}

static int test_guards_and_names(void)
{
    db_x64_scalar_state state = make_state();
    db_x64_step_summary summary;
    db_x64_instruction instruction =
        make_instruction(DB_X64_INSTRUCTION_NOP, 1u);
    union state_summary_alias {
        db_x64_scalar_state state;
        db_x64_step_summary summary;
    } alias;

    alias.state = state;
    CHECK(db_x64_interpret_one(&instruction,
                               &alias.state,
                               &alias.summary) ==
          DB_X64_INTERPRET_BUFFER_OVERLAP);
    CHECK(db_x64_interpret_one(NULL, &state, &summary) ==
          DB_X64_INTERPRET_NULL_ARGUMENT);
    CHECK(db_x64_interpret_one(&instruction, NULL, &summary) ==
          DB_X64_INTERPRET_NULL_ARGUMENT);
    CHECK(db_x64_interpret_one(&instruction, &state, NULL) ==
          DB_X64_INTERPRET_NULL_ARGUMENT);
    CHECK(db_x64_interpret_status_name(DB_X64_INTERPRET_OK)[0] == 'o');
    CHECK(db_x64_interpret_status_name(
              DB_X64_INTERPRET_UNSUPPORTED_INSTRUCTION)[0] == 'u');
    CHECK(db_x64_interpret_status_name(
              (db_x64_interpret_status)99)[0] == 'u');
    return 1;
}

int main(void)
{
    static const char pass[] = "X64 INTERPRET TEST: PASS\n";
    static const char fail[] = "X64 INTERPRET TEST: FAIL\n";

    if (test_nop_and_moves() &&
        test_xor_semantics() &&
        test_branches() &&
        test_failures() &&
        test_guards_and_names()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
