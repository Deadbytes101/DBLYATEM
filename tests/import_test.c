#include "dblyat/import.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define TEST_FILE_SIZE 0x0800u
#define TEST_IMAGE_SIZE 0x3000u
#define TEST_PAGE_SIZE 0x1000u
#define TEST_PAGE_COUNT 3u
#define TEST_MODULE_CAPACITY 4u
#define TEST_RECORD_CAPACITY 8u

#define PE_OFFSET 0x0080u
#define COFF_OFFSET (PE_OFFSET + 4u)
#define OPTIONAL_OFFSET (COFF_OFFSET + 20u)
#define SECTION_TABLE_OFFSET (OPTIONAL_OFFSET + 0x00f0u)
#define TEXT_SECTION_OFFSET SECTION_TABLE_OFFSET
#define IDATA_SECTION_OFFSET (SECTION_TABLE_OFFSET + 40u)

#define TEXT_RAW_OFFSET 0x0200u
#define IDATA_RAW_OFFSET 0x0400u
#define IDATA_RVA 0x2000u

#define IMPORT_DIRECTORY_ENTRY_OFFSET (OPTIONAL_OFFSET + 120u)
#define IMPORT_DIRECTORY_RVA 0x2000u
#define IMPORT_DIRECTORY_SIZE 40u
#define DLL_NAME_RVA 0x2080u
#define LOOKUP_TABLE_RVA 0x2100u
#define IAT_RVA 0x2120u
#define IMPORT_NAME_STRUCT_RVA 0x2200u
#define IMPORT_NAME_RVA (IMPORT_NAME_STRUCT_RVA + 2u)
#define ORDINAL_FLAG UINT64_C(0x8000000000000000)

_Static_assert(offsetof(db_import_record, module_index) == 0u,
               "module index offset mismatch");
_Static_assert(offsetof(db_import_record, lookup_value) == 32u,
               "lookup value offset mismatch");
_Static_assert(sizeof(db_import_record) == 48u,
               "import record must be 48 bytes");

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

static size_t idata_raw(uint32_t rva)
{
    return IDATA_RAW_OFFSET + (size_t)(rva - IDATA_RVA);
}

static void put_ascii(uint8_t *bytes, size_t offset, const char *text)
{
    size_t index = 0u;

    while (text[index] != '\0') {
        bytes[offset + index] = (uint8_t)text[index];
        ++index;
    }
    bytes[offset + index] = 0u;
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
    put_u16(bytes, COFF_OFFSET + 2u, 2u);
    put_u32(bytes, COFF_OFFSET + 4u, 0x12345678u);
    put_u16(bytes, COFF_OFFSET + 16u, 0x00f0u);
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
    put_u32(bytes, IMPORT_DIRECTORY_ENTRY_OFFSET,
            IMPORT_DIRECTORY_RVA);
    put_u32(bytes, IMPORT_DIRECTORY_ENTRY_OFFSET + 4u,
            IMPORT_DIRECTORY_SIZE);

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

    bytes[IDATA_SECTION_OFFSET] = (uint8_t)'.';
    bytes[IDATA_SECTION_OFFSET + 1u] = (uint8_t)'i';
    bytes[IDATA_SECTION_OFFSET + 2u] = (uint8_t)'d';
    bytes[IDATA_SECTION_OFFSET + 3u] = (uint8_t)'a';
    bytes[IDATA_SECTION_OFFSET + 4u] = (uint8_t)'t';
    bytes[IDATA_SECTION_OFFSET + 5u] = (uint8_t)'a';
    put_u32(bytes, IDATA_SECTION_OFFSET + 8u, 0x1000u);
    put_u32(bytes, IDATA_SECTION_OFFSET + 12u, IDATA_RVA);
    put_u32(bytes, IDATA_SECTION_OFFSET + 16u, 0x0400u);
    put_u32(bytes, IDATA_SECTION_OFFSET + 20u, IDATA_RAW_OFFSET);
    put_u32(bytes, IDATA_SECTION_OFFSET + 36u, 0xc0000040u);

    for (index = 0u; index < 0x0200u; ++index) {
        bytes[TEXT_RAW_OFFSET + index] =
            (uint8_t)(0x40u + (index & 0x3fu));
    }

    put_u32(bytes, idata_raw(IMPORT_DIRECTORY_RVA), LOOKUP_TABLE_RVA);
    put_u32(bytes, idata_raw(IMPORT_DIRECTORY_RVA) + 12u,
            DLL_NAME_RVA);
    put_u32(bytes, idata_raw(IMPORT_DIRECTORY_RVA) + 16u,
            IAT_RVA);

    put_ascii(bytes, idata_raw(DLL_NAME_RVA), "KERNEL32.dll");

    put_u64(bytes, idata_raw(LOOKUP_TABLE_RVA),
            IMPORT_NAME_STRUCT_RVA);
    put_u64(bytes, idata_raw(LOOKUP_TABLE_RVA) + 8u,
            ORDINAL_FLAG | UINT64_C(7));
    put_u64(bytes, idata_raw(LOOKUP_TABLE_RVA) + 16u, 0u);

    put_u64(bytes, idata_raw(IAT_RVA), IMPORT_NAME_STRUCT_RVA);
    put_u64(bytes, idata_raw(IAT_RVA) + 8u,
            ORDINAL_FLAG | UINT64_C(7));
    put_u64(bytes, idata_raw(IAT_RVA) + 16u, 0u);

    put_u16(bytes, idata_raw(IMPORT_NAME_STRUCT_RVA), 0x1234u);
    put_ascii(bytes, idata_raw(IMPORT_NAME_RVA), "CreateFileW");
}

static int make_fixture_with_page_size(test_fixture *fixture,
                                       size_t page_size,
                                       size_t page_capacity)
{
    make_valid_file(fixture->source);
    fill_bytes(fixture->image, sizeof(fixture->image), 0xa5u);
    fill_bytes(fixture->pages, sizeof(fixture->pages), 0x5au);
    fill_bytes(&fixture->mapped, sizeof(fixture->mapped), 0x3cu);

    return db_map_pe64(fixture->source,
                       sizeof(fixture->source),
                       fixture->image,
                       sizeof(fixture->image),
                       page_size,
                       fixture->pages,
                       page_capacity,
                       &fixture->mapped) == DB_MAP_OK;
}

static int make_fixture(test_fixture *fixture)
{
    return make_fixture_with_page_size(fixture,
                                       TEST_PAGE_SIZE,
                                       TEST_PAGE_COUNT);
}

static int expect_failure_unchanged(test_fixture *fixture,
                                    size_t page_size,
                                    size_t page_capacity,
                                    db_import_module *modules,
                                    size_t module_capacity,
                                    db_import_record *records,
                                    size_t record_capacity,
                                    db_import_status expected)
{
    uint8_t image_copy[TEST_IMAGE_SIZE];
    db_map_page_protection page_copy[TEST_PAGE_COUNT];
    db_mapped_image mapped_copy;
    db_import_summary summary;
    db_import_status actual;

    copy_bytes(image_copy, fixture->image, sizeof(image_copy));
    copy_bytes(page_copy, fixture->pages, sizeof(page_copy));
    mapped_copy = fixture->mapped;
    fill_bytes(modules,
               TEST_MODULE_CAPACITY * sizeof(*modules),
               0xa5u);
    fill_bytes(records,
               TEST_RECORD_CAPACITY * sizeof(*records),
               0x5au);
    fill_bytes(&summary, sizeof(summary), 0x3cu);

    actual = db_import_plan_pe64(fixture->image,
                                 sizeof(fixture->image),
                                 &fixture->mapped,
                                 page_size,
                                 fixture->pages,
                                 page_capacity,
                                 modules,
                                 module_capacity,
                                 records,
                                 record_capacity,
                                 &summary);

    return actual == expected &&
           bytes_equal(fixture->image, image_copy, sizeof(image_copy)) &&
           bytes_equal(fixture->pages, page_copy, sizeof(page_copy)) &&
           bytes_equal(&fixture->mapped, &mapped_copy, sizeof(mapped_copy)) &&
           all_bytes_are(modules,
                         TEST_MODULE_CAPACITY * sizeof(*modules),
                         0xa5u) &&
           all_bytes_are(records,
                         TEST_RECORD_CAPACITY * sizeof(*records),
                         0x5au) &&
           all_bytes_are(&summary, sizeof(summary), 0x3cu);
}

static int test_valid_plan(void)
{
    test_fixture fixture;
    uint8_t image_copy[TEST_IMAGE_SIZE];
    db_map_page_protection page_copy[TEST_PAGE_COUNT];
    db_mapped_image mapped_copy;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];
    db_import_summary summary;
    db_import_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    copy_bytes(image_copy, fixture.image, sizeof(image_copy));
    copy_bytes(page_copy, fixture.pages, sizeof(page_copy));
    mapped_copy = fixture.mapped;
    fill_bytes(modules, sizeof(modules), 0xa5u);
    fill_bytes(records, sizeof(records), 0x5au);
    fill_bytes(&summary, sizeof(summary), 0x3cu);

    status = db_import_plan_pe64(fixture.image,
                                 sizeof(fixture.image),
                                 &fixture.mapped,
                                 TEST_PAGE_SIZE,
                                 fixture.pages,
                                 TEST_PAGE_COUNT,
                                 modules,
                                 TEST_MODULE_CAPACITY,
                                 records,
                                 TEST_RECORD_CAPACITY,
                                 &summary);

    return status == DB_IMPORT_OK &&
           bytes_equal(fixture.image, image_copy, sizeof(image_copy)) &&
           bytes_equal(fixture.pages, page_copy, sizeof(page_copy)) &&
           bytes_equal(&fixture.mapped, &mapped_copy, sizeof(mapped_copy)) &&
           summary.directory_rva == IMPORT_DIRECTORY_RVA &&
           summary.directory_size == IMPORT_DIRECTORY_SIZE &&
           summary.module_count == 1u &&
           summary.record_count == 2u &&
           modules[0].name_rva == DLL_NAME_RVA &&
           modules[0].name_length == 12u &&
           modules[0].lookup_table_rva == LOOKUP_TABLE_RVA &&
           modules[0].iat_rva == IAT_RVA &&
           modules[0].first_record == 0u &&
           modules[0].record_count == 2u &&
           records[0].module_index == 0u &&
           records[0].lookup_rva == LOOKUP_TABLE_RVA &&
           records[0].iat_rva == IAT_RVA &&
           records[0].name_rva == IMPORT_NAME_RVA &&
           records[0].name_length == 11u &&
           records[0].hint == 0x1234u &&
           records[0].ordinal == 0u &&
           records[0].kind == DB_IMPORT_BY_NAME &&
           records[0].reserved[0] == 0u &&
           records[0].reserved[1] == 0u &&
           records[0].reserved[2] == 0u &&
           records[0].lookup_value == IMPORT_NAME_STRUCT_RVA &&
           records[0].original_iat_value == IMPORT_NAME_STRUCT_RVA &&
           records[1].module_index == 0u &&
           records[1].lookup_rva == LOOKUP_TABLE_RVA + 8u &&
           records[1].iat_rva == IAT_RVA + 8u &&
           records[1].name_rva == 0u &&
           records[1].name_length == 0u &&
           records[1].hint == 0u &&
           records[1].ordinal == 7u &&
           records[1].kind == DB_IMPORT_BY_ORDINAL &&
           records[1].lookup_value == (ORDINAL_FLAG | UINT64_C(7)) &&
           records[1].original_iat_value ==
               (ORDINAL_FLAG | UINT64_C(7)) &&
           all_bytes_are(&modules[1],
                         3u * sizeof(modules[1]),
                         0xa5u) &&
           all_bytes_are(&records[2],
                         6u * sizeof(records[2]),
                         0x5au);
}

static int test_no_directory(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];
    db_import_summary summary;
    db_import_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, IMPORT_DIRECTORY_ENTRY_OFFSET, 0u);
    put_u32(fixture.image, IMPORT_DIRECTORY_ENTRY_OFFSET + 4u, 0u);
    fill_bytes(modules, sizeof(modules), 0xa5u);
    fill_bytes(records, sizeof(records), 0x5au);
    fill_bytes(&summary, sizeof(summary), 0x3cu);

    status = db_import_plan_pe64(fixture.image,
                                 sizeof(fixture.image),
                                 &fixture.mapped,
                                 TEST_PAGE_SIZE,
                                 fixture.pages,
                                 TEST_PAGE_COUNT,
                                 modules,
                                 TEST_MODULE_CAPACITY,
                                 records,
                                 TEST_RECORD_CAPACITY,
                                 &summary);

    return status == DB_IMPORT_OK &&
           summary.directory_rva == 0u &&
           summary.directory_size == 0u &&
           summary.module_count == 0u &&
           summary.record_count == 0u &&
           all_bytes_are(modules, sizeof(modules), 0xa5u) &&
           all_bytes_are(records, sizeof(records), 0x5au);
}

static int test_directory_count_absent(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];
    db_import_summary summary;
    db_import_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, OPTIONAL_OFFSET + 108u, 1u);
    fill_bytes(modules, sizeof(modules), 0xa5u);
    fill_bytes(records, sizeof(records), 0x5au);
    fill_bytes(&summary, sizeof(summary), 0x3cu);

    status = db_import_plan_pe64(fixture.image,
                                 sizeof(fixture.image),
                                 &fixture.mapped,
                                 TEST_PAGE_SIZE,
                                 fixture.pages,
                                 TEST_PAGE_COUNT,
                                 modules,
                                 TEST_MODULE_CAPACITY,
                                 records,
                                 TEST_RECORD_CAPACITY,
                                 &summary);

    return status == DB_IMPORT_OK &&
           summary.directory_rva == 0u &&
           summary.directory_size == 0u &&
           summary.module_count == 0u &&
           summary.record_count == 0u &&
           all_bytes_are(modules, sizeof(modules), 0xa5u) &&
           all_bytes_are(records, sizeof(records), 0x5au);
}

static int test_one_sided_directory(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, IMPORT_DIRECTORY_ENTRY_OFFSET + 4u, 0u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_BAD_DIRECTORY);
}

static int test_directory_out_of_range(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, IMPORT_DIRECTORY_ENTRY_OFFSET,
            TEST_IMAGE_SIZE - 16u);
    put_u32(fixture.image, IMPORT_DIRECTORY_ENTRY_OFFSET + 4u, 40u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_DIRECTORY_OUT_OF_RANGE);
}

static int test_directory_unmapped_gap(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, IMPORT_DIRECTORY_ENTRY_OFFSET, 0x1800u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_DIRECTORY_UNMAPPED);
}

static int test_directory_page_none(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fixture.pages[2] = DB_MAP_PAGE_NONE;
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_DIRECTORY_UNMAPPED);
}

static int test_bad_page_plan(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fixture.pages[1] = 2u;
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_BAD_PAGE_PLAN);
}

static int test_descriptor_unterminated(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, IMPORT_DIRECTORY_ENTRY_OFFSET + 4u, 20u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_DESCRIPTOR_UNTERMINATED);
}

static int test_nonzero_directory_tail(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, IMPORT_DIRECTORY_ENTRY_OFFSET + 4u, 44u);
    fixture.image[IMPORT_DIRECTORY_RVA + 40u] = 0x5au;
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_BAD_DIRECTORY);
}

static int test_bad_descriptor(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, IMPORT_DIRECTORY_RVA + 12u, 0u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_BAD_DESCRIPTOR);
}

static int test_dll_name_unmapped(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, IMPORT_DIRECTORY_RVA + 12u, 0x1800u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_DLL_NAME_UNMAPPED);
}

static int test_dll_name_empty(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fixture.image[DLL_NAME_RVA] = 0u;
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_DLL_NAME_INVALID);
}

static int test_dll_name_invalid(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fixture.image[DLL_NAME_RVA + 3u] = 0x1fu;
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_DLL_NAME_INVALID);
}

static int test_dll_name_unterminated(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];
    uint32_t cursor;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, IMPORT_DIRECTORY_RVA + 12u, 0x2ff0u);
    for (cursor = 0x2ff0u; cursor < TEST_IMAGE_SIZE; ++cursor) {
        fixture.image[cursor] = (uint8_t)'A';
    }

    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_DLL_NAME_UNTERMINATED);
}

static int test_thunk_unmapped(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, IMPORT_DIRECTORY_RVA, 0x1800u);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_THUNK_UNMAPPED);
}

static int test_thunk_unterminated(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, IMPORT_DIRECTORY_RVA, 0u);
    put_u32(fixture.image, IMPORT_DIRECTORY_RVA + 16u, 0x2ff8u);
    put_u64(fixture.image, 0x2ff8u, IMPORT_NAME_STRUCT_RVA);

    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_THUNK_UNTERMINATED);
}

static int test_ordinal_reserved_bits(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u64(fixture.image,
            LOOKUP_TABLE_RVA + 8u,
            ORDINAL_FLAG | UINT64_C(0x00010007));
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_THUNK_VALUE_INVALID);
}

static int test_name_thunk_above_u32(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u64(fixture.image,
            LOOKUP_TABLE_RVA,
            UINT64_C(0x0000000100002200));
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_THUNK_VALUE_INVALID);
}

static int test_import_name_unmapped(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u64(fixture.image, LOOKUP_TABLE_RVA, UINT64_C(0x1800));
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_NAME_UNMAPPED);
}

static int test_import_name_empty(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fixture.image[IMPORT_NAME_RVA] = 0u;
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_NAME_INVALID);
}

static int test_import_name_invalid(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fixture.image[IMPORT_NAME_RVA + 2u] = 0x7fu;
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_NAME_INVALID);
}

static void add_second_overlapping_module(test_fixture *fixture)
{
    const uint32_t second_descriptor = IMPORT_DIRECTORY_RVA + 20u;
    const uint32_t second_name_rva = 0x20a0u;
    const uint32_t second_lookup_rva = 0x2140u;
    const uint32_t second_iat_rva = IAT_RVA + 8u;

    put_u32(fixture->image,
            IMPORT_DIRECTORY_ENTRY_OFFSET + 4u,
            60u);

    put_u32(fixture->image, second_descriptor, second_lookup_rva);
    put_u32(fixture->image, second_descriptor + 12u, second_name_rva);
    put_u32(fixture->image, second_descriptor + 16u, second_iat_rva);
    fill_bytes(fixture->image + second_descriptor + 20u, 20u, 0u);

    copy_bytes(fixture->image + second_name_rva,
               "USER32.dll\0",
               11u);
    put_u64(fixture->image,
            second_lookup_rva,
            ORDINAL_FLAG | UINT64_C(9));
    put_u64(fixture->image, second_lookup_rva + 8u, 0u);
    put_u64(fixture->image,
            second_iat_rva,
            ORDINAL_FLAG | UINT64_C(9));
    put_u64(fixture->image, second_iat_rva + 8u, 0u);
}

static int test_iat_overlap(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    add_second_overlapping_module(&fixture);
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_IAT_OVERLAP);
}

static int test_module_buffer_too_small(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    0u,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_MODULE_BUFFER_TOO_SMALL);
}

static int test_record_buffer_too_small(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    1u,
                                    DB_IMPORT_RECORD_BUFFER_TOO_SMALL);
}

static int test_module_capacity_overflow(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];
    db_import_summary summary;
    db_import_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fill_bytes(modules, sizeof(modules), 0xa5u);
    fill_bytes(records, sizeof(records), 0x5au);
    fill_bytes(&summary, sizeof(summary), 0x3cu);

    status = db_import_plan_pe64(
        fixture.image,
        sizeof(fixture.image),
        &fixture.mapped,
        TEST_PAGE_SIZE,
        fixture.pages,
        TEST_PAGE_COUNT,
        modules,
        SIZE_MAX / sizeof(modules[0]) + 1u,
        records,
        TEST_RECORD_CAPACITY,
        &summary);

    return status == DB_IMPORT_MODULE_CAPACITY_OVERFLOW &&
           all_bytes_are(modules, sizeof(modules), 0xa5u) &&
           all_bytes_are(records, sizeof(records), 0x5au) &&
           all_bytes_are(&summary, sizeof(summary), 0x3cu);
}

static int test_record_capacity_overflow(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];
    db_import_summary summary;
    db_import_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    fill_bytes(modules, sizeof(modules), 0xa5u);
    fill_bytes(records, sizeof(records), 0x5au);
    fill_bytes(&summary, sizeof(summary), 0x3cu);

    status = db_import_plan_pe64(
        fixture.image,
        sizeof(fixture.image),
        &fixture.mapped,
        TEST_PAGE_SIZE,
        fixture.pages,
        TEST_PAGE_COUNT,
        modules,
        TEST_MODULE_CAPACITY,
        records,
        SIZE_MAX / sizeof(records[0]) + 1u,
        &summary);

    return status == DB_IMPORT_RECORD_CAPACITY_OVERFLOW &&
           all_bytes_are(modules, sizeof(modules), 0xa5u) &&
           all_bytes_are(records, sizeof(records), 0x5au) &&
           all_bytes_are(&summary, sizeof(summary), 0x3cu);
}

static int test_output_overlaps_image(void)
{
    test_fixture fixture;
    uint8_t image_copy[TEST_IMAGE_SIZE];
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];
    db_import_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    copy_bytes(image_copy, fixture.image, sizeof(image_copy));
    fill_bytes(modules, sizeof(modules), 0xa5u);
    fill_bytes(records, sizeof(records), 0x5au);

    status = db_import_plan_pe64(
        fixture.image,
        sizeof(fixture.image),
        &fixture.mapped,
        TEST_PAGE_SIZE,
        fixture.pages,
        TEST_PAGE_COUNT,
        modules,
        TEST_MODULE_CAPACITY,
        records,
        TEST_RECORD_CAPACITY,
        (db_import_summary *)(void *)(fixture.image + 0x100u));

    return status == DB_IMPORT_BUFFER_OVERLAP &&
           bytes_equal(fixture.image, image_copy, sizeof(image_copy)) &&
           all_bytes_are(modules, sizeof(modules), 0xa5u) &&
           all_bytes_are(records, sizeof(records), 0x5au);
}

static int test_original_first_thunk_fallback(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];
    db_import_summary summary;
    db_import_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u32(fixture.image, IMPORT_DIRECTORY_RVA, 0u);
    fill_bytes(modules, sizeof(modules), 0xa5u);
    fill_bytes(records, sizeof(records), 0x5au);
    fill_bytes(&summary, sizeof(summary), 0x3cu);

    status = db_import_plan_pe64(fixture.image,
                                 sizeof(fixture.image),
                                 &fixture.mapped,
                                 TEST_PAGE_SIZE,
                                 fixture.pages,
                                 TEST_PAGE_COUNT,
                                 modules,
                                 TEST_MODULE_CAPACITY,
                                 records,
                                 TEST_RECORD_CAPACITY,
                                 &summary);

    return status == DB_IMPORT_OK &&
           modules[0].lookup_table_rva == IAT_RVA &&
           records[0].lookup_rva == IAT_RVA &&
           records[1].lookup_rva == IAT_RVA + 8u;
}

static int test_bound_iat_values_are_recorded(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];
    db_import_summary summary;
    db_import_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u64(fixture.image, IAT_RVA, UINT64_C(0x1111222233334444));
    put_u64(fixture.image, IAT_RVA + 8u,
            UINT64_C(0x5555666677778888));

    status = db_import_plan_pe64(fixture.image,
                                 sizeof(fixture.image),
                                 &fixture.mapped,
                                 TEST_PAGE_SIZE,
                                 fixture.pages,
                                 TEST_PAGE_COUNT,
                                 modules,
                                 TEST_MODULE_CAPACITY,
                                 records,
                                 TEST_RECORD_CAPACITY,
                                 &summary);

    return status == DB_IMPORT_OK &&
           records[0].lookup_value == IMPORT_NAME_STRUCT_RVA &&
           records[0].original_iat_value ==
               UINT64_C(0x1111222233334444) &&
           records[1].lookup_value ==
               (ORDINAL_FLAG | UINT64_C(7)) &&
           records[1].original_iat_value ==
               UINT64_C(0x5555666677778888);
}

static int test_nonzero_iat_terminator(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u64(fixture.image, IAT_RVA + 16u, UINT64_C(1));
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_THUNK_VALUE_INVALID);
}

static int test_late_failure_is_transactional(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];

    if (!make_fixture(&fixture)) {
        return 0;
    }

    put_u64(fixture.image,
            LOOKUP_TABLE_RVA + 8u,
            ORDINAL_FLAG | UINT64_C(0x10007));
    return expect_failure_unchanged(&fixture,
                                    TEST_PAGE_SIZE,
                                    TEST_PAGE_COUNT,
                                    modules,
                                    TEST_MODULE_CAPACITY,
                                    records,
                                    TEST_RECORD_CAPACITY,
                                    DB_IMPORT_THUNK_VALUE_INVALID);
}

static int test_non_4k_host_page(void)
{
    test_fixture fixture;
    db_import_module modules[TEST_MODULE_CAPACITY];
    db_import_record records[TEST_RECORD_CAPACITY];
    db_import_summary summary;
    db_import_status status;

    if (!make_fixture_with_page_size(&fixture, 0x2000u, 2u)) {
        return 0;
    }

    status = db_import_plan_pe64(fixture.image,
                                 sizeof(fixture.image),
                                 &fixture.mapped,
                                 0x2000u,
                                 fixture.pages,
                                 2u,
                                 modules,
                                 TEST_MODULE_CAPACITY,
                                 records,
                                 TEST_RECORD_CAPACITY,
                                 &summary);

    return status == DB_IMPORT_OK &&
           summary.module_count == 1u &&
           summary.record_count == 2u;
}

static int test_null_module_nonzero_capacity(void)
{
    test_fixture fixture;
    db_import_record records[TEST_RECORD_CAPACITY];
    db_import_summary summary;
    db_import_status status;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    status = db_import_plan_pe64(fixture.image,
                                 sizeof(fixture.image),
                                 &fixture.mapped,
                                 TEST_PAGE_SIZE,
                                 fixture.pages,
                                 TEST_PAGE_COUNT,
                                 NULL,
                                 1u,
                                 records,
                                 TEST_RECORD_CAPACITY,
                                 &summary);

    return status == DB_IMPORT_NULL_ARGUMENT;
}

static int test_fixture_source_still_contains_expected_values(void)
{
    test_fixture fixture;

    if (!make_fixture(&fixture)) {
        return 0;
    }

    return read_u64(fixture.image, LOOKUP_TABLE_RVA) ==
               IMPORT_NAME_STRUCT_RVA &&
           read_u64(fixture.image, IAT_RVA + 8u) ==
               (ORDINAL_FLAG | UINT64_C(7));
}

int main(void)
{
    int failures = 0;

    failures += !test_valid_plan();
    failures += !test_no_directory();
    failures += !test_directory_count_absent();
    failures += !test_one_sided_directory();
    failures += !test_directory_out_of_range();
    failures += !test_directory_unmapped_gap();
    failures += !test_directory_page_none();
    failures += !test_bad_page_plan();
    failures += !test_descriptor_unterminated();
    failures += !test_nonzero_directory_tail();
    failures += !test_bad_descriptor();
    failures += !test_dll_name_unmapped();
    failures += !test_dll_name_empty();
    failures += !test_dll_name_invalid();
    failures += !test_dll_name_unterminated();
    failures += !test_thunk_unmapped();
    failures += !test_thunk_unterminated();
    failures += !test_ordinal_reserved_bits();
    failures += !test_name_thunk_above_u32();
    failures += !test_import_name_unmapped();
    failures += !test_import_name_empty();
    failures += !test_import_name_invalid();
    failures += !test_iat_overlap();
    failures += !test_module_buffer_too_small();
    failures += !test_record_buffer_too_small();
    failures += !test_module_capacity_overflow();
    failures += !test_record_capacity_overflow();
    failures += !test_output_overlaps_image();
    failures += !test_original_first_thunk_fallback();
    failures += !test_bound_iat_values_are_recorded();
    failures += !test_nonzero_iat_terminator();
    failures += !test_late_failure_is_transactional();
    failures += !test_non_4k_host_page();
    failures += !test_null_module_nonzero_capacity();
    failures += !test_fixture_source_still_contains_expected_values();

    if (failures != 0) {
        (void)write(STDERR_FILENO, "IMPORT TEST: FAIL\n", 18u);
        return 1;
    }

    (void)write(STDOUT_FILENO, "IMPORT TEST: PASS\n", 18u);
    return 0;
}
