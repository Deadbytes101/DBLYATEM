#include "dblyat/gateway_plan.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define BINDING_COUNT 5u
#define TARGET_CAPACITY 5u
#define BASE UINT64_C(0x0000000141000000)
#define REGION UINT64_C(0x100)
#define SLOT UINT64_C(0x20)

_Static_assert(sizeof(db_iat_target) == 16u,
               "IAT target size mismatch");

typedef struct fixture {
    db_import_binding bindings[BINDING_COUNT];
    db_iat_target targets[TARGET_CAPACITY];
    db_gateway_plan_summary summary;
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

static size_t length(const char *text)
{
    size_t result = 0u;

    while (text[result] != '\0') {
        ++result;
    }

    return result;
}

static int write_all(int descriptor, const char *text, size_t size)
{
    size_t offset = 0u;

    while (offset < size) {
        const ssize_t result = write(descriptor,
                                     text + offset,
                                     size - offset);

        if (result <= 0) {
            return 0;
        }
        offset += (size_t)result;
    }

    return 1;
}

static int fail(const char *name)
{
    static const char prefix[] = "GATEWAY PLAN TEST FAIL: ";
    static const char newline[] = "\n";

    (void)write_all(STDERR_FILENO, prefix, sizeof(prefix) - 1u);
    (void)write_all(STDERR_FILENO, name, length(name));
    (void)write_all(STDERR_FILENO, newline, sizeof(newline) - 1u);
    return 0;
}

static void set_binding(db_import_binding *binding,
                        uint32_t module_index,
                        uint32_t provider_id,
                        uint32_t symbol_id)
{
    binding->module_index = module_index;
    binding->provider_id = provider_id;
    binding->symbol_id = symbol_id;
    binding->reserved = 0u;
}

static void init_fixture(fixture *value)
{
    fill(value, sizeof(*value), 0u);
    set_binding(&value->bindings[0], 0u, 10u, 100u);
    set_binding(&value->bindings[1], 0u, 20u, 200u);
    set_binding(&value->bindings[2], 1u, 10u, 100u);
    set_binding(&value->bindings[3], 1u, 10u, 300u);
    set_binding(&value->bindings[4], 2u, 20u, 200u);
    fill(value->targets, sizeof(value->targets), 0xa5u);
    fill(&value->summary, sizeof(value->summary), 0x5au);
}

static int valid_plan(void)
{
    fixture value;

    init_fixture(&value);
    if (db_gateway_plan_targets(value.bindings,
                                BINDING_COUNT,
                                BASE,
                                REGION,
                                SLOT,
                                value.targets,
                                TARGET_CAPACITY,
                                &value.summary) != DB_GATEWAY_PLAN_OK) {
        return fail("valid status");
    }

    if (value.summary.binding_count != BINDING_COUNT ||
        value.summary.target_count != 3u ||
        value.summary.gateway_base != BASE ||
        value.summary.region_size != REGION ||
        value.summary.slot_size != SLOT ||
        value.summary.used_size != 3u * SLOT) {
        return fail("valid summary");
    }

    if (value.targets[0].provider_id != 10u ||
        value.targets[0].symbol_id != 100u ||
        value.targets[0].guest_value != BASE ||
        value.targets[1].provider_id != 20u ||
        value.targets[1].symbol_id != 200u ||
        value.targets[1].guest_value != BASE + SLOT ||
        value.targets[2].provider_id != 10u ||
        value.targets[2].symbol_id != 300u ||
        value.targets[2].guest_value != BASE + 2u * SLOT) {
        return fail("first-seen order");
    }

    if (((const uint8_t *)&value.targets[3])[0] != 0xa5u) {
        return fail("unused target preserved");
    }

    return 1;
}

static int transactional_failures(void)
{
    fixture value;
    db_iat_target targets_before[TARGET_CAPACITY];
    db_gateway_plan_summary summary_before;

    init_fixture(&value);
    fill(targets_before, sizeof(targets_before), 0xa5u);
    fill(&summary_before, sizeof(summary_before), 0x5au);
    value.bindings[4].reserved = 1u;

    if (db_gateway_plan_targets(value.bindings,
                                BINDING_COUNT,
                                BASE,
                                REGION,
                                SLOT,
                                value.targets,
                                TARGET_CAPACITY,
                                &value.summary) !=
            DB_GATEWAY_PLAN_BAD_BINDING ||
        !equal(value.targets, targets_before, sizeof(targets_before)) ||
        !equal(&value.summary,
               &summary_before,
               sizeof(summary_before))) {
        return fail("late failure preservation");
    }

    init_fixture(&value);
    if (db_gateway_plan_targets(value.bindings,
                                BINDING_COUNT,
                                BASE,
                                REGION,
                                SLOT,
                                value.targets,
                                2u,
                                &value.summary) !=
            DB_GATEWAY_PLAN_OUTPUT_BUFFER_TOO_SMALL ||
        !equal(value.targets, targets_before, sizeof(targets_before)) ||
        !equal(&value.summary,
               &summary_before,
               sizeof(summary_before))) {
        return fail("capacity preservation");
    }

    return 1;
}

static int argument_and_binding_failures(void)
{
    fixture value;

    init_fixture(&value);
    if (db_gateway_plan_targets(value.bindings, 1u, BASE, REGION, SLOT,
                                value.targets, TARGET_CAPACITY, NULL) !=
        DB_GATEWAY_PLAN_NULL_ARGUMENT) {
        return fail("null summary");
    }
    if (db_gateway_plan_targets(NULL, 1u, BASE, REGION, SLOT,
                                value.targets, TARGET_CAPACITY,
                                &value.summary) !=
        DB_GATEWAY_PLAN_NULL_ARGUMENT) {
        return fail("null bindings");
    }
    if (db_gateway_plan_targets(value.bindings, 1u, BASE, REGION, SLOT,
                                NULL, 1u, &value.summary) !=
        DB_GATEWAY_PLAN_NULL_ARGUMENT) {
        return fail("null targets");
    }

    value.bindings[0].provider_id = 0u;
    if (db_gateway_plan_targets(value.bindings, 1u, BASE, REGION, SLOT,
                                value.targets, TARGET_CAPACITY,
                                &value.summary) !=
        DB_GATEWAY_PLAN_BAD_BINDING) {
        return fail("zero provider");
    }
    value.bindings[0].provider_id = 1u;
    value.bindings[0].symbol_id = 0u;
    if (db_gateway_plan_targets(value.bindings, 1u, BASE, REGION, SLOT,
                                value.targets, TARGET_CAPACITY,
                                &value.summary) !=
        DB_GATEWAY_PLAN_BAD_BINDING) {
        return fail("zero symbol");
    }

    return 1;
}

static int layout_failures(void)
{
    fixture value;

    init_fixture(&value);
    if (db_gateway_plan_targets(value.bindings, 1u, BASE, REGION, 0u,
                                value.targets, TARGET_CAPACITY,
                                &value.summary) !=
        DB_GATEWAY_PLAN_INVALID_SLOT_SIZE) {
        return fail("zero slot");
    }
    if (db_gateway_plan_targets(value.bindings, 1u, BASE, REGION, 24u,
                                value.targets, TARGET_CAPACITY,
                                &value.summary) !=
        DB_GATEWAY_PLAN_INVALID_SLOT_SIZE) {
        return fail("non-power slot");
    }
    if (db_gateway_plan_targets(value.bindings, 1u, 0u, REGION, SLOT,
                                value.targets, TARGET_CAPACITY,
                                &value.summary) !=
        DB_GATEWAY_PLAN_INVALID_GATEWAY_BASE) {
        return fail("zero base");
    }
    if (db_gateway_plan_targets(value.bindings, 1u,
                                UINT64_C(0x0000800000000000),
                                REGION, SLOT, value.targets,
                                TARGET_CAPACITY, &value.summary) !=
        DB_GATEWAY_PLAN_INVALID_GATEWAY_BASE) {
        return fail("noncanonical base");
    }
    if (db_gateway_plan_targets(value.bindings, 1u, BASE + 1u,
                                REGION, SLOT, value.targets,
                                TARGET_CAPACITY, &value.summary) !=
        DB_GATEWAY_PLAN_INVALID_GATEWAY_BASE) {
        return fail("misaligned base");
    }
    if (db_gateway_plan_targets(value.bindings, 1u, BASE, SLOT / 2u,
                                SLOT, value.targets, TARGET_CAPACITY,
                                &value.summary) !=
        DB_GATEWAY_PLAN_INVALID_REGION) {
        return fail("region smaller than slot");
    }
    if (db_gateway_plan_targets(value.bindings, 2u, BASE, SLOT, SLOT,
                                value.targets, TARGET_CAPACITY,
                                &value.summary) !=
        DB_GATEWAY_PLAN_INVALID_REGION) {
        return fail("region too small");
    }
    if (db_gateway_plan_targets(value.bindings, 1u,
                                UINT64_C(0xfffffffffffff000),
                                UINT64_C(0x2000), UINT64_C(0x1000),
                                value.targets, TARGET_CAPACITY,
                                &value.summary) !=
        DB_GATEWAY_PLAN_INVALID_REGION) {
        return fail("region overflow");
    }
    if (db_gateway_plan_targets(value.bindings, 2u,
                                UINT64_C(0x00007ffffffff000),
                                UINT64_C(0x2000), UINT64_C(0x1000),
                                value.targets, TARGET_CAPACITY,
                                &value.summary) !=
        DB_GATEWAY_PLAN_NONCANONICAL_ADDRESS) {
        return fail("canonical hole");
    }

    if (db_gateway_plan_targets(value.bindings, 2u,
                                UINT64_C(0xffff800000000000),
                                REGION, SLOT, value.targets,
                                TARGET_CAPACITY, &value.summary) !=
            DB_GATEWAY_PLAN_OK ||
        value.targets[1].guest_value !=
            UINT64_C(0xffff800000000020)) {
        return fail("high canonical plan");
    }

    return 1;
}

static int zero_duplicate_and_overflow(void)
{
    fixture value;
    db_gateway_plan_summary summary;
    const size_t binding_overflow =
        SIZE_MAX / sizeof(db_import_binding) + 1u;
    const size_t target_overflow =
        SIZE_MAX / sizeof(db_iat_target) + 1u;

    fill(&summary, sizeof(summary), 0xa5u);
    if (db_gateway_plan_targets(NULL, 0u, BASE, REGION, SLOT,
                                NULL, 0u, &summary) !=
            DB_GATEWAY_PLAN_OK ||
        summary.target_count != 0u || summary.used_size != 0u) {
        return fail("zero bindings");
    }

    init_fixture(&value);
    if (db_gateway_plan_targets(value.bindings, 3u, BASE,
                                2u * SLOT, SLOT, value.targets,
                                2u, &value.summary) !=
            DB_GATEWAY_PLAN_OK ||
        value.summary.target_count != 2u) {
        return fail("duplicate reuse");
    }

    if (db_gateway_plan_targets(value.bindings, binding_overflow,
                                BASE, REGION, SLOT, NULL, 0u,
                                &value.summary) !=
        DB_GATEWAY_PLAN_COUNT_OVERFLOW) {
        return fail("binding count overflow");
    }
    if (db_gateway_plan_targets(value.bindings, 1u, BASE, REGION,
                                SLOT, value.targets, target_overflow,
                                &value.summary) !=
        DB_GATEWAY_PLAN_COUNT_OVERFLOW) {
        return fail("target count overflow");
    }

    return 1;
}

static int overlap_failures(void)
{
    union storage {
        db_import_binding bindings[4];
        db_iat_target targets[4];
        db_gateway_plan_summary summary;
    } shared;
    db_iat_target targets[4];
    db_gateway_plan_summary summary;

    fill(&shared, sizeof(shared), 0u);
    set_binding(&shared.bindings[0], 0u, 1u, 1u);
    if (db_gateway_plan_targets(shared.bindings, 1u, BASE, REGION,
                                SLOT, shared.targets, 1u, &summary) !=
        DB_GATEWAY_PLAN_BUFFER_OVERLAP) {
        return fail("binding target overlap");
    }

    fill(&shared, sizeof(shared), 0u);
    set_binding(&shared.bindings[0], 0u, 1u, 1u);
    if (db_gateway_plan_targets(shared.bindings, 1u, BASE, REGION,
                                SLOT, targets, 1u, &shared.summary) !=
        DB_GATEWAY_PLAN_BUFFER_OVERLAP) {
        return fail("binding summary overlap");
    }

    fill(&shared, sizeof(shared), 0u);
    set_binding(&shared.bindings[0], 0u, 1u, 1u);
    if (db_gateway_plan_targets(shared.bindings, 1u, BASE, REGION,
                                SLOT, shared.targets, 1u,
                                &shared.summary) !=
        DB_GATEWAY_PLAN_BUFFER_OVERLAP) {
        return fail("target summary overlap");
    }

    return 1;
}

int main(void)
{
    static const char pass[] = "GATEWAY PLAN TEST: PASS\n";

    if (!valid_plan() ||
        !transactional_failures() ||
        !argument_and_binding_failures() ||
        !layout_failures() ||
        !zero_duplicate_and_overflow() ||
        !overlap_failures() ||
        db_gateway_plan_status_name(DB_GATEWAY_PLAN_OK)[0] != 'o' ||
        db_gateway_plan_status_name((db_gateway_plan_status)999u)[0] != 'u') {
        return 1;
    }

    return write_all(STDOUT_FILENO, pass, sizeof(pass) - 1u) ? 0 : 1;
}
