#include "dblyat/gateway_executor.h"

#include <stdint.h>
#include <unistd.h>

#define CHECK(value) do { if (!(value)) return 0; } while (0)

typedef struct executor_context {
    size_t calls;
    uint64_t raw_result;
    uint32_t mode;
} executor_context;

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
        if (a[index] != b[index]) return 0;
    }
    return 1;
}

db_gateway_complete_status db_gateway_complete_raw(
    const db_gateway_frame_summary *frame,
    uint64_t raw_result,
    db_gateway_x64_guest_state *state,
    db_gateway_complete_summary *summary)
{
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

static db_gateway_executor_callback_status execute(
    void *opaque,
    const db_gateway_invoke_request *request,
    db_gateway_invoke_result *result)
{
    executor_context *context = (executor_context *)opaque;

    context->calls += 1u;
    if (context->mode == 1u) {
        return 77u;
    }

    result->rip = request->rip;
    result->slot_address = request->slot_address;
    result->raw_result = context->raw_result;
    result->slot_index = request->slot_index;
    result->provider_id = request->provider_id;
    result->symbol_id = request->symbol_id;
    result->handler_id = request->handler_id;
    result->reserved = 0u;

    if (context->mode == 2u) {
        result->handler_id += 1u;
    }
    return DB_GATEWAY_EXECUTOR_CALLBACK_OK;
}

static void make_valid(db_gateway_invoke_request *request,
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

    fill(state, sizeof(*state), UINT8_C(0x5a));
    state->rax = UINT64_C(0x1111222233334444);
    state->rsp = request->guest_rsp;
    state->rip = request->rip;
}

static int preserved(db_gateway_invoke_request *request,
                     executor_context *context,
                     db_gateway_x64_guest_state *state,
                     db_gateway_executor_status expected)
{
    db_gateway_x64_guest_state state_before = *state;
    db_gateway_executor_summary summary;
    db_gateway_executor_summary summary_before;

    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    summary_before = summary;

    return db_gateway_executor_run(request, execute, context, state, &summary) ==
               expected &&
           equal(state, &state_before, sizeof(*state)) &&
           equal(&summary, &summary_before, sizeof(summary));
}

static int test_success(void)
{
    db_gateway_invoke_request request;
    db_gateway_x64_guest_state state;
    db_gateway_executor_summary summary;
    executor_context context = {0u, UINT64_C(0xabcdef0123456789), 0u};

    make_valid(&request, &state);
    CHECK(db_gateway_executor_run(&request, execute, &context,
                                  &state, &summary) == DB_GATEWAY_EXECUTOR_OK);
    CHECK(context.calls == 1u);
    CHECK(state.rax == context.raw_result);
    CHECK(state.rsp == request.guest_rsp + 8u);
    CHECK(state.rip == request.return_address);
    CHECK(summary.result.raw_result == context.raw_result);
    CHECK(summary.completion.validated.handler_id == request.handler_id);
    CHECK(summary.callback_status == DB_GATEWAY_EXECUTOR_CALLBACK_OK);
    CHECK(summary.reserved == 0u);
    return 1;
}

static int test_failures(void)
{
    db_gateway_invoke_request request;
    db_gateway_x64_guest_state state;
    executor_context context;

    make_valid(&request, &state);
    request.arguments[request.argument_count] = 1u;
    context = (executor_context){0u, 1u, 0u};
    CHECK(preserved(&request, &context, &state,
                    DB_GATEWAY_EXECUTOR_BAD_REQUEST));
    CHECK(context.calls == 0u);

    make_valid(&request, &state);
    context = (executor_context){0u, 1u, 1u};
    CHECK(preserved(&request, &context, &state,
                    DB_GATEWAY_EXECUTOR_CALLBACK_FAILED));
    CHECK(context.calls == 1u);

    make_valid(&request, &state);
    context = (executor_context){0u, 1u, 2u};
    CHECK(preserved(&request, &context, &state,
                    DB_GATEWAY_EXECUTOR_COMPLETION_FAILED));
    CHECK(context.calls == 1u);

    make_valid(&request, &state);
    state.rip += 1u;
    context = (executor_context){0u, 1u, 0u};
    CHECK(preserved(&request, &context, &state,
                    DB_GATEWAY_EXECUTOR_COMPLETION_FAILED));
    CHECK(context.calls == 1u);
    return 1;
}

static int test_guards(void)
{
    union state_summary_alias {
        db_gateway_x64_guest_state state;
        db_gateway_executor_summary summary;
    } alias;
    db_gateway_invoke_request request;
    db_gateway_x64_guest_state state;
    db_gateway_executor_summary summary;
    executor_context context = {0u, 1u, 0u};

    make_valid(&request, &alias.state);
    CHECK(db_gateway_executor_run(&request, execute, &context,
                                  &alias.state, &alias.summary) ==
          DB_GATEWAY_EXECUTOR_BUFFER_OVERLAP);

    make_valid(&request, &state);
    CHECK(db_gateway_executor_run(NULL, execute, &context, &state, &summary) ==
          DB_GATEWAY_EXECUTOR_NULL_ARGUMENT);
    CHECK(db_gateway_executor_run(&request, NULL, &context, &state, &summary) ==
          DB_GATEWAY_EXECUTOR_NULL_ARGUMENT);
    CHECK(db_gateway_executor_run(&request, execute, &context, NULL, &summary) ==
          DB_GATEWAY_EXECUTOR_NULL_ARGUMENT);
    CHECK(db_gateway_executor_run(&request, execute, &context, &state, NULL) ==
          DB_GATEWAY_EXECUTOR_NULL_ARGUMENT);
    CHECK(db_gateway_executor_status_name(DB_GATEWAY_EXECUTOR_OK)[0] == 'o');
    CHECK(db_gateway_executor_status_name((db_gateway_executor_status)99)[0] ==
          'u');
    return 1;
}

int main(void)
{
    static const char pass[] = "GATEWAY EXECUTOR TEST: PASS\n";
    static const char fail[] = "GATEWAY EXECUTOR TEST: FAIL\n";

    if (!test_success() || !test_failures() || !test_guards()) {
        (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
        return 1;
    }

    (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
    return 0;
}
