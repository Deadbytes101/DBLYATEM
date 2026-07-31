#include "dblyat/x64_execute.h"

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
        state.registers[index] = UINT64_C(0x1000) + (uint64_t)index;
    }

    state.rip = UINT64_C(0x400000);
    state.rflags = DB_X64_RFLAGS_FIXED_ONE | UINT64_C(0x200);
    return state;
}

static int preserved(const uint8_t *bytes,
                     size_t available_size,
                     db_x64_scalar_state *state,
                     db_x64_execute_status expected)
{
    db_x64_scalar_state state_before = *state;
    db_x64_execute_summary summary;
    db_x64_execute_summary summary_before;

    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    summary_before = summary;

    return db_x64_execute_one(bytes,
                              available_size,
                              state,
                              &summary) == expected &&
           same(state, &state_before, sizeof(*state)) &&
           same(&summary, &summary_before, sizeof(summary));
}

static int test_mov_and_trailing_bytes(void)
{
    static const uint8_t bytes[] = {
        UINT8_C(0x48), UINT8_C(0xb8),
        UINT8_C(0x88), UINT8_C(0x77), UINT8_C(0x66), UINT8_C(0x55),
        UINT8_C(0x44), UINT8_C(0x33), UINT8_C(0x22), UINT8_C(0x11),
        UINT8_C(0xcc)
    };
    db_x64_scalar_state state = make_state();
    db_x64_execute_summary summary;

    CHECK(db_x64_execute_one(bytes,
                             sizeof(bytes),
                             &state,
                             &summary) == DB_X64_EXECUTE_OK);
    CHECK(state.registers[0] == UINT64_C(0x1122334455667788));
    CHECK(state.registers[1] == UINT64_C(0x1001));
    CHECK(state.rip == UINT64_C(0x40000a));
    CHECK(state.rflags ==
          (DB_X64_RFLAGS_FIXED_ONE | UINT64_C(0x200)));
    CHECK(summary.instruction.kind == DB_X64_INSTRUCTION_MOV_R64_IMM64);
    CHECK(summary.instruction.length == 10u);
    CHECK(summary.instruction.destination_register == 0u);
    CHECK(summary.instruction.immediate == UINT64_C(0x1122334455667788));
    CHECK(summary.step.source_rip == UINT64_C(0x400000));
    CHECK(summary.step.fallthrough_rip == UINT64_C(0x40000a));
    CHECK(summary.step.next_rip == UINT64_C(0x40000a));
    CHECK(summary.step.previous_destination == UINT64_C(0x1000));
    CHECK(summary.step.next_destination == UINT64_C(0x1122334455667788));
    CHECK(summary.step.branch_taken == 0u);
    CHECK(summary.reserved == 0u);
    return 1;
}

static int test_xor_flags(void)
{
    static const uint8_t bytes[] = {UINT8_C(0x31), UINT8_C(0xc0)};
    db_x64_scalar_state state = make_state();
    db_x64_execute_summary summary;
    uint64_t expected_flags;

    state.registers[0] = UINT64_MAX;
    state.rflags |= DB_X64_RFLAGS_CF |
                    DB_X64_RFLAGS_AF |
                    DB_X64_RFLAGS_SF |
                    DB_X64_RFLAGS_OF;

    CHECK(db_x64_execute_one(bytes,
                             sizeof(bytes),
                             &state,
                             &summary) == DB_X64_EXECUTE_OK);

    expected_flags = DB_X64_RFLAGS_FIXED_ONE |
                     UINT64_C(0x200) |
                     DB_X64_RFLAGS_PF |
                     DB_X64_RFLAGS_ZF;
    CHECK(state.registers[0] == 0u);
    CHECK(state.rip == UINT64_C(0x400002));
    CHECK(state.rflags == expected_flags);
    CHECK(summary.instruction.kind == DB_X64_INSTRUCTION_XOR_R32_R32);
    CHECK(summary.step.previous_destination == UINT64_MAX);
    CHECK(summary.step.next_destination == 0u);
    CHECK(summary.step.previous_rflags != summary.step.next_rflags);
    CHECK(summary.step.next_rflags == expected_flags);
    return 1;
}

static int test_conditional_branch(void)
{
    static const uint8_t bytes[] = {UINT8_C(0x74), UINT8_C(0x05)};
    db_x64_scalar_state state = make_state();
    db_x64_execute_summary summary;

    state.rflags |= DB_X64_RFLAGS_ZF;
    CHECK(db_x64_execute_one(bytes,
                             sizeof(bytes),
                             &state,
                             &summary) == DB_X64_EXECUTE_OK);
    CHECK(state.rip == UINT64_C(0x400007));
    CHECK(summary.instruction.kind == DB_X64_INSTRUCTION_JCC_REL8);
    CHECK(summary.instruction.condition == 4u);
    CHECK(summary.step.fallthrough_rip == UINT64_C(0x400002));
    CHECK(summary.step.next_rip == UINT64_C(0x400007));
    CHECK(summary.step.branch_taken == 1u);

    state = make_state();
    CHECK(db_x64_execute_one(bytes,
                             sizeof(bytes),
                             &state,
                             &summary) == DB_X64_EXECUTE_OK);
    CHECK(state.rip == UINT64_C(0x400002));
    CHECK(summary.step.branch_taken == 0u);
    return 1;
}

static int test_failures_preserve_outputs(void)
{
    static const uint8_t unsupported[] = {UINT8_C(0xf4)};
    static const uint8_t call[] = {
        UINT8_C(0xe8), 0u, 0u, 0u, 0u
    };
    static const uint8_t nop[] = {UINT8_C(0x90)};
    db_x64_scalar_state state = make_state();

    CHECK(preserved(unsupported,
                    sizeof(unsupported),
                    &state,
                    DB_X64_EXECUTE_DECODE_FAILED));
    CHECK(preserved(unsupported,
                    0u,
                    &state,
                    DB_X64_EXECUTE_DECODE_FAILED));
    CHECK(preserved(call,
                    sizeof(call),
                    &state,
                    DB_X64_EXECUTE_INTERPRET_FAILED));

    state.rip = 0u;
    CHECK(preserved(nop,
                    sizeof(nop),
                    &state,
                    DB_X64_EXECUTE_INTERPRET_FAILED));
    return 1;
}

static int test_guards_and_names(void)
{
    static const uint8_t nop[] = {UINT8_C(0x90)};
    db_x64_scalar_state state = make_state();
    db_x64_execute_summary summary;
    union bytes_state_alias {
        uint8_t bytes[sizeof(db_x64_scalar_state)];
        db_x64_scalar_state state;
    } bytes_state;
    union bytes_summary_alias {
        uint8_t bytes[sizeof(db_x64_execute_summary)];
        db_x64_execute_summary summary;
    } bytes_summary;
    union state_summary_alias {
        db_x64_scalar_state state;
        db_x64_execute_summary summary;
    } state_summary;

    bytes_state.bytes[0] = UINT8_C(0x90);
    CHECK(db_x64_execute_one(bytes_state.bytes,
                             sizeof(bytes_state.bytes),
                             &bytes_state.state,
                             &summary) == DB_X64_EXECUTE_BUFFER_OVERLAP);

    bytes_summary.bytes[0] = UINT8_C(0x90);
    CHECK(db_x64_execute_one(bytes_summary.bytes,
                             sizeof(bytes_summary.bytes),
                             &state,
                             &bytes_summary.summary) ==
          DB_X64_EXECUTE_BUFFER_OVERLAP);

    state_summary.state = state;
    CHECK(db_x64_execute_one(nop,
                             sizeof(nop),
                             &state_summary.state,
                             &state_summary.summary) ==
          DB_X64_EXECUTE_BUFFER_OVERLAP);

    CHECK(db_x64_execute_one(NULL,
                             sizeof(nop),
                             &state,
                             &summary) == DB_X64_EXECUTE_NULL_ARGUMENT);
    CHECK(db_x64_execute_one(nop,
                             sizeof(nop),
                             NULL,
                             &summary) == DB_X64_EXECUTE_NULL_ARGUMENT);
    CHECK(db_x64_execute_one(nop,
                             sizeof(nop),
                             &state,
                             NULL) == DB_X64_EXECUTE_NULL_ARGUMENT);
    CHECK(db_x64_execute_status_name(DB_X64_EXECUTE_OK)[0] == 'o');
    CHECK(db_x64_execute_status_name(DB_X64_EXECUTE_DECODE_FAILED)[0] == 'x');
    CHECK(db_x64_execute_status_name(DB_X64_EXECUTE_INTERPRET_FAILED)[0] == 'x');
    CHECK(db_x64_execute_status_name((db_x64_execute_status)99)[0] == 'u');
    return 1;
}

int main(void)
{
    static const char pass[] = "X64 EXECUTE TEST: PASS\n";
    static const char fail[] = "X64 EXECUTE TEST: FAIL\n";

    if (test_mov_and_trailing_bytes() &&
        test_xor_flags() &&
        test_conditional_branch() &&
        test_failures_preserve_outputs() &&
        test_guards_and_names()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
