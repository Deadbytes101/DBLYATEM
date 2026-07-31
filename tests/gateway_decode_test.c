#include "dblyat/gateway_decode.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define BASE UINT64_C(0x20000)
#define SLOT UINT64_C(64)
#define SLOT_COUNT 2u
#define BUFFER_SIZE 160u
#define CHECK(condition) do { if (!(condition)) return 0; } while (0)

typedef struct fixture {
    uint8_t buffer[BUFFER_SIZE];
    db_iat_target targets[3];
    db_gateway_decode_summary summary;
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

static void put_u16(uint8_t *bytes, size_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
    bytes[offset + 2u] = (uint8_t)(value >> 16u);
    bytes[offset + 3u] = (uint8_t)(value >> 24u);
}

static void put_u64(uint8_t *bytes, size_t offset, uint64_t value)
{
    put_u32(bytes, offset, (uint32_t)value);
    put_u32(bytes, offset + 4u, (uint32_t)(value >> 32u));
}

static void put_slot(uint8_t *slot,
                     uint32_t provider,
                     uint32_t symbol,
                     uint64_t address)
{
    size_t index;

    fill(slot, (size_t)SLOT, UINT8_C(0xcc));
    slot[0] = UINT8_C(0x0f);
    slot[1] = UINT8_C(0x0b);
    put_u16(slot, 2u, 1u);
    slot[4] = (uint8_t)'D';
    slot[5] = (uint8_t)'B';
    slot[6] = (uint8_t)'G';
    slot[7] = (uint8_t)'T';
    put_u32(slot, 8u, provider);
    put_u32(slot, 12u, symbol);
    put_u64(slot, 16u, address);
    for (index = 24u; index < 32u; ++index) {
        slot[index] = 0u;
    }
}

static void make_valid(fixture *value)
{
    fill(value->buffer, sizeof(value->buffer), UINT8_C(0x5a));
    put_slot(value->buffer, 1u, 11u, BASE);
    put_slot(value->buffer + (size_t)SLOT, 2u, 22u, BASE + SLOT);
    fill(value->targets, sizeof(value->targets), UINT8_C(0xa5));
    fill(&value->summary, sizeof(value->summary), UINT8_C(0x5a));
}

static int preserved(fixture *value, db_gateway_decode_status expected)
{
    db_iat_target targets[3];
    db_gateway_decode_summary summary = value->summary;
    db_gateway_decode_status status;
    size_t index;

    for (index = 0u; index < 3u; ++index) {
        targets[index] = value->targets[index];
    }

    status = db_gateway_decode_slots(value->buffer,
                                     sizeof(value->buffer),
                                     SLOT_COUNT,
                                     BASE,
                                     SLOT,
                                     value->targets,
                                     3u,
                                     &value->summary);
    return status == expected &&
           equal(targets, value->targets, sizeof(targets)) &&
           equal(&summary, &value->summary, sizeof(summary));
}

static int test_success(void)
{
    fixture value;

    make_valid(&value);
    CHECK(db_gateway_decode_slots(value.buffer, sizeof(value.buffer),
                                  SLOT_COUNT, BASE, SLOT,
                                  value.targets, 3u, &value.summary) ==
          DB_GATEWAY_DECODE_OK);
    CHECK(value.targets[0].provider_id == 1u);
    CHECK(value.targets[0].symbol_id == 11u);
    CHECK(value.targets[0].guest_value == BASE);
    CHECK(value.targets[1].provider_id == 2u);
    CHECK(value.targets[1].symbol_id == 22u);
    CHECK(value.targets[1].guest_value == BASE + SLOT);
    CHECK(value.summary.slot_count == SLOT_COUNT);
    CHECK(value.summary.bytes_consumed == SLOT_COUNT * (size_t)SLOT);
    CHECK(value.summary.gateway_base == BASE);
    CHECK(value.summary.slot_size == SLOT);
    CHECK(value.summary.format_version == 1u);
    return 1;
}

static int test_corruption(void)
{
    fixture value;

    make_valid(&value);
    value.buffer[(size_t)SLOT] = UINT8_C(0x90);
    CHECK(preserved(&value, DB_GATEWAY_DECODE_BAD_OPCODE));

    make_valid(&value);
    value.buffer[(size_t)SLOT + 2u] = 2u;
    CHECK(preserved(&value, DB_GATEWAY_DECODE_UNSUPPORTED_VERSION));

    make_valid(&value);
    value.buffer[(size_t)SLOT + 4u] = (uint8_t)'X';
    CHECK(preserved(&value, DB_GATEWAY_DECODE_BAD_MAGIC));

    make_valid(&value);
    put_u32(value.buffer + (size_t)SLOT, 12u, 0u);
    CHECK(preserved(&value, DB_GATEWAY_DECODE_BAD_TARGET));

    make_valid(&value);
    put_u32(value.buffer + (size_t)SLOT, 8u, 1u);
    put_u32(value.buffer + (size_t)SLOT, 12u, 11u);
    CHECK(preserved(&value, DB_GATEWAY_DECODE_DUPLICATE_TARGET_KEY));

    make_valid(&value);
    put_u64(value.buffer + (size_t)SLOT, 16u, BASE + 2u * SLOT);
    CHECK(preserved(&value, DB_GATEWAY_DECODE_TARGET_ADDRESS_MISMATCH));

    make_valid(&value);
    value.buffer[(size_t)SLOT + 24u] = 1u;
    CHECK(preserved(&value, DB_GATEWAY_DECODE_RESERVED_NONZERO));

    make_valid(&value);
    value.buffer[(size_t)SLOT + 40u] = 0u;
    CHECK(preserved(&value, DB_GATEWAY_DECODE_BAD_PADDING));
    return 1;
}

static int test_arguments_and_capacity(void)
{
    fixture value;
    db_gateway_decode_summary summary;
    db_iat_target target;

    make_valid(&value);
    CHECK(db_gateway_decode_slots(NULL, sizeof(value.buffer), SLOT_COUNT,
                                  BASE, SLOT, value.targets, 3u,
                                  &value.summary) ==
          DB_GATEWAY_DECODE_NULL_ARGUMENT);
    CHECK(db_gateway_decode_slots(value.buffer, sizeof(value.buffer),
                                  SLOT_COUNT, BASE, SLOT,
                                  value.targets, 3u, NULL) ==
          DB_GATEWAY_DECODE_NULL_ARGUMENT);
    CHECK(db_gateway_decode_slots(value.buffer, sizeof(value.buffer),
                                  SLOT_COUNT, BASE, 16u,
                                  value.targets, 3u, &value.summary) ==
          DB_GATEWAY_DECODE_INVALID_SLOT_SIZE);
    CHECK(db_gateway_decode_slots(value.buffer, sizeof(value.buffer),
                                  SLOT_COUNT, BASE + 1u, SLOT,
                                  value.targets, 3u, &value.summary) ==
          DB_GATEWAY_DECODE_INVALID_GATEWAY_BASE);
    CHECK(db_gateway_decode_slots(value.buffer,
                                  SLOT_COUNT * (size_t)SLOT - 1u,
                                  SLOT_COUNT, BASE, SLOT,
                                  value.targets, 3u, &value.summary) ==
          DB_GATEWAY_DECODE_BUFFER_TOO_SMALL);
    CHECK(db_gateway_decode_slots(value.buffer, sizeof(value.buffer),
                                  SLOT_COUNT, BASE, SLOT,
                                  value.targets, 1u, &value.summary) ==
          DB_GATEWAY_DECODE_OUTPUT_BUFFER_TOO_SMALL);
    CHECK(db_gateway_decode_slots(&target, sizeof(target), SIZE_MAX,
                                  BASE, SLOT, &target, 1u, &summary) ==
          DB_GATEWAY_DECODE_COUNT_OVERFLOW);
    return 1;
}

static int test_aliases(void)
{
    union storage {
        uint8_t bytes[BUFFER_SIZE];
        db_iat_target targets[10];
    } storage;
    union output {
        db_gateway_decode_summary summary;
        db_iat_target targets[3];
    } output;
    fixture value;
    db_gateway_decode_summary summary;

    fill(storage.bytes, sizeof(storage.bytes), UINT8_C(0x5a));
    put_slot(storage.bytes, 1u, 11u, BASE);
    put_slot(storage.bytes + (size_t)SLOT, 2u, 22u, BASE + SLOT);
    CHECK(db_gateway_decode_slots(storage.bytes, sizeof(storage.bytes),
                                  SLOT_COUNT, BASE, SLOT,
                                  storage.targets, 3u, &summary) ==
          DB_GATEWAY_DECODE_BUFFER_OVERLAP);

    make_valid(&value);
    CHECK(db_gateway_decode_slots(value.buffer, sizeof(value.buffer),
                                  SLOT_COUNT, BASE, SLOT,
                                  output.targets, 3u, &output.summary) ==
          DB_GATEWAY_DECODE_BUFFER_OVERLAP);
    return 1;
}

static int test_address_edges(void)
{
    fixture value;
    uint64_t base;

    make_valid(&value);
    base = UINT64_C(0x00007fffffffffc0);
    put_u64(value.buffer, 16u, base);
    put_u64(value.buffer + (size_t)SLOT, 16u, base + SLOT);
    CHECK(db_gateway_decode_slots(value.buffer, sizeof(value.buffer),
                                  SLOT_COUNT, base, SLOT,
                                  value.targets, 3u, &value.summary) ==
          DB_GATEWAY_DECODE_NONCANONICAL_ADDRESS);

    base = UINT64_C(0xffffffffffffffc0);
    CHECK(db_gateway_decode_slots(value.buffer, sizeof(value.buffer),
                                  SLOT_COUNT, base, SLOT,
                                  value.targets, 3u, &value.summary) ==
          DB_GATEWAY_DECODE_ADDRESS_OVERFLOW);

    make_valid(&value);
    base = UINT64_C(0xffff800000000000);
    put_u64(value.buffer, 16u, base);
    put_u64(value.buffer + (size_t)SLOT, 16u, base + SLOT);
    CHECK(db_gateway_decode_slots(value.buffer, sizeof(value.buffer),
                                  SLOT_COUNT, base, SLOT,
                                  value.targets, 3u, &value.summary) ==
          DB_GATEWAY_DECODE_OK);
    return 1;
}

static int test_zero_and_names(void)
{
    db_gateway_decode_summary summary;

    CHECK(db_gateway_decode_slots(NULL, 0u, 0u, BASE, SLOT,
                                  NULL, 0u, &summary) ==
          DB_GATEWAY_DECODE_OK);
    CHECK(summary.slot_count == 0u && summary.bytes_consumed == 0u);
    CHECK(db_gateway_decode_status_name(DB_GATEWAY_DECODE_OK)[0] == 'o');
    CHECK(db_gateway_decode_status_name((db_gateway_decode_status)999)[0] == 'u');
    return 1;
}

int main(void)
{
    if (!test_success() ||
        !test_corruption() ||
        !test_arguments_and_capacity() ||
        !test_aliases() ||
        !test_address_edges() ||
        !test_zero_and_names()) {
        static const char failure[] = "GATEWAY DECODE TEST: FAIL\n";
        (void)write(STDERR_FILENO, failure, sizeof(failure) - 1u);
        return 1;
    }

    {
        static const char success[] = "GATEWAY DECODE TEST: PASS\n";
        (void)write(STDOUT_FILENO, success, sizeof(success) - 1u);
    }
    return 0;
}
