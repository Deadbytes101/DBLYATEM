#include "dblyat/reloc_apply.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define TEST_FILE_SIZE 0x800u
#define TEST_IMAGE_SIZE 0x4000u
#define TEST_PAGE_SIZE 0x1000u
#define TEST_PAGE_COUNT 4u
#define TEST_RECORD_CAPACITY 4u

#define PE_OFFSET 0x80u
#define COFF_OFFSET (PE_OFFSET + 4u)
#define OPTIONAL_OFFSET (COFF_OFFSET + 20u)
#define SECTION_TABLE_OFFSET (OPTIONAL_OFFSET + 0xf0u)
#define TEXT_SECTION_OFFSET SECTION_TABLE_OFFSET
#define DATA_SECTION_OFFSET (SECTION_TABLE_OFFSET + 40u)
#define RELOC_SECTION_OFFSET (SECTION_TABLE_OFFSET + 80u)

#define TEXT_RAW_OFFSET 0x0200u
#define DATA_RAW_OFFSET 0x0400u
#define RELOC_RAW_OFFSET 0x0600u
#define RELOC_DIRECTORY_ENTRY_OFFSET (OPTIONAL_OFFSET + 152u)
#define RELOC_DIRECTORY_RVA 0x3000u
#define RELOC_DIRECTORY_SIZE 16u

#define FIRST_TARGET_RVA 0x2010u
#define SECOND_TARGET_RVA 0x2ff8u
#define FIRST_TARGET_VALUE UINT64_C(0x1122334455667788)
#define SECOND_TARGET_VALUE UINT64_C(0x2233445566778899)
#define POSITIVE_DELTA UINT64_C(0x00200000)
#define NEGATIVE_DELTA UINT64_C(0x00010000)

_Static_assert(sizeof(db_reloc_apply_result) >=
                   sizeof(uint64_t) * 2u + sizeof(size_t),
               "relocation result fields must fit");

typedef struct test_fixture {
    uint8_t source[TEST_FILE_SIZE];
    uint8_t image[TEST_IMAGE_SIZE];
    db_map_page_protection pages[TEST_PAGE_COUNT];
    db_mapped_image mapped;
    db_reloc_record records[TEST_RECORD_CAPACITY];
    db_reloc_summary summary;
} test_fixture;

static void put_u16(uint8_t *bytes, size_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value & 0xffu);
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)(value & 0xffu);
    bytes[offset + 1u] = (uint8_t)((value >> 8u) & 0xffu);
    bytes[offset + 2u] = (uint8_t)((value >> 16u) & 0xffu);
    bytes[offset + 3u] = (uint8_t)(value >> 24u);
}

static void put_u64(uint8_t *bytes, size_t offset, uint64_t value)
{
    put_u32(bytes, offset, (uint32_t)value);
    put_u32(bytes, offset + 4u, (uint32_t)(value >> 32u));
}

static uint32_t get_u32(const uint8_t *bytes, size_t offset)
{
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t get_u64(const uint8_t *bytes, size_t offset)
{
    return (uint64_t)get_u32(bytes, offset) |
           ((uint64_t)get_u32(bytes, offset + 4u) << 32u);
}

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
    uint8_t *destination_bytes = (uint8_t *)destination;
    const uint8_t *source_bytes = (const uint8_t *)source;
    size_t index;

    for (index = 0u; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
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

static int all_bytes_are(const void *data, size_t size, uint8_t value)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (bytes[index] != value) {
            return 0;
        }
    }

    return 1;
}

static void make_valid_file(uint8_t *bytes)
{
    size_t index;

    fill_bytes(bytes, TEST_FILE_SIZE, 0u);

    bytes[0] = (uint8_t)'M';
    bytes[1] = (uint8_t)'Z';
    put_u32(bytes, 0x3cu, PE_OFFSET);

    bytes[PE_OFFSET] = (uint8_t)'P';
    bytes[PE_OFFSET + 1u] = (uint8_t)'E';
    put_u16(bytes, COFF_OFFSET, 0x8664u);
    put_u16(bytes, COFF_OFFSET + 2u, 3u);
    put_u32(bytes, COFF_OFFSET + 4u, 0x12345678u);
    put_u16(bytes, COFF_OFFSET + 16u, 0xf0u);
    put_u16(bytes, COFF_OFFSET + 18u, 0x0022u);

    put_u16(bytes, OPTIONAL_OFFSET, 0x020bu);
    put_u32(bytes, OPTIONAL_OFFSET + 16u, 0x1000u);
    put_u64(bytes,
            OPTIONAL_OFFSET + 24u,
            UINT64_C(0x0000000140000000));
    put_u32(bytes, OPTIONAL_OFFSET + 32u, 0x1000u);
    put_u32(bytes, OPTIONAL_OFFSET + 36u, 0x0200u);
    put_u32(bytes, OPTIONAL_OFFSET + 56u, TEST_IMAGE_SIZE);
    put_u32(bytes, OPTIONAL_OFFSET + 60u, 0x0200u);
    put_u16(bytes, OPTIONAL_OFFSET + 68u, 3u);
    put_u16(bytes, OPTIONAL_OFFSET + 70u, 0x8160u);
    put_u32(bytes, OPTIONAL_OFFSET + 108u, 16u);
    put_u32(bytes,
            RELOC_DIRECTORY_ENTRY_OFFSET,
            RELOC_DIRECTORY_RVA);
    put_u32(bytes,
            RELOC_DIRECTORY_ENTRY_OFFSET + 4u,
            RELOC_DIRECTORY_SIZE);

    bytes[TEXT_SECTION_OFFSET] = (uint8_t)'.';
    bytes[TEXT_SECTION_OFFSET + 1u] = (uint8_t)'t';
    bytes[TEXT_SECTION_OFFSET + 2u] = (uint8_t)'e';
    bytes[TEXT_SECTION_OFFSET + 3u] = (uint8_t)'x';
    bytes[TEXT_SECTION_OFFSET + 4u] = (uint8_t)'t';
    put_u32(bytes, TEXT_SECTION_OFFSET + 8u, 0x0200u);
    put_u32(bytes, TEXT_SECTION_OFFSET + 12u, 0x1000u);
    put_u32(bytes, TEXT_SECTION_OFFSET + 16u, 0x0200u);
    put_u32(bytes, TEXT_SECTION_OFFSET + 20u, TEXT_RAW_OFFSET);
    put_u32(bytes, TEXT_SECTION_OFFSET + 36u, 0x60000020u);

    bytes[DATA_SECTION_OFFSET] = (uint8_t)'.';
    bytes[DATA_SECTION_OFFSET + 1u] = (uint8_t)'d';
    bytes[DATA_SECTION_OFFSET + 2u] = (uint8_t)'a';
    bytes[DATA_SECTION_OFFSET + 3u] = (uint8_t)'t';
    bytes[DATA_SECTION_OFFSET + 4u] = (uint8_t)'a';
    put_u32(bytes, DATA_SECTION_OFFSET + 8u, 0x1000u);
    put_u32(bytes, DATA_SECTION_OFFSET + 12u, 0x2000u);
    put_u32(bytes, DATA_SECTION_OFFSET + 16u, 0x0200u);
    put_u32(bytes, DATA_SECTION_OFFSET + 20u, DATA_RAW_OFFSET);
    put_u32(bytes, DATA_SECTION_OFFSET + 36u, 0xc0000040u);

    bytes[RELOC_SECTION_OFFSET] = (uint8_t)'.';
    bytes[RELOC_SECTION_OFFSET + 1u] = (uint8_t)'r';
    bytes[RELOC_SECTION_OFFSET + 2u] = (uint8_t)'e';
    bytes[RELOC_SECTION_OFFSET + 3u] = (uint8_t)'l';
    bytes[RELOC_SECTION_OFFSET + 4u] = (uint8_t)'o';
    bytes[RELOC_SECTION_OFFSET + 5u] = (uint8_t)'c';
    put_u32(bytes, RELOC_SECTION_OFFSET + 8u, 0x0200u);
    put_u32(bytes,
            RELOC_SECTION_OFFSET + 12u,
            RELOC_DIRECTORY_RVA);
    put_u32(bytes, RELOC_SECTION_OFFSET + 16u, 0x0200u);
    put_u32(bytes, RELOC_SECTION_OFFSET + 20u, RELOC_RAW_OFFSET);
    put_u32(bytes, RELOC_SECTION_OFFSET + 36u, 0x40000040u);

    for (index = 0u; index < 0x0200u; ++index) {
        bytes[TEXT_RAW_OFFSET + index] =
            (uint8_t)(0x40u + (index & 0x3fu));
        bytes[DATA_RAW_OFFSET + index] =
            (uint8_t)(0x80u + (index & 0x3fu));
    }

    put_u64(bytes, DATA_RAW_OFFSET + 0x10u, FIRST_TARGET_VALUE);

    put_u32(bytes, RELOC_RAW_OFFSET, 0x2000u);
    put_u32(bytes, RELOC_RAW_OFFSET + 4u, RELOC_DIRECTORY_SIZE);
    put_u16(bytes, RELOC_RAW_OFFSET + 8u, 0xa010u);
    put_u16(bytes, RELOC_RAW_OFFSET + 10u, 0x0000u);
    put_u16(bytes, RELOC_RAW_OFFSET + 12u, 0xaff8u);
    put_u16(bytes, RELOC_RAW_OFFSET + 14u, 0x0000u);
}

static int map_fixture(test_fixture *fixture)
{
    make_valid_file(fixture->source);
    fill_bytes(fixture->image, sizeof(fixture->image), 0xa5u);
    fill_bytes(fixture->pages, sizeof(fixture->pages), 0x5au);
    fill_bytes(&fixture->mapped, sizeof(fixture->mapped), 0x3cu);
    fill_bytes(fixture->records, sizeof(fixture->records), 0xa5u);
    fill_bytes(&fixture->summary, sizeof(fixture->summary), 0x3cu);

    return db_map_pe64(fixture->source,
                       sizeof(fixture->source),
                       fixture->image,
                       sizeof(fixture->image),
                       TEST_PAGE_SIZE,
                       fixture->pages,
                       TEST_PAGE_COUNT,
                       &fixture->mapped) == DB_MAP_OK;
}

static int plan_fixture(test_fixture *fixture)
{
    fill_bytes(fixture->records, sizeof(fixture->records), 0xa5u);
    fill_bytes(&fixture->summary, sizeof(fixture->summary), 0x3cu);

    return db_reloc_plan_pe64(fixture->image,
                              sizeof(fixture->image),
                              &fixture->mapped,
                              TEST_PAGE_SIZE,
                              fixture->pages,
                              TEST_PAGE_COUNT,
                              fixture->records,
                              TEST_RECORD_CAPACITY,
                              &fixture->summary) == DB_RELOC_OK;
}

static int make_fixture(test_fixture *fixture)
{
    return map_fixture(fixture) && plan_fixture(fixture);
}

static void set_single_relocation(test_fixture *fixture,
                                  uint32_t page_rva,
                                  uint16_t entry)
{
    put_u32(fixture->image,
            RELOC_DIRECTORY_ENTRY_OFFSET + 4u,
            10u);
    put_u32(fixture->image, RELOC_DIRECTORY_RVA, page_rva);
    put_u32(fixture->image, RELOC_DIRECTORY_RVA + 4u, 10u);
    put_u16(fixture->image, RELOC_DIRECTORY_RVA + 8u, entry);
}

static int expect_failure_unchanged(test_fixture *fixture,
                                    size_t page_size,
                                    size_t page_capacity,
                                    size_t record_count,
                                    uint64_t loaded_base,
                                    db_reloc_apply_status expected)
{
    uint8_t image_copy[TEST_IMAGE_SIZE];
    db_map_page_protection page_copy[TEST_PAGE_COUNT];
    db_mapped_image mapped_copy;
    db_reloc_record record_copy[TEST_RECORD_CAPACITY];
    db_reloc_summary summary_copy;
    db_reloc_apply_result result;
    db_reloc_apply_status actual;

    copy_bytes(image_copy, fixture->image, sizeof(image_copy));
    copy_bytes(page_copy, fixture->pages, sizeof(page_copy));
    mapped_copy = fixture->mapped;
    copy_bytes(record_copy, fixture->records, sizeof(record_copy));
    summary_copy = fixture->summary;
    fill_bytes(&result, sizeof(result), 0x5au);

    actual = db_reloc_apply_pe64(fixture->image,
                                 sizeof(fixture->image),
                                 &fixture->mapped,
                                 page_size,
                                 fixture->pages,
                                 page_capacity,
                                 fixture->records,
                                 record_count,
                                 &fixture->summary,
                                 loaded_base,
                                 &result);

    return actual == expected &&
           bytes_equal(fixture->image, image_copy, sizeof(image_copy)) &&
           bytes_equal(fixture->pages, page_copy, sizeof(page_copy)) &&
           bytes_equal(&fixture->mapped, &mapped_copy, sizeof(mapped_copy)) &&
           bytes_equal(fixture->records, record_copy, sizeof(record_copy)) &&
           bytes_equal(&fixture->summary,
                       &summary_copy,
                       sizeof(summary_copy)) &&
           all_bytes_are(&result, sizeof(result), 0x5au);
}

static int test_positive_delta(void)
{
    test_fixture fixture;
    uint8_t expected[TEST_IMAGE_SIZE];
    db_reloc_apply_result result;
    const uint64_t loaded_base =
        UINT64_C(0x0000000140000000) + POSITIVE_DELTA;
    db_reloc_apply_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    copy_bytes(expected, fixture.image, sizeof(expected));
    put_u64(expected,
            FIRST_TARGET_RVA,
            FIRST_TARGET_VALUE + POSITIVE_DELTA);
    put_u64(expected, SECOND_TARGET_RVA, POSITIVE_DELTA);
    fill_bytes(&result, sizeof(result), 0x5au);

    status = db_reloc_apply_pe64(fixture.image,
                                 sizeof(fixture.image),
                                 &fixture.mapped,
                                 TEST_PAGE_SIZE,
                                 fixture.pages,
                                 TEST_PAGE_COUNT,
                                 fixture.records,
                                 fixture.summary.record_count,
                                 &fixture.summary,
                                 loaded_base,
                                 &result);

    return status == DB_RELOC_APPLY_OK &&
           bytes_equal(fixture.image, expected, sizeof(expected)) &&
           result.preferred_base == fixture.mapped.preferred_base &&
           result.loaded_base == loaded_base &&
           result.applied_count == 2u;
}

static int test_negative_delta(void)
{
    test_fixture fixture;
    uint8_t expected[TEST_IMAGE_SIZE];
    db_reloc_apply_result result;
    uint64_t loaded_base;
    db_reloc_apply_status status;

    if (!map_fixture(&fixture)) {
        return 0;
    }

    put_u64(fixture.image, SECOND_TARGET_RVA, SECOND_TARGET_VALUE);
    if (!plan_fixture(&fixture)) {
        return 0;
    }

    loaded_base = fixture.mapped.preferred_base - NEGATIVE_DELTA;
    copy_bytes(expected, fixture.image, sizeof(expected));
    put_u64(expected,
            FIRST_TARGET_RVA,
            FIRST_TARGET_VALUE - NEGATIVE_DELTA);
    put_u64(expected,
            SECOND_TARGET_RVA,
            SECOND_TARGET_VALUE - NEGATIVE_DELTA);
    fill_bytes(&result, sizeof(result), 0x5au);

    status = db_reloc_apply_pe64(fixture.image,
                                 sizeof(fixture.image),
                                 &fixture.mapped,
                                 TEST_PAGE_SIZE,
                                 fixture.pages,
                                 TEST_PAGE_COUNT,
                                 fixture.records,
                                 fixture.summary.record_count,
                                 &fixture.summary,
                                 loaded_base,
                                 &result);

    return status == DB_RELOC_APPLY_OK &&
           bytes_equal(fixture.image, expected, sizeof(expected)) &&
           result.loaded_base == loaded_base &&
           result.applied_count == 2u;
}

static int test_zero_delta(void)
{
    test_fixture fixture;
    uint8_t image_copy[TEST_IMAGE_SIZE];
    db_reloc_apply_result result;
    db_reloc_apply_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    copy_bytes(image_copy, fixture.image, sizeof(image_copy));
    fill_bytes(&result, sizeof(result), 0x5au);

    status = db_reloc_apply_pe64(fixture.image,
                                 sizeof(fixture.image),
                                 &fixture.mapped,
                                 TEST_PAGE_SIZE,
                                 fixture.pages,
                                 TEST_PAGE_COUNT,
                                 fixture.records,
                                 fixture.summary.record_count,
                                 &fixture.summary,
                                 fixture.mapped.preferred_base,
                                 &result);

    return status == DB_RELOC_APPLY_OK &&
           bytes_equal(fixture.image, image_copy, sizeof(image_copy)) &&
           result.applied_count == 2u;
}

static int test_no_directory(void)
{
    test_fixture fixture;
    uint8_t image_copy[TEST_IMAGE_SIZE];
    db_reloc_apply_result result;
    db_reloc_apply_status status;

    if (!map_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, RELOC_DIRECTORY_ENTRY_OFFSET, 0u);
    put_u32(fixture.image, RELOC_DIRECTORY_ENTRY_OFFSET + 4u, 0u);
    if (!plan_fixture(&fixture)) {
        return 0;
    }

    copy_bytes(image_copy, fixture.image, sizeof(image_copy));
    fill_bytes(&result, sizeof(result), 0x5au);

    status = db_reloc_apply_pe64(fixture.image,
                                 sizeof(fixture.image),
                                 &fixture.mapped,
                                 TEST_PAGE_SIZE,
                                 fixture.pages,
                                 TEST_PAGE_COUNT,
                                 NULL,
                                 0u,
                                 &fixture.summary,
                                 fixture.mapped.preferred_base +
                                     POSITIVE_DELTA,
                                 &result);

    return status == DB_RELOC_APPLY_OK &&
           bytes_equal(fixture.image, image_copy, sizeof(image_copy)) &&
           result.applied_count == 0u;
}

static int test_stale_first_value(void)
{
    test_fixture fixture;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u64(fixture.image,
            FIRST_TARGET_RVA,
            FIRST_TARGET_VALUE + 1u);
    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        fixture.summary.record_count,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_STALE_VALUE);
}

static int test_transactional_second_stale(void)
{
    test_fixture fixture;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u64(fixture.image, SECOND_TARGET_RVA, 1u);
    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        fixture.summary.record_count,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_STALE_VALUE);
}

static int test_duplicate_target(void)
{
    test_fixture fixture;

    if (!map_fixture(&fixture)) {
        return 0;
    }

    put_u16(fixture.image, RELOC_DIRECTORY_RVA + 12u, 0xa010u);
    if (!plan_fixture(&fixture)) {
        return 0;
    }

    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        fixture.summary.record_count,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_DUPLICATE_TARGET);
}

static int test_overlapping_targets(void)
{
    test_fixture fixture;

    if (!map_fixture(&fixture)) {
        return 0;
    }

    put_u16(fixture.image, RELOC_DIRECTORY_RVA + 12u, 0xa014u);
    if (!plan_fixture(&fixture)) {
        return 0;
    }

    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        fixture.summary.record_count,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_TARGET_OVERLAP);
}

static int test_record_mismatch(void)
{
    test_fixture fixture;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    ++fixture.records[0].target_rva;
    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        fixture.summary.record_count,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_RECORD_MISMATCH);
}

static int test_reserved_field(void)
{
    test_fixture fixture;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fixture.records[0].reserved = 1u;
    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        fixture.summary.record_count,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_RECORD_MISMATCH);
}

static int test_summary_mismatch(void)
{
    test_fixture fixture;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    ++fixture.summary.directory_size;
    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        fixture.summary.record_count,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_BAD_SUMMARY);
}

static int test_record_count_mismatch(void)
{
    test_fixture fixture;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fixture.summary.record_count = 1u;
    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        1u,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_RECORD_COUNT_MISMATCH);
}

static int test_unsupported_type(void)
{
    test_fixture fixture;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u16(fixture.image, RELOC_DIRECTORY_RVA + 8u, 0x3010u);
    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        fixture.summary.record_count,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_UNSUPPORTED_TYPE);
}

static int test_target_out_of_range(void)
{
    test_fixture fixture;

    if (!map_fixture(&fixture)) {
        return 0;
    }

    set_single_relocation(&fixture, 0x3000u, 0xaffcu);
    fixture.summary.directory_rva = RELOC_DIRECTORY_RVA;
    fixture.summary.directory_size = 10u;
    fixture.summary.record_count = 1u;
    fixture.records[0].target_rva = 0x3ffcu;
    fixture.records[0].type = 10u;
    fixture.records[0].reserved = 0u;
    fixture.records[0].original_value = 0u;

    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        1u,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_TARGET_OUT_OF_RANGE);
}

static int test_target_unmapped_gap(void)
{
    test_fixture fixture;

    if (!map_fixture(&fixture)) {
        return 0;
    }

    set_single_relocation(&fixture, 0u, 0xa800u);
    fixture.summary.directory_rva = RELOC_DIRECTORY_RVA;
    fixture.summary.directory_size = 10u;
    fixture.summary.record_count = 1u;
    fixture.records[0].target_rva = 0x0800u;
    fixture.records[0].type = 10u;
    fixture.records[0].reserved = 0u;
    fixture.records[0].original_value = 0u;

    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        1u,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_TARGET_UNMAPPED);
}

static int test_target_page_none(void)
{
    test_fixture fixture;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fixture.pages[2] = DB_MAP_PAGE_NONE;
    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        fixture.summary.record_count,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_TARGET_UNMAPPED);
}

static int test_bad_page_plan(void)
{
    test_fixture fixture;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fixture.pages[2] = 7u;
    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        fixture.summary.record_count,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_BAD_PAGE_PLAN);
}

static int test_target_overlaps_directory(void)
{
    test_fixture fixture;

    if (!map_fixture(&fixture)) {
        return 0;
    }

    set_single_relocation(&fixture, RELOC_DIRECTORY_RVA, 0xa000u);
    fixture.summary.directory_rva = RELOC_DIRECTORY_RVA;
    fixture.summary.directory_size = 10u;
    fixture.summary.record_count = 1u;
    fixture.records[0].target_rva = RELOC_DIRECTORY_RVA;
    fixture.records[0].type = 10u;
    fixture.records[0].reserved = 0u;
    fixture.records[0].original_value = get_u64(
        fixture.image, RELOC_DIRECTORY_RVA);

    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        1u,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        DB_RELOC_APPLY_TARGET_OVERLAPS_DIRECTORY);
}

static int test_loaded_base_unaligned(void)
{
    test_fixture fixture;

    return make_fixture(&fixture) &&
           expect_failure_unchanged(
               &fixture,
               TEST_PAGE_SIZE,
               TEST_PAGE_COUNT,
               fixture.summary.record_count,
               fixture.mapped.preferred_base + 1u,
               DB_RELOC_APPLY_LOADED_BASE_UNALIGNED);
}

static int test_image_range_overflow(void)
{
    test_fixture fixture;
    const uint64_t loaded_base =
        UINT64_MAX - (UINT64_MAX % TEST_PAGE_SIZE);

    return make_fixture(&fixture) &&
           expect_failure_unchanged(
               &fixture,
               TEST_PAGE_SIZE,
               TEST_PAGE_COUNT,
               fixture.summary.record_count,
               loaded_base,
               DB_RELOC_APPLY_IMAGE_RANGE_OVERFLOW);
}

static int test_positive_value_overflow(void)
{
    test_fixture fixture;

    if (!map_fixture(&fixture)) {
        return 0;
    }

    set_single_relocation(&fixture, 0x2000u, 0xa010u);
    put_u64(fixture.image,
            FIRST_TARGET_RVA,
            UINT64_MAX - 0x1000u);
    if (!plan_fixture(&fixture)) {
        return 0;
    }

    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        fixture.summary.record_count,
        fixture.mapped.preferred_base + 0x2000u,
        DB_RELOC_APPLY_VALUE_OVERFLOW);
}

static int test_negative_value_overflow(void)
{
    test_fixture fixture;

    if (!map_fixture(&fixture)) {
        return 0;
    }

    set_single_relocation(&fixture, 0x2000u, 0xa010u);
    put_u64(fixture.image, FIRST_TARGET_RVA, 0x1000u);
    if (!plan_fixture(&fixture)) {
        return 0;
    }

    return expect_failure_unchanged(
        &fixture,
        TEST_PAGE_SIZE,
        TEST_PAGE_COUNT,
        fixture.summary.record_count,
        fixture.mapped.preferred_base - 0x2000u,
        DB_RELOC_APPLY_VALUE_OVERFLOW);
}

static int test_result_overlaps_image(void)
{
    test_fixture fixture;
    uint8_t image_copy[TEST_IMAGE_SIZE];
    db_reloc_apply_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    copy_bytes(image_copy, fixture.image, sizeof(image_copy));
    status = db_reloc_apply_pe64(
        fixture.image,
        sizeof(fixture.image),
        &fixture.mapped,
        TEST_PAGE_SIZE,
        fixture.pages,
        TEST_PAGE_COUNT,
        fixture.records,
        fixture.summary.record_count,
        &fixture.summary,
        fixture.mapped.preferred_base + POSITIVE_DELTA,
        (db_reloc_apply_result *)(void *)(fixture.image + 0x100u));

    return status == DB_RELOC_APPLY_BUFFER_OVERLAP &&
           bytes_equal(fixture.image, image_copy, sizeof(image_copy));
}

static int test_record_count_overflow(void)
{
    test_fixture fixture;
    db_reloc_apply_result result;
    db_reloc_apply_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fill_bytes(&result, sizeof(result), 0x5au);
    status = db_reloc_apply_pe64(fixture.image,
                                 sizeof(fixture.image),
                                 &fixture.mapped,
                                 TEST_PAGE_SIZE,
                                 fixture.pages,
                                 TEST_PAGE_COUNT,
                                 fixture.records,
                                 SIZE_MAX,
                                 &fixture.summary,
                                 fixture.mapped.preferred_base,
                                 &result);

    return status == DB_RELOC_APPLY_RECORD_COUNT_OVERFLOW &&
           all_bytes_are(&result, sizeof(result), 0x5au);
}

static int test_null_records_with_count(void)
{
    test_fixture fixture;
    db_reloc_apply_result result;
    db_reloc_apply_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fill_bytes(&result, sizeof(result), 0x5au);
    status = db_reloc_apply_pe64(fixture.image,
                                 sizeof(fixture.image),
                                 &fixture.mapped,
                                 TEST_PAGE_SIZE,
                                 fixture.pages,
                                 TEST_PAGE_COUNT,
                                 NULL,
                                 fixture.summary.record_count,
                                 &fixture.summary,
                                 fixture.mapped.preferred_base,
                                 &result);

    return status == DB_RELOC_APPLY_NULL_ARGUMENT &&
           all_bytes_are(&result, sizeof(result), 0x5au);
}

int main(void)
{
    int failures = 0;

    failures += !test_positive_delta();
    failures += !test_negative_delta();
    failures += !test_zero_delta();
    failures += !test_no_directory();
    failures += !test_stale_first_value();
    failures += !test_transactional_second_stale();
    failures += !test_duplicate_target();
    failures += !test_overlapping_targets();
    failures += !test_record_mismatch();
    failures += !test_reserved_field();
    failures += !test_summary_mismatch();
    failures += !test_record_count_mismatch();
    failures += !test_unsupported_type();
    failures += !test_target_out_of_range();
    failures += !test_target_unmapped_gap();
    failures += !test_target_page_none();
    failures += !test_bad_page_plan();
    failures += !test_target_overlaps_directory();
    failures += !test_loaded_base_unaligned();
    failures += !test_image_range_overflow();
    failures += !test_positive_value_overflow();
    failures += !test_negative_value_overflow();
    failures += !test_result_overlaps_image();
    failures += !test_record_count_overflow();
    failures += !test_null_records_with_count();

    if (failures != 0) {
        (void)write(STDERR_FILENO, "RELOC APPLY TEST: FAIL\n", 23u);
        return 1;
    }

    (void)write(STDOUT_FILENO, "RELOC APPLY TEST: PASS\n", 23u);
    return 0;
}
