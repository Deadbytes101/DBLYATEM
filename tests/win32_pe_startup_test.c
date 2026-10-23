#include "dblyat/gateway_decode.h"
#include "dblyat/win32_pe_startup.h"
#include "dblyat/win32_provider.h"
#include "dblyat/x64_block.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FILE_SIZE 0x0800u
#define IMAGE_SIZE 0x3000u
#define PAGE_SIZE 0x1000u
#define PAGE_COUNT 3u
#define GATEWAY_SIZE 0x1000u
#define GATEWAY_BASE UINT64_C(0x0000000180000000)
#define GATEWAY_SLOT UINT64_C(64)
#define STACK_BASE UINT64_C(0x0000000070000000)
#define STACK_SIZE 0x1000u
#define INITIAL_RSP (STACK_BASE + STACK_SIZE)
#define IMAGE_BASE UINT64_C(0x0000000140000000)

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
#define IMPORT_DIRECTORY_ENTRY_OFFSET (OPTIONAL_OFFSET + 120u)
#define IMPORT_DIRECTORY_RVA 0x2000u
#define IMPORT_DIRECTORY_SIZE 40u
#define DLL_NAME_RVA 0x2080u
#define LOOKUP_TABLE_RVA 0x2100u
#define IAT_RVA 0x2120u
#define IMPORT_NAME_STRUCT_RVA 0x2200u
#define IMPORT_NAME_RVA (IMPORT_NAME_STRUCT_RVA + 2u)

#define CHECK(value)                                                         \
    do {                                                                     \
        if (!(value)) {                                                      \
            printf("WIN32 PE STARTUP TEST: FAIL line=%d\n", __LINE__);      \
            return 0;                                                        \
        }                                                                    \
    } while (0)

typedef struct fixture {
    uint8_t source[FILE_SIZE];
    uint8_t final_image[IMAGE_SIZE];
    uint8_t staging_image[IMAGE_SIZE];
    db_map_page_protection final_pages[PAGE_COUNT];
    db_map_page_protection staging_pages[PAGE_COUNT];
    uint8_t final_gateway[GATEWAY_SIZE];
    uint8_t staging_gateway[GATEWAY_SIZE];
    db_reloc_record relocations[8];
    db_import_module modules[4];
    db_import_record imports[8];
    db_bind_provider providers[4];
    db_bind_provider_symbol provider_symbols[16];
    db_gateway_dispatch_handler handlers[16];
    db_gateway_frame_signature signatures[16];
    db_import_binding bindings[8];
    db_iat_target targets[8];
    db_iat_patch patches[8];
    db_win32_pe_startup_config config;
    db_win32_pe_startup_workspace workspace;
    db_mapped_image mapped;
    db_x64_scalar_state scalar;
    db_x64_code_view code;
    db_win32_pe_startup_summary summary;
} fixture;

static void put_u16(uint8_t *bytes, size_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
    bytes[offset + 2u] = (uint8_t)(value >> 16u);
    bytes[offset + 3u] = (uint8_t)(value >> 24u);
}

static void put_u64(uint8_t *bytes, size_t offset, uint64_t value)
{
    put_u32(bytes, offset, (uint32_t)value);
    put_u32(bytes, offset + 4u, (uint32_t)(value >> 32u));
}

static uint64_t get_u64(const uint8_t *bytes, size_t offset)
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

static void make_pe(uint8_t *bytes, const char *symbol)
{
    const uint32_t call_disp = IAT_RVA - (TEXT_RVA + 6u);

    memset(bytes, 0, FILE_SIZE);
    bytes[0] = (uint8_t)'M';
    bytes[1] = (uint8_t)'Z';
    put_u32(bytes, 0x3cu, PE_OFFSET);

    bytes[PE_OFFSET] = (uint8_t)'P';
    bytes[PE_OFFSET + 1u] = (uint8_t)'E';
    put_u16(bytes, COFF_OFFSET, UINT16_C(0x8664));
    put_u16(bytes, COFF_OFFSET + 2u, 2u);
    put_u32(bytes, COFF_OFFSET + 4u, UINT32_C(0x12345678));
    put_u16(bytes, COFF_OFFSET + 16u, UINT16_C(0x00f0));
    put_u16(bytes, COFF_OFFSET + 18u, UINT16_C(0x0022));

    put_u16(bytes, OPTIONAL_OFFSET, UINT16_C(0x020b));
    put_u32(bytes, OPTIONAL_OFFSET + 16u, TEXT_RVA);
    put_u64(bytes, OPTIONAL_OFFSET + 24u, IMAGE_BASE);
    put_u32(bytes, OPTIONAL_OFFSET + 32u, PAGE_SIZE);
    put_u32(bytes, OPTIONAL_OFFSET + 36u, UINT32_C(0x0200));
    put_u32(bytes, OPTIONAL_OFFSET + 56u, IMAGE_SIZE);
    put_u32(bytes, OPTIONAL_OFFSET + 60u, UINT32_C(0x0200));
    put_u16(bytes, OPTIONAL_OFFSET + 68u, 3u);
    put_u16(bytes, OPTIONAL_OFFSET + 70u, UINT16_C(0x8160));
    put_u32(bytes, OPTIONAL_OFFSET + 108u, 16u);
    put_u32(bytes, IMPORT_DIRECTORY_ENTRY_OFFSET, IMPORT_DIRECTORY_RVA);
    put_u32(bytes, IMPORT_DIRECTORY_ENTRY_OFFSET + 4u, IMPORT_DIRECTORY_SIZE);

    memcpy(&bytes[TEXT_SECTION_OFFSET], ".text", 5u);
    put_u32(bytes, TEXT_SECTION_OFFSET + 8u, UINT32_C(0x0200));
    put_u32(bytes, TEXT_SECTION_OFFSET + 12u, TEXT_RVA);
    put_u32(bytes, TEXT_SECTION_OFFSET + 16u, UINT32_C(0x0200));
    put_u32(bytes, TEXT_SECTION_OFFSET + 20u, TEXT_RAW_OFFSET);
    put_u32(bytes, TEXT_SECTION_OFFSET + 36u, UINT32_C(0x60000020));

    memcpy(&bytes[IDATA_SECTION_OFFSET], ".idata", 6u);
    put_u32(bytes, IDATA_SECTION_OFFSET + 8u, UINT32_C(0x1000));
    put_u32(bytes, IDATA_SECTION_OFFSET + 12u, IDATA_RVA);
    put_u32(bytes, IDATA_SECTION_OFFSET + 16u, UINT32_C(0x0400));
    put_u32(bytes, IDATA_SECTION_OFFSET + 20u, IDATA_RAW_OFFSET);
    put_u32(bytes, IDATA_SECTION_OFFSET + 36u, UINT32_C(0xc0000040));

    bytes[TEXT_RAW_OFFSET] = UINT8_C(0xff);
    bytes[TEXT_RAW_OFFSET + 1u] = UINT8_C(0x15);
    put_u32(bytes, TEXT_RAW_OFFSET + 2u, call_disp);
    bytes[TEXT_RAW_OFFSET + 6u] = UINT8_C(0xcc);

    put_u32(bytes, idata_raw(IMPORT_DIRECTORY_RVA), LOOKUP_TABLE_RVA);
    put_u32(bytes, idata_raw(IMPORT_DIRECTORY_RVA) + 12u, DLL_NAME_RVA);
    put_u32(bytes, idata_raw(IMPORT_DIRECTORY_RVA) + 16u, IAT_RVA);
    put_ascii(bytes, idata_raw(DLL_NAME_RVA), "KERNEL32.dll");

    put_u64(bytes, idata_raw(LOOKUP_TABLE_RVA), IMPORT_NAME_STRUCT_RVA);
    put_u64(bytes, idata_raw(LOOKUP_TABLE_RVA) + 8u, 0u);
    put_u64(bytes, idata_raw(IAT_RVA), IMPORT_NAME_STRUCT_RVA);
    put_u64(bytes, idata_raw(IAT_RVA) + 8u, 0u);
    put_u16(bytes, idata_raw(IMPORT_NAME_STRUCT_RVA), 0u);
    put_ascii(bytes, idata_raw(IMPORT_NAME_RVA), symbol);
}

static void init_fixture(fixture *value, const char *symbol)
{
    memset(value, 0, sizeof(*value));
    make_pe(value->source, symbol);

    memset(value->final_image, 0xa5, sizeof(value->final_image));
    memset(value->staging_image, 0x5a, sizeof(value->staging_image));
    memset(value->final_pages, 0xa5, sizeof(value->final_pages));
    memset(value->staging_pages, 0x5a, sizeof(value->staging_pages));
    memset(value->final_gateway, 0xa5, sizeof(value->final_gateway));
    memset(value->staging_gateway, 0x5a, sizeof(value->staging_gateway));
    memset(&value->mapped, 0xa5, sizeof(value->mapped));
    memset(&value->scalar, 0xa5, sizeof(value->scalar));
    memset(&value->code, 0xa5, sizeof(value->code));
    memset(&value->summary, 0xa5, sizeof(value->summary));

    value->config.source = value->source;
    value->config.image = value->final_image;
    value->config.page_plan = value->final_pages;
    value->config.gateway = value->final_gateway;
    value->config.source_size = sizeof(value->source);
    value->config.image_size = sizeof(value->final_image);
    value->config.page_capacity = PAGE_COUNT;
    value->config.gateway_size = sizeof(value->final_gateway);
    value->config.page_size = PAGE_SIZE;
    value->config.loaded_base = IMAGE_BASE;
    value->config.gateway_base = GATEWAY_BASE;
    value->config.gateway_region_size = GATEWAY_SIZE;
    value->config.slot_size = GATEWAY_SLOT;
    value->config.guest_stack_base = STACK_BASE;
    value->config.guest_stack_size = STACK_SIZE;
    value->config.initial_rsp = INITIAL_RSP;
    value->config.format_version = DB_WIN32_PE_STARTUP_FORMAT_VERSION;

    value->workspace.staging_image = value->staging_image;
    value->workspace.staging_page_plan = value->staging_pages;
    value->workspace.staging_gateway = value->staging_gateway;
    value->workspace.relocations = value->relocations;
    value->workspace.modules = value->modules;
    value->workspace.imports = value->imports;
    value->workspace.providers = value->providers;
    value->workspace.provider_symbols = value->provider_symbols;
    value->workspace.handlers = value->handlers;
    value->workspace.signatures = value->signatures;
    value->workspace.bindings = value->bindings;
    value->workspace.targets = value->targets;
    value->workspace.patches = value->patches;
    value->workspace.staging_image_size = sizeof(value->staging_image);
    value->workspace.staging_page_capacity = PAGE_COUNT;
    value->workspace.staging_gateway_size = sizeof(value->staging_gateway);
    value->workspace.relocation_capacity = 8u;
    value->workspace.module_capacity = 4u;
    value->workspace.import_capacity = 8u;
    value->workspace.provider_capacity = 4u;
    value->workspace.provider_symbol_capacity = 16u;
    value->workspace.handler_capacity = 16u;
    value->workspace.signature_capacity = 16u;
    value->workspace.binding_capacity = 8u;
    value->workspace.target_capacity = 8u;
    value->workspace.patch_capacity = 8u;
}

static int final_preserved(const fixture *after, const fixture *before)
{
    return memcmp(after->final_image,
                  before->final_image,
                  sizeof(after->final_image)) == 0 &&
           memcmp(after->final_pages,
                  before->final_pages,
                  sizeof(after->final_pages)) == 0 &&
           memcmp(after->final_gateway,
                  before->final_gateway,
                  sizeof(after->final_gateway)) == 0 &&
           memcmp(&after->mapped, &before->mapped, sizeof(after->mapped)) == 0 &&
           memcmp(&after->scalar, &before->scalar, sizeof(after->scalar)) == 0 &&
           memcmp(&after->code, &before->code, sizeof(after->code)) == 0 &&
           memcmp(&after->summary, &before->summary, sizeof(after->summary)) == 0;
}

static int test_success(void)
{
    fixture value;
    db_iat_target decoded[2];
    db_gateway_decode_summary gateway_summary;
    db_x64_block_record block_records[4];
    db_x64_block_summary block;
    size_t index;

    init_fixture(&value, "GetLastError");
    CHECK(db_win32_pe_startup_prepare(
              &value.config,
              &value.workspace,
              &value.mapped,
              &value.scalar,
              &value.code,
              &value.summary) == DB_WIN32_PE_STARTUP_OK);

    CHECK(value.summary.pe.entry_rva == TEXT_RVA);
    CHECK(value.summary.loaded_base == IMAGE_BASE);
    CHECK(value.summary.entry_rip == IMAGE_BASE + TEXT_RVA);
    CHECK(value.summary.initial_rsp == INITIAL_RSP);
    CHECK(value.summary.provider_tables.provider_count == 1u);
    CHECK(value.summary.imports.module_count == 1u);
    CHECK(value.summary.imports.record_count == 1u);
    CHECK(value.summary.bind.binding_count == 1u);
    CHECK(value.summary.gateway_plan.target_count == 1u);
    CHECK(value.summary.gateway_emit.bytes_written == GATEWAY_SLOT);
    CHECK(value.summary.iat_plan.patch_count == 1u);
    CHECK(value.summary.iat_apply.patch_count == 1u);
    CHECK(value.summary.image_bytes_committed == IMAGE_SIZE);
    CHECK(value.summary.page_entries_committed == PAGE_COUNT);
    CHECK(value.summary.gateway_bytes_committed == GATEWAY_SLOT);

    CHECK(value.bindings[0].provider_id == DB_WIN32_PROVIDER_KERNEL32);
    CHECK(value.bindings[0].symbol_id ==
          DB_WIN32_SYMBOL_KERNEL32_GET_LAST_ERROR);
    CHECK(value.targets[0].provider_id == DB_WIN32_PROVIDER_KERNEL32);
    CHECK(value.targets[0].symbol_id ==
          DB_WIN32_SYMBOL_KERNEL32_GET_LAST_ERROR);
    CHECK(value.targets[0].guest_value == GATEWAY_BASE);
    CHECK(get_u64(value.final_image, IAT_RVA) == GATEWAY_BASE);

    memset(decoded, 0, sizeof(decoded));
    memset(&gateway_summary, 0, sizeof(gateway_summary));
    CHECK(db_gateway_decode_slots(value.final_gateway,
                                  GATEWAY_SLOT,
                                  1u,
                                  GATEWAY_BASE,
                                  GATEWAY_SLOT,
                                  decoded,
                                  2u,
                                  &gateway_summary) == DB_GATEWAY_DECODE_OK);
    CHECK(decoded[0].provider_id == DB_WIN32_PROVIDER_KERNEL32);
    CHECK(decoded[0].symbol_id ==
          DB_WIN32_SYMBOL_KERNEL32_GET_LAST_ERROR);
    CHECK(decoded[0].guest_value == GATEWAY_BASE);

    CHECK(value.mapped.preferred_base == IMAGE_BASE);
    CHECK(value.scalar.rip == IMAGE_BASE + TEXT_RVA);
    CHECK(value.scalar.registers[4] == INITIAL_RSP);
    CHECK(value.scalar.rflags == DB_X64_RFLAGS_FIXED_ONE);
    for (index = 0u; index < 16u; ++index) {
        if (index != 4u) {
            CHECK(value.scalar.registers[index] == 0u);
        }
    }
    CHECK(value.code.bytes == value.final_image);
    CHECK(value.code.size == IMAGE_SIZE);
    CHECK(value.code.guest_base == IMAGE_BASE);

    memset(block_records, 0, sizeof(block_records));
    memset(&block, 0, sizeof(block));
    CHECK(db_x64_block_build(&value.code,
                             value.scalar.rip,
                             4u,
                             32u,
                             block_records,
                             4u,
                             &block) == DB_X64_BLOCK_OK);
    CHECK(block.record_count == 1u);
    CHECK(block.stop_reason == DB_X64_BLOCK_STOP_TERMINATOR);
    CHECK(block_records[0].control == DB_X64_BLOCK_CONTROL_INDIRECT_CALL);
    CHECK(block_records[0].memory.memory.address.mode ==
          DB_X64_ADDRESS_RIP_RELATIVE);
    CHECK(block_records[0].fallthrough_rip == IMAGE_BASE + TEXT_RVA + 6u);
    CHECK(block_records[0].memory.memory.address.displacement ==
          (int64_t)(IAT_RVA - (TEXT_RVA + 6u)));
    return 1;
}

static int test_failures_preserve_final_outputs(void)
{
    fixture value;
    fixture before;

    init_fixture(&value, "CreateFileW");
    before = value;
    CHECK(db_win32_pe_startup_prepare(
              &value.config,
              &value.workspace,
              &value.mapped,
              &value.scalar,
              &value.code,
              &value.summary) == DB_WIN32_PE_STARTUP_BIND_FAILED);
    CHECK(final_preserved(&value, &before));

    init_fixture(&value, "GetLastError");
    value.config.initial_rsp = STACK_BASE - 8u;
    before = value;
    CHECK(db_win32_pe_startup_prepare(
              &value.config,
              &value.workspace,
              &value.mapped,
              &value.scalar,
              &value.code,
              &value.summary) == DB_WIN32_PE_STARTUP_BAD_STACK);
    CHECK(final_preserved(&value, &before));

    init_fixture(&value, "GetLastError");
    value.config.gateway_size = 32u;
    before = value;
    CHECK(db_win32_pe_startup_prepare(
              &value.config,
              &value.workspace,
              &value.mapped,
              &value.scalar,
              &value.code,
              &value.summary) == DB_WIN32_PE_STARTUP_WORKSPACE_TOO_SMALL);
    CHECK(final_preserved(&value, &before));

    init_fixture(&value, "GetLastError");
    value.source[0] = 0u;
    before = value;
    CHECK(db_win32_pe_startup_prepare(
              &value.config,
              &value.workspace,
              &value.mapped,
              &value.scalar,
              &value.code,
              &value.summary) == DB_WIN32_PE_STARTUP_PE_FAILED);
    CHECK(final_preserved(&value, &before));
    return 1;
}

int main(void)
{
    if (!test_success() || !test_failures_preserve_final_outputs()) {
        return 1;
    }

    printf("WIN32 PE STARTUP TEST: PASS\n");
    printf("PE MAP RELOC IMPORT BOOTSTRAP: PASS\n");
    printf("CATALOG DERIVED KERNEL32 BINDING: PASS\n");
    printf("DBGT IAT PATCH AGREEMENT: PASS\n");
    printf("ENTRY FF2 IAT CONTROL FLOW: PASS\n");
    printf("INITIAL X64 STATE CONTRACT: PASS\n");
    printf("WHOLE STARTUP TRANSACTIONALITY: PASS\n");
    printf("HOST FILE LOADING CLI: NOT IMPLEMENTED\n");
    printf("NATIVE GUEST MEMORY BACKEND: NOT IMPLEMENTED\n");
    printf("NATIVE ARM64 PE EXECUTION: NOT RUN\n");
    printf("DBLYATEM IDENTICAL PE EXECUTION: NOT RUN\n");
    printf("WINLATOR COMPARISON: NOT RUN\n");
    return 0;
}
