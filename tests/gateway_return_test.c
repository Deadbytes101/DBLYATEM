#include "dblyat/gateway_return.h"

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

static void make_valid(db_gateway_frame_summary *frame)
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
}

static int preserved(db_gateway_frame_summary *frame,
                     db_gateway_return_status expected)
{
    db_gateway_return_plan plan;
    db_gateway_return_plan before;

    fill(&plan, sizeof(plan), UINT8_C(0xa5));
    before = plan;

    return db_gateway_return_plan_raw(
               frame,
               UINT64_C(0xdeadbeefcafebabe),
               &plan) == expected &&
           equal(&plan, &before, sizeof(plan));
}

static int test_success(void)
{
    db_gateway_frame_summary frame;
    db_gateway_return_plan plan;

    make_valid(&frame);
    CHECK(db_gateway_return_plan_raw(
              &frame,
              UINT64_C(0xdeadbeefcafebabe),
              &plan) == DB_GATEWAY_RETURN_OK);
    CHECK(plan.source_rip == frame.rip);
    CHECK(plan.slot_address == frame.slot_address);
    CHECK(plan.next_rip == frame.return_address);
    CHECK(plan.previous_rsp == frame.guest_rsp);
    CHECK(plan.next_rsp == frame.guest_rsp + 8u);
    CHECK(plan.rax == UINT64_C(0xdeadbeefcafebabe));
    CHECK(plan.slot_index == frame.slot_index);
    CHECK(plan.argument_count == frame.argument_count);
    CHECK(plan.provider_id == frame.provider_id);
    CHECK(plan.symbol_id == frame.symbol_id);
    CHECK(plan.handler_id == frame.handler_id);
    CHECK(plan.reserved == 0u);

    make_valid(&frame);
    frame.guest_rsp = UINT64_C(0xfffffffffffffff0);
    CHECK(db_gateway_return_plan_raw(
              &frame,
              0u,
              &plan) == DB_GATEWAY_RETURN_OK);
    CHECK(plan.next_rsp == UINT64_C(0xfffffffffffffff8));
    CHECK(plan.rax == 0u);
    return 1;
}

static int test_bad_frame(void)
{
    db_gateway_frame_summary frame;

    make_valid(&frame);
    frame.slot_address += 1u;
    CHECK(preserved(&frame,
                    DB_GATEWAY_RETURN_BAD_FRAME_SUMMARY));

    make_valid(&frame);
    frame.slot_index = SIZE_MAX;
    CHECK(preserved(&frame,
                    DB_GATEWAY_RETURN_BAD_FRAME_SUMMARY));

    make_valid(&frame);
    frame.argument_count = DB_GATEWAY_FRAME_MAX_ARGUMENTS + 1u;
    CHECK(preserved(&frame,
                    DB_GATEWAY_RETURN_BAD_FRAME_SUMMARY));

    make_valid(&frame);
    frame.provider_id = 0u;
    CHECK(preserved(&frame,
                    DB_GATEWAY_RETURN_BAD_FRAME_SUMMARY));

    make_valid(&frame);
    frame.reserved = 1u;
    CHECK(preserved(&frame,
                    DB_GATEWAY_RETURN_BAD_FRAME_SUMMARY));
    return 1;
}

static int test_addresses(void)
{
    db_gateway_frame_summary frame;

    make_valid(&frame);
    frame.rip = UINT64_C(0x0000800000000000);
    frame.slot_address = frame.rip;
    CHECK(preserved(&frame,
                    DB_GATEWAY_RETURN_NONCANONICAL_ADDRESS));

    make_valid(&frame);
    frame.return_address = 0u;
    CHECK(preserved(&frame,
                    DB_GATEWAY_RETURN_NONCANONICAL_ADDRESS));

    make_valid(&frame);
    frame.return_address = UINT64_C(0x0000800000000000);
    CHECK(preserved(&frame,
                    DB_GATEWAY_RETURN_NONCANONICAL_ADDRESS));

    make_valid(&frame);
    frame.guest_rsp = UINT64_MAX;
    CHECK(preserved(&frame,
                    DB_GATEWAY_RETURN_ADDRESS_OVERFLOW));

    make_valid(&frame);
    frame.guest_rsp = UINT64_C(0x00007ffffffffff8);
    CHECK(preserved(&frame,
                    DB_GATEWAY_RETURN_NONCANONICAL_ADDRESS));
    return 1;
}

static int test_alias_null_and_names(void)
{
    union alias {
        db_gateway_frame_summary frame;
        db_gateway_return_plan plan;
    } alias;
    db_gateway_frame_summary frame;
    db_gateway_return_plan plan;

    make_valid(&alias.frame);
    CHECK(db_gateway_return_plan_raw(
              &alias.frame,
              1u,
              &alias.plan) == DB_GATEWAY_RETURN_BUFFER_OVERLAP);

    make_valid(&frame);
    CHECK(db_gateway_return_plan_raw(
              NULL,
              1u,
              &plan) == DB_GATEWAY_RETURN_NULL_ARGUMENT);
    CHECK(db_gateway_return_plan_raw(
              &frame,
              1u,
              NULL) == DB_GATEWAY_RETURN_NULL_ARGUMENT);
    CHECK(db_gateway_return_status_name(
              DB_GATEWAY_RETURN_OK)[0] == 'o');
    CHECK(db_gateway_return_status_name(
              (db_gateway_return_status)999)[0] == 'u');
    return 1;
}

int main(void)
{
    if (!test_success() ||
        !test_bad_frame() ||
        !test_addresses() ||
        !test_alias_null_and_names()) {
        static const char failure[] =
            "GATEWAY RETURN TEST: FAIL\n";
        (void)write(STDERR_FILENO,
                    failure,
                    sizeof(failure) - 1u);
        return 1;
    }

    {
        static const char success[] =
            "GATEWAY RETURN TEST: PASS\n";
        (void)write(STDOUT_FILENO,
                    success,
                    sizeof(success) - 1u);
    }

    return 0;
}
