#include "dblyat/ir.h"
#include "dblyat/win32_provider.h"
#include "dblyat/win32_translated_gateway_bridge.h"

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
#define TEST_RDX 2u
#define TEST_RBX 3u
#define TEST_RSP_INDEX 4u
#define TEST_RBP 5u
#define TEST_RSI 6u
#define TEST_RDI 7u
#define TEST_R8 8u
#define TEST_R9 9u
#define TEST_R10 10u
#define TEST_R11 11u
#define TEST_R12 12u
#define TEST_R13 13u
#define TEST_R14 14u
#define TEST_R15 15u

typedef struct bridge_fixture {
    uint8_t gateway[(size_t)TEST_SLOT_SIZE];
    uint8_t stack[TEST_STACK_SIZE];
    db_iat_target target;
    db_gateway_dispatch_handler handler;
    db_gateway_frame_signature signature;
    db_win32_kernel32_state kernel32;
    db_x64_scalar_state scalar;
    db_translated_terminal_exit terminal_exit;
} bridge_fixture;

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

static int fixture_init(bridge_fixture *fixture,
                        uint32_t symbol_id,
                        uint32_t handler_id,
                        uint16_t argument_count,
                        uint64_t first_argument)
{
    db_gateway_emit_summary emit_summary;
    size_t index;

    fill(fixture, sizeof(*fixture), 0u);

    fixture->target.provider_id = DB_WIN32_PROVIDER_KERNEL32;
    fixture->target.symbol_id = symbol_id;
    fixture->target.guest_value = TEST_GATEWAY_BASE;

    fixture->handler.provider_id = DB_WIN32_PROVIDER_KERNEL32;
    fixture->handler.symbol_id = symbol_id;
    fixture->handler.handler_id = handler_id;
    fixture->handler.reserved = 0u;

    fixture->signature.handler_id = handler_id;
    fixture->signature.argument_count = argument_count;
    fixture->signature.reserved = 0u;

    CHECK(db_gateway_emit_slots(&fixture->target,
                                1u,
                                TEST_GATEWAY_BASE,
                                TEST_SLOT_SIZE,
                                fixture->gateway,
                                sizeof(fixture->gateway),
                                &emit_summary) == DB_GATEWAY_EMIT_OK);
    CHECK(emit_summary.target_count == 1u);
    CHECK(emit_summary.bytes_written == sizeof(fixture->gateway));

    put_u64(fixture->stack,
            (size_t)(TEST_RSP - TEST_STACK_BASE),
            TEST_RETURN_ADDRESS);

    fixture->kernel32.process_id = 10u;
    fixture->kernel32.thread_id = 20u;
    fixture->kernel32.last_error = 5u;
    fixture->kernel32.reserved = 0u;

    for (index = 0u; index < 16u; ++index) {
        fixture->scalar.registers[index] =
            UINT64_C(0x1111000000000000) + (uint64_t)index;
    }
    fixture->scalar.registers[TEST_RCX] = first_argument;
    fixture->scalar.registers[TEST_RSP_INDEX] = TEST_RSP;
    fixture->scalar.rip = TEST_GATEWAY_BASE;
    fixture->scalar.rflags = DB_X64_RFLAGS_FIXED_ONE | DB_X64_RFLAGS_CF;

    CHECK(db_translated_terminal_exit_plan_trap(
              TEST_GATEWAY_BASE,
              (uint32_t)DB_IR_TRAP_INVALID_OPCODE,
              &fixture->terminal_exit) == DB_TRANSLATED_TERMINAL_EXIT_OK);
    CHECK(db_translated_terminal_exit_validate_observed(
              &fixture->terminal_exit) == DB_TRANSLATED_TERMINAL_EXIT_OK);
    return 1;
}

static db_win32_translated_gateway_bridge_status run_fixture(
    bridge_fixture *fixture,
    db_win32_translated_gateway_bridge_summary *summary)
{
    return db_win32_translated_gateway_bridge_run(
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

static int test_authenticated_continue_and_register_mapping(void)
{
    bridge_fixture fixture;
    db_x64_scalar_state before;
    db_win32_translated_gateway_bridge_summary summary;

    CHECK(fixture_init(&fixture,
                       DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR,
                       DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                       1u,
                       UINT64_C(42)));
    before = fixture.scalar;
    fill(&summary, sizeof(summary), UINT8_C(0xa5));

    CHECK(run_fixture(&fixture, &summary) ==
          DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_OK);
    CHECK(fixture.kernel32.last_error == 42u);
    CHECK(fixture.scalar.registers[TEST_RAX] == 0u);
    CHECK(fixture.scalar.registers[TEST_RCX] == 42u);
    CHECK(fixture.scalar.registers[TEST_RDX] ==
          before.registers[TEST_RDX]);
    CHECK(fixture.scalar.registers[TEST_RBX] ==
          before.registers[TEST_RBX]);
    CHECK(fixture.scalar.registers[TEST_RSP_INDEX] == TEST_RSP + 8u);
    CHECK(fixture.scalar.registers[TEST_RBP] ==
          before.registers[TEST_RBP]);
    CHECK(fixture.scalar.registers[TEST_RSI] ==
          before.registers[TEST_RSI]);
    CHECK(fixture.scalar.registers[TEST_RDI] ==
          before.registers[TEST_RDI]);
    CHECK(fixture.scalar.registers[TEST_R8] == before.registers[TEST_R8]);
    CHECK(fixture.scalar.registers[TEST_R9] == before.registers[TEST_R9]);
    CHECK(fixture.scalar.registers[TEST_R10] == before.registers[TEST_R10]);
    CHECK(fixture.scalar.registers[TEST_R11] == before.registers[TEST_R11]);
    CHECK(fixture.scalar.registers[TEST_R12] == before.registers[TEST_R12]);
    CHECK(fixture.scalar.registers[TEST_R13] == before.registers[TEST_R13]);
    CHECK(fixture.scalar.registers[TEST_R14] == before.registers[TEST_R14]);
    CHECK(fixture.scalar.registers[TEST_R15] == before.registers[TEST_R15]);
    CHECK(fixture.scalar.rip == TEST_RETURN_ADDRESS);
    CHECK(fixture.scalar.rflags == before.rflags);

    CHECK(summary.authenticated_target.provider_id ==
          DB_WIN32_PROVIDER_KERNEL32);
    CHECK(summary.authenticated_target.symbol_id ==
          DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR);
    CHECK(summary.authenticated_target.guest_value == TEST_GATEWAY_BASE);
    CHECK(summary.slot_index == 0u);
    CHECK(summary.format_version ==
          DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_FORMAT_VERSION);
    CHECK(summary.gateway_consumed == 1u);
    CHECK(summary.reserved[0] == 0u &&
          summary.reserved[1] == 0u &&
          summary.reserved[2] == 0u);
    CHECK(summary.request.rip == TEST_GATEWAY_BASE);
    CHECK(summary.request.return_address == TEST_RETURN_ADDRESS);
    CHECK(summary.request.argument_count == 1u);
    CHECK(summary.request.arguments[0] == 42u);
    CHECK(summary.dispatch.outcome == DB_WIN32_RUNTIME_OUTCOME_CONTINUE);
    CHECK(summary.dispatch.exit_code == 0u);
    return 1;
}

static int test_exit_process_propagates_terminal_outcome(void)
{
    bridge_fixture fixture;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar_before;
    db_win32_translated_gateway_bridge_summary summary;

    CHECK(fixture_init(&fixture,
                       DB_WIN32_SYMBOL_KERNEL32_EXIT_PROCESS,
                       DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS,
                       1u,
                       UINT64_C(37)));
    kernel_before = fixture.kernel32;
    scalar_before = fixture.scalar;
    fill(&summary, sizeof(summary), UINT8_C(0xa5));

    CHECK(run_fixture(&fixture, &summary) ==
          DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_OK);
    CHECK(same(&fixture.kernel32,
               &kernel_before,
               sizeof(kernel_before)));
    CHECK(same(&fixture.scalar,
               &scalar_before,
               sizeof(scalar_before)));
    CHECK(summary.gateway_consumed == 1u);
    CHECK(summary.dispatch.outcome ==
          DB_WIN32_RUNTIME_OUTCOME_GUEST_TERMINATED);
    CHECK(summary.dispatch.exit_code == 37u);
    CHECK(summary.request.arguments[0] == 37u);
    return 1;
}

static int test_ordinary_ud2_is_not_consumed(void)
{
    bridge_fixture fixture;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar_before;
    db_win32_translated_gateway_bridge_summary summary;
    db_win32_translated_gateway_bridge_summary summary_before;
    const uint64_t ordinary_ud2 = UINT64_C(0x401100);

    CHECK(fixture_init(&fixture,
                       DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR,
                       DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                       1u,
                       UINT64_C(42)));
    CHECK(db_translated_terminal_exit_plan_trap(
              ordinary_ud2,
              (uint32_t)DB_IR_TRAP_INVALID_OPCODE,
              &fixture.terminal_exit) == DB_TRANSLATED_TERMINAL_EXIT_OK);
    fixture.scalar.rip = ordinary_ud2;
    kernel_before = fixture.kernel32;
    scalar_before = fixture.scalar;
    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    summary_before = summary;

    CHECK(run_fixture(&fixture, &summary) ==
          DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_NOT_GATEWAY);
    CHECK(same(&fixture.kernel32,
               &kernel_before,
               sizeof(kernel_before)));
    CHECK(same(&fixture.scalar,
               &scalar_before,
               sizeof(scalar_before)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}

static int test_interior_gateway_rip_is_not_consumed(void)
{
    bridge_fixture fixture;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar_before;
    db_win32_translated_gateway_bridge_summary summary;
    db_win32_translated_gateway_bridge_summary summary_before;
    const uint64_t interior = TEST_GATEWAY_BASE + UINT64_C(1);

    CHECK(fixture_init(&fixture,
                       DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR,
                       DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                       1u,
                       UINT64_C(42)));
    CHECK(db_translated_terminal_exit_plan_trap(
              interior,
              (uint32_t)DB_IR_TRAP_INVALID_OPCODE,
              &fixture.terminal_exit) == DB_TRANSLATED_TERMINAL_EXIT_OK);
    fixture.scalar.rip = interior;
    kernel_before = fixture.kernel32;
    scalar_before = fixture.scalar;
    fill(&summary, sizeof(summary), UINT8_C(0x5a));
    summary_before = summary;

    CHECK(run_fixture(&fixture, &summary) ==
          DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_NOT_GATEWAY);
    CHECK(same(&fixture.kernel32,
               &kernel_before,
               sizeof(kernel_before)));
    CHECK(same(&fixture.scalar,
               &scalar_before,
               sizeof(scalar_before)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}

static int test_malformed_dbgt_is_transactional(void)
{
    bridge_fixture fixture;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar_before;
    db_win32_translated_gateway_bridge_summary summary;
    db_win32_translated_gateway_bridge_summary summary_before;

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

    CHECK(run_fixture(&fixture, &summary) ==
          DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_AUTHENTICATION_FAILED);
    CHECK(same(&fixture.kernel32,
               &kernel_before,
               sizeof(kernel_before)));
    CHECK(same(&fixture.scalar,
               &scalar_before,
               sizeof(scalar_before)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}

static int test_stale_target_identity_is_transactional(void)
{
    bridge_fixture fixture;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar_before;
    db_win32_translated_gateway_bridge_summary summary;
    db_win32_translated_gateway_bridge_summary summary_before;

    CHECK(fixture_init(&fixture,
                       DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR,
                       DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                       1u,
                       UINT64_C(42)));
    fixture.target.symbol_id = DB_WIN32_SYMBOL_KERNEL32_GET_LAST_ERROR;
    kernel_before = fixture.kernel32;
    scalar_before = fixture.scalar;
    fill(&summary, sizeof(summary), UINT8_C(0x5a));
    summary_before = summary;

    CHECK(run_fixture(&fixture, &summary) ==
          DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_IDENTITY_MISMATCH);
    CHECK(same(&fixture.kernel32,
               &kernel_before,
               sizeof(kernel_before)));
    CHECK(same(&fixture.scalar,
               &scalar_before,
               sizeof(scalar_before)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}

static int test_runtime_failure_is_transactional(void)
{
    bridge_fixture fixture;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar_before;
    db_win32_translated_gateway_bridge_summary summary;
    db_win32_translated_gateway_bridge_summary summary_before;

    CHECK(fixture_init(&fixture,
                       DB_WIN32_SYMBOL_KERNEL32_GET_LAST_ERROR,
                       DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS,
                       1u,
                       UINT64_C(37)));
    kernel_before = fixture.kernel32;
    scalar_before = fixture.scalar;
    fill(&summary, sizeof(summary), UINT8_C(0x5a));
    summary_before = summary;

    CHECK(run_fixture(&fixture, &summary) ==
          DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_RUNTIME_DISPATCH_FAILED);
    CHECK(same(&fixture.kernel32,
               &kernel_before,
               sizeof(kernel_before)));
    CHECK(same(&fixture.scalar,
               &scalar_before,
               sizeof(scalar_before)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}

static int test_bad_exit_and_state_are_transactional(void)
{
    bridge_fixture fixture;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar_before;
    db_win32_translated_gateway_bridge_summary summary;
    db_win32_translated_gateway_bridge_summary summary_before;

    CHECK(fixture_init(&fixture,
                       DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR,
                       DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                       1u,
                       UINT64_C(42)));
    kernel_before = fixture.kernel32;
    scalar_before = fixture.scalar;
    fill(&summary, sizeof(summary), UINT8_C(0x5a));
    summary_before = summary;

    fixture.terminal_exit.format_version += 1u;
    CHECK(run_fixture(&fixture, &summary) ==
          DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_BAD_TERMINAL_EXIT);
    CHECK(same(&fixture.kernel32,
               &kernel_before,
               sizeof(kernel_before)));
    CHECK(same(&fixture.scalar,
               &scalar_before,
               sizeof(scalar_before)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));

    CHECK(fixture_init(&fixture,
                       DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR,
                       DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                       1u,
                       UINT64_C(42)));
    kernel_before = fixture.kernel32;
    fixture.scalar.rip = TEST_GATEWAY_BASE + UINT64_C(8);
    scalar_before = fixture.scalar;

    CHECK(run_fixture(&fixture, &summary) ==
          DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_BAD_SCALAR_STATE);
    CHECK(same(&fixture.kernel32,
               &kernel_before,
               sizeof(kernel_before)));
    CHECK(same(&fixture.scalar,
               &scalar_before,
               sizeof(scalar_before)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}

static int test_argument_and_alias_guards(void)
{
    bridge_fixture fixture;
    db_win32_translated_gateway_bridge_summary summary;
    union {
        db_x64_scalar_state scalar;
        db_win32_translated_gateway_bridge_summary summary;
    } overlap;

    CHECK(fixture_init(&fixture,
                       DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR,
                       DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                       1u,
                       UINT64_C(42)));

    CHECK(db_win32_translated_gateway_bridge_run(
              NULL,
              fixture.gateway,
              sizeof(fixture.gateway),
              &fixture.target,
              1u,
              TEST_GATEWAY_BASE,
              TEST_SLOT_SIZE,
              &fixture.handler,
              1u,
              &fixture.signature,
              1u,
              fixture.stack,
              sizeof(fixture.stack),
              TEST_STACK_BASE,
              &fixture.kernel32,
              &fixture.scalar,
              &summary) ==
          DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_NULL_ARGUMENT);

    overlap.scalar = fixture.scalar;
    CHECK(db_win32_translated_gateway_bridge_run(
              &fixture.terminal_exit,
              fixture.gateway,
              sizeof(fixture.gateway),
              &fixture.target,
              1u,
              TEST_GATEWAY_BASE,
              TEST_SLOT_SIZE,
              &fixture.handler,
              1u,
              &fixture.signature,
              1u,
              fixture.stack,
              sizeof(fixture.stack),
              TEST_STACK_BASE,
              &fixture.kernel32,
              &overlap.scalar,
              &overlap.summary) ==
          DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_BUFFER_OVERLAP);
    return 1;
}

static int test_names(void)
{
    CHECK(db_win32_translated_gateway_bridge_status_name(
              DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_OK)[0] == 'o');
    CHECK(db_win32_translated_gateway_bridge_status_name(
              DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_NOT_GATEWAY)[0] == 't');
    CHECK(db_win32_translated_gateway_bridge_status_name(
              DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_AUTHENTICATION_FAILED)[0] ==
          'g');
    return 1;
}

int main(void)
{
    static const char pass[] =
        "WIN32 TRANSLATED GATEWAY BRIDGE TEST: PASS\n"
        "ORDINARY UD2 PRESERVATION: PASS\n"
        "DBGT AUTHENTICATED CONTINUE: PASS\n"
        "DBGT EXITPROCESS TERMINATION: PASS\n"
        "DBGT ADVERSARIAL TRANSACTIONALITY: PASS\n"
        "SCALAR GATEWAY REGISTER MAPPING: PASS\n";
    static const char fail[] =
        "WIN32 TRANSLATED GATEWAY BRIDGE TEST: FAIL\n";

    if (test_authenticated_continue_and_register_mapping() &&
        test_exit_process_propagates_terminal_outcome() &&
        test_ordinary_ud2_is_not_consumed() &&
        test_interior_gateway_rip_is_not_consumed() &&
        test_malformed_dbgt_is_transactional() &&
        test_stale_target_identity_is_transactional() &&
        test_runtime_failure_is_transactional() &&
        test_bad_exit_and_state_are_transactional() &&
        test_argument_and_alias_guards() &&
        test_names()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
