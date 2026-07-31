#include "dblyat/gateway_lookup.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define BASE UINT64_C(0x30000)
#define SLOT UINT64_C(64)
#define TARGET_COUNT 3u
#define CHECK(condition) do { if (!(condition)) return 0; } while (0)

typedef struct fixture {
    db_iat_target targets[TARGET_COUNT];
    db_gateway_lookup_result result;
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

    for (index = 0u; index < TARGET_COUNT; ++index) {
        value->targets[index].provider_id = (uint32_t)(index + 1u);
        value->targets[index].symbol_id = (uint32_t)(100u + index);
        value->targets[index].guest_value = BASE + (uint64_t)index * SLOT;
    }
    fill(&value->result, sizeof(value->result), UINT8_C(0xa5));
}

static int preserved(fixture *value,
                     uint64_t rip,
                     db_gateway_lookup_status expected)
{
    db_gateway_lookup_result before = value->result;
    const db_gateway_lookup_status status =
        db_gateway_lookup_rip(value->targets,
                              TARGET_COUNT,
                              BASE,
                              SLOT,
                              rip,
                              &value->result);

    return status == expected &&
           equal(&before, &value->result, sizeof(before));
}

static int test_entries(void)
{
    fixture value;
    size_t index;

    make_valid(&value);
    for (index = 0u; index < TARGET_COUNT; ++index) {
        const uint64_t rip = BASE + (uint64_t)index * SLOT;

        CHECK(db_gateway_lookup_rip(value.targets,
                                    TARGET_COUNT,
                                    BASE,
                                    SLOT,
                                    rip,
                                    &value.result) == DB_GATEWAY_LOOKUP_OK);
        CHECK(value.result.kind == DB_GATEWAY_LOOKUP_ENTRY);
        CHECK(value.result.rip == rip);
        CHECK(value.result.slot_address == rip);
        CHECK(value.result.slot_index == index);
        CHECK(value.result.provider_id == value.targets[index].provider_id);
        CHECK(value.result.symbol_id == value.targets[index].symbol_id);
        CHECK(value.result.reserved[0] == 0u &&
              value.result.reserved[6] == 0u);
    }
    return 1;
}

static int test_interior_and_miss(void)
{
    fixture value;

    make_valid(&value);
    CHECK(db_gateway_lookup_rip(value.targets, TARGET_COUNT, BASE, SLOT,
                                BASE + SLOT + 1u, &value.result) ==
          DB_GATEWAY_LOOKUP_OK);
    CHECK(value.result.kind == DB_GATEWAY_LOOKUP_INTERIOR);
    CHECK(value.result.slot_index == 1u);
    CHECK(value.result.slot_address == BASE + SLOT);
    CHECK(value.result.provider_id == 0u && value.result.symbol_id == 0u);

    CHECK(db_gateway_lookup_rip(value.targets, TARGET_COUNT, BASE, SLOT,
                                BASE + TARGET_COUNT * SLOT - 1u,
                                &value.result) == DB_GATEWAY_LOOKUP_OK);
    CHECK(value.result.kind == DB_GATEWAY_LOOKUP_INTERIOR);
    CHECK(value.result.slot_index == TARGET_COUNT - 1u);

    CHECK(db_gateway_lookup_rip(value.targets, TARGET_COUNT, BASE, SLOT,
                                BASE - 1u, &value.result) ==
          DB_GATEWAY_LOOKUP_OK);
    CHECK(value.result.kind == DB_GATEWAY_LOOKUP_MISS);
    CHECK(value.result.slot_index == SIZE_MAX);
    CHECK(value.result.slot_address == 0u);

    CHECK(db_gateway_lookup_rip(value.targets, TARGET_COUNT, BASE, SLOT,
                                BASE + TARGET_COUNT * SLOT,
                                &value.result) == DB_GATEWAY_LOOKUP_OK);
    CHECK(value.result.kind == DB_GATEWAY_LOOKUP_MISS);
    return 1;
}

static int test_validation_failures(void)
{
    fixture value;

    make_valid(&value);
    value.targets[2].symbol_id = 0u;
    CHECK(preserved(&value, BASE, DB_GATEWAY_LOOKUP_BAD_TARGET));

    make_valid(&value);
    value.targets[2].provider_id = value.targets[0].provider_id;
    value.targets[2].symbol_id = value.targets[0].symbol_id;
    CHECK(preserved(&value, BASE, DB_GATEWAY_LOOKUP_DUPLICATE_TARGET_KEY));

    make_valid(&value);
    value.targets[2].guest_value += SLOT;
    CHECK(preserved(&value, BASE, DB_GATEWAY_LOOKUP_TARGET_ADDRESS_MISMATCH));

    make_valid(&value);
    CHECK(preserved(&value,
                    UINT64_C(0x0000800000000000),
                    DB_GATEWAY_LOOKUP_NONCANONICAL_ADDRESS));
    return 1;
}

static int test_arguments_and_aliases(void)
{
    fixture value;
    union storage {
        db_iat_target targets[TARGET_COUNT];
        db_gateway_lookup_result result;
    } storage;

    make_valid(&value);
    CHECK(db_gateway_lookup_rip(NULL, TARGET_COUNT, BASE, SLOT, BASE,
                                &value.result) ==
          DB_GATEWAY_LOOKUP_NULL_ARGUMENT);
    CHECK(db_gateway_lookup_rip(value.targets, TARGET_COUNT, BASE, SLOT, BASE,
                                NULL) == DB_GATEWAY_LOOKUP_NULL_ARGUMENT);
    CHECK(db_gateway_lookup_rip(value.targets, TARGET_COUNT, BASE, 16u, BASE,
                                &value.result) ==
          DB_GATEWAY_LOOKUP_INVALID_SLOT_SIZE);
    CHECK(db_gateway_lookup_rip(value.targets, TARGET_COUNT, BASE, 48u, BASE,
                                &value.result) ==
          DB_GATEWAY_LOOKUP_INVALID_SLOT_SIZE);
    CHECK(db_gateway_lookup_rip(value.targets, TARGET_COUNT, 0u, SLOT, BASE,
                                &value.result) ==
          DB_GATEWAY_LOOKUP_INVALID_GATEWAY_BASE);
    CHECK(db_gateway_lookup_rip(value.targets, TARGET_COUNT, BASE + 1u,
                                SLOT, BASE, &value.result) ==
          DB_GATEWAY_LOOKUP_INVALID_GATEWAY_BASE);

    storage.targets[0] = value.targets[0];
    storage.targets[1] = value.targets[1];
    storage.targets[2] = value.targets[2];
    CHECK(db_gateway_lookup_rip(storage.targets, TARGET_COUNT, BASE, SLOT,
                                BASE, &storage.result) ==
          DB_GATEWAY_LOOKUP_BUFFER_OVERLAP);
    return 1;
}

static int test_address_edges(void)
{
    db_iat_target targets[2];
    db_gateway_lookup_result result;
    uint64_t base;

    base = UINT64_C(0x00007fffffffffc0);
    targets[0].provider_id = 1u;
    targets[0].symbol_id = 1u;
    targets[0].guest_value = base;
    targets[1].provider_id = 2u;
    targets[1].symbol_id = 2u;
    targets[1].guest_value = base + SLOT;
    CHECK(db_gateway_lookup_rip(targets, 2u, base, SLOT, base, &result) ==
          DB_GATEWAY_LOOKUP_NONCANONICAL_ADDRESS);

    base = UINT64_C(0xffffffffffffffc0);
    targets[0].guest_value = base;
    targets[1].guest_value = 0u;
    CHECK(db_gateway_lookup_rip(targets, 2u, base, SLOT, base, &result) ==
          DB_GATEWAY_LOOKUP_ADDRESS_OVERFLOW);

    base = UINT64_C(0xffff800000000000);
    targets[0].guest_value = base;
    targets[1].guest_value = base + SLOT;
    CHECK(db_gateway_lookup_rip(targets, 2u, base, SLOT, base + SLOT,
                                &result) == DB_GATEWAY_LOOKUP_OK);
    CHECK(result.kind == DB_GATEWAY_LOOKUP_ENTRY && result.slot_index == 1u);

    base = UINT64_C(0xffffffffffffffc0);
    targets[0].provider_id = 7u;
    targets[0].symbol_id = 9u;
    targets[0].guest_value = base;
    CHECK(db_gateway_lookup_rip(targets, 1u, base, SLOT, UINT64_MAX,
                                &result) == DB_GATEWAY_LOOKUP_OK);
    CHECK(result.kind == DB_GATEWAY_LOOKUP_INTERIOR);
    CHECK(result.slot_index == 0u && result.slot_address == base);
    return 1;
}

static int test_zero_and_names(void)
{
    db_gateway_lookup_result result;

    CHECK(db_gateway_lookup_rip(NULL, 0u, BASE, SLOT, BASE, &result) ==
          DB_GATEWAY_LOOKUP_OK);
    CHECK(result.kind == DB_GATEWAY_LOOKUP_MISS);
    CHECK(result.slot_index == SIZE_MAX);
    CHECK(db_gateway_lookup_status_name(DB_GATEWAY_LOOKUP_OK)[0] == 'o');
    CHECK(db_gateway_lookup_status_name((db_gateway_lookup_status)999)[0] ==
          'u');
    return 1;
}

int main(void)
{
    if (!test_entries() ||
        !test_interior_and_miss() ||
        !test_validation_failures() ||
        !test_arguments_and_aliases() ||
        !test_address_edges() ||
        !test_zero_and_names()) {
        static const char failure[] = "GATEWAY LOOKUP TEST: FAIL\n";
        (void)write(STDERR_FILENO, failure, sizeof(failure) - 1u);
        return 1;
    }

    {
        static const char success[] = "GATEWAY LOOKUP TEST: PASS\n";
        (void)write(STDOUT_FILENO, success, sizeof(success) - 1u);
    }
    return 0;
}
