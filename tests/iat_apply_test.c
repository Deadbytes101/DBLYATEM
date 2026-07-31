#include "dblyat/iat_apply.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define IMAGE_SIZE 128u
#define PAGE_SIZE 32u
#define PAGE_COUNT 4u
#define PATCH_COUNT 2u

_Static_assert(offsetof(db_iat_apply_summary, patch_count) == 0u,
               "summary patch count offset mismatch");

typedef struct fixture {
    _Alignas(db_iat_apply_summary) uint8_t image[IMAGE_SIZE];
    db_map_page_protection pages[PAGE_COUNT];
    db_iat_patch patches[PATCH_COUNT];
    db_iat_apply_summary summary;
} fixture;

static void fill_bytes(void *data, size_t size, uint8_t value)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        bytes[index] = value;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    uint8_t *output = (uint8_t *)destination;
    const uint8_t *input = (const uint8_t *)source;
    size_t index;

    for (index = 0u; index < size; ++index) {
        output[index] = input[index];
    }
}

static int bytes_equal(const void *left, const void *right, size_t size)
{
    const uint8_t *left_bytes = (const uint8_t *)left;
    const uint8_t *right_bytes = (const uint8_t *)right;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return 0;
        }
    }

    return 1;
}

static void put_u64(uint8_t *bytes, size_t offset, uint64_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
    bytes[offset + 2u] = (uint8_t)(value >> 16u);
    bytes[offset + 3u] = (uint8_t)(value >> 24u);
    bytes[offset + 4u] = (uint8_t)(value >> 32u);
    bytes[offset + 5u] = (uint8_t)(value >> 40u);
    bytes[offset + 6u] = (uint8_t)(value >> 48u);
    bytes[offset + 7u] = (uint8_t)(value >> 56u);
}

static uint64_t read_u64(const uint8_t *bytes, size_t offset)
{
    return (uint64_t)bytes[offset] |
           ((uint64_t)bytes[offset + 1u] << 8u) |
           ((uint64_t)bytes[offset + 2u] << 16u) |
           ((uint64_t)bytes[offset + 3u] << 24u) |
           ((uint64_t)bytes[offset + 4u] << 32u) |
           ((uint64_t)bytes[offset + 5u] << 40u) |
           ((uint64_t)bytes[offset + 6u] << 48u) |
           ((uint64_t)bytes[offset + 7u] << 56u);
}

static void make_fixture(fixture *value)
{
    size_t page;

    fill_bytes(value, sizeof(*value), 0u);
    for (page = 0u; page < PAGE_COUNT; ++page) {
        value->pages[page] = DB_MAP_PAGE_RW;
    }

    value->patches[0].record_index = 0u;
    value->patches[0].module_index = 1u;
    value->patches[0].provider_id = 10u;
    value->patches[0].symbol_id = 20u;
    value->patches[0].iat_rva = 16u;
    value->patches[0].expected_value = UINT64_C(0x1111222233334444);
    value->patches[0].replacement_value = UINT64_C(0x0000000140001000);

    value->patches[1].record_index = 1u;
    value->patches[1].module_index = 2u;
    value->patches[1].provider_id = 11u;
    value->patches[1].symbol_id = 21u;
    value->patches[1].iat_rva = 72u;
    value->patches[1].expected_value = UINT64_C(0x5555666677778888);
    value->patches[1].replacement_value = UINT64_C(0x0000000140002000);

    put_u64(value->image, 16u, value->patches[0].expected_value);
    put_u64(value->image, 72u, value->patches[1].expected_value);
}

static db_iat_apply_status apply_fixture(fixture *value)
{
    return db_iat_apply_patches(value->image,
                                sizeof(value->image),
                                PAGE_SIZE,
                                value->pages,
                                PAGE_COUNT,
                                value->patches,
                                PATCH_COUNT,
                                &value->summary);
}

static int expect_failure_unchanged(fixture *value,
                                    db_iat_apply_status expected)
{
    uint8_t before[IMAGE_SIZE];
    db_iat_apply_summary summary_before;
    db_iat_apply_status status;

    fill_bytes(&value->summary, sizeof(value->summary), 0xa5u);
    copy_bytes(before, value->image, sizeof(before));
    copy_bytes(&summary_before, &value->summary, sizeof(summary_before));

    status = apply_fixture(value);
    return status == expected &&
           bytes_equal(before, value->image, sizeof(before)) &&
           bytes_equal(&summary_before,
                       &value->summary,
                       sizeof(summary_before));
}

static int test_success(void)
{
    fixture value;

    make_fixture(&value);
    if (apply_fixture(&value) != DB_IAT_APPLY_OK) {
        return 0;
    }

    return read_u64(value.image, 16u) ==
               value.patches[0].replacement_value &&
           read_u64(value.image, 72u) ==
               value.patches[1].replacement_value &&
           value.summary.patch_count == PATCH_COUNT &&
           value.summary.bytes_written == PATCH_COUNT * 8u &&
           value.summary.first_iat_rva == 16u &&
           value.summary.last_iat_end == 80u;
}

static int test_empty_plan(void)
{
    fixture value;
    db_iat_apply_summary summary;

    make_fixture(&value);
    fill_bytes(&summary, sizeof(summary), 0xa5u);

    if (db_iat_apply_patches(value.image,
                             sizeof(value.image),
                             PAGE_SIZE,
                             value.pages,
                             PAGE_COUNT,
                             NULL,
                             0u,
                             &summary) != DB_IAT_APPLY_OK) {
        return 0;
    }

    return summary.patch_count == 0u &&
           summary.bytes_written == 0u &&
           summary.first_iat_rva == 0u &&
           summary.last_iat_end == 0u;
}

static int test_late_stale_preserves_image(void)
{
    fixture value;

    make_fixture(&value);
    put_u64(value.image, 72u, UINT64_C(0xaaaaaaaaaaaaaaaa));
    return expect_failure_unchanged(&value, DB_IAT_APPLY_STALE_IAT);
}

static int test_late_page_failure_preserves_image(void)
{
    fixture value;

    make_fixture(&value);
    value.pages[2] = DB_MAP_PAGE_R;
    return expect_failure_unchanged(
        &value, DB_IAT_APPLY_IAT_PAGE_NOT_WRITABLE);
}

static int test_overlap_preserves_image(void)
{
    fixture value;

    make_fixture(&value);
    value.patches[1].iat_rva = value.patches[0].iat_rva;
    value.patches[1].expected_value = value.patches[0].expected_value;
    return expect_failure_unchanged(&value, DB_IAT_APPLY_IAT_OVERLAP);
}

static int test_bad_patch_preserves_image(void)
{
    fixture value;

    make_fixture(&value);
    value.patches[1].replacement_value = 0u;
    return expect_failure_unchanged(&value, DB_IAT_APPLY_BAD_PATCH);
}


static int test_record_order_rejected(void)
{
    fixture value;

    make_fixture(&value);
    value.patches[1].record_index = 7u;
    return expect_failure_unchanged(&value, DB_IAT_APPLY_BAD_PATCH);
}

static int test_reserved_rejected(void)
{
    fixture value;

    make_fixture(&value);
    value.patches[1].reserved = 1u;
    return expect_failure_unchanged(&value, DB_IAT_APPLY_BAD_PATCH);
}

static int test_noncanonical_target(void)
{
    fixture value;

    make_fixture(&value);
    value.patches[1].replacement_value = UINT64_C(0x0001000000000000);
    return expect_failure_unchanged(&value, DB_IAT_APPLY_BAD_PATCH);
}

static int test_cross_page_requires_both_rw(void)
{
    fixture value;

    make_fixture(&value);
    value.patches[0].iat_rva = PAGE_SIZE - 4u;
    put_u64(value.image,
            value.patches[0].iat_rva,
            value.patches[0].expected_value);
    value.pages[1] = DB_MAP_PAGE_R;
    return expect_failure_unchanged(
        &value, DB_IAT_APPLY_IAT_PAGE_NOT_WRITABLE);
}

static int test_out_of_range(void)
{
    fixture value;

    make_fixture(&value);
    value.patches[1].iat_rva = IMAGE_SIZE - 4u;
    return expect_failure_unchanged(&value, DB_IAT_APPLY_IAT_OUT_OF_RANGE);
}

static int test_bad_page_plan(void)
{
    fixture value;

    make_fixture(&value);
    value.pages[3] = 7u;
    return expect_failure_unchanged(&value, DB_IAT_APPLY_BAD_PAGE_PLAN);
}

static int test_page_plan_too_small(void)
{
    fixture value;
    db_iat_apply_summary summary;

    make_fixture(&value);
    return db_iat_apply_patches(value.image,
                                sizeof(value.image),
                                PAGE_SIZE,
                                value.pages,
                                PAGE_COUNT - 1u,
                                value.patches,
                                PATCH_COUNT,
                                &summary) ==
           DB_IAT_APPLY_PAGE_PLAN_TOO_SMALL;
}

static int test_invalid_page_size(void)
{
    fixture value;
    db_iat_apply_summary summary;

    make_fixture(&value);
    return db_iat_apply_patches(value.image,
                                sizeof(value.image),
                                24u,
                                value.pages,
                                PAGE_COUNT,
                                value.patches,
                                PATCH_COUNT,
                                &summary) ==
           DB_IAT_APPLY_INVALID_PAGE_SIZE;
}

static int test_summary_alias_rejected(void)
{
    fixture value;
    db_iat_apply_summary *summary;

    make_fixture(&value);
    summary = (db_iat_apply_summary *)(void *)value.image;

    return db_iat_apply_patches(value.image,
                                sizeof(value.image),
                                PAGE_SIZE,
                                value.pages,
                                PAGE_COUNT,
                                value.patches,
                                PATCH_COUNT,
                                summary) == DB_IAT_APPLY_BUFFER_OVERLAP;
}

static int test_count_overflow(void)
{
    fixture value;
    db_iat_apply_summary summary;

    make_fixture(&value);
    return db_iat_apply_patches(value.image,
                                sizeof(value.image),
                                PAGE_SIZE,
                                value.pages,
                                PAGE_COUNT,
                                value.patches,
                                SIZE_MAX,
                                &summary) == DB_IAT_APPLY_COUNT_OVERFLOW;
}

static int test_status_names(void)
{
    return db_iat_apply_status_name(DB_IAT_APPLY_OK)[0] == 'o' &&
           db_iat_apply_status_name((db_iat_apply_status)99)[0] == 'u';
}

int main(void)
{
    if (!test_success() ||
        !test_empty_plan() ||
        !test_late_stale_preserves_image() ||
        !test_late_page_failure_preserves_image() ||
        !test_overlap_preserves_image() ||
        !test_bad_patch_preserves_image() ||
        !test_record_order_rejected() ||
        !test_reserved_rejected() ||
        !test_noncanonical_target() ||
        !test_cross_page_requires_both_rw() ||
        !test_out_of_range() ||
        !test_bad_page_plan() ||
        !test_page_plan_too_small() ||
        !test_invalid_page_size() ||
        !test_summary_alias_rejected() ||
        !test_count_overflow() ||
        !test_status_names()) {
        static const char failure[] = "IAT APPLY TEST: FAIL\n";
        (void)write(STDERR_FILENO, failure, sizeof(failure) - 1u);
        return 1;
    }

    {
        static const char success[] = "IAT APPLY TEST: PASS\n";
        (void)write(STDOUT_FILENO, success, sizeof(success) - 1u);
    }
    return 0;
}
