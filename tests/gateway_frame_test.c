#include "dblyat/gateway_frame.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define STACK_BASE UINT64_C(0x70000000)
#define STACK_SIZE 128u
#define ARG_CAPACITY 8u
#define CHECK(value) do { if (!(value)) return 0; } while (0)

typedef struct fixture {
    db_gateway_dispatch_plan dispatch;
    db_gateway_frame_signature signatures[3];
    db_gateway_guest_registers registers;
    uint8_t stack[STACK_SIZE];
    uint64_t arguments[ARG_CAPACITY];
    db_gateway_frame_summary summary;
} fixture;

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

static void put_u64(uint8_t *bytes, size_t offset, uint64_t value)
{
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        bytes[offset + index] =
            (uint8_t)(value >> (index * 8u));
    }
}

static void make_valid(fixture *value)
{
    fill(value, sizeof(*value), 0u);

    value->dispatch.rip = UINT64_C(0x10000);
    value->dispatch.slot_address = UINT64_C(0x10000);
    value->dispatch.slot_index = 3u;
    value->dispatch.provider_id = 7u;
    value->dispatch.symbol_id = 9u;
    value->dispatch.handler_id = 11u;

    value->signatures[0].handler_id = 5u;
    value->signatures[0].argument_count = 1u;
    value->signatures[1].handler_id = 11u;
    value->signatures[1].argument_count = 6u;
    value->signatures[2].handler_id = 17u;

    value->registers.rcx = UINT64_C(0x11);
    value->registers.rdx = UINT64_C(0x22);
    value->registers.r8 = UINT64_C(0x33);
    value->registers.r9 = UINT64_C(0x44);
    value->registers.rsp = STACK_BASE + 16u;

    fill(value->stack, sizeof(value->stack), UINT8_C(0xa5));
    put_u64(value->stack, 16u, UINT64_C(0x123456789abcdef0));
    put_u64(value->stack, 16u + 0x28u, UINT64_C(0x55));
    put_u64(value->stack, 16u + 0x30u, UINT64_C(0x66));
    fill(value->arguments, sizeof(value->arguments), UINT8_C(0xcc));
    fill(&value->summary, sizeof(value->summary), UINT8_C(0xdd));
}

static db_gateway_frame_status call(fixture *value,
                                    size_t stack_size,
                                    size_t capacity)
{
    return db_gateway_frame_plan_call(&value->dispatch,
                                      value->signatures,
                                      3u,
                                      &value->registers,
                                      value->stack,
                                      stack_size,
                                      STACK_BASE,
                                      value->arguments,
                                      capacity,
                                      &value->summary);
}

static int preserved(fixture *value,
                     db_gateway_frame_status expected,
                     size_t stack_size,
                     size_t capacity)
{
    uint64_t arguments[ARG_CAPACITY];
    db_gateway_frame_summary summary = value->summary;
    size_t index;

    for (index = 0u; index < ARG_CAPACITY; ++index) {
        arguments[index] = value->arguments[index];
    }

    return call(value, stack_size, capacity) == expected &&
           equal(arguments, value->arguments, sizeof(arguments)) &&
           equal(&summary, &value->summary, sizeof(summary));
}

static int test_success(void)
{
    fixture value;

    make_valid(&value);
    CHECK(call(&value, sizeof(value.stack), ARG_CAPACITY) ==
          DB_GATEWAY_FRAME_OK);
    CHECK(value.arguments[0] == UINT64_C(0x11));
    CHECK(value.arguments[1] == UINT64_C(0x22));
    CHECK(value.arguments[2] == UINT64_C(0x33));
    CHECK(value.arguments[3] == UINT64_C(0x44));
    CHECK(value.arguments[4] == UINT64_C(0x55));
    CHECK(value.arguments[5] == UINT64_C(0x66));
    CHECK(value.arguments[6] == UINT64_C(0xcccccccccccccccc));
    CHECK(value.summary.rip == value.dispatch.rip);
    CHECK(value.summary.slot_address == value.dispatch.slot_address);
    CHECK(value.summary.slot_index == value.dispatch.slot_index);
    CHECK(value.summary.provider_id == 7u);
    CHECK(value.summary.symbol_id == 9u);
    CHECK(value.summary.handler_id == 11u);
    CHECK(value.summary.guest_rsp == value.registers.rsp);
    CHECK(value.summary.return_address ==
          UINT64_C(0x123456789abcdef0));
    CHECK(value.summary.argument_count == 6u);
    CHECK(value.summary.reserved == 0u);

    make_valid(&value);
    value.signatures[1].argument_count = 0u;
    CHECK(db_gateway_frame_plan_call(&value.dispatch,
                                     value.signatures,
                                     3u,
                                     &value.registers,
                                     value.stack,
                                     sizeof(value.stack),
                                     STACK_BASE,
                                     NULL,
                                     0u,
                                     &value.summary) ==
          DB_GATEWAY_FRAME_OK);
    CHECK(value.summary.argument_count == 0u);
    CHECK(value.summary.return_address ==
          UINT64_C(0x123456789abcdef0));
    return 1;
}

static int test_transactional_failures(void)
{
    fixture value;

    make_valid(&value);
    value.signatures[2].handler_id = 11u;
    CHECK(preserved(&value,
                    DB_GATEWAY_FRAME_DUPLICATE_HANDLER_ID,
                    sizeof(value.stack),
                    ARG_CAPACITY));

    make_valid(&value);
    value.signatures[1].handler_id = 12u;
    CHECK(preserved(&value,
                    DB_GATEWAY_FRAME_SIGNATURE_NOT_FOUND,
                    sizeof(value.stack),
                    ARG_CAPACITY));

    make_valid(&value);
    value.signatures[2].reserved = 1u;
    CHECK(preserved(&value,
                    DB_GATEWAY_FRAME_BAD_SIGNATURE,
                    sizeof(value.stack),
                    ARG_CAPACITY));

    make_valid(&value);
    value.signatures[1].argument_count =
        (uint16_t)(DB_GATEWAY_FRAME_MAX_ARGUMENTS + 1u);
    CHECK(preserved(&value,
                    DB_GATEWAY_FRAME_BAD_SIGNATURE,
                    sizeof(value.stack),
                    ARG_CAPACITY));

    make_valid(&value);
    value.dispatch.slot_address += 1u;
    CHECK(preserved(&value,
                    DB_GATEWAY_FRAME_BAD_DISPATCH_PLAN,
                    sizeof(value.stack),
                    ARG_CAPACITY));

    make_valid(&value);
    CHECK(preserved(&value,
                    DB_GATEWAY_FRAME_STACK_OUT_OF_RANGE,
                    16u + 0x30u + 7u,
                    ARG_CAPACITY));

    make_valid(&value);
    CHECK(preserved(&value,
                    DB_GATEWAY_FRAME_OUTPUT_BUFFER_TOO_SMALL,
                    sizeof(value.stack),
                    5u));
    return 1;
}

static int test_address_and_window_failures(void)
{
    fixture value;

    make_valid(&value);
    value.registers.rsp = UINT64_C(0x0000800000000000);
    CHECK(preserved(&value,
                    DB_GATEWAY_FRAME_NONCANONICAL_ADDRESS,
                    sizeof(value.stack),
                    ARG_CAPACITY));

    make_valid(&value);
    value.registers.rsp = UINT64_C(0xffffffffffffffd8);
    CHECK(db_gateway_frame_plan_call(
              &value.dispatch,
              value.signatures,
              3u,
              &value.registers,
              value.stack,
              sizeof(value.stack),
              UINT64_C(0xffffffffffffff80),
              value.arguments,
              ARG_CAPACITY,
              &value.summary) ==
          DB_GATEWAY_FRAME_ADDRESS_OVERFLOW);

    make_valid(&value);
    CHECK(db_gateway_frame_plan_call(
              &value.dispatch,
              value.signatures,
              3u,
              &value.registers,
              NULL,
              0u,
              STACK_BASE,
              value.arguments,
              ARG_CAPACITY,
              &value.summary) ==
          DB_GATEWAY_FRAME_INVALID_STACK_WINDOW);

    make_valid(&value);
    CHECK(db_gateway_frame_plan_call(
              &value.dispatch,
              value.signatures,
              3u,
              &value.registers,
              value.stack,
              sizeof(value.stack),
              UINT64_C(0x00007fffffffffc0),
              value.arguments,
              ARG_CAPACITY,
              &value.summary) ==
          DB_GATEWAY_FRAME_NONCANONICAL_ADDRESS);
    return 1;
}

static int test_alias_and_overflow(void)
{
    fixture value;
    union stack_alias {
        uint8_t stack[STACK_SIZE];
        uint64_t arguments[STACK_SIZE / sizeof(uint64_t)];
    } stack_alias;
    union summary_alias {
        uint64_t arguments[ARG_CAPACITY];
        db_gateway_frame_summary summary;
    } summary_alias;
    db_gateway_frame_signature signature;
    uint64_t argument;
    db_gateway_frame_summary summary;

    make_valid(&value);
    fill(stack_alias.stack, sizeof(stack_alias.stack), 0u);
    CHECK(db_gateway_frame_plan_call(
              &value.dispatch,
              value.signatures,
              3u,
              &value.registers,
              stack_alias.stack,
              sizeof(stack_alias.stack),
              STACK_BASE,
              stack_alias.arguments,
              ARG_CAPACITY,
              &value.summary) ==
          DB_GATEWAY_FRAME_BUFFER_OVERLAP);

    make_valid(&value);
    CHECK(db_gateway_frame_plan_call(
              &value.dispatch,
              value.signatures,
              3u,
              &value.registers,
              value.stack,
              sizeof(value.stack),
              STACK_BASE,
              summary_alias.arguments,
              ARG_CAPACITY,
              &summary_alias.summary) ==
          DB_GATEWAY_FRAME_BUFFER_OVERLAP);

    signature.handler_id = 1u;
    signature.argument_count = 0u;
    signature.reserved = 0u;
    make_valid(&value);
    CHECK(db_gateway_frame_plan_call(
              &value.dispatch,
              &signature,
              SIZE_MAX,
              &value.registers,
              value.stack,
              sizeof(value.stack),
              STACK_BASE,
              &argument,
              1u,
              &summary) ==
          DB_GATEWAY_FRAME_COUNT_OVERFLOW);
    return 1;
}

static int test_arguments_and_names(void)
{
    fixture value;

    make_valid(&value);
    CHECK(db_gateway_frame_plan_call(
              NULL,
              value.signatures,
              3u,
              &value.registers,
              value.stack,
              sizeof(value.stack),
              STACK_BASE,
              value.arguments,
              ARG_CAPACITY,
              &value.summary) ==
          DB_GATEWAY_FRAME_NULL_ARGUMENT);
    CHECK(db_gateway_frame_status_name(DB_GATEWAY_FRAME_OK)[0] ==
          'o');
    CHECK(db_gateway_frame_status_name(
              (db_gateway_frame_status)999)[0] == 'u');
    return 1;
}

int main(void)
{
    if (!test_success() ||
        !test_transactional_failures() ||
        !test_address_and_window_failures() ||
        !test_alias_and_overflow() ||
        !test_arguments_and_names()) {
        static const char failure[] =
            "GATEWAY FRAME TEST: FAIL\n";
        (void)write(STDERR_FILENO,
                    failure,
                    sizeof(failure) - 1u);
        return 1;
    }

    {
        static const char success[] =
            "GATEWAY FRAME TEST: PASS\n";
        (void)write(STDOUT_FILENO,
                    success,
                    sizeof(success) - 1u);
    }

    return 0;
}
