#include "dblyat/gateway_auth_complete.h"

#include <stdint.h>
#include <unistd.h>

#define CHECK(value) do { if (!(value)) return 0; } while (0)

static db_gateway_complete_status forced_complete_status =
    DB_GATEWAY_COMPLETE_OK;

static void fill(void *data, size_t size, uint8_t value)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        bytes[index] = value;
    }
}

static int equal(const void *left, const void *right, size_t size)
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

db_gateway_complete_status db_gateway_complete_raw(
    const db_gateway_frame_summary *frame,
    uint64_t raw_result,
    db_gateway_x64_guest_state *state,
    db_gateway_complete_summary *summary)
{
    if (forced_complete_status != DB_GATEWAY_COMPLETE_OK) {
        state->rax = UINT64_C(0xbad0bad0);
        return forced_complete_status;
    }

    if (state->rip != frame->rip || state->rsp != frame->guest_rsp) {
        return DB_GATEWAY_COMPLETE_RETURN_APPLY_FAILED;
    }

    fill(summary, sizeof(*summary), 0u);
    summary->return_plan.source_rip = frame->rip;
    summary->return_plan.slot_address = frame->slot_address;
    summary->return_plan.next_rip = frame->return_address;
    summary->return_plan.previous_rsp = frame->guest_rsp;
    summary->return_plan.next_rsp = frame->guest_rsp + 8u;
    summary->return_plan.rax = raw_result;
    summary->return_plan.slot_index = frame->slot_index;
    summary->return_plan.argument_count = frame->argument_count;
    summary->return_plan.provider_id = frame->provider_id;
    summary->return_plan.symbol_id = frame->symbol_id;
    summary->return_plan.handler_id = frame->handler_id;

    summary->apply_summary.source_rip = frame->rip;
    summary->apply_summary.next_rip = frame->return_address;
    summary->apply_summary.previous_rsp = frame->guest_rsp;
    summary->apply_summary.next_rsp = frame->guest_rsp + 8u;
    summary->apply_summary.previous_rax = state->rax;
    summary->apply_summary.next_rax = raw_result;
    summary->apply_summary.slot_index = frame->slot_index;
    summary->apply_summary.argument_count = frame->argument_count;
    summary->apply_summary.provider_id = frame->provider_id;
    summary->apply_summary.symbol_id = frame->symbol_id;
    summary->apply_summary.handler_id = frame->handler_id;

    state->rax = raw_result;
    state->rsp = frame->guest_rsp + 8u;
    state->rip = frame->return_address;
    return DB_GATEWAY_COMPLETE_OK;
}

static void make_valid(db_gateway_invoke_request *request,
                       db_gateway_invoke_result *result,
                       db_gateway_x64_guest_state *state)
{
    size_t index;

    fill(request, sizeof(*request), 0u);
    request->rip = UINT64_C(0x10040);
    request->slot_address = request->rip;
    request->guest_rsp = UINT64_C(0x70000020);
    request->return_address = UINT64_C(0x401000);
    request->slot_index = 1u;
    request->argument_count = 6u;
    request->provider_id = 7u;
    request->symbol_id = 9u;
    request->handler_id = 11u;
    for (index = 0u; index < request->argument_count; ++index) {
        request->arguments[index] = UINT64_C(0x1000) + index;
    }

    fill(result, sizeof(*result), 0u);
    result->rip = request->rip;
    result->slot_address = request->slot_address;
    result->raw_result = UINT64_C(0xdeadbeefcafebabe);
    result->slot_index = request->slot_index;
    result->provider_id = request->provider_id;
    result->symbol_id = request->symbol_id;
    result->handler_id = request->handler_id;

    fill(state, sizeof(*state), UINT8_C(0x5a));
    state->rax = UINT64_C(0x1111222233334444);
    state->rsp = request->guest_rsp;
    state->rip = request->rip;
}

static int preserved(db_gateway_invoke_request *request,
                     db_gateway_invoke_result *result,
                     db_gateway_x64_guest_state *state,
                     db_gateway_auth_complete_status expected)
{
    db_gateway_x64_guest_state state_before = *state;
    db_gateway_auth_complete_summary summary;
    db_gateway_auth_complete_summary summary_before;

    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    summary_before = summary;

    return db_gateway_auth_complete(request, result, state, &summary) ==
               expected &&
           equal(state, &state_before, sizeof(*state)) &&
           equal(&summary, &summary_before, sizeof(summary));
}

static int test_success(void)
{
    db_gateway_invoke_request request;
    db_gateway_invoke_result result;
    db_gateway_x64_guest_state state;
    db_gateway_x64_guest_state before;
    db_gateway_auth_complete_summary summary;

    make_valid(&request, &result, &state);
    before = state;
    forced_complete_status = DB_GATEWAY_COMPLETE_OK;

    CHECK(db_gateway_auth_complete(&request, &result, &state, &summary) ==
          DB_GATEWAY_AUTH_COMPLETE_OK);
    CHECK(state.rax == result.raw_result);
    CHECK(state.rsp == request.guest_rsp + 8u);
    CHECK(state.rip == request.return_address);
    CHECK(state.rbx == before.rbx && state.r15 == before.r15);
    CHECK(summary.validated.raw_result == result.raw_result);
    CHECK(summary.validated.handler_id == request.handler_id);
    CHECK(summary.complete.return_plan.next_rip == request.return_address);
    CHECK(summary.complete.apply_summary.previous_rax == before.rax);
    return 1;
}

static int test_failures(void)
{
    db_gateway_invoke_request request;
    db_gateway_invoke_result result;
    db_gateway_x64_guest_state state;

    make_valid(&request, &result, &state);
    result.handler_id += 1u;
    CHECK(preserved(&request, &result, &state,
                    DB_GATEWAY_AUTH_COMPLETE_RESULT_FAILED));

    make_valid(&request, &result, &state);
    state.rip += 1u;
    CHECK(preserved(&request, &result, &state,
                    DB_GATEWAY_AUTH_COMPLETE_COMPLETION_FAILED));

    make_valid(&request, &result, &state);
    forced_complete_status = DB_GATEWAY_COMPLETE_RETURN_PLAN_FAILED;
    CHECK(preserved(&request, &result, &state,
                    DB_GATEWAY_AUTH_COMPLETE_COMPLETION_FAILED));
    forced_complete_status = DB_GATEWAY_COMPLETE_OK;
    return 1;
}

static int test_alias_null_and_names(void)
{
    union state_summary_alias {
        db_gateway_x64_guest_state state;
        db_gateway_auth_complete_summary summary;
    } alias;
    db_gateway_invoke_request request;
    db_gateway_invoke_result result;
    db_gateway_x64_guest_state state;
    db_gateway_auth_complete_summary summary;

    make_valid(&request, &result, &alias.state);
    CHECK(db_gateway_auth_complete(&request, &result, &alias.state,
                                   &alias.summary) ==
          DB_GATEWAY_AUTH_COMPLETE_BUFFER_OVERLAP);

    make_valid(&request, &result, &state);
    CHECK(db_gateway_auth_complete(NULL, &result, &state, &summary) ==
          DB_GATEWAY_AUTH_COMPLETE_NULL_ARGUMENT);
    CHECK(db_gateway_auth_complete(&request, NULL, &state, &summary) ==
          DB_GATEWAY_AUTH_COMPLETE_NULL_ARGUMENT);
    CHECK(db_gateway_auth_complete(&request, &result, NULL, &summary) ==
          DB_GATEWAY_AUTH_COMPLETE_NULL_ARGUMENT);
    CHECK(db_gateway_auth_complete(&request, &result, &state, NULL) ==
          DB_GATEWAY_AUTH_COMPLETE_NULL_ARGUMENT);
    CHECK(db_gateway_auth_complete_status_name(
              DB_GATEWAY_AUTH_COMPLETE_OK)[0] == 'o');
    CHECK(db_gateway_auth_complete_status_name(
              (db_gateway_auth_complete_status)99)[0] == 'u');
    return 1;
}

int main(void)
{
    static const char pass[] = "GATEWAY AUTH COMPLETE TEST: PASS\n";
    static const char fail[] = "GATEWAY AUTH COMPLETE TEST: FAIL\n";

    if (!test_success() || !test_failures() ||
        !test_alias_null_and_names()) {
        (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
        return 1;
    }

    (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
    return 0;
}
