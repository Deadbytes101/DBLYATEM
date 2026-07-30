#include "dblyat/reloc.h"

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

_Static_assert(sizeof(db_reloc_record) == 16u,
               "relocation record must be 16 bytes");
_Static_assert(offsetof(db_reloc_record, target_rva) == 0u,
               "target RVA offset mismatch");
_Static_assert(offsetof(db_reloc_record, original_value) == 8u,
               "original value offset mismatch");

typedef struct test_fixture {
    uint8_t source[TEST_FILE_SIZE];
    uint8_t image[TEST_IMAGE_SIZE];
    db_map_page_protection pages[TEST_PAGE_COUNT];
    db_mapped_image mapped;
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
    put_u64(bytes, OPTIONAL_OFFSET + 24u,
            UINT64_C(0x0000000140000000));
    put_u32(bytes, OPTIONAL_OFFSET + 32u, 0x1000u);
    put_u32(bytes, OPTIONAL_OFFSET + 36u, 0x0200u);
    put_u32(bytes, OPTIONAL_OFFSET + 56u, TEST_IMAGE_SIZE);
    put_u32(bytes, OPTIONAL_OFFSET + 60u, 0x0200u);
    put_u16(bytes, OPTIONAL_OFFSET + 68u, 3u);
    put_u16(bytes, OPTIONAL_OFFSET + 70u, 0x8160u);
    put_u32(bytes, OPTIONAL_OFFSET + 108u, 16u);
    put_u32(bytes, RELOC_DIRECTORY_ENTRY_OFFSET,
            RELOC_DIRECTORY_RVA);
    put_u32(bytes, RELOC_DIRECTORY_ENTRY_OFFSET + 4u,
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
    put_u32(bytes, RELOC_SECTION_OFFSET + 12u, RELOC_DIRECTORY_RVA);
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

static int make_fixture(test_fixture *fixture)
{
    make_valid_file(fixture->source);
    fill_bytes(fixture->image, sizeof(fixture->image), 0xa5u);
    fill_bytes(fixture->pages, sizeof(fixture->pages), 0x5au);
    fill_bytes(&fixture->mapped, sizeof(fixture->mapped), 0x3cu);

    return db_map_pe64(fixture->source,
                       sizeof(fixture->source),
                       fixture->image,
                       sizeof(fixture->image),
                       TEST_PAGE_SIZE,
                       fixture->pages,
                       TEST_PAGE_COUNT,
                       &fixture->mapped) == DB_MAP_OK;
}

static int expect_failure_unchanged(test_fixture *fixture,
                                    size_t page_size,
                                    size_t page_capacity,
                                    db_reloc_record *records,
                                    size_t record_capacity,
                                    db_reloc_status expected)
{
    uint8_t image_copy[TEST_IMAGE_SIZE];
    db_map_page_protection page_copy[TEST_PAGE_COUNT];
    db_mapped_image mapped_copy;
    db_reloc_summary summary;
    db_reloc_status actual;

    copy_bytes(image_copy, fixture->image, sizeof(image_copy));
    copy_bytes(page_copy, fixture->pages, sizeof(page_copy));
    mapped_copy = fixture->mapped;
    fill_bytes(records,
               TEST_RECORD_CAPACITY * sizeof(*records),
               0xa5u);
    fill_bytes(&summary, sizeof(summary), 0x3cu);

    actual = db_reloc_plan_pe64(fixture->image,
                                sizeof(fixture->image),
                                &fixture->mapped,
                                page_size,
                                fixture->pages,
                                page_capacity,
                                records,
                                record_capacity,
                                &summary);

    return actual == expected &&
           bytes_equal(fixture->image, image_copy, sizeof(image_copy)) &&
           bytes_equal(fixture->pages, page_copy, sizeof(page_copy)) &&
           bytes_equal(&fixture->mapped, &mapped_copy, sizeof(mapped_copy)) &&
           all_bytes_are(records,
                         TEST_RECORD_CAPACITY * sizeof(*records),
                         0xa5u) &&
           all_bytes_are(&summary, sizeof(summary), 0x3cu);
}

static int test_valid_plan(void)
{
    test_fixture fixture;
    uint8_t image_copy[TEST_IMAGE_SIZE];
    db_map_page_protection page_copy[TEST_PAGE_COUNT];
    db_mapped_image mapped_copy;
    db_reloc_record records[TEST_RECORD_CAPACITY];
    db_reloc_summary summary;
    db_reloc_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    copy_bytes(image_copy, fixture.image, sizeof(image_copy));
    copy_bytes(page_copy, fixture.pages, sizeof(page_copy));
    mapped_copy = fixture.mapped;
    fill_bytes(records, sizeof(records), 0xa5u);
    fill_bytes(&summary, sizeof(summary), 0x3cu);

    status = db_reloc_plan_pe64(fixture.image,
                                sizeof(fixture.image),
                                &fixture.mapped,
                                TEST_PAGE_SIZE,
                                fixture.pages,
                                TEST_PAGE_COUNT,
                                records,
                                TEST_RECORD_CAPACITY,
                                &summary);

    return status == DB_RELOC_OK &&
           bytes_equal(fixture.image, image_copy, sizeof(image_copy)) &&
           bytes_equal(fixture.pages, page_copy, sizeof(page_copy)) &&
           bytes_equal(&fixture.mapped, &mapped_copy, sizeof(mapped_copy)) &&
           summary.directory_rva == RELOC_DIRECTORY_RVA &&
           summary.directory_size == RELOC_DIRECTORY_SIZE &&
           summary.record_count == 2u &&
           records[0].target_rva == FIRST_TARGET_RVA &&
           records[0].type == 10u &&
           records[0].reserved == 0u &&
           records[0].original_value == FIRST_TARGET_VALUE &&
           records[1].target_rva == SECOND_TARGET_RVA &&
           records[1].type == 10u &&
           records[1].reserved == 0u &&
           records[1].original_value == 0u &&
           all_bytes_are(&records[2],
                         2u * sizeof(records[2]),
                         0xa5u);
}

static int test_no_directory(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];
    db_reloc_summary summary;
    db_reloc_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, RELOC_DIRECTORY_ENTRY_OFFSET, 0u);
    put_u32(fixture.image, RELOC_DIRECTORY_ENTRY_OFFSET + 4u, 0u);
    fill_bytes(records, sizeof(records), 0xa5u);
    fill_bytes(&summary, sizeof(summary), 0x3cu);

    status = db_reloc_plan_pe64(fixture.image,
                                sizeof(fixture.image),
                                &fixture.mapped,
                                TEST_PAGE_SIZE,
                                fixture.pages,
                                TEST_PAGE_COUNT,
                                records,
                                TEST_RECORD_CAPACITY,
                                &summary);

    return status == DB_RELOC_OK &&
           summary.directory_rva == 0u &&
           summary.directory_size == 0u &&
           summary.record_count == 0u &&
           all_bytes_are(records, sizeof(records), 0xa5u);
}

static int test_directory_count_absent(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];
    db_reloc_summary summary;
    db_reloc_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, OPTIONAL_OFFSET + 108u, 5u);
    fill_bytes(records, sizeof(records), 0xa5u);
    fill_bytes(&summary, sizeof(summary), 0x3cu);

    status = db_reloc_plan_pe64(fixture.image,
                                sizeof(fixture.image),
                                &fixture.mapped,
                                TEST_PAGE_SIZE,
                                fixture.pages,
                                TEST_PAGE_COUNT,
                                records,
                                TEST_RECORD_CAPACITY,
                                &summary);

    return status == DB_RELOC_OK &&
           summary.directory_rva == 0u &&
           summary.directory_size == 0u &&
           summary.record_count == 0u &&
           all_bytes_are(records, sizeof(records), 0xa5u);
}

static int test_empty_block(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];
    db_reloc_summary summary;
    db_reloc_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, RELOC_DIRECTORY_ENTRY_OFFSET + 4u, 8u);
    put_u32(fixture.image, RELOC_DIRECTORY_RVA + 4u, 8u);
    fill_bytes(records, sizeof(records), 0xa5u);
    fill_bytes(&summary, sizeof(summary), 0x3cu);

    status = db_reloc_plan_pe64(fixture.image,
                                sizeof(fixture.image),
                                &fixture.mapped,
                                TEST_PAGE_SIZE,
                                fixture.pages,
                                TEST_PAGE_COUNT,
                                records,
                                TEST_RECORD_CAPACITY,
                                &summary);

    return status == DB_RELOC_OK &&
           summary.directory_rva == RELOC_DIRECTORY_RVA &&
           summary.directory_size == 8u &&
           summary.record_count == 0u &&
           all_bytes_are(records, sizeof(records), 0xa5u);
}

static int test_record_buffer_too_small(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    return make_fixture(&fixture) &&
           expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    1u,
                                    DB_RELOC_RECORD_BUFFER_TOO_SMALL);
}

static int test_invalid_page_size(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    return make_fixture(&fixture) &&
           expect_failure_unchanged(&fixture,
                                    0x1800u,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_INVALID_PAGE_SIZE);
}

static int test_page_plan_too_small(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    return make_fixture(&fixture) &&
           expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT - 1u,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_PAGE_PLAN_TOO_SMALL);
}

static int test_bad_mapped_metadata(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    --fixture.mapped.image_size;
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_BAD_MAPPED_IMAGE);
}

static int test_one_sided_directory(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, RELOC_DIRECTORY_ENTRY_OFFSET + 4u, 0u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_BAD_DIRECTORY);
}

static int test_directory_out_of_range(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, RELOC_DIRECTORY_ENTRY_OFFSET, 0x3ff8u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_DIRECTORY_OUT_OF_RANGE);
}

static int test_directory_unmapped_gap(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, RELOC_DIRECTORY_ENTRY_OFFSET, 0x0800u);
    put_u32(fixture.image, RELOC_DIRECTORY_ENTRY_OFFSET + 4u, 8u);
    put_u32(fixture.image, 0x0800u, 0x2000u);
    put_u32(fixture.image, 0x0804u, 8u);

    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_DIRECTORY_UNMAPPED);
}

static int test_directory_page_none(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fixture.pages[3] = DB_MAP_PAGE_NONE;
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_DIRECTORY_UNMAPPED);
}

static int test_bad_block_size(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, RELOC_DIRECTORY_RVA + 4u, 6u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_BAD_BLOCK);
}

static int test_odd_block_size(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, RELOC_DIRECTORY_RVA + 4u, 9u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_BAD_BLOCK);
}

static int test_block_escapes_directory(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, RELOC_DIRECTORY_RVA + 4u, 18u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_BAD_BLOCK);
}

static int test_unaligned_block_page(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, RELOC_DIRECTORY_RVA, 0x2100u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_BAD_BLOCK);
}

static int test_unsupported_type(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u16(fixture.image, RELOC_DIRECTORY_RVA + 8u, 0x3010u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_UNSUPPORTED_TYPE);
}

static int test_target_out_of_range(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, RELOC_DIRECTORY_RVA, 0x3000u);
    put_u16(fixture.image, RELOC_DIRECTORY_RVA + 8u, 0xaffcu);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_TARGET_OUT_OF_RANGE);
}

static int test_target_unmapped_gap(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, RELOC_DIRECTORY_RVA, 0u);
    put_u16(fixture.image, RELOC_DIRECTORY_RVA + 8u, 0xa800u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_TARGET_UNMAPPED);
}

static int test_target_page_none(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fixture.pages[2] = DB_MAP_PAGE_NONE;
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_RELOC_TARGET_UNMAPPED);
}

static int test_output_overlaps_image(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];
    uint8_t image_copy[TEST_IMAGE_SIZE];
    db_reloc_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    copy_bytes(image_copy, fixture.image, sizeof(image_copy));
    fill_bytes(records, sizeof(records), 0xa5u);

    status = db_reloc_plan_pe64(
        fixture.image,
        sizeof(fixture.image),
        &fixture.mapped,
        TEST_PAGE_SIZE,
        fixture.pages,
        TEST_PAGE_COUNT,
        records,
        TEST_RECORD_CAPACITY,
        (db_reloc_summary *)(void *)(fixture.image + 0x100u));

    return status == DB_RELOC_BUFFER_OVERLAP &&
           bytes_equal(fixture.image, image_copy, sizeof(image_copy)) &&
           all_bytes_are(records, sizeof(records), 0xa5u);
}

static int test_capacity_overflow(void)
{
    test_fixture fixture;
    db_reloc_record records[TEST_RECORD_CAPACITY];
    db_reloc_summary summary;
    db_reloc_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fill_bytes(records, sizeof(records), 0xa5u);
    fill_bytes(&summary, sizeof(summary), 0x3cu);

    status = db_reloc_plan_pe64(fixture.image,
                                sizeof(fixture.image),
                                &fixture.mapped,
                                TEST_PAGE_SIZE,
                                fixture.pages,
                                TEST_PAGE_COUNT,
                                records,
                                SIZE_MAX,
                                &summary);

    return status == DB_RELOC_RECORD_CAPACITY_OVERFLOW &&
           all_bytes_are(records, sizeof(records), 0xa5u) &&
           all_bytes_are(&summary, sizeof(summary), 0x3cu);
}

static int test_null_zero_capacity(void)
{
    test_fixture fixture;
    db_reloc_summary summary;
    db_reloc_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fill_bytes(&summary, sizeof(summary), 0x3cu);
    status = db_reloc_plan_pe64(fixture.image,
                                sizeof(fixture.image),
                                &fixture.mapped,
                                TEST_PAGE_SIZE,
                                fixture.pages,
                                TEST_PAGE_COUNT,
                                NULL,
                                0u,
                                &summary);

    return status == DB_RELOC_RECORD_BUFFER_TOO_SMALL &&
           all_bytes_are(&summary, sizeof(summary), 0x3cu);
}

int main(void)
{
    int failures = 0;

    failures += !test_valid_plan();
    failures += !test_no_directory();
    failures += !test_directory_count_absent();
    failures += !test_empty_block();
    failures += !test_record_buffer_too_small();
    failures += !test_invalid_page_size();
    failures += !test_page_plan_too_small();
    failures += !test_bad_mapped_metadata();
    failures += !test_one_sided_directory();
    failures += !test_directory_out_of_range();
    failures += !test_directory_unmapped_gap();
    failures += !test_directory_page_none();
    failures += !test_bad_block_size();
    failures += !test_odd_block_size();
    failures += !test_block_escapes_directory();
    failures += !test_unaligned_block_page();
    failures += !test_unsupported_type();
    failures += !test_target_out_of_range();
    failures += !test_target_unmapped_gap();
    failures += !test_target_page_none();
    failures += !test_output_overlaps_image();
    failures += !test_capacity_overflow();
    failures += !test_null_zero_capacity();

    if (failures != 0) {
        (void)write(STDERR_FILENO, "RELOC TEST: FAIL\n", 17u);
        return 1;
    }

    (void)write(STDOUT_FILENO, "RELOC TEST: PASS\n", 17u);
    return 0;
}
