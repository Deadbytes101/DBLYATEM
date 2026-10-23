#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PROBE_SIZE 0x0600u
#define PE_OFFSET 0x0080u
#define COFF_OFFSET (PE_OFFSET + 4u)
#define OPTIONAL_OFFSET (COFF_OFFSET + 20u)
#define SECTION_TABLE_OFFSET (OPTIONAL_OFFSET + 0x00f0u)
#define TEXT_SECTION_OFFSET SECTION_TABLE_OFFSET
#define IDATA_SECTION_OFFSET (SECTION_TABLE_OFFSET + 40u)
#define TEXT_RAW_OFFSET 0x0200u
#define IDATA_RAW_OFFSET 0x0400u
#define IDATA_RVA 0x2000u

static uint16_t read_u16(const uint8_t *bytes, size_t offset)
{
    return (uint16_t)((uint16_t)bytes[offset] |
                      (uint16_t)((uint16_t)bytes[offset + 1u] << 8u));
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset)
{
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t read_u64(const uint8_t *bytes, size_t offset)
{
    return (uint64_t)read_u32(bytes, offset) |
           ((uint64_t)read_u32(bytes, offset + 4u) << 32u);
}

static size_t idata_raw(uint32_t rva)
{
    return IDATA_RAW_OFFSET + (size_t)(rva - IDATA_RVA);
}

static int bytes_are_zero(const uint8_t *bytes,
                          size_t offset,
                          size_t size)
{
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (bytes[offset + index] != 0u) {
            return 0;
        }
    }

    return 1;
}

static int exact_ascii_z(const uint8_t *bytes,
                         size_t offset,
                         const char *expected)
{
    const size_t length = strlen(expected);

    return memcmp(bytes + offset, expected, length) == 0 &&
           bytes[offset + length] == 0u;
}

static int exact_section_name(const uint8_t *bytes,
                              size_t offset,
                              const char *expected)
{
    uint8_t canonical[8];
    size_t index;
    const size_t length = strlen(expected);

    if (length > sizeof(canonical)) {
        return 0;
    }

    for (index = 0u; index < sizeof(canonical); ++index) {
        canonical[index] = 0u;
    }
    for (index = 0u; index < length; ++index) {
        canonical[index] = (uint8_t)expected[index];
    }

    return memcmp(bytes + offset, canonical, sizeof(canonical)) == 0;
}

static int check_headers(const uint8_t *bytes)
{
    if (bytes[0] != (uint8_t)'M' ||
        bytes[1] != (uint8_t)'Z' ||
        read_u32(bytes, 0x003cu) != PE_OFFSET ||
        memcmp(bytes + PE_OFFSET, "PE\0\0", 4u) != 0) {
        return 0;
    }

    if (read_u16(bytes, COFF_OFFSET) != UINT16_C(0x8664) ||
        read_u16(bytes, COFF_OFFSET + 2u) != UINT16_C(2) ||
        read_u32(bytes, COFF_OFFSET + 4u) != UINT32_C(0) ||
        read_u16(bytes, COFF_OFFSET + 16u) != UINT16_C(0x00f0) ||
        read_u16(bytes, COFF_OFFSET + 18u) != UINT16_C(0x0022)) {
        return 0;
    }

    if (read_u16(bytes, OPTIONAL_OFFSET) != UINT16_C(0x020b) ||
        read_u32(bytes, OPTIONAL_OFFSET + 4u) != UINT32_C(0x0200) ||
        read_u32(bytes, OPTIONAL_OFFSET + 8u) != UINT32_C(0x0200) ||
        read_u32(bytes, OPTIONAL_OFFSET + 16u) != UINT32_C(0x1000) ||
        read_u32(bytes, OPTIONAL_OFFSET + 20u) != UINT32_C(0x1000) ||
        read_u64(bytes, OPTIONAL_OFFSET + 24u) !=
            UINT64_C(0x0000000140000000) ||
        read_u32(bytes, OPTIONAL_OFFSET + 32u) != UINT32_C(0x1000) ||
        read_u32(bytes, OPTIONAL_OFFSET + 36u) != UINT32_C(0x0200) ||
        read_u32(bytes, OPTIONAL_OFFSET + 56u) != UINT32_C(0x3000) ||
        read_u32(bytes, OPTIONAL_OFFSET + 60u) != UINT32_C(0x0200) ||
        read_u16(bytes, OPTIONAL_OFFSET + 68u) != UINT16_C(3) ||
        read_u16(bytes, OPTIONAL_OFFSET + 70u) != UINT16_C(0x0100) ||
        read_u32(bytes, OPTIONAL_OFFSET + 108u) != UINT32_C(16) ||
        read_u32(bytes, OPTIONAL_OFFSET + 120u) != UINT32_C(0x2000) ||
        read_u32(bytes, OPTIONAL_OFFSET + 124u) != UINT32_C(40) ||
        read_u32(bytes, OPTIONAL_OFFSET + 208u) != UINT32_C(0x2080) ||
        read_u32(bytes, OPTIONAL_OFFSET + 212u) != UINT32_C(32)) {
        return 0;
    }

    return 1;
}

static int check_sections(const uint8_t *bytes)
{
    if (!exact_section_name(bytes, TEXT_SECTION_OFFSET, ".text") ||
        read_u32(bytes, TEXT_SECTION_OFFSET + 8u) != UINT32_C(0x002a) ||
        read_u32(bytes, TEXT_SECTION_OFFSET + 12u) != UINT32_C(0x1000) ||
        read_u32(bytes, TEXT_SECTION_OFFSET + 16u) != UINT32_C(0x0200) ||
        read_u32(bytes, TEXT_SECTION_OFFSET + 20u) != UINT32_C(0x0200) ||
        read_u32(bytes, TEXT_SECTION_OFFSET + 36u) !=
            UINT32_C(0x60000020)) {
        return 0;
    }

    if (!exact_section_name(bytes, IDATA_SECTION_OFFSET, ".idata") ||
        read_u32(bytes, IDATA_SECTION_OFFSET + 8u) != UINT32_C(0x0160) ||
        read_u32(bytes, IDATA_SECTION_OFFSET + 12u) != UINT32_C(0x2000) ||
        read_u32(bytes, IDATA_SECTION_OFFSET + 16u) != UINT32_C(0x0200) ||
        read_u32(bytes, IDATA_SECTION_OFFSET + 20u) != UINT32_C(0x0400) ||
        read_u32(bytes, IDATA_SECTION_OFFSET + 36u) !=
            UINT32_C(0xc0000040)) {
        return 0;
    }

    return 1;
}

static int check_code(const uint8_t *bytes)
{
    static const uint8_t expected[] = {
        UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xec), UINT8_C(0x28),
        UINT8_C(0xb9), UINT8_C(0x2a), UINT8_C(0x00), UINT8_C(0x00),
        UINT8_C(0x00), UINT8_C(0xff), UINT8_C(0x15), UINT8_C(0x71),
        UINT8_C(0x10), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0xff),
        UINT8_C(0x15), UINT8_C(0x73), UINT8_C(0x10), UINT8_C(0x00),
        UINT8_C(0x00), UINT8_C(0x83), UINT8_C(0xf8), UINT8_C(0x2a),
        UINT8_C(0x75), UINT8_C(0x04), UINT8_C(0x31), UINT8_C(0xc9),
        UINT8_C(0xeb), UINT8_C(0x05), UINT8_C(0xb9), UINT8_C(0x01),
        UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0xff),
        UINT8_C(0x15), UINT8_C(0x67), UINT8_C(0x10), UINT8_C(0x00),
        UINT8_C(0x00), UINT8_C(0xcc)
    };

    return memcmp(bytes + TEXT_RAW_OFFSET, expected, sizeof(expected)) == 0 &&
           bytes_are_zero(bytes,
                          TEXT_RAW_OFFSET + sizeof(expected),
                          UINT32_C(0x0200) - sizeof(expected));
}

static int check_imports(const uint8_t *bytes)
{
    const size_t descriptor = idata_raw(UINT32_C(0x2000));
    const size_t lookup = idata_raw(UINT32_C(0x2040));
    const size_t iat = idata_raw(UINT32_C(0x2080));

    if (read_u32(bytes, descriptor) != UINT32_C(0x2040) ||
        read_u32(bytes, descriptor + 4u) != UINT32_C(0) ||
        read_u32(bytes, descriptor + 8u) != UINT32_C(0) ||
        read_u32(bytes, descriptor + 12u) != UINT32_C(0x20c0) ||
        read_u32(bytes, descriptor + 16u) != UINT32_C(0x2080) ||
        !bytes_are_zero(bytes, descriptor + 20u, 20u)) {
        return 0;
    }

    if (read_u64(bytes, lookup) != UINT64_C(0x2100) ||
        read_u64(bytes, lookup + 8u) != UINT64_C(0x2120) ||
        read_u64(bytes, lookup + 16u) != UINT64_C(0x2140) ||
        read_u64(bytes, lookup + 24u) != UINT64_C(0) ||
        read_u64(bytes, iat) != UINT64_C(0x2100) ||
        read_u64(bytes, iat + 8u) != UINT64_C(0x2120) ||
        read_u64(bytes, iat + 16u) != UINT64_C(0x2140) ||
        read_u64(bytes, iat + 24u) != UINT64_C(0)) {
        return 0;
    }

    if (!exact_ascii_z(bytes, idata_raw(UINT32_C(0x20c0)),
                       "KERNEL32.dll") ||
        read_u16(bytes, idata_raw(UINT32_C(0x2100))) != UINT16_C(0) ||
        !exact_ascii_z(bytes, idata_raw(UINT32_C(0x2100)) + 2u,
                       "SetLastError") ||
        read_u16(bytes, idata_raw(UINT32_C(0x2120))) != UINT16_C(0) ||
        !exact_ascii_z(bytes, idata_raw(UINT32_C(0x2120)) + 2u,
                       "GetLastError") ||
        read_u16(bytes, idata_raw(UINT32_C(0x2140))) != UINT16_C(0) ||
        !exact_ascii_z(bytes, idata_raw(UINT32_C(0x2140)) + 2u,
                       "ExitProcess")) {
        return 0;
    }

    return 1;
}

static int load_probe(const char *path, uint8_t *bytes)
{
    FILE *input = fopen(path, "rb");
    int trailing;

    if (input == NULL) {
        return 0;
    }

    if (fread(bytes, 1u, PROBE_SIZE, input) != PROBE_SIZE) {
        (void)fclose(input);
        return 0;
    }

    trailing = fgetc(input);
    if (trailing != EOF) {
        (void)fclose(input);
        return 0;
    }

    if (ferror(input) != 0) {
        (void)fclose(input);
        return 0;
    }

    return fclose(input) == 0;
}

int main(int argc, char **argv)
{
    uint8_t bytes[PROBE_SIZE];

    if (argc != 2) {
        (void)fprintf(stderr, "usage: verify-probe PROBE.exe\n");
        return 2;
    }

    if (!load_probe(argv[1], bytes)) {
        (void)fprintf(stderr, "verify-probe: exact read failed\n");
        return 1;
    }

    if (!check_headers(bytes)) {
        (void)fprintf(stderr, "verify-probe: header contract failed\n");
        return 1;
    }

    if (!check_sections(bytes)) {
        (void)fprintf(stderr, "verify-probe: section contract failed\n");
        return 1;
    }

    if (!check_code(bytes)) {
        (void)fprintf(stderr, "verify-probe: code contract failed\n");
        return 1;
    }

    if (!check_imports(bytes)) {
        (void)fprintf(stderr, "verify-probe: import contract failed\n");
        return 1;
    }

    (void)printf("IDENTICAL X64 WORKLOAD STRUCTURE: PASS\n");
    (void)printf("PE_MACHINE=x86_64\n");
    (void)printf("PE_FORMAT=PE32_PLUS\n");
    (void)printf("SUBSYSTEM=CONSOLE\n");
    (void)printf("ENTRY_POINT_RVA=00001000\n");
    (void)printf("SECTION_COUNT=2\n");
    (void)printf("IMPORT_DLL_COUNT=1\n");
    (void)printf("IMPORT_SYMBOL_COUNT=3\n");
    (void)printf("WORKLOAD=SET_LAST_ERROR_42_VERIFY_AND_EXIT\n");
    return 0;
}
