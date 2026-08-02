#include "dblyat/pe.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define TEST_IMAGE_SIZE 0x400u

static int write_all(int descriptor, const char *text, size_t size)
{
    size_t offset = 0u;

    while (offset < size) {
        const ssize_t written = write(descriptor, text + offset, size - offset);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            return 0;
        }

        if (written == 0) {
            return 0;
        }

        offset += (size_t)written;
    }

    return 1;
}

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

static void zero_bytes(uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0u; index < size; ++index) {
        bytes[index] = 0u;
    }
}

static void make_valid_image(uint8_t *bytes)
{
    const size_t pe = 0x80u;
    const size_t coff = pe + 4u;
    const size_t optional = coff + 20u;
    const size_t section = optional + 0xf0u;

    zero_bytes(bytes, TEST_IMAGE_SIZE);
    bytes[0] = (uint8_t)'M';
    bytes[1] = (uint8_t)'Z';
    put_u32(bytes, 0x3cu, (uint32_t)pe);

    bytes[pe] = (uint8_t)'P';
    bytes[pe + 1u] = (uint8_t)'E';
    put_u16(bytes, coff, 0x8664u);
    put_u16(bytes, coff + 2u, 1u);
    put_u32(bytes, coff + 4u, 0x12345678u);
    put_u16(bytes, coff + 16u, 0xf0u);
    put_u16(bytes, coff + 18u, 0x0022u);

    put_u16(bytes, optional, 0x020bu);
    put_u32(bytes, optional + 16u, 0x1000u);
    put_u64(bytes, optional + 24u, UINT64_C(0x0000000140000000));
    put_u32(bytes, optional + 32u, 0x1000u);
    put_u32(bytes, optional + 36u, 0x0200u);
    put_u32(bytes, optional + 56u, 0x2000u);
    put_u32(bytes, optional + 60u, 0x0200u);
    put_u16(bytes, optional + 68u, 3u);
    put_u16(bytes, optional + 70u, 0x8160u);
    put_u32(bytes, optional + 108u, 16u);

    bytes[section] = (uint8_t)'.';
    bytes[section + 1u] = (uint8_t)'t';
    bytes[section + 2u] = (uint8_t)'e';
    bytes[section + 3u] = (uint8_t)'x';
    bytes[section + 4u] = (uint8_t)'t';
    put_u32(bytes, section + 8u, 1u);
    put_u32(bytes, section + 12u, 0x1000u);
    put_u32(bytes, section + 16u, 0x0200u);
    put_u32(bytes, section + 20u, 0x0200u);
    put_u32(bytes, section + 36u, 0x60000020u);
    bytes[0x200u] = 0xc3u;
}

static int expect_status(const char *name,
                         const uint8_t *bytes,
                         size_t size,
                         db_pe_status expected)
{
    db_pe_image image;
    const db_pe_status actual = db_pe_verify(bytes, size, &image);

    if (actual == expected) {
        return 0;
    }

    if (!write_all(STDERR_FILENO, name, 1u)) {
        return 1;
    }

    return 1;
}

int main(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    int failures = 0;

    make_valid_image(image);
    failures += expect_status("valid", image, sizeof(image), DB_PE_OK);

    make_valid_image(image);
    image[0] = 0u;
    failures += expect_status("mz", image, sizeof(image), DB_PE_BAD_DOS_SIGNATURE);

    make_valid_image(image);
    put_u16(image, 0x84u, 0x014cu);
    failures += expect_status("machine", image, sizeof(image),
                              DB_PE_UNSUPPORTED_MACHINE);

    make_valid_image(image);
    put_u32(image, 0x98u + 36u, 0x0300u);
    failures += expect_status("alignment", image, sizeof(image),
                              DB_PE_BAD_ALIGNMENT);

    make_valid_image(image);
    put_u32(image, 0x188u + 20u, 0x0400u);
    failures += expect_status("raw", image, sizeof(image),
                              DB_PE_BAD_SECTION_RAW_RANGE);

    make_valid_image(image);
    put_u32(image, 0x98u + 16u, 0x1800u);
    failures += expect_status("entry", image, sizeof(image),
                              DB_PE_ENTRY_OUT_OF_RANGE);

    make_valid_image(image);
    put_u32(image, 0x3cu, UINT32_MAX);
    failures += expect_status("offset", image, sizeof(image), DB_PE_BAD_PE_OFFSET);

    if (failures != 0) {
        if (!write_all(STDERR_FILENO,
                       "PE TEST: FAIL\n",
                       sizeof("PE TEST: FAIL\n") - 1u)) {
            return 2;
        }

        return 1;
    }

    if (!write_all(STDOUT_FILENO,
                   "PE TEST: PASS\n",
                   sizeof("PE TEST: PASS\n") - 1u)) {
        return 2;
    }

    return 0;
}
