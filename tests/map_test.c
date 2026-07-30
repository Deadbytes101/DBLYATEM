#include "dblyat/map.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define TEST_FILE_SIZE 0x600u
#define TEST_IMAGE_SIZE 0x3000u
#define TEST_PAGE_SIZE 0x1000u
#define TEST_PAGE_COUNT 3u

#define PE_OFFSET 0x80u
#define COFF_OFFSET (PE_OFFSET + 4u)
#define OPTIONAL_OFFSET (COFF_OFFSET + 20u)
#define SECTION_TABLE_OFFSET (OPTIONAL_OFFSET + 0xf0u)
#define TEXT_SECTION_OFFSET SECTION_TABLE_OFFSET
#define DATA_SECTION_OFFSET (SECTION_TABLE_OFFSET + 40u)

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

static void fill_bytes(uint8_t *bytes, size_t size, uint8_t value)
{
    size_t index;

    for (index = 0u; index < size; ++index) {
        bytes[index] = value;
    }
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

static int bytes_equal(const uint8_t *left,
                       const uint8_t *right,
                       size_t size)
{
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }

    return 1;
}

static void make_valid_image(uint8_t *bytes)
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

    bytes[TEXT_SECTION_OFFSET] = (uint8_t)'.';
    bytes[TEXT_SECTION_OFFSET + 1u] = (uint8_t)'t';
    bytes[TEXT_SECTION_OFFSET + 2u] = (uint8_t)'e';
    bytes[TEXT_SECTION_OFFSET + 3u] = (uint8_t)'x';
    bytes[TEXT_SECTION_OFFSET + 4u] = (uint8_t)'t';
    put_u32(bytes, TEXT_SECTION_OFFSET + 8u, 0x0180u);
    put_u32(bytes, TEXT_SECTION_OFFSET + 12u, 0x1000u);
    put_u32(bytes, TEXT_SECTION_OFFSET + 16u, 0x0200u);
    put_u32(bytes, TEXT_SECTION_OFFSET + 20u, 0x0200u);
    put_u32(bytes, TEXT_SECTION_OFFSET + 36u, 0x60000020u);

    bytes[DATA_SECTION_OFFSET] = (uint8_t)'.';
    bytes[DATA_SECTION_OFFSET + 1u] = (uint8_t)'d';
    bytes[DATA_SECTION_OFFSET + 2u] = (uint8_t)'a';
    bytes[DATA_SECTION_OFFSET + 3u] = (uint8_t)'t';
    bytes[DATA_SECTION_OFFSET + 4u] = (uint8_t)'a';
    put_u32(bytes, DATA_SECTION_OFFSET + 8u, 0x0300u);
    put_u32(bytes, DATA_SECTION_OFFSET + 12u, 0x2000u);
    put_u32(bytes, DATA_SECTION_OFFSET + 16u, 0x0200u);
    put_u32(bytes, DATA_SECTION_OFFSET + 20u, 0x0400u);
    put_u32(bytes, DATA_SECTION_OFFSET + 36u, 0xc0000040u);

    for (index = 0u; index < 0x0200u; ++index) {
        bytes[0x0200u + index] = (uint8_t)(0x40u + (index & 0x3fu));
        bytes[0x0400u + index] = (uint8_t)(0x80u + (index & 0x3fu));
    }
}

static int expect_failure_unchanged(const uint8_t *source,
                                    size_t source_size,
                                    size_t destination_size,
                                    size_t page_size,
                                    size_t page_capacity,
                                    db_map_status expected)
{
    uint8_t destination[TEST_IMAGE_SIZE];
    db_map_page_protection pages[TEST_PAGE_COUNT];
    db_mapped_image mapped;
    db_map_status actual;

    fill_bytes(destination, sizeof(destination), 0xa5u);
    fill_bytes(pages, sizeof(pages), 0x5au);
    fill_bytes((uint8_t *)&mapped, sizeof(mapped), 0x3cu);

    actual = db_map_pe64(source, source_size,
                         destination, destination_size,
                         page_size, pages, page_capacity, &mapped);

    return actual == expected &&
           all_bytes_are(destination, sizeof(destination), 0xa5u) &&
           all_bytes_are(pages, sizeof(pages), 0x5au) &&
           all_bytes_are(&mapped, sizeof(mapped), 0x3cu);
}

static int test_valid_map(void)
{
    uint8_t source[TEST_FILE_SIZE];
    uint8_t destination[TEST_IMAGE_SIZE];
    db_map_page_protection pages[TEST_PAGE_COUNT];
    db_mapped_image mapped;
    db_map_status status;

    make_valid_image(source);
    fill_bytes(destination, sizeof(destination), 0xa5u);
    fill_bytes(pages, sizeof(pages), 0x5au);
    fill_bytes((uint8_t *)&mapped, sizeof(mapped), 0x3cu);

    status = db_map_pe64(source, sizeof(source),
                         destination, sizeof(destination),
                         TEST_PAGE_SIZE, pages, TEST_PAGE_COUNT, &mapped);

    if (status != DB_MAP_OK ||
        mapped.preferred_base != UINT64_C(0x0000000140000000) ||
        mapped.image_size != TEST_IMAGE_SIZE ||
        mapped.headers_size != 0x0200u ||
        mapped.entry_rva != 0x1000u ||
        mapped.section_count != 2u ||
        mapped.page_count != TEST_PAGE_COUNT ||
        pages[0] != DB_MAP_PAGE_R ||
        pages[1] != DB_MAP_PAGE_RX ||
        pages[2] != DB_MAP_PAGE_RW) {
        return 0;
    }

    if (!bytes_equal(destination, source, 0x0200u) ||
        !bytes_equal(destination + 0x1000u, source + 0x0200u, 0x0180u) ||
        !bytes_equal(destination + 0x2000u, source + 0x0400u, 0x0200u)) {
        return 0;
    }

    return all_bytes_are(destination + 0x1180u, 0x0e80u, 0u) &&
           all_bytes_are(destination + 0x2200u, 0x0100u, 0u) &&
           all_bytes_are(destination + 0x2300u, 0x0d00u, 0u);
}

static int test_destination_too_small(void)
{
    uint8_t source[TEST_FILE_SIZE];

    make_valid_image(source);
    return expect_failure_unchanged(source, sizeof(source),
                                    TEST_IMAGE_SIZE - 1u,
                                    TEST_PAGE_SIZE, TEST_PAGE_COUNT,
                                    DB_MAP_DESTINATION_TOO_SMALL);
}

static int test_page_plan_too_small(void)
{
    uint8_t source[TEST_FILE_SIZE];

    make_valid_image(source);
    return expect_failure_unchanged(source, sizeof(source),
                                    TEST_IMAGE_SIZE,
                                    TEST_PAGE_SIZE, TEST_PAGE_COUNT - 1u,
                                    DB_MAP_PAGE_PLAN_TOO_SMALL);
}

static int test_bad_page_size(void)
{
    uint8_t source[TEST_FILE_SIZE];

    make_valid_image(source);
    return expect_failure_unchanged(source, sizeof(source),
                                    TEST_IMAGE_SIZE,
                                    0x1800u, TEST_PAGE_COUNT,
                                    DB_MAP_INVALID_PAGE_SIZE);
}

static int test_write_execute_section(void)
{
    uint8_t source[TEST_FILE_SIZE];

    make_valid_image(source);
    put_u32(source, TEXT_SECTION_OFFSET + 36u, 0xe0000020u);
    return expect_failure_unchanged(source, sizeof(source),
                                    TEST_IMAGE_SIZE,
                                    TEST_PAGE_SIZE, TEST_PAGE_COUNT,
                                    DB_MAP_WRITE_EXECUTE_SECTION);
}

static int test_write_execute_page(void)
{
    uint8_t source[TEST_FILE_SIZE];
    uint8_t destination[TEST_IMAGE_SIZE];
    db_map_page_protection page;
    db_mapped_image mapped;
    db_map_status status;

    make_valid_image(source);
    fill_bytes(destination, sizeof(destination), 0xa5u);
    page = 0x5au;
    fill_bytes((uint8_t *)&mapped, sizeof(mapped), 0x3cu);

    status = db_map_pe64(source, sizeof(source),
                         destination, sizeof(destination),
                         0x4000u, &page, 1u, &mapped);

    return status == DB_MAP_WRITE_EXECUTE_PAGE &&
           all_bytes_are(destination, sizeof(destination), 0xa5u) &&
           page == 0x5au &&
           all_bytes_are(&mapped, sizeof(mapped), 0x3cu);
}

static int test_section_overlap(void)
{
    uint8_t source[TEST_FILE_SIZE];

    make_valid_image(source);
    put_u32(source, DATA_SECTION_OFFSET + 12u, 0x1000u);
    return expect_failure_unchanged(source, sizeof(source),
                                    TEST_IMAGE_SIZE,
                                    TEST_PAGE_SIZE, TEST_PAGE_COUNT,
                                    DB_MAP_SECTION_OVERLAP);
}

static int test_entry_in_raw_padding(void)
{
    uint8_t source[TEST_FILE_SIZE];

    make_valid_image(source);
    put_u32(source, OPTIONAL_OFFSET + 16u, 0x1180u);
    return expect_failure_unchanged(source, sizeof(source),
                                    TEST_IMAGE_SIZE,
                                    TEST_PAGE_SIZE, TEST_PAGE_COUNT,
                                    DB_MAP_ENTRY_UNMAPPED);
}

static int test_bad_pe_unchanged(void)
{
    uint8_t source[TEST_FILE_SIZE];

    make_valid_image(source);
    source[0] = 0u;
    return expect_failure_unchanged(source, sizeof(source),
                                    TEST_IMAGE_SIZE,
                                    TEST_PAGE_SIZE, TEST_PAGE_COUNT,
                                    DB_MAP_BAD_PE_IMAGE);
}

static int test_headers_exceed_image(void)
{
    uint8_t source[0x4000u];
    uint8_t destination[TEST_IMAGE_SIZE];
    db_map_page_protection pages[TEST_PAGE_COUNT];
    db_mapped_image mapped;
    db_map_status status;

    fill_bytes(source, sizeof(source), 0u);
    make_valid_image(source);
    put_u32(source, OPTIONAL_OFFSET + 16u, 0u);
    put_u32(source, OPTIONAL_OFFSET + 60u, 0x4000u);
    put_u32(source, TEXT_SECTION_OFFSET + 8u, 0u);
    put_u32(source, TEXT_SECTION_OFFSET + 16u, 0u);
    put_u32(source, DATA_SECTION_OFFSET + 8u, 0u);
    put_u32(source, DATA_SECTION_OFFSET + 16u, 0u);

    fill_bytes(destination, sizeof(destination), 0xa5u);
    fill_bytes(pages, sizeof(pages), 0x5au);
    fill_bytes((uint8_t *)&mapped, sizeof(mapped), 0x3cu);

    status = db_map_pe64(source, sizeof(source),
                         destination, sizeof(destination),
                         TEST_PAGE_SIZE, pages, TEST_PAGE_COUNT, &mapped);

    return status == DB_MAP_BAD_IMAGE_LAYOUT &&
           all_bytes_are(destination, sizeof(destination), 0xa5u) &&
           all_bytes_are(pages, sizeof(pages), 0x5au) &&
           all_bytes_are(&mapped, sizeof(mapped), 0x3cu);
}

static int test_source_destination_overlap(void)
{
    uint8_t storage[TEST_IMAGE_SIZE];
    db_map_page_protection pages[TEST_PAGE_COUNT];
    db_mapped_image mapped;
    db_map_status status;

    make_valid_image(storage);
    fill_bytes(pages, sizeof(pages), 0x5au);
    fill_bytes((uint8_t *)&mapped, sizeof(mapped), 0x3cu);

    status = db_map_pe64(storage, TEST_FILE_SIZE,
                         storage, sizeof(storage),
                         TEST_PAGE_SIZE, pages, TEST_PAGE_COUNT, &mapped);

    return status == DB_MAP_BUFFER_OVERLAP &&
           pages[0] == 0x5au && pages[1] == 0x5au && pages[2] == 0x5au &&
           all_bytes_are(&mapped, sizeof(mapped), 0x3cu);
}

int main(void)
{
    int failures = 0;

    failures += !test_valid_map();
    failures += !test_destination_too_small();
    failures += !test_page_plan_too_small();
    failures += !test_bad_page_size();
    failures += !test_write_execute_section();
    failures += !test_write_execute_page();
    failures += !test_section_overlap();
    failures += !test_entry_in_raw_padding();
    failures += !test_bad_pe_unchanged();
    failures += !test_headers_exceed_image();
    failures += !test_source_destination_overlap();

    if (failures != 0) {
        (void)write(STDERR_FILENO, "MAP TEST: FAIL\n", 15u);
        return 1;
    }

    (void)write(STDOUT_FILENO, "MAP TEST: PASS\n", 15u);
    return 0;
}
