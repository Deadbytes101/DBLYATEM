#include "dblyat/gateway_dispatch.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) return 0; } while (0)
#define HANDLER_COUNT 3u

typedef struct fixture {
    db_gateway_lookup_result lookup;
    db_gateway_dispatch_handler handlers[HANDLER_COUNT];
    db_gateway_dispatch_plan plan;
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

static void make_valid(fixture *value)
{
    size_t index;

    fill(&value->lookup, sizeof(value->lookup), 0u);
    value->lookup.rip = UINT64_C(0x40040);
    value->lookup.slot_address = UINT64_C(0x40040);
    value->lookup.slot_index = 1u;
    value->lookup.provider_id = 20u;
    value->lookup.symbol_id = 200u;
    value->lookup.kind = DB_GATEWAY_LOOKUP_ENTRY;

    for (index = 0u; index < HANDLER_COUNT; ++index) {
        value->handlers[index].provider_id = (uint32_t)((index + 1u) * 10u);
        value->handlers[index].symbol_id = (uint32_t)((index + 1u) * 100u);
        value->handlers[index].handler_id = (uint32_t)(index + 1000u);
        value->handlers[index].reserved = 0u;
    }

    fill(&value->plan, sizeof(value->plan), UINT8_C(0xa5));
}

static int preserved(fixture *value,
                     db_gateway_dispatch_status expected)
{
    const db_gateway_dispatch_plan before = value->plan;
    const db_gateway_dispatch_status status =
        db_gateway_dispatch_plan_entry(&value->lookup,
                                       value->handlers,
                                       HANDLER_COUNT,
                                       &value->plan);

    return status == expected &&
           equal(&before, &value->plan, sizeof(before));
}

static int test_success(void)
{
    fixture value;

    make_valid(&value);
    CHECK(db_gateway_dispatch_plan_entry(&value.lookup,
                                         value.handlers,
                                         HANDLER_COUNT,
                                         &value.plan) ==
          DB_GATEWAY_DISPATCH_OK);
    CHECK(value.plan.rip == value.lookup.rip);
    CHECK(value.plan.slot_address == value.lookup.slot_address);
    CHECK(value.plan.slot_index == value.lookup.slot_index);
    CHECK(value.plan.provider_id == value.lookup.provider_id);
    CHECK(value.plan.symbol_id == value.lookup.symbol_id);
    CHECK(value.plan.handler_id == 1001u);
    CHECK(value.plan.reserved == 0u);
    return 1;
}

static int test_not_entry(void)
{
    fixture value;

    make_valid(&value);
    value.lookup.kind = DB_GATEWAY_LOOKUP_MISS;
    CHECK(preserved(&value, DB_GATEWAY_DISPATCH_NOT_ENTRY));

    make_valid(&value);
    value.lookup.kind = DB_GATEWAY_LOOKUP_INTERIOR;
    value.lookup.provider_id = 0u;
    value.lookup.symbol_id = 0u;
    CHECK(preserved(&value, DB_GATEWAY_DISPATCH_NOT_ENTRY));
    return 1;
}

static int test_bad_lookup(void)
{
    fixture value;

    make_valid(&value);
    value.lookup.kind = (db_gateway_lookup_kind)99u;
    CHECK(preserved(&value, DB_GATEWAY_DISPATCH_BAD_LOOKUP_RESULT));

    make_valid(&value);
    value.lookup.rip += 1u;
    CHECK(preserved(&value, DB_GATEWAY_DISPATCH_BAD_LOOKUP_RESULT));

    make_valid(&value);
    value.lookup.slot_index = SIZE_MAX;
    CHECK(preserved(&value, DB_GATEWAY_DISPATCH_BAD_LOOKUP_RESULT));

    make_valid(&value);
    value.lookup.rip = UINT64_C(0x0000800000000000);
    value.lookup.slot_address = value.lookup.rip;
    CHECK(preserved(&value, DB_GATEWAY_DISPATCH_BAD_LOOKUP_RESULT));

    make_valid(&value);
    value.lookup.provider_id = 0u;
    CHECK(preserved(&value, DB_GATEWAY_DISPATCH_BAD_LOOKUP_RESULT));

    make_valid(&value);
    value.lookup.reserved[6] = 1u;
    CHECK(preserved(&value, DB_GATEWAY_DISPATCH_BAD_LOOKUP_RESULT));
    return 1;
}

static int test_catalog_failures(void)
{
    fixture value;

    make_valid(&value);
    value.handlers[2].handler_id = 0u;
    CHECK(preserved(&value, DB_GATEWAY_DISPATCH_BAD_HANDLER));

    make_valid(&value);
    value.handlers[2].reserved = 1u;
    CHECK(preserved(&value, DB_GATEWAY_DISPATCH_BAD_HANDLER));

    make_valid(&value);
    value.handlers[2].provider_id = value.handlers[0].provider_id;
    value.handlers[2].symbol_id = value.handlers[0].symbol_id;
    CHECK(preserved(&value, DB_GATEWAY_DISPATCH_DUPLICATE_HANDLER_KEY));

    make_valid(&value);
    value.handlers[2].handler_id = value.handlers[0].handler_id;
    CHECK(preserved(&value, DB_GATEWAY_DISPATCH_DUPLICATE_HANDLER_ID));

    make_valid(&value);
    value.lookup.provider_id = 99u;
    value.lookup.symbol_id = 999u;
    CHECK(preserved(&value, DB_GATEWAY_DISPATCH_HANDLER_NOT_FOUND));
    return 1;
}

static int test_arguments_and_overflow(void)
{
    fixture value;
    db_gateway_dispatch_handler handler;
    db_gateway_dispatch_plan plan;

    make_valid(&value);
    CHECK(db_gateway_dispatch_plan_entry(NULL,
                                         value.handlers,
                                         HANDLER_COUNT,
                                         &value.plan) ==
          DB_GATEWAY_DISPATCH_NULL_ARGUMENT);
    CHECK(db_gateway_dispatch_plan_entry(&value.lookup,
                                         NULL,
                                         HANDLER_COUNT,
                                         &value.plan) ==
          DB_GATEWAY_DISPATCH_NULL_ARGUMENT);
    CHECK(db_gateway_dispatch_plan_entry(&value.lookup,
                                         value.handlers,
                                         HANDLER_COUNT,
                                         NULL) ==
          DB_GATEWAY_DISPATCH_NULL_ARGUMENT);

    handler.provider_id = 1u;
    handler.symbol_id = 1u;
    handler.handler_id = 1u;
    handler.reserved = 0u;
    CHECK(db_gateway_dispatch_plan_entry(&value.lookup,
                                         &handler,
                                         SIZE_MAX,
                                         &plan) ==
          DB_GATEWAY_DISPATCH_COUNT_OVERFLOW);
    return 1;
}

static int test_aliases(void)
{
    union lookup_output {
        db_gateway_lookup_result lookup;
        db_gateway_dispatch_plan plan;
    } lookup_output;
    union catalog_output {
        db_gateway_dispatch_handler handlers[HANDLER_COUNT];
        db_gateway_dispatch_plan plan;
    } catalog_output;
    union lookup_catalog {
        db_gateway_lookup_result lookup;
        db_gateway_dispatch_handler handlers[HANDLER_COUNT];
    } lookup_catalog;
    fixture value;

    make_valid(&value);
    lookup_output.lookup = value.lookup;
    CHECK(db_gateway_dispatch_plan_entry(&lookup_output.lookup,
                                         value.handlers,
                                         HANDLER_COUNT,
                                         &lookup_output.plan) ==
          DB_GATEWAY_DISPATCH_BUFFER_OVERLAP);

    make_valid(&value);
    catalog_output.handlers[0] = value.handlers[0];
    catalog_output.handlers[1] = value.handlers[1];
    catalog_output.handlers[2] = value.handlers[2];
    CHECK(db_gateway_dispatch_plan_entry(&value.lookup,
                                         catalog_output.handlers,
                                         HANDLER_COUNT,
                                         &catalog_output.plan) ==
          DB_GATEWAY_DISPATCH_BUFFER_OVERLAP);

    make_valid(&value);
    lookup_catalog.lookup = value.lookup;
    CHECK(db_gateway_dispatch_plan_entry(&lookup_catalog.lookup,
                                         lookup_catalog.handlers,
                                         HANDLER_COUNT,
                                         &value.plan) ==
          DB_GATEWAY_DISPATCH_BUFFER_OVERLAP);
    return 1;
}

static int test_zero_catalog_and_names(void)
{
    fixture value;

    make_valid(&value);
    CHECK(db_gateway_dispatch_plan_entry(&value.lookup,
                                         NULL,
                                         0u,
                                         &value.plan) ==
          DB_GATEWAY_DISPATCH_HANDLER_NOT_FOUND);
    CHECK(db_gateway_dispatch_status_name(DB_GATEWAY_DISPATCH_OK)[0] == 'o');
    CHECK(db_gateway_dispatch_status_name((db_gateway_dispatch_status)999)[0] ==
          'u');
    return 1;
}

int main(void)
{
    if (!test_success() ||
        !test_not_entry() ||
        !test_bad_lookup() ||
        !test_catalog_failures() ||
        !test_arguments_and_overflow() ||
        !test_aliases() ||
        !test_zero_catalog_and_names()) {
        static const char failure[] = "GATEWAY DISPATCH TEST: FAIL\n";
        (void)write(STDERR_FILENO, failure, sizeof(failure) - 1u);
        return 1;
    }

    {
        static const char success[] = "GATEWAY DISPATCH TEST: PASS\n";
        (void)write(STDOUT_FILENO, success, sizeof(success) - 1u);
    }
    return 0;
}
