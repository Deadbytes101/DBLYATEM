#include "win32_pe_translated_session_internal.h"

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
#define ENTRY_RIP (IMAGE_BASE + UINT64_C(0x1000))
#define UNMAPPED_RIP UINT64_C(0x0000000160000000)

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

#define STORE_CAPACITY 8u
#define RECORD_CAPACITY 8u
#define IR_CAPACITY 16u
#define SELECT_CAPACITY 32u
#define PLAN_CAPACITY 64u
#define SOURCE_WORD_CAPACITY 64u
#define RELOCATION_CAPACITY 32u
#define OUTPUT_WORD_CAPACITY 128u
#define RESOLUTION_CAPACITY 32u
#define TRANSLATED_IMAGE_CAPACITY (OUTPUT_WORD_CAPACITY * sizeof(uint32_t))
#define TEST_GENERATION UINT64_C(17)
#define TEST_TRANSLATION_KEY UINT64_C(0x149149)

#define CHECK(value)                                                         \
    do {                                                                     \
        if (!(value)) {                                                      \
            printf("WIN32 PE TRANSLATED SESSION CHECK: FAIL line=%d\n",     \
                   __LINE__);                                                \
            return 0;                                                        \
        }                                                                    \
    } while (0)

typedef struct translation_storage {
    db_x64_block_record block_records[RECORD_CAPACITY];
    db_ir_instruction ir[IR_CAPACITY];
    db_arm64_select_record selections[SELECT_CAPACITY];
    db_x64_arm64_plan_record plans[PLAN_CAPACITY];
    uint32_t source_words[SOURCE_WORD_CAPACITY];
    db_x64_arm64_relocation relocations[RELOCATION_CAPACITY];
    uint32_t output_words[OUTPUT_WORD_CAPACITY];
    db_x64_arm64_resolution resolutions[RESOLUTION_CAPACITY];
    uint8_t image_bytes[TRANSLATED_IMAGE_CAPACITY];
} translation_storage;

typedef struct fake_platform_context {
    uint8_t *arena;
    size_t arena_size;
    uint32_t map_count;
    uint32_t unmap_count;
    uint8_t fail_unmap;
} fake_platform_context;

typedef struct code_base_context {
    uint8_t *arena;
    size_t next_page;
} code_base_context;

typedef struct scripted_step {
    uint64_t expected_rip;
    uint64_t next_rip;
    uint32_t outcome;
    uint32_t exit_code;
    uint32_t last_error;
} scripted_step;

typedef struct scripted_executor {
    const scripted_step *steps;
    size_t count;
    size_t index;
} scripted_executor;

typedef struct fixture {
    uint8_t source[FILE_SIZE];
    uint8_t final_image[IMAGE_SIZE];
    uint8_t staging_image[IMAGE_SIZE];
    db_map_page_protection final_pages[PAGE_COUNT];
    db_map_page_protection staging_pages[PAGE_COUNT];
    uint8_t final_gateway[GATEWAY_SIZE];
    uint8_t staging_gateway[GATEWAY_SIZE];
    uint8_t guest_stack[STACK_SIZE];
    db_reloc_record startup_relocations[8];
    db_import_module modules[4];
    db_import_record imports[8];
    db_bind_provider providers[4];
    db_bind_provider_symbol provider_symbols[16];
    db_gateway_dispatch_handler handlers[16];
    db_gateway_frame_signature signatures[16];
    db_import_binding bindings[8];
    db_iat_target targets[8];
    db_iat_patch patches[8];
    db_win32_pe_startup_config startup;
    db_win32_pe_startup_workspace startup_workspace;

    db_arm64_block_cache_slot cache_slots[STORE_CAPACITY];
    db_linux_arm64_wx_handle cache_handles[STORE_CAPACITY];
    uint8_t cache_owned[STORE_CAPACITY];
    translation_storage translated_storage;
    db_arm64_translation_workspace translation_workspace;
    fake_platform_context platform_context;
    code_base_context base_context;
    db_linux_arm64_wx_platform platform;
    db_x64_arm64_helper_targets helper_targets;
    db_win32_pe_translated_session_config session_config;
    db_win32_pe_translated_session session;
} fixture;

static _Alignas(4096) uint8_t fake_arena[PAGE_SIZE * 8u];

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

static void make_pe(uint8_t *bytes)
{
    const uint32_t call_disp = IAT_RVA - (TEXT_RVA + 6u);

    (void)memset(bytes, 0, FILE_SIZE);
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
    put_u32(bytes, IMPORT_DIRECTORY_ENTRY_OFFSET + 4u,
            IMPORT_DIRECTORY_SIZE);

    (void)memcpy(&bytes[TEXT_SECTION_OFFSET], ".text", 5u);
    put_u32(bytes, TEXT_SECTION_OFFSET + 8u, UINT32_C(0x0200));
    put_u32(bytes, TEXT_SECTION_OFFSET + 12u, TEXT_RVA);
    put_u32(bytes, TEXT_SECTION_OFFSET + 16u, UINT32_C(0x0200));
    put_u32(bytes, TEXT_SECTION_OFFSET + 20u, TEXT_RAW_OFFSET);
    put_u32(bytes, TEXT_SECTION_OFFSET + 36u, UINT32_C(0x60000020));

    (void)memcpy(&bytes[IDATA_SECTION_OFFSET], ".idata", 6u);
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
    put_u64(bytes, idata_raw(IAT_RVA), IMPORT_NAME_STRUCT_RVA);
    put_u16(bytes, idata_raw(IMPORT_NAME_STRUCT_RVA), 0u);
    put_ascii(bytes, idata_raw(IMPORT_NAME_RVA), "GetLastError");
}

static int within_arena(const fake_platform_context *context,
                        const void *address,
                        size_t size)
{
    const uintptr_t begin = (uintptr_t)context->arena;
    const uintptr_t end = begin + context->arena_size;
    const uintptr_t value = (uintptr_t)address;

    return value >= begin && size <= context->arena_size &&
           value <= end - size;
}

static void *fake_map_rw(void *opaque,
                         void *requested_address,
                         size_t allocation_size)
{
    fake_platform_context *context = (fake_platform_context *)opaque;
    ++context->map_count;
    return within_arena(context, requested_address, allocation_size)
               ? requested_address
               : NULL;
}

static int fake_copy(void *opaque,
                     void *destination,
                     const void *source,
                     size_t byte_count)
{
    fake_platform_context *context = (fake_platform_context *)opaque;
    if (!within_arena(context, destination, byte_count)) {
        return -1;
    }
    (void)memcpy(destination, source, byte_count);
    return 0;
}

static int fake_protect_rx(void *opaque,
                           void *mapping,
                           size_t allocation_size)
{
    return within_arena((fake_platform_context *)opaque,
                        mapping,
                        allocation_size)
               ? 0
               : -1;
}

static int fake_sync_icache(void *opaque, void *begin, size_t byte_count)
{
    return within_arena((fake_platform_context *)opaque, begin, byte_count)
               ? 0
               : -1;
}

static int fake_unmap(void *opaque, void *mapping, size_t allocation_size)
{
    fake_platform_context *context = (fake_platform_context *)opaque;
    ++context->unmap_count;
    if (context->fail_unmap != 0u) {
        return -1;
    }
    return within_arena(context, mapping, allocation_size) ? 0 : -1;
}

static int reserve_code_base(void *opaque,
                             uint64_t guest_source_rip,
                             uint64_t source_generation,
                             uint64_t translation_key,
                             uint64_t page_size,
                             uint64_t *code_base)
{
    code_base_context *context = (code_base_context *)opaque;
    uintptr_t address;

    (void)guest_source_rip;
    if (source_generation != TEST_GENERATION ||
        translation_key != TEST_TRANSLATION_KEY || page_size != PAGE_SIZE ||
        context->next_page >= 8u) {
        return -1;
    }
    address = (uintptr_t)context->arena +
              context->next_page * (size_t)page_size;
    ++context->next_page;
    *code_base = (uint64_t)address;
    return 0;
}

static db_arm64_translation_workspace make_translation_workspace(
    translation_storage *storage)
{
    db_arm64_translation_workspace workspace;

    (void)memset(&workspace, 0, sizeof(workspace));
    workspace.block_records = storage->block_records;
    workspace.ir = storage->ir;
    workspace.selections = storage->selections;
    workspace.plans = storage->plans;
    workspace.source_words = storage->source_words;
    workspace.relocations = storage->relocations;
    workspace.output_words = storage->output_words;
    workspace.resolutions = storage->resolutions;
    workspace.image_bytes = storage->image_bytes;
    workspace.block_record_capacity = RECORD_CAPACITY;
    workspace.ir_capacity = IR_CAPACITY;
    workspace.selection_capacity = SELECT_CAPACITY;
    workspace.plan_capacity = PLAN_CAPACITY;
    workspace.source_word_capacity = SOURCE_WORD_CAPACITY;
    workspace.relocation_capacity = RELOCATION_CAPACITY;
    workspace.output_word_capacity = OUTPUT_WORD_CAPACITY;
    workspace.resolution_capacity = RESOLUTION_CAPACITY;
    workspace.image_capacity = TRANSLATED_IMAGE_CAPACITY;
    return workspace;
}

static void make_helper_targets(db_x64_arm64_helper_targets *targets)
{
    uint32_t index;
    (void)memset(targets, 0, sizeof(*targets));
    for (index = (uint32_t)DB_X64_ARM64_HELPER_FORM_GUEST_ADDRESS;
         index < DB_X64_ARM64_HELPER_TARGET_COUNT;
         ++index) {
        targets->addresses[index] =
            (uint64_t)(uintptr_t)(fake_arena + 2048u);
    }
}

static db_linux_arm64_wx_platform make_platform(
    fake_platform_context *context)
{
    db_linux_arm64_wx_platform platform;
    (void)memset(&platform, 0, sizeof(platform));
    platform.context = context;
    platform.map_rw = fake_map_rw;
    platform.copy_bytes = fake_copy;
    platform.protect_rx = fake_protect_rx;
    platform.sync_icache = fake_sync_icache;
    platform.unmap = fake_unmap;
    return platform;
}

static void init_fixture(fixture *value, uint32_t block_budget)
{
    (void)memset(fake_arena, 0, sizeof(fake_arena));
    (void)memset(value, 0, sizeof(*value));
    make_pe(value->source);

    value->startup.source = value->source;
    value->startup.image = value->final_image;
    value->startup.page_plan = value->final_pages;
    value->startup.gateway = value->final_gateway;
    value->startup.source_size = sizeof(value->source);
    value->startup.image_size = sizeof(value->final_image);
    value->startup.page_capacity = PAGE_COUNT;
    value->startup.gateway_size = sizeof(value->final_gateway);
    value->startup.page_size = PAGE_SIZE;
    value->startup.loaded_base = IMAGE_BASE;
    value->startup.gateway_base = GATEWAY_BASE;
    value->startup.gateway_region_size = GATEWAY_SIZE;
    value->startup.slot_size = GATEWAY_SLOT;
    value->startup.guest_stack_base = STACK_BASE;
    value->startup.guest_stack_size = STACK_SIZE;
    value->startup.initial_rsp = INITIAL_RSP;
    value->startup.format_version = DB_WIN32_PE_STARTUP_FORMAT_VERSION;

    value->startup_workspace.staging_image = value->staging_image;
    value->startup_workspace.staging_page_plan = value->staging_pages;
    value->startup_workspace.staging_gateway = value->staging_gateway;
    value->startup_workspace.relocations = value->startup_relocations;
    value->startup_workspace.modules = value->modules;
    value->startup_workspace.imports = value->imports;
    value->startup_workspace.providers = value->providers;
    value->startup_workspace.provider_symbols = value->provider_symbols;
    value->startup_workspace.handlers = value->handlers;
    value->startup_workspace.signatures = value->signatures;
    value->startup_workspace.bindings = value->bindings;
    value->startup_workspace.targets = value->targets;
    value->startup_workspace.patches = value->patches;
    value->startup_workspace.staging_image_size = sizeof(value->staging_image);
    value->startup_workspace.staging_page_capacity = PAGE_COUNT;
    value->startup_workspace.staging_gateway_size =
        sizeof(value->staging_gateway);
    value->startup_workspace.relocation_capacity = 8u;
    value->startup_workspace.module_capacity = 4u;
    value->startup_workspace.import_capacity = 8u;
    value->startup_workspace.provider_capacity = 4u;
    value->startup_workspace.provider_symbol_capacity = 16u;
    value->startup_workspace.handler_capacity = 16u;
    value->startup_workspace.signature_capacity = 16u;
    value->startup_workspace.binding_capacity = 8u;
    value->startup_workspace.target_capacity = 8u;
    value->startup_workspace.patch_capacity = 8u;

    value->translation_workspace =
        make_translation_workspace(&value->translated_storage);
    value->platform_context.arena = fake_arena;
    value->platform_context.arena_size = sizeof(fake_arena);
    value->base_context.arena = fake_arena;
    value->platform = make_platform(&value->platform_context);
    make_helper_targets(&value->helper_targets);

    value->session_config.startup = &value->startup;
    value->session_config.startup_workspace = &value->startup_workspace;
    value->session_config.translation_workspace =
        &value->translation_workspace;
    value->session_config.cache_slots = value->cache_slots;
    value->session_config.cache_handles = value->cache_handles;
    value->session_config.cache_owned = value->cache_owned;
    value->session_config.guest_stack = value->guest_stack;
    value->session_config.wx_platform = &value->platform;
    value->session_config.code_base_context = &value->base_context;
    value->session_config.reserve_code_base = reserve_code_base;
    value->session_config.cache_capacity = STORE_CAPACITY;
    value->session_config.source_generation = TEST_GENERATION;
    value->session_config.translation_key = TEST_TRANSLATION_KEY;
    value->session_config.translation_page_size = PAGE_SIZE;
    value->session_config.translation_instruction_budget = 8u;
    value->session_config.translation_byte_budget = 64u;
    value->session_config.run_block_budget = block_budget;
    value->session_config.process_id = UINT32_C(1001);
    value->session_config.thread_id = UINT32_C(2002);
    value->session_config.format_version =
        DB_WIN32_PE_TRANSLATED_SESSION_FORMAT_VERSION;
    db_win32_pe_translated_session_init(&value->session);
}

static db_win32_translated_execution_status scripted_execute(
    void *opaque,
    const db_arm64_published_block *published,
    const db_win32_translated_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_execution_summary *summary)
{
    scripted_executor *script = (scripted_executor *)opaque;
    const scripted_step *step;

    if (script == NULL || published == NULL || config == NULL ||
        kernel32_state == NULL || scalar_state == NULL || summary == NULL ||
        script->index >= script->count || config->guest_memory == NULL) {
        return DB_WIN32_TRANSLATED_EXECUTION_NULL_ARGUMENT;
    }

    step = &script->steps[script->index++];
    if (published->guest_source_rip != step->expected_rip ||
        scalar_state->rip != step->expected_rip ||
        config->guest_memory->image_guest_base != IMAGE_BASE ||
        config->guest_memory->stack_guest_base != STACK_BASE ||
        config->guest_memory->gateway_guest_base != GATEWAY_BASE ||
        config->guest_memory->gateway_size != GATEWAY_SLOT) {
        return DB_WIN32_TRANSLATED_EXECUTION_BAD_PUBLISHED_BLOCK;
    }

    (void)memset(summary, 0, sizeof(*summary));
    summary->observed_exit = published->terminal_exit;
    if (published->terminal_exit.kind ==
        (uint32_t)DB_TRANSLATED_TERMINAL_EXIT_TARGET) {
        summary->observed_exit.runtime_target = step->next_rip;
        summary->observed_exit.runtime_target_valid = 1u;
    }
    summary->outcome = step->outcome;
    summary->exit_code = step->exit_code;
    summary->format_version = DB_WIN32_TRANSLATED_EXECUTION_FORMAT_VERSION;
    summary->entry_executed = 1u;
    kernel32_state->last_error = step->last_error;

    if (step->outcome ==
        (uint32_t)DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE) {
        scalar_state->rip = step->next_rip;
        summary->next_rip = step->next_rip;
    } else if (step->outcome ==
               (uint32_t)DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED) {
        scalar_state->rip = published->terminal_exit.source_rip;
        summary->next_rip = 0u;
    } else {
        return DB_WIN32_TRANSLATED_EXECUTION_BAD_GATEWAY_OUTCOME;
    }

    return DB_WIN32_TRANSLATED_EXECUTION_OK;
}

static int test_prepare_derives_exact_runtime(void)
{
    fixture value;

    init_fixture(&value, 1u);
    CHECK(db_win32_pe_translated_session_prepare_with_helper_targets(
              &value.session_config,
              &value.helper_targets,
              &value.session) == DB_WIN32_PE_TRANSLATED_SESSION_OK);
    CHECK(value.session.state ==
          (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_READY);
    CHECK(value.session.scalar.rip == ENTRY_RIP);
    CHECK(value.session.scalar.registers[4] == INITIAL_RSP);
    CHECK(value.session.kernel32.process_id == 1001u);
    CHECK(value.session.kernel32.thread_id == 2002u);
    CHECK(value.session.translation.code == &value.session.code);
    CHECK(value.session.translation.helper_targets ==
          &value.session.helper_targets);
    CHECK(value.session.demand.store == &value.session.store);
    CHECK(value.session.demand.translation == &value.session.translation);
    CHECK(value.session.demand.image_page_count ==
          value.session.startup.page_entries_committed);
    CHECK(value.session.demand.image_page_count == PAGE_COUNT);
    CHECK(value.session.demand.gateway_buffer_size ==
          value.session.startup.gateway_bytes_committed);
    CHECK(value.session.demand.gateway_buffer_size == GATEWAY_SLOT);
    CHECK(value.session.demand.target_count ==
          value.session.startup.gateway_plan.target_count);
    CHECK(value.session.demand.target_count == 1u);
    CHECK(value.session.demand.handler_count ==
          value.session.startup.provider_tables.handler_count);
    CHECK(value.session.demand.signature_count ==
          value.session.startup.provider_tables.signature_count);
    CHECK(value.session.store.cache.count == 0u);
    CHECK(db_win32_pe_translated_session_dispose(&value.session) ==
          DB_WIN32_PE_TRANSLATED_SESSION_OK);
    CHECK(value.session.state ==
          (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_EMPTY);
    return 1;
}

static int test_repeated_run_reuses_store_and_terminates(void)
{
    fixture value;
    db_win32_pe_translated_session_run_summary first;
    db_win32_pe_translated_session_run_summary second;
    db_win32_pe_translated_session_run_summary rejected;
    const scripted_step steps[] = {
        {ENTRY_RIP,
         GATEWAY_BASE,
         DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
         0u,
         41u},
        {GATEWAY_BASE,
         0u,
         DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED,
         23u,
         42u}
    };
    scripted_executor script = {steps, 2u, 0u};
    db_win32_pe_translated_session before_rejected;

    init_fixture(&value, 1u);
    CHECK(db_win32_pe_translated_session_prepare_with_helper_targets(
              &value.session_config,
              &value.helper_targets,
              &value.session) == DB_WIN32_PE_TRANSLATED_SESSION_OK);

    (void)memset(&first, 0, sizeof(first));
    CHECK(db_win32_pe_translated_session_run_with_executor(
              &value.session,
              &first,
              scripted_execute,
              &script) == DB_WIN32_PE_TRANSLATED_SESSION_OK);
    CHECK(first.stop_reason ==
          (uint32_t)DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_BLOCK_BUDGET);
    CHECK(first.state_after ==
          (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_READY);
    CHECK(value.session.scalar.rip == GATEWAY_BASE);
    CHECK(value.session.kernel32.last_error == 41u);
    CHECK(value.session.store.cache.count == 1u);
    CHECK(value.session.run_count == 1u);

    (void)memset(&second, 0, sizeof(second));
    CHECK(db_win32_pe_translated_session_run_with_executor(
              &value.session,
              &second,
              scripted_execute,
              &script) == DB_WIN32_PE_TRANSLATED_SESSION_OK);
    CHECK(script.index == 2u);
    CHECK(second.stop_reason ==
          (uint32_t)DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TERMINATED);
    CHECK(second.exit_code == 23u);
    CHECK(second.state_after ==
          (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_TERMINATED);
    CHECK(value.session.state ==
          (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_TERMINATED);
    CHECK(value.session.exit_code == 23u);
    CHECK(value.session.kernel32.last_error == 42u);
    CHECK(value.session.store.cache.count == 2u);
    CHECK(value.session.run_count == 2u);

    before_rejected = value.session;
    (void)memset(&rejected, 0xa5, sizeof(rejected));
    CHECK(db_win32_pe_translated_session_run_with_executor(
              &value.session,
              &rejected,
              scripted_execute,
              &script) ==
          DB_WIN32_PE_TRANSLATED_SESSION_ALREADY_TERMINATED);
    CHECK(memcmp(&before_rejected,
                 &value.session,
                 sizeof(value.session)) == 0);

    CHECK(db_win32_pe_translated_session_dispose(&value.session) ==
          DB_WIN32_PE_TRANSLATED_SESSION_OK);
    CHECK(value.platform_context.unmap_count == 2u);
    CHECK(value.session.state ==
          (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_EMPTY);
    return 1;
}

static int test_run_failure_preserves_architecture_and_cache(void)
{
    fixture value;
    db_win32_pe_translated_session_run_summary summary;
    db_x64_scalar_state scalar_before;
    db_win32_kernel32_state kernel_before;
    const scripted_step steps[] = {
        {ENTRY_RIP,
         UNMAPPED_RIP,
         DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
         0u,
         77u}
    };
    scripted_executor script = {steps, 1u, 0u};

    init_fixture(&value, 2u);
    CHECK(db_win32_pe_translated_session_prepare_with_helper_targets(
              &value.session_config,
              &value.helper_targets,
              &value.session) == DB_WIN32_PE_TRANSLATED_SESSION_OK);
    scalar_before = value.session.scalar;
    kernel_before = value.session.kernel32;
    (void)memset(&summary, 0xa5, sizeof(summary));

    CHECK(db_win32_pe_translated_session_run_with_executor(
              &value.session,
              &summary,
              scripted_execute,
              &script) == DB_WIN32_PE_TRANSLATED_SESSION_RUN_FAILED);
    CHECK(script.index == 1u);
    CHECK(memcmp(&scalar_before,
                 &value.session.scalar,
                 sizeof(scalar_before)) == 0);
    CHECK(memcmp(&kernel_before,
                 &value.session.kernel32,
                 sizeof(kernel_before)) == 0);
    CHECK(value.session.state ==
          (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_READY);
    CHECK(value.session.run_count == 0u);
    CHECK(value.session.store.cache.count == 1u);
    CHECK(value.platform_context.map_count == 1u);

    CHECK(db_win32_pe_translated_session_dispose(&value.session) ==
          DB_WIN32_PE_TRANSLATED_SESSION_OK);
    CHECK(value.platform_context.unmap_count == 1u);
    return 1;
}

static int test_startup_failure_leaves_empty_session(void)
{
    fixture value;
    db_win32_pe_translated_session expected;

    init_fixture(&value, 1u);
    value.source[0] = 0u;
    db_win32_pe_translated_session_init(&expected);
    CHECK(db_win32_pe_translated_session_prepare_with_helper_targets(
              &value.session_config,
              &value.helper_targets,
              &value.session) == DB_WIN32_PE_TRANSLATED_SESSION_STARTUP_FAILED);
    CHECK(memcmp(&expected, &value.session, sizeof(expected)) == 0);
    CHECK(value.platform_context.map_count == 0u);
    CHECK(value.platform_context.unmap_count == 0u);
    return 1;
}

static int test_dispose_failure_is_retryable(void)
{
    fixture value;
    db_win32_pe_translated_session_run_summary summary;
    const scripted_step steps[] = {
        {ENTRY_RIP,
         GATEWAY_BASE,
         DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
         0u,
         0u}
    };
    scripted_executor script = {steps, 1u, 0u};

    init_fixture(&value, 1u);
    CHECK(db_win32_pe_translated_session_prepare_with_helper_targets(
              &value.session_config,
              &value.helper_targets,
              &value.session) == DB_WIN32_PE_TRANSLATED_SESSION_OK);
    CHECK(db_win32_pe_translated_session_run_with_executor(
              &value.session,
              &summary,
              scripted_execute,
              &script) == DB_WIN32_PE_TRANSLATED_SESSION_OK);
    CHECK(value.session.store.cache.count == 1u);

    value.platform_context.fail_unmap = 1u;
    CHECK(db_win32_pe_translated_session_dispose(&value.session) ==
          DB_WIN32_PE_TRANSLATED_SESSION_DISPOSE_FAILED);
    CHECK(value.session.state ==
          (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_CLEANUP_REQUIRED);

    value.platform_context.fail_unmap = 0u;
    CHECK(db_win32_pe_translated_session_dispose(&value.session) ==
          DB_WIN32_PE_TRANSLATED_SESSION_OK);
    CHECK(value.session.state ==
          (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_EMPTY);
    return 1;
}

static int test_public_prepare_host_boundary(void)
{
#if defined(__aarch64__)
    printf("PUBLIC SESSION NATIVE HELPER TARGETS: NOT RUN IN PORTABLE CHECK\n");
    return 1;
#else
    fixture value;
    db_win32_pe_translated_session before;

    init_fixture(&value, 1u);
    before = value.session;
    CHECK(db_win32_pe_translated_session_prepare(
              &value.session_config,
              &value.session) ==
          DB_WIN32_PE_TRANSLATED_SESSION_UNSUPPORTED_HOST);
    CHECK(memcmp(&before, &value.session, sizeof(before)) == 0);
    printf("PUBLIC SESSION NONAARCH64 FAIL CLOSED: PASS\n");
    return 1;
#endif
}

int main(void)
{
    CHECK(test_prepare_derives_exact_runtime());
    CHECK(test_repeated_run_reuses_store_and_terminates());
    CHECK(test_run_failure_preserves_architecture_and_cache());
    CHECK(test_startup_failure_leaves_empty_session());
    CHECK(test_dispose_failure_is_retryable());
    CHECK(test_public_prepare_host_boundary());

    printf("SESSION STARTUP DERIVED COUNTS: PASS\n");
    printf("SESSION REPEATED RUN CACHE REUSE: PASS\n");
    printf("SESSION READY TO TERMINATED: PASS\n");
    printf("SESSION TERMINATED RUN REJECTION: PASS\n");
    printf("SESSION INFRA FAILURE ARCH ROLLBACK: PASS\n");
    printf("SESSION VALID CACHE DURABILITY: PASS\n");
    printf("SESSION STARTUP FAILURE EMPTY: PASS\n");
    printf("SESSION DISPOSE RETRY: PASS\n");
    printf("HOST FILE LOADING CLI: NOT IMPLEMENTED\n");
    printf("NATIVE ARM64 PE SESSION EXECUTION: NOT RUN\n");
    printf("PRECISE X86 PAGE FAULT MODEL: NOT IMPLEMENTED\n");
    printf("DBLYATEM IDENTICAL PE EXECUTION: NOT RUN\n");
    printf("WINLATOR COMPARISON: NOT RUN\n");
    printf("WIN32 PE TRANSLATED SESSION CHECK: PASS\n");
    return 0;
}
