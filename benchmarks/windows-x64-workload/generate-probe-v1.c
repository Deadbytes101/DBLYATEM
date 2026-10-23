#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define PROBE_SIZE 0x0600u
#define PE_OFFSET 0x0080u
#define COFF_OFFSET (PE_OFFSET + 4u)
#define OPTIONAL_OFFSET (COFF_OFFSET + 20u)
#define SECTION_TABLE_OFFSET (OPTIONAL_OFFSET + 0x00f0u)
#define TEXT_SECTION_OFFSET SECTION_TABLE_OFFSET
#define IDATA_SECTION_OFFSET (SECTION_TABLE_OFFSET + 40u)
#define TEXT_RAW_OFFSET 0x0200u
#define IDATA_RAW_OFFSET 0x0400u
#define TEXT_RVA 0x1000u
#define IDATA_RVA 0x2000u

static void put_u16(uint8_t *bytes, size_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value & UINT16_C(0x00ff));
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)(value & UINT32_C(0x000000ff));
    bytes[offset + 1u] =
        (uint8_t)((value >> 8u) & UINT32_C(0x000000ff));
    bytes[offset + 2u] =
        (uint8_t)((value >> 16u) & UINT32_C(0x000000ff));
    bytes[offset + 3u] = (uint8_t)(value >> 24u);
}

static void put_u64(uint8_t *bytes, size_t offset, uint64_t value)
{
    put_u32(bytes, offset, (uint32_t)value);
    put_u32(bytes, offset + 4u, (uint32_t)(value >> 32u));
}

static void put_ascii_z(uint8_t *bytes, size_t offset, const char *text)
{
    size_t index = 0u;

    while (text[index] != '\0') {
        bytes[offset + index] = (uint8_t)text[index];
        ++index;
    }
    bytes[offset + index] = 0u;
}

static void put_section_name(uint8_t *bytes,
                             size_t offset,
                             const char *name)
{
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        bytes[offset + index] = 0u;
    }

    index = 0u;
    while (name[index] != '\0' && index < 8u) {
        bytes[offset + index] = (uint8_t)name[index];
        ++index;
    }
}

static size_t idata_raw(uint32_t rva)
{
    return IDATA_RAW_OFFSET + (size_t)(rva - IDATA_RVA);
}

static void put_call_iat(uint8_t *bytes,
                         size_t raw_offset,
                         uint32_t instruction_rva,
                         uint32_t iat_rva)
{
    const int64_t displacement =
        (int64_t)iat_rva - (int64_t)(instruction_rva + 6u);
    const int32_t displacement32 = (int32_t)displacement;

    bytes[raw_offset] = UINT8_C(0xff);
    bytes[raw_offset + 1u] = UINT8_C(0x15);
    put_u32(bytes, raw_offset + 2u, (uint32_t)displacement32);
}

static int write_probe(const char *path)
{
    static const uint8_t prefix_code[] = {
        UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xec), UINT8_C(0x28),
        UINT8_C(0xb9), UINT8_C(0x2a), UINT8_C(0x00), UINT8_C(0x00),
        UINT8_C(0x00)
    };
    static const uint8_t suffix_code[] = {
        UINT8_C(0x83), UINT8_C(0xf8), UINT8_C(0x2a),
        UINT8_C(0x75), UINT8_C(0x04),
        UINT8_C(0x31), UINT8_C(0xc9),
        UINT8_C(0xeb), UINT8_C(0x05),
        UINT8_C(0xb9), UINT8_C(0x01), UINT8_C(0x00),
        UINT8_C(0x00), UINT8_C(0x00)
    };
    uint8_t bytes[PROBE_SIZE];
    FILE *output;
    size_t index;
    size_t code_offset;

    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = 0u;
    }

    bytes[0] = (uint8_t)'M';
    bytes[1] = (uint8_t)'Z';
    put_u32(bytes, 0x003cu, PE_OFFSET);

    bytes[PE_OFFSET] = (uint8_t)'P';
    bytes[PE_OFFSET + 1u] = (uint8_t)'E';

    put_u16(bytes, COFF_OFFSET, UINT16_C(0x8664));
    put_u16(bytes, COFF_OFFSET + 2u, UINT16_C(2));
    put_u32(bytes, COFF_OFFSET + 4u, UINT32_C(0));
    put_u16(bytes, COFF_OFFSET + 16u, UINT16_C(0x00f0));
    put_u16(bytes, COFF_OFFSET + 18u, UINT16_C(0x0022));

    put_u16(bytes, OPTIONAL_OFFSET, UINT16_C(0x020b));
    bytes[OPTIONAL_OFFSET + 2u] = UINT8_C(14);
    put_u32(bytes, OPTIONAL_OFFSET + 4u, UINT32_C(0x0200));
    put_u32(bytes, OPTIONAL_OFFSET + 8u, UINT32_C(0x0200));
    put_u32(bytes, OPTIONAL_OFFSET + 16u, TEXT_RVA);
    put_u32(bytes, OPTIONAL_OFFSET + 20u, TEXT_RVA);
    put_u64(bytes, OPTIONAL_OFFSET + 24u,
            UINT64_C(0x0000000140000000));
    put_u32(bytes, OPTIONAL_OFFSET + 32u, UINT32_C(0x1000));
    put_u32(bytes, OPTIONAL_OFFSET + 36u, UINT32_C(0x0200));
    put_u16(bytes, OPTIONAL_OFFSET + 40u, UINT16_C(6));
    put_u16(bytes, OPTIONAL_OFFSET + 48u, UINT16_C(6));
    put_u32(bytes, OPTIONAL_OFFSET + 56u, UINT32_C(0x3000));
    put_u32(bytes, OPTIONAL_OFFSET + 60u, UINT32_C(0x0200));
    put_u16(bytes, OPTIONAL_OFFSET + 68u, UINT16_C(3));
    put_u16(bytes, OPTIONAL_OFFSET + 70u, UINT16_C(0x0100));
    put_u64(bytes, OPTIONAL_OFFSET + 72u, UINT64_C(0x100000));
    put_u64(bytes, OPTIONAL_OFFSET + 80u, UINT64_C(0x1000));
    put_u64(bytes, OPTIONAL_OFFSET + 88u, UINT64_C(0x100000));
    put_u64(bytes, OPTIONAL_OFFSET + 96u, UINT64_C(0x1000));
    put_u32(bytes, OPTIONAL_OFFSET + 108u, UINT32_C(16));
    put_u32(bytes, OPTIONAL_OFFSET + 120u, UINT32_C(0x2000));
    put_u32(bytes, OPTIONAL_OFFSET + 124u, UINT32_C(40));
    put_u32(bytes, OPTIONAL_OFFSET + 208u, UINT32_C(0x2080));
    put_u32(bytes, OPTIONAL_OFFSET + 212u, UINT32_C(32));

    put_section_name(bytes, TEXT_SECTION_OFFSET, ".text");
    put_u32(bytes, TEXT_SECTION_OFFSET + 8u, UINT32_C(0x002a));
    put_u32(bytes, TEXT_SECTION_OFFSET + 12u, TEXT_RVA);
    put_u32(bytes, TEXT_SECTION_OFFSET + 16u, UINT32_C(0x0200));
    put_u32(bytes, TEXT_SECTION_OFFSET + 20u, TEXT_RAW_OFFSET);
    put_u32(bytes, TEXT_SECTION_OFFSET + 36u, UINT32_C(0x60000020));

    put_section_name(bytes, IDATA_SECTION_OFFSET, ".idata");
    put_u32(bytes, IDATA_SECTION_OFFSET + 8u, UINT32_C(0x0160));
    put_u32(bytes, IDATA_SECTION_OFFSET + 12u, IDATA_RVA);
    put_u32(bytes, IDATA_SECTION_OFFSET + 16u, UINT32_C(0x0200));
    put_u32(bytes, IDATA_SECTION_OFFSET + 20u, IDATA_RAW_OFFSET);
    put_u32(bytes, IDATA_SECTION_OFFSET + 36u, UINT32_C(0xc0000040));

    code_offset = TEXT_RAW_OFFSET;
    for (index = 0u; index < sizeof(prefix_code); ++index) {
        bytes[code_offset + index] = prefix_code[index];
    }
    code_offset += sizeof(prefix_code);

    put_call_iat(bytes, code_offset,
                 TEXT_RVA + (uint32_t)(code_offset - TEXT_RAW_OFFSET),
                 UINT32_C(0x2080));
    code_offset += 6u;

    put_call_iat(bytes, code_offset,
                 TEXT_RVA + (uint32_t)(code_offset - TEXT_RAW_OFFSET),
                 UINT32_C(0x2088));
    code_offset += 6u;

    for (index = 0u; index < sizeof(suffix_code); ++index) {
        bytes[code_offset + index] = suffix_code[index];
    }
    code_offset += sizeof(suffix_code);

    put_call_iat(bytes, code_offset,
                 TEXT_RVA + (uint32_t)(code_offset - TEXT_RAW_OFFSET),
                 UINT32_C(0x2090));
    code_offset += 6u;
    bytes[code_offset] = UINT8_C(0xcc);

    put_u32(bytes, idata_raw(UINT32_C(0x2000)),
            UINT32_C(0x2040));
    put_u32(bytes, idata_raw(UINT32_C(0x2000)) + 12u,
            UINT32_C(0x20c0));
    put_u32(bytes, idata_raw(UINT32_C(0x2000)) + 16u,
            UINT32_C(0x2080));

    put_u64(bytes, idata_raw(UINT32_C(0x2040)),
            UINT64_C(0x2100));
    put_u64(bytes, idata_raw(UINT32_C(0x2040)) + 8u,
            UINT64_C(0x2120));
    put_u64(bytes, idata_raw(UINT32_C(0x2040)) + 16u,
            UINT64_C(0x2140));

    put_u64(bytes, idata_raw(UINT32_C(0x2080)),
            UINT64_C(0x2100));
    put_u64(bytes, idata_raw(UINT32_C(0x2080)) + 8u,
            UINT64_C(0x2120));
    put_u64(bytes, idata_raw(UINT32_C(0x2080)) + 16u,
            UINT64_C(0x2140));

    put_ascii_z(bytes, idata_raw(UINT32_C(0x20c0)),
                "KERNEL32.dll");

    put_u16(bytes, idata_raw(UINT32_C(0x2100)), UINT16_C(0));
    put_ascii_z(bytes, idata_raw(UINT32_C(0x2100)) + 2u,
                "SetLastError");

    put_u16(bytes, idata_raw(UINT32_C(0x2120)), UINT16_C(0));
    put_ascii_z(bytes, idata_raw(UINT32_C(0x2120)) + 2u,
                "GetLastError");

    put_u16(bytes, idata_raw(UINT32_C(0x2140)), UINT16_C(0));
    put_ascii_z(bytes, idata_raw(UINT32_C(0x2140)) + 2u,
                "ExitProcess");

    output = fopen(path, "wb");
    if (output == NULL) {
        return 0;
    }

    if (fwrite(bytes, 1u, sizeof(bytes), output) != sizeof(bytes)) {
        (void)fclose(output);
        return 0;
    }

    if (fclose(output) != 0) {
        return 0;
    }

    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        (void)fprintf(stderr, "usage: generate-probe OUTPUT.exe\n");
        return 2;
    }

    if (!write_probe(argv[1])) {
        (void)fprintf(stderr, "generate-probe: write failed\n");
        return 1;
    }

    return 0;
}
