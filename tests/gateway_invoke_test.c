#include "dblyat/gateway_invoke.h"

#include <stdint.h>
#include <unistd.h>

#define CHECK(v) do { if (!(v)) return 0; } while (0)

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

static void make_valid(db_gateway_enter_summary *entry)
{
    fill(entry, sizeof(*entry), 0u);
    entry->lookup.rip = UINT64_C(0x10040);
    entry->lookup.slot_address = UINT64_C(0x10040);
    entry->lookup.slot_index = 1u;
    entry->lookup.provider_id = 2u;
    entry->lookup.symbol_id = 22u;
    entry->lookup.kind = DB_GATEWAY_LOOKUP_ENTRY;

    entry->dispatch.rip = entry->lookup.rip;
    entry->dispatch.slot_address = entry->lookup.slot_address;
    entry->dispatch.slot_index = entry->lookup.slot_index;
    entry->dispatch.provider_id = entry->lookup.provider_id;
    entry->dispatch.symbol_id = entry->lookup.symbol_id;
    entry->dispatch.handler_id = 202u;

    entry->frame.rip = entry->dispatch.rip;
    entry->frame.slot_address = entry->dispatch.slot_address;
    entry->frame.guest_rsp = UINT64_C(0x70000010);
    entry->frame.return_address = UINT64_C(0x401000);
    entry->frame.slot_index = entry->dispatch.slot_index;
    entry->frame.argument_count = 6u;
    entry->frame.provider_id = entry->dispatch.provider_id;
    entry->frame.symbol_id = entry->dispatch.symbol_id;
    entry->frame.handler_id = entry->dispatch.handler_id;
}

static int preserved(db_gateway_enter_summary *entry,
                     const uint64_t *arguments,
                     size_t argument_count,
                     db_gateway_invoke_status expected)
{
    db_gateway_invoke_request request;
    db_gateway_invoke_request before;

    fill(&request, sizeof(request), UINT8_C(0xa5));
    before = request;
    return db_gateway_invoke_plan(entry, arguments, argument_count,
                                  &request) == expected &&
           same(&before, &request, sizeof(request));
}

static int test_success(void)
{
    db_gateway_enter_summary entry;
    db_gateway_invoke_request request;
    uint64_t arguments[6] = {11u, 22u, 33u, 44u, 55u, 66u};
    size_t index;

    make_valid(&entry);
    fill(&request, sizeof(request), UINT8_C(0xa5));
    CHECK(db_gateway_invoke_plan(&entry, arguments, 6u, &request) ==
          DB_GATEWAY_INVOKE_OK);
    CHECK(request.rip == entry.frame.rip);
    CHECK(request.slot_address == entry.frame.slot_address);
    CHECK(request.guest_rsp == entry.frame.guest_rsp);
    CHECK(request.return_address == entry.frame.return_address);
    CHECK(request.slot_index == 1u && request.argument_count == 6u);
    CHECK(request.provider_id == 2u && request.symbol_id == 22u);
    CHECK(request.handler_id == 202u && request.reserved == 0u);
    for (index = 0u; index < 6u; ++index) {
        CHECK(request.arguments[index] == arguments[index]);
    }
    for (index = 6u; index < DB_GATEWAY_FRAME_MAX_ARGUMENTS; ++index) {
        CHECK(request.arguments[index] == 0u);
    }
    return 1;
}

static int test_failures(void)
{
    db_gateway_enter_summary entry;
    uint64_t arguments[6] = {0u};

    make_valid(&entry);
    entry.lookup.kind = DB_GATEWAY_LOOKUP_MISS;
    CHECK(preserved(&entry, arguments, 6u,
                    DB_GATEWAY_INVOKE_BAD_ENTRY_SUMMARY));

    make_valid(&entry);
    entry.dispatch.handler_id = 0u;
    CHECK(preserved(&entry, arguments, 6u,
                    DB_GATEWAY_INVOKE_BAD_ENTRY_SUMMARY));

    make_valid(&entry);
    entry.frame.symbol_id += 1u;
    CHECK(preserved(&entry, arguments, 6u,
                    DB_GATEWAY_INVOKE_BAD_ENTRY_SUMMARY));

    make_valid(&entry);
    entry.frame.return_address = 0u;
    CHECK(preserved(&entry, arguments, 6u,
                    DB_GATEWAY_INVOKE_BAD_ENTRY_SUMMARY));

    make_valid(&entry);
    CHECK(preserved(&entry, arguments, 5u,
                    DB_GATEWAY_INVOKE_ARGUMENT_COUNT_MISMATCH));
    return 1;
}

static int test_guards(void)
{
    db_gateway_enter_summary entry;
    db_gateway_invoke_request request;
    uint64_t arguments[6] = {0u};
    union alias_storage {
        db_gateway_invoke_request request;
        db_gateway_enter_summary entry;
    } alias;

    make_valid(&entry);
    CHECK(db_gateway_invoke_plan(NULL, arguments, 6u, &request) ==
          DB_GATEWAY_INVOKE_NULL_ARGUMENT);
    CHECK(db_gateway_invoke_plan(&entry, NULL, 6u, &request) ==
          DB_GATEWAY_INVOKE_NULL_ARGUMENT);
    CHECK(db_gateway_invoke_plan(&entry, arguments, 6u, NULL) ==
          DB_GATEWAY_INVOKE_NULL_ARGUMENT);

    alias.entry = entry;
    CHECK(db_gateway_invoke_plan(&alias.entry, arguments, 6u,
                                 &alias.request) ==
          DB_GATEWAY_INVOKE_BUFFER_OVERLAP);

    make_valid(&entry);
    CHECK(db_gateway_invoke_plan(&entry,
                                 (const uint64_t *)&request,
                                 DB_GATEWAY_FRAME_MAX_ARGUMENTS,
                                 &request) ==
          DB_GATEWAY_INVOKE_BUFFER_OVERLAP);

    CHECK(db_gateway_invoke_status_name(DB_GATEWAY_INVOKE_OK)[0] == 'o');
    CHECK(db_gateway_invoke_status_name((db_gateway_invoke_status)999)[0] ==
          'u');
    return 1;
}

int main(void)
{
    if (!test_success() || !test_failures() || !test_guards()) {
        static const char failure[] = "GATEWAY INVOKE TEST: FAIL\n";
        (void)write(STDERR_FILENO, failure, sizeof(failure) - 1u);
        return 1;
    }

    {
        static const char success[] = "GATEWAY INVOKE TEST: PASS\n";
        (void)write(STDOUT_FILENO, success, sizeof(success) - 1u);
    }
    return 0;
}
