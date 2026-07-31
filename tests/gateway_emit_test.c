#include "dblyat/gateway_emit.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define BASE UINT64_C(0x10000)
#define SLOT UINT64_C(64)
#define BUFFER_SIZE 192u
#define CHECK(condition) do { if (!(condition)) return 0; } while (0)

_Static_assert(offsetof(db_iat_target, guest_value) == 8u,
               "target layout mismatch");
_Static_assert(sizeof(db_iat_target) == 16u,
               "target size mismatch");

typedef struct fixture {
    db_iat_target targets[2];
    uint8_t buffer[BUFFER_SIZE];
    db_gateway_emit_summary summary;
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

static uint32_t read_u32(const uint8_t *bytes, size_t offset)
{
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t read_u64(const uint8_t *bytes, size_t offset)
{
    return (uint64_t)read_u32(bytes, offset) |
           ((uint64_t)read_u32(bytes, offset + 4u) << 32u);
}

static void make_valid(fixture *value)
{
    value->targets[0].provider_id = 1u;
    value->targets[0].symbol_id = 11u;
    value->targets[0].guest_value = BASE;
    value->targets[1].provider_id = 2u;
    value->targets[1].symbol_id = 22u;
    value->targets[1].guest_value = BASE + SLOT;
    fill(value->buffer, sizeof(value->buffer), UINT8_C(0x5a));
    fill(&value->summary, sizeof(value->summary), UINT8_C(0xa5));
}

static int slot_ok(const uint8_t *slot,
                   uint32_t provider,
                   uint32_t symbol,
                   uint64_t address)
{
    size_t index;

    CHECK(slot[0] == UINT8_C(0x0f));
    CHECK(slot[1] == UINT8_C(0x0b));
    CHECK(slot[2] == 1u && slot[3] == 0u);
    CHECK(slot[4] == (uint8_t)'D' && slot[5] == (uint8_t)'B');
    CHECK(slot[6] == (uint8_t)'G' && slot[7] == (uint8_t)'T');
    CHECK(read_u32(slot, 8u) == provider);
    CHECK(read_u32(slot, 12u) == symbol);
    CHECK(read_u64(slot, 16u) == address);

    for (index = 24u; index < 32u; ++index) {
        CHECK(slot[index] == 0u);
    }
    for (index = 32u; index < (size_t)SLOT; ++index) {
        CHECK(slot[index] == UINT8_C(0xcc));
    }
    return 1;
}

static int preserved(fixture *value, db_gateway_emit_status expected)
{
    uint8_t before[BUFFER_SIZE];
    db_gateway_emit_summary summary = value->summary;
    db_gateway_emit_status status;
    size_t index;

    for (index = 0u; index < sizeof(before); ++index) {
        before[index] = value->buffer[index];
    }
    status = db_gateway_emit_slots(value->targets, 2u, BASE, SLOT,
                                   value->buffer, sizeof(value->buffer),
                                   &value->summary);
    return status == expected &&
           equal(before, value->buffer, sizeof(before)) &&
           equal(&summary, &value->summary, sizeof(summary));
}

static int test_success(void)
{
    fixture value;
    size_t index;

    make_valid(&value);
    CHECK(db_gateway_emit_slots(value.targets, 2u, BASE, SLOT,
                                value.buffer, sizeof(value.buffer),
                                &value.summary) == DB_GATEWAY_EMIT_OK);
    CHECK(slot_ok(value.buffer, 1u, 11u, BASE));
    CHECK(slot_ok(value.buffer + (size_t)SLOT,
                  2u, 22u, BASE + SLOT));
    for (index = 2u * (size_t)SLOT; index < sizeof(value.buffer); ++index) {
        CHECK(value.buffer[index] == UINT8_C(0x5a));
    }
    CHECK(value.summary.target_count == 2u);
    CHECK(value.summary.bytes_written == 2u * (size_t)SLOT);
    CHECK(value.summary.gateway_base == BASE);
    CHECK(value.summary.slot_size == SLOT);
    CHECK(value.summary.format_version == 1u);
    return 1;
}

static int test_failures(void)
{
    fixture value;
    db_iat_target one;
    uint8_t byte = 0u;
    db_gateway_emit_summary summary;

    make_valid(&value);
    CHECK(db_gateway_emit_slots(NULL, 2u, BASE, SLOT,
                                value.buffer, sizeof(value.buffer),
                                &value.summary) ==
          DB_GATEWAY_EMIT_NULL_ARGUMENT);
    CHECK(db_gateway_emit_slots(value.targets, 2u, BASE, SLOT,
                                value.buffer, sizeof(value.buffer), NULL) ==
          DB_GATEWAY_EMIT_NULL_ARGUMENT);
    CHECK(db_gateway_emit_slots(value.targets, 2u, BASE, 16u,
                                value.buffer, sizeof(value.buffer),
                                &value.summary) ==
          DB_GATEWAY_EMIT_INVALID_SLOT_SIZE);
    CHECK(db_gateway_emit_slots(value.targets, 2u, BASE, 48u,
                                value.buffer, sizeof(value.buffer),
                                &value.summary) ==
          DB_GATEWAY_EMIT_INVALID_SLOT_SIZE);
    CHECK(db_gateway_emit_slots(value.targets, 2u, 0u, SLOT,
                                value.buffer, sizeof(value.buffer),
                                &value.summary) ==
          DB_GATEWAY_EMIT_INVALID_GATEWAY_BASE);
    CHECK(db_gateway_emit_slots(value.targets, 2u, BASE + 1u, SLOT,
                                value.buffer, sizeof(value.buffer),
                                &value.summary) ==
          DB_GATEWAY_EMIT_INVALID_GATEWAY_BASE);
    CHECK(db_gateway_emit_slots(value.targets, 2u,
                                UINT64_C(0x0000800000000000), SLOT,
                                value.buffer, sizeof(value.buffer),
                                &value.summary) ==
          DB_GATEWAY_EMIT_INVALID_GATEWAY_BASE);
    CHECK(db_gateway_emit_slots(value.targets, 2u, BASE, SLOT,
                                value.buffer, 2u * (size_t)SLOT - 1u,
                                &value.summary) ==
          DB_GATEWAY_EMIT_BUFFER_TOO_SMALL);

    one.provider_id = 1u;
    one.symbol_id = 1u;
    one.guest_value = BASE;
    CHECK(db_gateway_emit_slots(&one, SIZE_MAX, BASE, SLOT,
                                &byte, sizeof(byte), &summary) ==
          DB_GATEWAY_EMIT_COUNT_OVERFLOW);
    return 1;
}

static int test_transactional_failures(void)
{
    fixture value;

    make_valid(&value);
    value.targets[1].symbol_id = 0u;
    CHECK(preserved(&value, DB_GATEWAY_EMIT_BAD_TARGET));

    make_valid(&value);
    value.targets[1].provider_id = value.targets[0].provider_id;
    value.targets[1].symbol_id = value.targets[0].symbol_id;
    CHECK(preserved(&value, DB_GATEWAY_EMIT_DUPLICATE_TARGET_KEY));

    make_valid(&value);
    value.targets[1].guest_value += SLOT;
    CHECK(preserved(&value, DB_GATEWAY_EMIT_TARGET_ADDRESS_MISMATCH));
    return 1;
}

static int test_aliases(void)
{
    union storage {
        db_iat_target targets[2];
        uint8_t bytes[BUFFER_SIZE];
    } storage;
    union output {
        db_gateway_emit_summary summary;
        uint8_t bytes[BUFFER_SIZE];
    } output;
    fixture value;
    db_gateway_emit_summary summary;

    storage.targets[0].provider_id = 1u;
    storage.targets[0].symbol_id = 1u;
    storage.targets[0].guest_value = BASE;
    storage.targets[1].provider_id = 2u;
    storage.targets[1].symbol_id = 2u;
    storage.targets[1].guest_value = BASE + SLOT;
    CHECK(db_gateway_emit_slots(storage.targets, 2u, BASE, SLOT,
                                storage.bytes, sizeof(storage.bytes),
                                &summary) == DB_GATEWAY_EMIT_BUFFER_OVERLAP);

    make_valid(&value);
    CHECK(db_gateway_emit_slots(value.targets, 2u, BASE, SLOT,
                                output.bytes, sizeof(output.bytes),
                                &output.summary) ==
          DB_GATEWAY_EMIT_BUFFER_OVERLAP);
    return 1;
}

static int test_address_edges(void)
{
    fixture value;
    db_iat_target one;
    uint8_t buffer[(size_t)SLOT];
    db_gateway_emit_summary summary;
    uint64_t base;

    make_valid(&value);
    base = UINT64_C(0x00007fffffffffc0);
    value.targets[0].guest_value = base;
    value.targets[1].guest_value = base + SLOT;
    CHECK(db_gateway_emit_slots(value.targets, 2u, base, SLOT,
                                value.buffer, sizeof(value.buffer),
                                &value.summary) ==
          DB_GATEWAY_EMIT_NONCANONICAL_ADDRESS);

    base = UINT64_C(0xffffffffffffffc0);
    CHECK(db_gateway_emit_slots(value.targets, 2u, base, SLOT,
                                value.buffer, sizeof(value.buffer),
                                &value.summary) ==
          DB_GATEWAY_EMIT_ADDRESS_OVERFLOW);

    make_valid(&value);
    base = UINT64_C(0xffff800000000000);
    value.targets[0].guest_value = base;
    value.targets[1].guest_value = base + SLOT;
    CHECK(db_gateway_emit_slots(value.targets, 2u, base, SLOT,
                                value.buffer, sizeof(value.buffer),
                                &value.summary) == DB_GATEWAY_EMIT_OK);

    base = UINT64_C(0xffffffffffffffc0);
    one.provider_id = 9u;
    one.symbol_id = 99u;
    one.guest_value = base;
    CHECK(db_gateway_emit_slots(&one, 1u, base, SLOT,
                                buffer, sizeof(buffer), &summary) ==
          DB_GATEWAY_EMIT_OK);
    CHECK(slot_ok(buffer, 9u, 99u, base));
    return 1;
}

static int test_zero_and_names(void)
{
    db_gateway_emit_summary summary;

    CHECK(db_gateway_emit_slots(NULL, 0u, BASE, SLOT,
                                NULL, 0u, &summary) ==
          DB_GATEWAY_EMIT_OK);
    CHECK(summary.target_count == 0u && summary.bytes_written == 0u);
    CHECK(db_gateway_emit_status_name(DB_GATEWAY_EMIT_OK)[0] == 'o');
    CHECK(db_gateway_emit_status_name((db_gateway_emit_status)999)[0] == 'u');
    return 1;
}

int main(void)
{
    if (!test_success() ||
        !test_failures() ||
        !test_transactional_failures() ||
        !test_aliases() ||
        !test_address_edges() ||
        !test_zero_and_names()) {
        static const char failure[] = "GATEWAY EMIT TEST: FAIL\n";
        (void)write(STDERR_FILENO, failure, sizeof(failure) - 1u);
        return 1;
    }

    {
        static const char success[] = "GATEWAY EMIT TEST: PASS\n";
        (void)write(STDOUT_FILENO, success, sizeof(success) - 1u);
    }
    return 0;
}
