#include "dblyat/win32_runtime_dispatch.h"
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

static db_win32_kernel32_state make_kernel32_state(void)
{
    db_win32_kernel32_state state;

    state.process_id = 10u;
    state.thread_id = 20u;
    state.last_error = 5u;
    state.reserved = 0u;
    return state;
}

static db_gateway_x64_guest_state make_guest(
    const db_gateway_invoke_request *request)
{
    db_gateway_x64_guest_state state;

    fill(&state, sizeof(state), 0u);
    state.rax = UINT64_C(7);
    state.rbx = UINT64_C(8);
    state.rcx = UINT64_C(9);
    state.rdx = UINT64_C(10);
    state.rsp = request->guest_rsp;
    state.rip = request->rip;
    return state;
}

static void make_request(db_gateway_invoke_request *request,
                         uint32_t symbol_id,
                         uint32_t handler_id,
                         size_t argument_count,
                         uint64_t first_argument)
{
    fill(request, sizeof(*request), 0u);
    request->rip = UINT64_C(0x10040);
    request->slot_address = request->rip;
    request->guest_rsp = UINT64_C(0x70000010);
    request->return_address = UINT64_C(0x401000);
    request->slot_index = 1u;
    request->argument_count = argument_count;
    request->provider_id = DB_WIN32_PROVIDER_KERNEL32;
    request->symbol_id = symbol_id;
    request->handler_id = handler_id;
    request->reserved = 0u;
    if (argument_count != 0u) {
        request->arguments[0] = first_argument;
    }
}

static int test_continue_matches_gateway(void)
{
    db_gateway_invoke_request request;
    db_win32_kernel32_state direct_provider;
    db_win32_kernel32_state runtime_provider;
    db_gateway_x64_guest_state direct_guest;
    db_gateway_x64_guest_state runtime_guest;
    db_win32_kernel32_gateway_summary direct_summary;
    db_win32_runtime_dispatch_summary runtime_summary;

    make_request(&request,
                 DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR,
                 DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                 1u,
                 42u);
    direct_provider = make_kernel32_state();
    runtime_provider = direct_provider;
    direct_guest = make_guest(&request);
    runtime_guest = direct_guest;
    fill(&direct_summary, sizeof(direct_summary), UINT8_C(0xa5));
    fill(&runtime_summary, sizeof(runtime_summary), UINT8_C(0xa5));

    CHECK(db_win32_kernel32_gateway_run(
              &request,
              &direct_provider,
              &direct_guest,
              &direct_summary) == DB_WIN32_KERNEL32_GATEWAY_OK);

    CHECK(db_win32_runtime_dispatch_run(
              &request,
              &runtime_provider,
              &runtime_guest,
              &runtime_summary) == DB_WIN32_RUNTIME_DISPATCH_OK);

    CHECK(same(&runtime_provider,
               &direct_provider,
               sizeof(runtime_provider)));
    CHECK(same(&runtime_guest,
               &direct_guest,
               sizeof(runtime_guest)));
    CHECK(same(&runtime_summary.gateway,
               &direct_summary,
               sizeof(direct_summary)));
    CHECK(runtime_summary.provider_id == DB_WIN32_PROVIDER_KERNEL32);
    CHECK(runtime_summary.outcome == DB_WIN32_RUNTIME_OUTCOME_CONTINUE);
    CHECK(runtime_summary.exit_code == 0u);
    CHECK(runtime_summary.reserved == 0u);
    CHECK(runtime_provider.last_error == 42u);
    return 1;
}

static int test_exit_process_propagates_terminal_outcome(void)
{
    db_gateway_invoke_request request;
    db_win32_kernel32_state direct_provider;
    db_win32_kernel32_state runtime_provider;
    db_win32_kernel32_state provider_before;
    db_gateway_x64_guest_state direct_guest;
    db_gateway_x64_guest_state runtime_guest;
    db_gateway_x64_guest_state guest_before;
    db_win32_kernel32_gateway_summary direct_summary;
    db_win32_runtime_dispatch_summary runtime_summary;

    make_request(&request,
                 DB_WIN32_SYMBOL_KERNEL32_EXIT_PROCESS,
                 DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS,
                 1u,
                 37u);
    direct_provider = make_kernel32_state();
    runtime_provider = direct_provider;
    provider_before = direct_provider;
    direct_guest = make_guest(&request);
    runtime_guest = direct_guest;
    guest_before = direct_guest;
    fill(&direct_summary, sizeof(direct_summary), UINT8_C(0xa5));
    fill(&runtime_summary, sizeof(runtime_summary), UINT8_C(0xa5));

    CHECK(db_win32_kernel32_gateway_run(
              &request,
              &direct_provider,
              &direct_guest,
              &direct_summary) ==
          DB_WIN32_KERNEL32_GATEWAY_GUEST_TERMINATED);

    CHECK(db_win32_runtime_dispatch_run(
              &request,
              &runtime_provider,
              &runtime_guest,
              &runtime_summary) == DB_WIN32_RUNTIME_DISPATCH_OK);

    CHECK(same(&direct_provider,
               &provider_before,
               sizeof(direct_provider)));
    CHECK(same(&runtime_provider,
               &provider_before,
               sizeof(runtime_provider)));
    CHECK(same(&direct_guest,
               &guest_before,
               sizeof(direct_guest)));
    CHECK(same(&runtime_guest,
               &guest_before,
               sizeof(runtime_guest)));
    CHECK(same(&runtime_summary.gateway,
               &direct_summary,
               sizeof(direct_summary)));
    CHECK(runtime_summary.provider_id == DB_WIN32_PROVIDER_KERNEL32);
    CHECK(runtime_summary.outcome ==
          DB_WIN32_RUNTIME_OUTCOME_GUEST_TERMINATED);
    CHECK(runtime_summary.exit_code == 37u);
    CHECK(runtime_summary.reserved == 0u);
    return 1;
}

static int test_unsupported_provider_is_transactional(void)
{
    db_gateway_invoke_request request;
    db_win32_kernel32_state provider = make_kernel32_state();
    db_win32_kernel32_state provider_before = provider;
    db_gateway_x64_guest_state guest;
    db_gateway_x64_guest_state guest_before;
    db_win32_runtime_dispatch_summary summary;
    db_win32_runtime_dispatch_summary summary_before;

    make_request(&request,
                 DB_WIN32_SYMBOL_KERNEL32_GET_LAST_ERROR,
                 DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR,
                 0u,
                 0u);
    request.provider_id = UINT32_C(0xfeed);
    guest = make_guest(&request);
    guest_before = guest;
    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    summary_before = summary;

    CHECK(db_win32_runtime_dispatch_run(
              &request,
              &provider,
              &guest,
              &summary) ==
          DB_WIN32_RUNTIME_DISPATCH_UNSUPPORTED_PROVIDER);
    CHECK(same(&provider, &provider_before, sizeof(provider)));
    CHECK(same(&guest, &guest_before, sizeof(guest)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}

static int test_gateway_failure_is_transactional(void)
{
    db_gateway_invoke_request request;
    db_win32_kernel32_state provider = make_kernel32_state();
    db_win32_kernel32_state provider_before = provider;
    db_gateway_x64_guest_state guest;
    db_gateway_x64_guest_state guest_before;
    db_win32_runtime_dispatch_summary summary;
    db_win32_runtime_dispatch_summary summary_before;

    make_request(&request,
                 DB_WIN32_SYMBOL_KERNEL32_GET_LAST_ERROR,
                 DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS,
                 1u,
                 37u);
    guest = make_guest(&request);
    guest_before = guest;
    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    summary_before = summary;

    CHECK(db_win32_runtime_dispatch_run(
              &request,
              &provider,
              &guest,
              &summary) == DB_WIN32_RUNTIME_DISPATCH_GATEWAY_FAILED);
    CHECK(same(&provider, &provider_before, sizeof(provider)));
    CHECK(same(&guest, &guest_before, sizeof(guest)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}

static int test_argument_guards(void)
{
    db_gateway_invoke_request request;
    db_win32_kernel32_state provider = make_kernel32_state();
    db_gateway_x64_guest_state guest;
    db_win32_runtime_dispatch_summary summary;
    union {
        db_gateway_invoke_request request;
        db_win32_runtime_dispatch_summary summary;
    } overlap;

    make_request(&request,
                 DB_WIN32_SYMBOL_KERNEL32_GET_LAST_ERROR,
                 DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR,
                 0u,
                 0u);
    guest = make_guest(&request);

    CHECK(db_win32_runtime_dispatch_run(
              NULL, &provider, &guest, &summary) ==
          DB_WIN32_RUNTIME_DISPATCH_NULL_ARGUMENT);
    CHECK(db_win32_runtime_dispatch_run(
              &request, NULL, &guest, &summary) ==
          DB_WIN32_RUNTIME_DISPATCH_NULL_ARGUMENT);
    CHECK(db_win32_runtime_dispatch_run(
              &request, &provider, NULL, &summary) ==
          DB_WIN32_RUNTIME_DISPATCH_NULL_ARGUMENT);
    CHECK(db_win32_runtime_dispatch_run(
              &request, &provider, &guest, NULL) ==
          DB_WIN32_RUNTIME_DISPATCH_NULL_ARGUMENT);

    overlap.request = request;
    CHECK(db_win32_runtime_dispatch_run(
              &overlap.request,
              &provider,
              &guest,
              &overlap.summary) ==
          DB_WIN32_RUNTIME_DISPATCH_BUFFER_OVERLAP);
    return 1;
}

static int test_names(void)
{
    CHECK(db_win32_runtime_dispatch_status_name(
              DB_WIN32_RUNTIME_DISPATCH_OK)[0] == 'o');
    CHECK(db_win32_runtime_dispatch_status_name(
              DB_WIN32_RUNTIME_DISPATCH_UNSUPPORTED_PROVIDER)[0] == 'u');
    CHECK(db_win32_runtime_outcome_name(
              DB_WIN32_RUNTIME_OUTCOME_CONTINUE)[0] == 'c');
    CHECK(db_win32_runtime_outcome_name(
              DB_WIN32_RUNTIME_OUTCOME_GUEST_TERMINATED)[0] == 'g');
    return 1;
}

int main(void)
{
    static const char pass[] = "WIN32 RUNTIME DISPATCH TEST: PASS\n";
    static const char fail[] = "WIN32 RUNTIME DISPATCH TEST: FAIL\n";

    if (test_continue_matches_gateway() &&
        test_exit_process_propagates_terminal_outcome() &&
        test_unsupported_provider_is_transactional() &&
        test_gateway_failure_is_transactional() &&
        test_argument_guards() &&
        test_names()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
