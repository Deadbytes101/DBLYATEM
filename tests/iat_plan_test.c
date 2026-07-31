#include "dblyat/iat_plan.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define IMAGE_SIZE 256u
#define RECORD_COUNT 2u
#define TARGET_COUNT 3u

_Static_assert(offsetof(db_iat_patch, record_index) == 0u,
               "record index offset mismatch");
_Static_assert(offsetof(db_iat_patch, expected_value) % 8u == 0u,
               "expected value alignment mismatch");

typedef struct fixture {
    _Alignas(db_iat_patch) uint8_t image[IMAGE_SIZE];
    db_import_record records[RECORD_COUNT];
    db_import_binding bindings[RECORD_COUNT];
    db_iat_target targets[TARGET_COUNT];
    db_iat_patch patches[RECORD_COUNT];
    db_iat_plan_summary summary;
} fixture;

static void fill(void *data, size_t size, uint8_t value)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        bytes[index] = value;
    }
}

static void copy(void *destination, const void *source, size_t size)
{
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    size_t index;

    for (index = 0u; index < size; ++index) {
        out[index] = in[index];
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
        bytes[offset + index] = (uint8_t)(value >> (index * 8u));
    }
}

static void init_fixture(fixture *test)
{
    db_import_record *name_record;
    db_import_record *ordinal_record;

    fill(test, sizeof(*test), 0u);
    name_record = &test->records[0];
    ordinal_record = &test->records[1];

    name_record->module_index = 0u;
    name_record->lookup_rva = 32u;
    name_record->iat_rva = 64u;
    name_record->name_rva = 130u;
    name_record->name_length = 6u;
    name_record->hint = 7u;
    name_record->kind = DB_IMPORT_BY_NAME;
    name_record->lookup_value = 128u;
    name_record->original_iat_value = UINT64_C(0xa0);

    ordinal_record->module_index = 1u;
    ordinal_record->lookup_rva = 48u;
    ordinal_record->iat_rva = 80u;
    ordinal_record->ordinal = 12u;
    ordinal_record->kind = DB_IMPORT_BY_ORDINAL;
    ordinal_record->lookup_value = UINT64_C(0x800000000000000c);
    ordinal_record->original_iat_value = UINT64_C(0x800000000000000c);

    test->bindings[0].module_index = 0u;
    test->bindings[0].provider_id = 10u;
    test->bindings[0].symbol_id = 100u;
    test->bindings[1].module_index = 1u;
    test->bindings[1].provider_id = 20u;
    test->bindings[1].symbol_id = 200u;

    test->targets[0].provider_id = 20u;
    test->targets[0].symbol_id = 200u;
    test->targets[0].guest_value = UINT64_C(0x0000000140002000);
    test->targets[1].provider_id = 10u;
    test->targets[1].symbol_id = 100u;
    test->targets[1].guest_value = UINT64_C(0x0000000140001000);
    test->targets[2].provider_id = 99u;
    test->targets[2].symbol_id = 999u;
    test->targets[2].guest_value = UINT64_C(0xffff800000009000);

    put_u64(test->image, 64u, name_record->original_iat_value);
    put_u64(test->image, 80u, ordinal_record->original_iat_value);
    fill(test->patches, sizeof(test->patches), 0xa5u);
    fill(&test->summary, sizeof(test->summary), 0x5au);
}

static db_iat_plan_status plan(fixture *test)
{
    return db_iat_plan_patches(test->image,
                               sizeof(test->image),
                               test->records,
                               RECORD_COUNT,
                               test->bindings,
                               RECORD_COUNT,
                               test->targets,
                               TARGET_COUNT,
                               test->patches,
                               RECORD_COUNT,
                               &test->summary);
}

static int test_success(void)
{
    fixture test;
    uint8_t image_snapshot[IMAGE_SIZE];

    init_fixture(&test);
    copy(image_snapshot, test.image, sizeof(image_snapshot));

    return plan(&test) == DB_IAT_PLAN_OK &&
           equal(test.image, image_snapshot, sizeof(image_snapshot)) &&
           test.summary.record_count == RECORD_COUNT &&
           test.summary.target_count == TARGET_COUNT &&
           test.summary.patch_count == RECORD_COUNT &&
           test.patches[0].record_index == 0u &&
           test.patches[0].provider_id == 10u &&
           test.patches[0].symbol_id == 100u &&
           test.patches[0].iat_rva == 64u &&
           test.patches[0].expected_value == UINT64_C(0xa0) &&
           test.patches[0].replacement_value ==
               UINT64_C(0x0000000140001000) &&
           test.patches[1].record_index == 1u &&
           test.patches[1].provider_id == 20u &&
           test.patches[1].symbol_id == 200u &&
           test.patches[1].iat_rva == 80u &&
           test.patches[1].replacement_value ==
               UINT64_C(0x0000000140002000);
}

static int test_empty(void)
{
    uint8_t image = 0u;
    db_iat_plan_summary summary;

    fill(&summary, sizeof(summary), 0xa5u);
    return db_iat_plan_patches(&image, 1u,
                               NULL, 0u, NULL, 0u, NULL, 0u,
                               NULL, 0u, &summary) == DB_IAT_PLAN_OK &&
           summary.record_count == 0u &&
           summary.target_count == 0u &&
           summary.patch_count == 0u;
}

static int preserves_on_error(db_iat_plan_status expected,
                              void (*mutate)(fixture *))
{
    fixture test;
    db_iat_patch patch_snapshot[RECORD_COUNT];
    db_iat_plan_summary summary_snapshot;

    init_fixture(&test);
    copy(patch_snapshot, test.patches, sizeof(patch_snapshot));
    summary_snapshot = test.summary;
    mutate(&test);

    return plan(&test) == expected &&
           equal(test.patches, patch_snapshot, sizeof(patch_snapshot)) &&
           equal(&test.summary, &summary_snapshot, sizeof(summary_snapshot));
}

static void stale(fixture *test)
{
    put_u64(test->image, 80u, UINT64_C(0x1111111111111111));
}

static void missing(fixture *test)
{
    test->bindings[1].symbol_id = 201u;
}

static void duplicate_target(fixture *test)
{
    test->targets[2] = test->targets[0];
}

static void bad_target(fixture *test)
{
    test->targets[1].guest_value = UINT64_C(0x0001000000000000);
}

static void bad_binding(fixture *test)
{
    test->bindings[1].module_index = 0u;
}

static void bad_record(fixture *test)
{
    test->records[0].lookup_value += 1u;
}

static void out_of_range(fixture *test)
{
    test->records[1].iat_rva = IMAGE_SIZE - 4u;
}

static void overlap(fixture *test)
{
    test->records[1].iat_rva = 68u;
}

static int test_capacity(void)
{
    fixture test;
    db_iat_patch snapshot[RECORD_COUNT];
    db_iat_plan_summary summary_snapshot;
    db_iat_plan_status status;

    init_fixture(&test);
    copy(snapshot, test.patches, sizeof(snapshot));
    summary_snapshot = test.summary;
    status = db_iat_plan_patches(test.image, sizeof(test.image),
                                 test.records, RECORD_COUNT,
                                 test.bindings, RECORD_COUNT,
                                 test.targets, TARGET_COUNT,
                                 test.patches, 1u, &test.summary);

    return status == DB_IAT_PLAN_OUTPUT_BUFFER_TOO_SMALL &&
           equal(test.patches, snapshot, sizeof(snapshot)) &&
           equal(&test.summary, &summary_snapshot, sizeof(summary_snapshot));
}

static int test_guards(void)
{
    fixture test;
    const size_t huge = SIZE_MAX / sizeof(db_iat_patch) + 1u;

    init_fixture(&test);
    if (db_iat_plan_patches(test.image, sizeof(test.image),
                            test.records, RECORD_COUNT,
                            test.bindings, 1u,
                            test.targets, TARGET_COUNT,
                            test.patches, RECORD_COUNT,
                            &test.summary) !=
        DB_IAT_PLAN_BINDING_COUNT_MISMATCH) {
        return 0;
    }

    if (db_iat_plan_patches(test.image, sizeof(test.image),
                            test.records, RECORD_COUNT,
                            test.bindings, RECORD_COUNT,
                            test.targets, TARGET_COUNT,
                            (db_iat_patch *)(void *)test.image,
                            RECORD_COUNT, &test.summary) !=
        DB_IAT_PLAN_BUFFER_OVERLAP) {
        return 0;
    }

    if (db_iat_plan_patches(test.image, sizeof(test.image),
                            test.records, RECORD_COUNT,
                            test.bindings, RECORD_COUNT,
                            test.targets, TARGET_COUNT,
                            test.patches, huge, &test.summary) !=
        DB_IAT_PLAN_COUNT_OVERFLOW) {
        return 0;
    }

    return db_iat_plan_patches(NULL, sizeof(test.image),
                               test.records, RECORD_COUNT,
                               test.bindings, RECORD_COUNT,
                               test.targets, TARGET_COUNT,
                               test.patches, RECORD_COUNT,
                               &test.summary) == DB_IAT_PLAN_NULL_ARGUMENT;
}

static int run_tests(void)
{
    int pass = 1;

    pass &= test_success();
    pass &= test_empty();
    pass &= preserves_on_error(DB_IAT_PLAN_STALE_IAT, stale);
    pass &= preserves_on_error(DB_IAT_PLAN_TARGET_NOT_FOUND, missing);
    pass &= preserves_on_error(DB_IAT_PLAN_DUPLICATE_TARGET_KEY,
                               duplicate_target);
    pass &= preserves_on_error(DB_IAT_PLAN_BAD_TARGET, bad_target);
    pass &= preserves_on_error(DB_IAT_PLAN_BAD_BINDING, bad_binding);
    pass &= preserves_on_error(DB_IAT_PLAN_BAD_IMPORT_RECORD, bad_record);
    pass &= preserves_on_error(DB_IAT_PLAN_IAT_OUT_OF_RANGE, out_of_range);
    pass &= preserves_on_error(DB_IAT_PLAN_IAT_OVERLAP, overlap);
    pass &= test_capacity();
    pass &= test_guards();
    pass &= db_iat_plan_status_name(DB_IAT_PLAN_OK)[0] == 'o';
    pass &= db_iat_plan_status_name((db_iat_plan_status)99)[0] == 'u';

    return pass;
}

int main(void)
{
    static const char pass[] = "IAT PLAN TEST: PASS\n";
    static const char fail[] = "IAT PLAN TEST: FAIL\n";

    if (run_tests()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
