#include "dblyat/win32_kernel32_gateway.h"
#include "dblyat/win32_provider.h"

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

static db_win32_kernel32_state make_provider_state(void)
{
    db_win32_kernel32_state state;

    state.process_id = 10u;
    state.thread_id = 20u;
    state.last_error = 5u;
    state.reserved = 0u;
    return state;
}

static void make_exit_request(db_gateway_invoke_request *request,
                              uint32_t exit_code)
{
    fill(request, sizeof(*request), 0u);
    request->rip = UINT64_C(0x10040);
    request->slot_address = request->rip;
    request->guest_rsp = UINT64_C(0x70000010);
    request->return_address = UINT64_C(0x401000);
    request->slot_index = 1u;
    request->argument_count = 1u;
    request->provider_id = DB_WIN32_PROVIDER_KERNEL32;
    request->symbol_id = DB_WIN32_SYMBOL_KERNEL32_EXIT_PROCESS;
    request->handler_id = DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS;
    request->reserved = 0u;
    request->arguments[0] = exit_code;
}

static db_gateway_x64_guest_state make_guest(
    const db_gateway_invoke_request *request)
{
    db_gateway_x64_guest_state state;

    fill(&state, sizeof(state), 0u);
    state.rax = 7u;
    state.rbx = 8u;
    state.rcx = 9u;
    state.rsp = request->guest_rsp;
    state.rip = request->rip;
    return state;
}

static int test_provider_catalog(void)
{
    const db_win32_provider_record *catalog;
    db_win32_provider_match match;
    size_t count = 0u;

    catalog = db_win32_kernel32_catalog(&count);
    CHECK(catalog != NULL);
    CHECK(count == 5u);
    CHECK(db_win32_provider_lookup(catalog,
                                   count,
                                   "kernel32.dll",
                                   12u,
                                   "ExitProcess",
                                   11u,
                                   &match) == DB_WIN32_PROVIDER_OK);
    CHECK(match.catalog_index == 4u);
    CHECK(match.provider_id == DB_WIN32_PROVIDER_KERNEL32);
    CHECK(match.symbol_id == DB_WIN32_SYMBOL_KERNEL32_EXIT_PROCESS);
    CHECK(match.handler_id == DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS);
    CHECK(match.argument_count == 1u);
    CHECK(match.flags == DB_WIN32_PROVIDER_FLAG_PROCESS_STATE);
    CHECK(match.reserved == 0u);
    return 1;
}

static int test_provider_execution(void)
{
    db_win32_kernel32_state state = make_provider_state();
    db_win32_kernel32_state before = state;
    db_win32_kernel32_result result;
    db_win32_kernel32_result result_before;
    uint64_t argument = 37u;

    CHECK(db_win32_kernel32_execute(
              &state,
              DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS,
              &argument,
              1u,
              &result) == DB_WIN32_KERNEL32_OK);
    CHECK(same(&state, &before, sizeof(state)));
    CHECK(result.raw_result == 37u);
    CHECK(result.handler_id == DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS);
    CHECK(result.reserved == 0u);

    state = before;
    argument = UINT64_C(0x100000000);
    fill(&result, sizeof(result), UINT8_C(0xa5));
    result_before = result;
    CHECK(db_win32_kernel32_execute(
              &state,
              DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS,
              &argument,
              1u,
              &result) == DB_WIN32_KERNEL32_ARGUMENT_OUT_OF_RANGE);
    CHECK(same(&state, &before, sizeof(state)));
    CHECK(same(&result, &result_before, sizeof(result)));
    return 1;
}

static int test_invoke_plan(void)
{
    db_gateway_invoke_request request;
    db_win32_kernel32_state current = make_provider_state();
    db_win32_kernel32_state planned;
    db_gateway_invoke_result result;

    make_exit_request(&request, 37u);
    CHECK(db_win32_kernel32_invoke_plan(
              &request,
              &current,
              &planned,
              &result) == DB_WIN32_KERNEL32_INVOKE_OK);
    CHECK(same(&planned, &current, sizeof(current)));
    CHECK(result.rip == request.rip);
    CHECK(result.slot_address == request.slot_address);
    CHECK(result.raw_result == 37u);
    CHECK(result.slot_index == request.slot_index);
    CHECK(result.provider_id == DB_WIN32_PROVIDER_KERNEL32);
    CHECK(result.symbol_id == DB_WIN32_SYMBOL_KERNEL32_EXIT_PROCESS);
    CHECK(result.handler_id == DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS);
    CHECK(result.reserved == 0u);
    return 1;
}

static int test_gateway_termination(void)
{
    db_gateway_invoke_request request;
    db_win32_kernel32_state provider = make_provider_state();
    db_win32_kernel32_state provider_before;
    db_gateway_x64_guest_state guest;
    db_gateway_x64_guest_state guest_before;
    db_win32_kernel32_gateway_summary summary;
    db_gateway_auth_complete_summary zero_completion;

    make_exit_request(&request, 37u);
    guest = make_guest(&request);
    provider_before = provider;
    guest_before = guest;
    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    fill(&zero_completion, sizeof(zero_completion), 0u);

    CHECK(db_win32_kernel32_gateway_run(
              &request,
              &provider,
              &guest,
              &summary) == DB_WIN32_KERNEL32_GATEWAY_GUEST_TERMINATED);
    CHECK(same(&provider, &provider_before, sizeof(provider)));
    CHECK(same(&guest, &guest_before, sizeof(guest)));
    CHECK(summary.result.rip == request.rip);
    CHECK(summary.result.slot_address == request.slot_address);
    CHECK(summary.result.raw_result == 37u);
    CHECK(summary.result.slot_index == request.slot_index);
    CHECK(summary.result.provider_id == DB_WIN32_PROVIDER_KERNEL32);
    CHECK(summary.result.symbol_id == DB_WIN32_SYMBOL_KERNEL32_EXIT_PROCESS);
    CHECK(summary.result.handler_id == DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS);
    CHECK(summary.result.reserved == 0u);
    CHECK(same(&summary.completion,
               &zero_completion,
               sizeof(summary.completion)));
    CHECK(summary.reserved == 0u);
    CHECK(db_win32_kernel32_gateway_status_name(
              DB_WIN32_KERNEL32_GATEWAY_GUEST_TERMINATED)[0] == 'g');
    return 1;
}

int main(void)
{
    static const char pass[] = "WIN32 EXITPROCESS BOUNDARY TEST: PASS\n";
    static const char fail[] = "WIN32 EXITPROCESS BOUNDARY TEST: FAIL\n";

    if (test_provider_catalog() &&
        test_provider_execution() &&
        test_invoke_plan() &&
        test_gateway_termination()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
