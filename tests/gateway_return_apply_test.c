#include "dblyat/gateway_return_apply.h"

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

static void make_valid(db_gateway_return_plan *plan,
                       db_gateway_x64_guest_state *state)
{
    fill(plan, sizeof(*plan), 0u);
    plan->source_rip = UINT64_C(0x10000);
    plan->slot_address = UINT64_C(0x10000);
    plan->next_rip = UINT64_C(0x401000);
    plan->previous_rsp = UINT64_C(0x70000010);
    plan->next_rsp = UINT64_C(0x70000018);
    plan->rax = UINT64_C(0xdeadbeefcafebabe);
    plan->slot_index = 3u;
    plan->argument_count = 6u;
    plan->provider_id = 7u;
    plan->symbol_id = 9u;
    plan->handler_id = 11u;

    fill(state, sizeof(*state), UINT8_C(0x5a));
    state->rax = UINT64_C(0x1111222233334444);
    state->rsp = plan->previous_rsp;
    state->rip = plan->source_rip;
}

static int preserved(db_gateway_return_plan *plan,
                     db_gateway_x64_guest_state *state,
                     db_gateway_return_apply_status expected)
{
    db_gateway_x64_guest_state state_before = *state;
    db_gateway_return_apply_summary summary;
    db_gateway_return_apply_summary summary_before;

    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    summary_before = summary;

    return db_gateway_return_apply_plan(plan,
                                        state,
                                        &summary) == expected &&
           equal(state, &state_before, sizeof(*state)) &&
           equal(&summary, &summary_before, sizeof(summary));
}

static int test_success(void)
{
    db_gateway_return_plan plan;
    db_gateway_x64_guest_state state;
    db_gateway_x64_guest_state expected;
    db_gateway_return_apply_summary summary;

    make_valid(&plan, &state);
    expected = state;
    expected.rax = plan.rax;
    expected.rsp = plan.next_rsp;
    expected.rip = plan.next_rip;

    CHECK(db_gateway_return_apply_plan(
              &plan,
              &state,
              &summary) == DB_GATEWAY_RETURN_APPLY_OK);
    CHECK(equal(&state, &expected, sizeof(state)));
    CHECK(summary.source_rip == plan.source_rip);
    CHECK(summary.next_rip == plan.next_rip);
    CHECK(summary.previous_rsp == plan.previous_rsp);
    CHECK(summary.next_rsp == plan.next_rsp);
    CHECK(summary.previous_rax == UINT64_C(0x1111222233334444));
    CHECK(summary.next_rax == plan.rax);
    CHECK(summary.slot_index == plan.slot_index);
    CHECK(summary.argument_count == plan.argument_count);
    CHECK(summary.provider_id == plan.provider_id);
    CHECK(summary.symbol_id == plan.symbol_id);
    CHECK(summary.handler_id == plan.handler_id);
    CHECK(summary.reserved == 0u);
    return 1;
}

static int test_stale_state(void)
{
    db_gateway_return_plan plan;
    db_gateway_x64_guest_state state;

    make_valid(&plan, &state);
    state.rip += 1u;
    CHECK(preserved(&plan,
                    &state,
                    DB_GATEWAY_RETURN_APPLY_STALE_STATE));

    make_valid(&plan, &state);
    state.rsp += 8u;
    CHECK(preserved(&plan,
                    &state,
                    DB_GATEWAY_RETURN_APPLY_STALE_STATE));
    return 1;
}

static int test_bad_plan(void)
{
    db_gateway_return_plan plan;
    db_gateway_x64_guest_state state;

    make_valid(&plan, &state);
    plan.slot_address += 1u;
    CHECK(preserved(&plan,
                    &state,
                    DB_GATEWAY_RETURN_APPLY_BAD_RETURN_PLAN));

    make_valid(&plan, &state);
    plan.slot_index = SIZE_MAX;
    CHECK(preserved(&plan,
                    &state,
                    DB_GATEWAY_RETURN_APPLY_BAD_RETURN_PLAN));

    make_valid(&plan, &state);
    plan.argument_count = DB_GATEWAY_FRAME_MAX_ARGUMENTS + 1u;
    CHECK(preserved(&plan,
                    &state,
                    DB_GATEWAY_RETURN_APPLY_BAD_RETURN_PLAN));

    make_valid(&plan, &state);
    plan.handler_id = 0u;
    CHECK(preserved(&plan,
                    &state,
                    DB_GATEWAY_RETURN_APPLY_BAD_RETURN_PLAN));

    make_valid(&plan, &state);
    plan.reserved = 1u;
    CHECK(preserved(&plan,
                    &state,
                    DB_GATEWAY_RETURN_APPLY_BAD_RETURN_PLAN));

    make_valid(&plan, &state);
    plan.next_rsp += 8u;
    CHECK(preserved(&plan,
                    &state,
                    DB_GATEWAY_RETURN_APPLY_BAD_RETURN_PLAN));
    return 1;
}

static int test_addresses(void)
{
    db_gateway_return_plan plan;
    db_gateway_x64_guest_state state;

    make_valid(&plan, &state);
    plan.next_rip = UINT64_C(0x0000800000000000);
    CHECK(preserved(&plan,
                    &state,
                    DB_GATEWAY_RETURN_APPLY_NONCANONICAL_ADDRESS));

    make_valid(&plan, &state);
    plan.previous_rsp = UINT64_MAX;
    plan.next_rsp = 7u;
    state.rsp = plan.previous_rsp;
    CHECK(preserved(&plan,
                    &state,
                    DB_GATEWAY_RETURN_APPLY_ADDRESS_OVERFLOW));

    make_valid(&plan, &state);
    plan.previous_rsp = UINT64_C(0x00007ffffffffff8);
    plan.next_rsp = UINT64_C(0x0000800000000000);
    state.rsp = plan.previous_rsp;
    CHECK(preserved(&plan,
                    &state,
                    DB_GATEWAY_RETURN_APPLY_NONCANONICAL_ADDRESS));
    return 1;
}

static int test_alias_null_and_names(void)
{
    union plan_state_alias {
        db_gateway_return_plan plan;
        db_gateway_x64_guest_state state;
    } plan_state;
    union state_summary_alias {
        db_gateway_x64_guest_state state;
        db_gateway_return_apply_summary summary;
    } state_summary;
    db_gateway_return_plan plan;
    db_gateway_x64_guest_state state;
    db_gateway_return_apply_summary summary;

    make_valid(&plan_state.plan, &state);
    CHECK(db_gateway_return_apply_plan(
              &plan_state.plan,
              &plan_state.state,
              &summary) == DB_GATEWAY_RETURN_APPLY_BUFFER_OVERLAP);

    make_valid(&plan, &state_summary.state);
    CHECK(db_gateway_return_apply_plan(
              &plan,
              &state_summary.state,
              &state_summary.summary) ==
          DB_GATEWAY_RETURN_APPLY_BUFFER_OVERLAP);

    make_valid(&plan, &state);
    CHECK(db_gateway_return_apply_plan(
              NULL,
              &state,
              &summary) == DB_GATEWAY_RETURN_APPLY_NULL_ARGUMENT);
    CHECK(db_gateway_return_apply_plan(
              &plan,
              NULL,
              &summary) == DB_GATEWAY_RETURN_APPLY_NULL_ARGUMENT);
    CHECK(db_gateway_return_apply_plan(
              &plan,
              &state,
              NULL) == DB_GATEWAY_RETURN_APPLY_NULL_ARGUMENT);
    CHECK(db_gateway_return_apply_status_name(
              DB_GATEWAY_RETURN_APPLY_OK)[0] == 'o');
    CHECK(db_gateway_return_apply_status_name(
              (db_gateway_return_apply_status)999)[0] == 'u');
    return 1;
}

int main(void)
{
    if (!test_success() ||
        !test_stale_state() ||
        !test_bad_plan() ||
        !test_addresses() ||
        !test_alias_null_and_names()) {
        static const char failure[] =
            "GATEWAY RETURN APPLY TEST: FAIL\n";
        (void)write(STDERR_FILENO,
                    failure,
                    sizeof(failure) - 1u);
        return 1;
    }

    {
        static const char success[] =
            "GATEWAY RETURN APPLY TEST: PASS\n";
        (void)write(STDOUT_FILENO,
                    success,
                    sizeof(success) - 1u);
    }

    return 0;
}
