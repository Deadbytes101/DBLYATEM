#include "dblyat/ir.h"
#include "dblyat/win32_provider.h"
#include "dblyat/win32_translated_execution.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define CHECK(value) do { if (!(value)) return 0; } while (0)

#define TEST_GATEWAY_BASE UINT64_C(0x10000)
#define TEST_SLOT_SIZE UINT64_C(64)
#define TEST_STACK_BASE UINT64_C(0x70000000)
#define TEST_STACK_SIZE 96u
#define TEST_RSP (TEST_STACK_BASE + UINT64_C(0x10))
#define TEST_RETURN_ADDRESS UINT64_C(0x401000)

#define TEST_RAX 0u
#define TEST_RCX 1u
#define TEST_RSP_INDEX 4u

typedef struct execution_fixture {
    uint8_t gateway[(size_t)TEST_SLOT_SIZE];
    uint8_t stack[TEST_STACK_SIZE];
    db_iat_target target;
    db_gateway_dispatch_handler handler;
    db_gateway_frame_signature signature;
    db_win32_kernel32_state kernel32;
    db_x64_scalar_state scalar;
    db_translated_terminal_exit terminal_exit;
} execution_fixture;

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

static void put_u64(uint8_t *bytes, size_t offset, uint64_t value)
{
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        bytes[offset + index] =
            (uint8_t)(value >> (uint32_t)(index * 8u));
    }
}

static void scalar_init(db_x64_scalar_state *state, uint64_t rip)
{
    size_t index;

    fill(state, sizeof(*state), 0u);
    for (index = 0u; index < 16u; ++index) {
        state->registers[index] =
            UINT64_C(0x1111000000000000) + (uint64_t)index;
    }
    state->rip = rip;
    state->rflags = DB_X64_RFLAGS_FIXED_ONE | DB_X64_RFLAGS_CF;
}

static void kernel_init(db_win32_kernel32_state *state)
{
    fill(state, sizeof(*state), 0u);
    state->process_id = 10u;
    state->thread_id = 20u;
    state->last_error = 5u;
}

static int fixture_init(execution_fixture *fixture,
                        uint32_t symbol_id,
                        uint32_t handler_id,
                        uint16_t argument_count,
                        uint64_t first_argument)
{
    db_gateway_emit_summary emit_summary;

    fill(fixture, sizeof(*fixture), 0u);

    fixture->target.provider_id = DB_WIN32_PROVIDER_KERNEL32;
    fixture->target.symbol_id = symbol_id;
    fixture->target.guest_value = TEST_GATEWAY_BASE;

    fixture->handler.provider_id = DB_WIN32_PROVIDER_KERNEL32;
    fixture->handler.symbol_id = symbol_id;
    fixture->handler.handler_id = handler_id;

    fixture->signature.handler_id = handler_id;
    fixture->signature.argument_count = argument_count;

    CHECK(db_gateway_emit_slots(&fixture->target,
                                1u,
                                TEST_GATEWAY_BASE,
                                TEST_SLOT_SIZE,
                                fixture->gateway,
                                sizeof(fixture->gateway),
                                &emit_summary) == DB_GATEWAY_EMIT_OK);
    CHECK(emit_summary.target_count == 1u);

    put_u64(fixture->stack,
            (size_t)(TEST_RSP - TEST_STACK_BASE),
            TEST_RETURN_ADDRESS);

    kernel_init(&fixture->kernel32);
    scalar_init(&fixture->scalar, TEST_GATEWAY_BASE);
    fixture->scalar.registers[TEST_RCX] = first_argument;
    fixture->scalar.registers[TEST_RSP_INDEX] = TEST_RSP;

    CHECK(db_translated_terminal_exit_plan_trap(
              TEST_GATEWAY_BASE,
              (uint32_t)DB_IR_TRAP_INVALID_OPCODE,
              &fixture->terminal_exit) == DB_TRANSLATED_TERMINAL_EXIT_OK);
    return 1;
}

static db_win32_translated_execution_status resolve_fixture(
    execution_fixture *fixture,
    db_win32_translated_execution_summary *summary)
{
    return db_win32_translated_execution_resolve_exit(
        &fixture->terminal_exit,
        fixture->gateway,
        sizeof(fixture->gateway),
        &fixture->target,
        1u,
        TEST_GATEWAY_BASE,
        TEST_SLOT_SIZE,
        &fixture->handler,
        1u,
        &fixture->signature,
        1u,
        fixture->stack,
        sizeof(fixture->stack),
        TEST_STACK_BASE,
        &fixture->kernel32,
        &fixture->scalar,
        summary);
}

static int test_direct_target_and_conditional(void)
{
    db_win32_kernel32_state kernel32;
    db_x64_scalar_state scalar;
    db_x64_scalar_state before;
    db_win32_translated_execution_summary summary;
    db_translated_terminal_exit terminal;

    kernel_init(&kernel32);
    scalar_init(&scalar, UINT64_C(0x2000));
    before = scalar;
    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    CHECK(db_translated_terminal_exit_plan_direct(
              UINT64_C(0x2000), UINT64_C(0x3000), &terminal) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    CHECK(db_win32_translated_execution_resolve_exit(
              &terminal, NULL, 0u, NULL, 0u, 0u, 0u,
              NULL, 0u, NULL, 0u, NULL, 0u, 0u,
              &kernel32, &scalar, &summary) ==
          DB_WIN32_TRANSLATED_EXECUTION_OK);
    CHECK(summary.outcome ==
          DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE);
    CHECK(summary.next_rip == UINT64_C(0x3000));
    CHECK(scalar.rip == UINT64_C(0x3000));
    CHECK(scalar.registers[TEST_RAX] == before.registers[TEST_RAX]);
    CHECK(scalar.rflags == before.rflags);

    scalar_init(&scalar, UINT64_C(0x4000));
    CHECK(db_translated_terminal_exit_plan_target(
              UINT64_C(0x4000), &terminal) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    terminal.runtime_target = UINT64_C(0x5000);
    terminal.runtime_target_valid = 1u;
    CHECK(db_win32_translated_execution_resolve_exit(
              &terminal, NULL, 0u, NULL, 0u, 0u, 0u,
              NULL, 0u, NULL, 0u, NULL, 0u, 0u,
              &kernel32, &scalar, &summary) ==
          DB_WIN32_TRANSLATED_EXECUTION_OK);
    CHECK(scalar.rip == UINT64_C(0x5000));
    CHECK(summary.next_rip == UINT64_C(0x5000));

    scalar_init(&scalar, UINT64_C(0x6000));
    CHECK(db_translated_terminal_exit_plan_conditional(
              UINT64_C(0x6000),
              UINT64_C(0x7000),
              UINT64_C(0x6002),
              (uint32_t)DB_IR_CONDITION_EQUAL,
              &terminal) == DB_TRANSLATED_TERMINAL_EXIT_OK);
    terminal.runtime_target = UINT64_C(0x7000);
    terminal.runtime_value = UINT64_C(1);
    terminal.runtime_target_valid = 1u;
    terminal.runtime_value_valid = 1u;
    terminal.condition_taken = 1u;
    terminal.condition_taken_valid = 1u;
    CHECK(db_win32_translated_execution_resolve_exit(
              &terminal, NULL, 0u, NULL, 0u, 0u, 0u,
              NULL, 0u, NULL, 0u, NULL, 0u, 0u,
              &kernel32, &scalar, &summary) ==
          DB_WIN32_TRANSLATED_EXECUTION_OK);
    CHECK(scalar.rip == UINT64_C(0x7000));
    CHECK(summary.next_rip == UINT64_C(0x7000));
    CHECK(summary.gateway_attempted == 0u);
    CHECK(summary.gateway_consumed == 0u);
    return 1;
}

static int test_non_invalid_trap(void)
{
    db_win32_kernel32_state kernel32;
    db_x64_scalar_state scalar;
    db_win32_translated_execution_summary summary;
    db_translated_terminal_exit terminal;

    kernel_init(&kernel32);
    scalar_init(&scalar, UINT64_C(0x8000));
    CHECK(db_translated_terminal_exit_plan_trap(
              UINT64_C(0x8000),
              (uint32_t)DB_IR_TRAP_BREAKPOINT,
              &terminal) == DB_TRANSLATED_TERMINAL_EXIT_OK);
    CHECK(db_win32_translated_execution_resolve_exit(
              &terminal, NULL, 0u, NULL, 0u, 0u, 0u,
              NULL, 0u, NULL, 0u, NULL, 0u, 0u,
              &kernel32, &scalar, &summary) ==
          DB_WIN32_TRANSLATED_EXECUTION_OK);
    CHECK(summary.outcome ==
          DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP);
    CHECK(summary.trap == (uint32_t)DB_IR_TRAP_BREAKPOINT);
    CHECK(summary.next_rip == UINT64_C(0x8000));
    CHECK(scalar.rip == UINT64_C(0x8000));
    CHECK(summary.gateway_attempted == 0u);
    return 1;
}

static int test_ordinary_ud2_preserved(void)
{
    execution_fixture fixture;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar_before;
    db_win32_translated_execution_summary summary;
    const uint64_t ordinary_rip = UINT64_C(0x401100);

    CHECK(fixture_init(&fixture,
                       DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR,
                       DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                       1u,
                       UINT64_C(42)));
    CHECK(db_translated_terminal_exit_plan_trap(
              ordinary_rip,
              (uint32_t)DB_IR_TRAP_INVALID_OPCODE,
              &fixture.terminal_exit) == DB_TRANSLATED_TERMINAL_EXIT_OK);
    fixture.scalar.rip = ordinary_rip;
    kernel_before = fixture.kernel32;
    scalar_before = fixture.scalar;

    CHECK(resolve_fixture(&fixture, &summary) ==
          DB_WIN32_TRANSLATED_EXECUTION_OK);
    CHECK(summary.outcome ==
          DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP);
    CHECK(summary.trap == (uint32_t)DB_IR_TRAP_INVALID_OPCODE);
    CHECK(summary.gateway_attempted == 1u);
    CHECK(summary.gateway_consumed == 0u);
    CHECK(same(&fixture.kernel32,
               &kernel_before,
               sizeof(kernel_before)));
    CHECK(same(&fixture.scalar,
               &scalar_before,
               sizeof(scalar_before)));
    return 1;
}

static int test_authenticated_continue(void)
{
    execution_fixture fixture;
    db_x64_scalar_state scalar_before;
    db_win32_translated_execution_summary summary;

    CHECK(fixture_init(&fixture,
                       DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR,
                       DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                       1u,
                       UINT64_C(42)));
    scalar_before = fixture.scalar;

    CHECK(resolve_fixture(&fixture, &summary) ==
          DB_WIN32_TRANSLATED_EXECUTION_OK);
    CHECK(fixture.kernel32.last_error == 42u);
    CHECK(fixture.scalar.registers[TEST_RAX] == 0u);
    CHECK(fixture.scalar.registers[TEST_RCX] == 42u);
    CHECK(fixture.scalar.registers[TEST_RSP_INDEX] == TEST_RSP + 8u);
    CHECK(fixture.scalar.rip == TEST_RETURN_ADDRESS);
    CHECK(fixture.scalar.rflags == scalar_before.rflags);
    CHECK(summary.outcome ==
          DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE);
    CHECK(summary.next_rip == TEST_RETURN_ADDRESS);
    CHECK(summary.gateway_attempted == 1u);
    CHECK(summary.gateway_consumed == 1u);
    CHECK(summary.gateway.dispatch.outcome ==
          DB_WIN32_RUNTIME_OUTCOME_CONTINUE);
    return 1;
}

static int test_authenticated_termination(void)
{
    execution_fixture fixture;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar_before;
    db_win32_translated_execution_summary summary;

    CHECK(fixture_init(&fixture,
                       DB_WIN32_SYMBOL_KERNEL32_EXIT_PROCESS,
                       DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS,
                       1u,
                       UINT64_C(37)));
    kernel_before = fixture.kernel32;
    scalar_before = fixture.scalar;

    CHECK(resolve_fixture(&fixture, &summary) ==
          DB_WIN32_TRANSLATED_EXECUTION_OK);
    CHECK(summary.outcome ==
          DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED);
    CHECK(summary.exit_code == 37u);
    CHECK(summary.next_rip == 0u);
    CHECK(summary.gateway_attempted == 1u);
    CHECK(summary.gateway_consumed == 1u);
    CHECK(same(&fixture.kernel32,
               &kernel_before,
               sizeof(kernel_before)));
    CHECK(same(&fixture.scalar,
               &scalar_before,
               sizeof(scalar_before)));
    return 1;
}

static int test_gateway_failure_is_transactional(void)
{
    execution_fixture fixture;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar_before;
    db_win32_translated_execution_summary summary;
    db_win32_translated_execution_summary summary_before;

    CHECK(fixture_init(&fixture,
                       DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR,
                       DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                       1u,
                       UINT64_C(42)));
    fixture.gateway[4] = (uint8_t)'X';
    kernel_before = fixture.kernel32;
    scalar_before = fixture.scalar;
    fill(&summary, sizeof(summary), UINT8_C(0x5a));
    summary_before = summary;

    CHECK(resolve_fixture(&fixture, &summary) ==
          DB_WIN32_TRANSLATED_EXECUTION_GATEWAY_FAILED);
    CHECK(same(&fixture.kernel32,
               &kernel_before,
               sizeof(kernel_before)));
    CHECK(same(&fixture.scalar,
               &scalar_before,
               sizeof(scalar_before)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}

static int test_validation_transactionality(void)
{
    db_win32_kernel32_state kernel32;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar;
    db_x64_scalar_state scalar_before;
    db_win32_translated_execution_summary summary;
    db_win32_translated_execution_summary summary_before;
    db_translated_terminal_exit terminal;

    kernel_init(&kernel32);
    scalar_init(&scalar, UINT64_C(0x9000));
    CHECK(db_translated_terminal_exit_plan_direct(
              UINT64_C(0x9000), UINT64_C(0xa000), &terminal) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    kernel_before = kernel32;
    scalar_before = scalar;
    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    summary_before = summary;

    terminal.format_version += 1u;
    CHECK(db_win32_translated_execution_resolve_exit(
              &terminal, NULL, 0u, NULL, 0u, 0u, 0u,
              NULL, 0u, NULL, 0u, NULL, 0u, 0u,
              &kernel32, &scalar, &summary) ==
          DB_WIN32_TRANSLATED_EXECUTION_BAD_TERMINAL_EXIT);
    CHECK(same(&kernel32, &kernel_before, sizeof(kernel32)));
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));

    terminal.format_version = DB_TRANSLATED_TERMINAL_EXIT_FORMAT_VERSION;
    scalar.rip = UINT64_C(0x9001);
    scalar_before = scalar;
    CHECK(db_win32_translated_execution_resolve_exit(
              &terminal, NULL, 0u, NULL, 0u, 0u, 0u,
              NULL, 0u, NULL, 0u, NULL, 0u, 0u,
              &kernel32, &scalar, &summary) ==
          DB_WIN32_TRANSLATED_EXECUTION_BAD_SCALAR_STATE);
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}

#if !defined(__aarch64__)
static int test_non_aarch64_step_is_transactional(void)
{
    db_arm64_published_block published;
    db_win32_kernel32_state kernel32;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar;
    db_x64_scalar_state scalar_before;
    db_win32_translated_execution_summary summary;
    db_win32_translated_execution_summary summary_before;

    fill(&published, sizeof(published), 0u);
    published.guest_source_rip = UINT64_C(0x1000);
    published.guest_end_rip = UINT64_C(0x1002);
    published.source_generation = UINT64_C(1);
    published.translation_key = UINT64_C(1);
    published.host_mapping_address = UINT64_C(0x100000);
    published.host_entry_address = UINT64_C(0x100000);
    published.host_allocation_end = UINT64_C(0x100fff);
    published.host_allocation_size = UINT64_C(4096);
    published.arm64_image_byte_count = UINT64_C(4);
    published.guest_byte_count = 2u;
    published.guest_instruction_count = 1u;
    published.arm64_word_count = 1u;
    published.block_stop_reason = DB_X64_BLOCK_STOP_TERMINATOR;
    published.format_version = DB_ARM64_PUBLISHED_BLOCK_FORMAT_VERSION;
    CHECK(db_translated_terminal_exit_plan_trap(
              UINT64_C(0x1000),
              (uint32_t)DB_IR_TRAP_INVALID_OPCODE,
              &published.terminal_exit) == DB_TRANSLATED_TERMINAL_EXIT_OK);

    kernel_init(&kernel32);
    scalar_init(&scalar, UINT64_C(0x1000));
    kernel_before = kernel32;
    scalar_before = scalar;
    fill(&summary, sizeof(summary), UINT8_C(0x5a));
    summary_before = summary;

    CHECK(db_win32_translated_execution_step(
              &published,
              NULL, 0u, NULL, 0u, 0u, 0u,
              NULL, 0u, NULL, 0u,
              NULL, 0u, 0u,
              &kernel32, &scalar, &summary) ==
          DB_WIN32_TRANSLATED_EXECUTION_ENTRY_FAILED);
    CHECK(same(&kernel32, &kernel_before, sizeof(kernel32)));
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}
#endif

int main(void)
{
    if (!test_direct_target_and_conditional()) {
        return 1;
    }
    if (!test_non_invalid_trap()) {
        return 2;
    }
    if (!test_ordinary_ud2_preserved()) {
        return 3;
    }
    if (!test_authenticated_continue()) {
        return 4;
    }
    if (!test_authenticated_termination()) {
        return 5;
    }
    if (!test_gateway_failure_is_transactional()) {
        return 6;
    }
    if (!test_validation_transactionality()) {
        return 7;
    }
#if !defined(__aarch64__)
    if (!test_non_aarch64_step_is_transactional()) {
        return 8;
    }
#endif

    static const char output[] =
        "WIN32 TRANSLATED EXECUTION TEST: PASS\n"
        "TRANSLATED DIRECT TARGET CONDITIONAL RESOLUTION: PASS\n"
        "TRANSLATED ORDINARY UD2 OUTCOME: PASS\n"
        "TRANSLATED DBGT CONTINUE COORDINATION: PASS\n"
        "TRANSLATED DBGT TERMINATION COORDINATION: PASS\n"
        "TRANSLATED EXECUTION TRANSACTIONALITY: PASS\n"
#if !defined(__aarch64__)
        "TRANSLATED NATIVE ENTRY ON THIS HOST: NOT RUN\n"
#endif
        ;

    if (write(STDOUT_FILENO, output, sizeof(output) - 1u) < 0) {
        return 9;
    }
    return 0;
}
