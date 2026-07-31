#include "dblyat/gateway_complete.h"

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

static void make_valid(db_gateway_frame_summary *frame,
                       db_gateway_x64_guest_state *state)
{
    fill(frame, sizeof(*frame), 0u);
    frame->rip = UINT64_C(0x10000);
    frame->slot_address = UINT64_C(0x10000);
    frame->guest_rsp = UINT64_C(0x70000010);
    frame->return_address = UINT64_C(0x401000);
    frame->slot_index = 3u;
    frame->argument_count = 6u;
    frame->provider_id = 7u;
    frame->symbol_id = 9u;
    frame->handler_id = 11u;

    fill(state, sizeof(*state), UINT8_C(0x5a));
    state->rax = UINT64_C(0x1111222233334444);
    state->rsp = frame->guest_rsp;
    state->rip = frame->rip;
}

static int preserved(db_gateway_frame_summary *frame,
                     db_gateway_x64_guest_state *state,
                     uint64_t raw_result,
                     db_gateway_complete_status expected)
{
    db_gateway_x64_guest_state state_before = *state;
    db_gateway_complete_summary summary;
    db_gateway_complete_summary summary_before;

    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    summary_before = summary;

    return db_gateway_complete_raw(frame,
                                   raw_result,
                                   state,
                                   &summary) == expected &&
           equal(state, &state_before, sizeof(*state)) &&
           equal(&summary, &summary_before, sizeof(summary));
}

static int test_success(void)
{
    db_gateway_frame_summary frame;
    db_gateway_x64_guest_state state;
    db_gateway_x64_guest_state expected;
    db_gateway_complete_summary summary;
    const uint64_t result = UINT64_C(0xdeadbeefcafebabe);

    make_valid(&frame, &state);
    expected = state;
    expected.rax = result;
    expected.rsp = frame.guest_rsp + 8u;
    expected.rip = frame.return_address;

    CHECK(db_gateway_complete_raw(&frame,
                                  result,
                                  &state,
                                  &summary) == DB_GATEWAY_COMPLETE_OK);
    CHECK(equal(&state, &expected, sizeof(state)));
    CHECK(summary.return_plan.source_rip == frame.rip);
    CHECK(summary.return_plan.next_rip == frame.return_address);
    CHECK(summary.return_plan.previous_rsp == frame.guest_rsp);
    CHECK(summary.return_plan.next_rsp == frame.guest_rsp + 8u);
    CHECK(summary.return_plan.rax == result);
    CHECK(summary.return_plan.provider_id == frame.provider_id);
    CHECK(summary.return_plan.symbol_id == frame.symbol_id);
    CHECK(summary.return_plan.handler_id == frame.handler_id);
    CHECK(summary.apply_summary.previous_rax ==
          UINT64_C(0x1111222233334444));
    CHECK(summary.apply_summary.next_rax == result);
    CHECK(summary.apply_summary.next_rip == frame.return_address);
    CHECK(summary.apply_summary.next_rsp == frame.guest_rsp + 8u);

    make_valid(&frame, &state);
    CHECK(db_gateway_complete_raw(&frame,
                                  0u,
                                  &state,
                                  &summary) == DB_GATEWAY_COMPLETE_OK);
    CHECK(state.rax == 0u);
    return 1;
}

static int test_planning_failure(void)
{
    db_gateway_frame_summary frame;
    db_gateway_x64_guest_state state;

    make_valid(&frame, &state);
    frame.return_address = 0u;
    CHECK(preserved(&frame,
                    &state,
                    1u,
                    DB_GATEWAY_COMPLETE_RETURN_PLAN_FAILED));

    make_valid(&frame, &state);
    frame.guest_rsp = UINT64_MAX;
    state.rsp = frame.guest_rsp;
    CHECK(preserved(&frame,
                    &state,
                    1u,
                    DB_GATEWAY_COMPLETE_RETURN_PLAN_FAILED));
    return 1;
}

static int test_apply_failure(void)
{
    db_gateway_frame_summary frame;
    db_gateway_x64_guest_state state;

    make_valid(&frame, &state);
    state.rip += 1u;
    CHECK(preserved(&frame,
                    &state,
                    1u,
                    DB_GATEWAY_COMPLETE_RETURN_APPLY_FAILED));

    make_valid(&frame, &state);
    state.rsp += 8u;
    CHECK(preserved(&frame,
                    &state,
                    1u,
                    DB_GATEWAY_COMPLETE_RETURN_APPLY_FAILED));
    return 1;
}

static int test_alias_null_and_names(void)
{
    union frame_state_alias {
        db_gateway_frame_summary frame;
        db_gateway_x64_guest_state state;
    } frame_state;
    union state_summary_alias {
        db_gateway_x64_guest_state state;
        db_gateway_complete_summary summary;
    } state_summary;
    db_gateway_frame_summary frame;
    db_gateway_x64_guest_state state;
    db_gateway_complete_summary summary;

    make_valid(&frame_state.frame, &state);
    CHECK(db_gateway_complete_raw(
              &frame_state.frame,
              1u,
              &frame_state.state,
              &summary) == DB_GATEWAY_COMPLETE_BUFFER_OVERLAP);

    make_valid(&frame, &state_summary.state);
    CHECK(db_gateway_complete_raw(
              &frame,
              1u,
              &state_summary.state,
              &state_summary.summary) == DB_GATEWAY_COMPLETE_BUFFER_OVERLAP);

    make_valid(&frame, &state);
    CHECK(db_gateway_complete_raw(
              NULL,
              1u,
              &state,
              &summary) == DB_GATEWAY_COMPLETE_NULL_ARGUMENT);
    CHECK(db_gateway_complete_raw(
              &frame,
              1u,
              NULL,
              &summary) == DB_GATEWAY_COMPLETE_NULL_ARGUMENT);
    CHECK(db_gateway_complete_raw(
              &frame,
              1u,
              &state,
              NULL) == DB_GATEWAY_COMPLETE_NULL_ARGUMENT);
    CHECK(db_gateway_complete_status_name(
              DB_GATEWAY_COMPLETE_OK)[0] == 'o');
    CHECK(db_gateway_complete_status_name(
              (db_gateway_complete_status)999)[0] == 'u');
    return 1;
}

int main(void)
{
    if (!test_success() ||
        !test_planning_failure() ||
        !test_apply_failure() ||
        !test_alias_null_and_names()) {
        static const char failure[] =
            "GATEWAY COMPLETE TEST: FAIL\n";
        (void)write(STDERR_FILENO,
                    failure,
                    sizeof(failure) - 1u);
        return 1;
    }

    {
        static const char success[] =
            "GATEWAY COMPLETE TEST: PASS\n";
        (void)write(STDOUT_FILENO,
                    success,
                    sizeof(success) - 1u);
    }

    return 0;
}
