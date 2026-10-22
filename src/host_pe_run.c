#define _POSIX_C_SOURCE 200809L

#include "dblyat/host_pe_run.h"
#include "dblyat/host_pe_input.h"
#include "dblyat/win32_pe_translated_session.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int host_low_canonical_range(uint64_t base, uint64_t size)
{
    uint64_t end;

    if (size == 0u || base == 0u || UINT64_MAX - base < size) {
        return 0;
    }
    end = base + size;
    return (base >> 47u) == 0u && (end >> 47u) == 0u;
}

static int host_range_overlap(uint64_t first, uint64_t first_size,
                              uint64_t second, uint64_t second_size)
{
    return first < second + second_size &&
           second < first + first_size;
}

db_host_pe_run_plan_status db_host_pe_run_plan(
    const db_pe_image *pe,
    size_t page_size,
    db_host_pe_run_layout *layout)
{
    db_host_pe_run_layout planned;

    if (pe == NULL || layout == NULL) {
        return DB_HOST_PE_RUN_PLAN_NULL_ARGUMENT;
    }
    if (page_size < 4096u || (page_size & (page_size - 1u)) != 0u ||
        page_size > 65536u) {
        return DB_HOST_PE_RUN_PLAN_BAD_PAGE_SIZE;
    }
    if (pe->image_size == 0u ||
        pe->image_size > DB_HOST_PE_RUN_MAX_IMAGE) {
        return DB_HOST_PE_RUN_PLAN_BAD_IMAGE_SIZE;
    }
    if (!host_low_canonical_range(pe->image_base, pe->image_size) ||
        host_range_overlap(pe->image_base, pe->image_size,
                           DB_HOST_PE_RUN_STACK_BASE,
                           DB_HOST_PE_RUN_STACK_BYTES) ||
        host_range_overlap(pe->image_base, pe->image_size,
                           DB_HOST_PE_RUN_GATEWAY_BASE,
                           DB_HOST_PE_RUN_GATEWAY_BYTES)) {
        return DB_HOST_PE_RUN_PLAN_BAD_GUEST_RANGE;
    }

    (void)memset(&planned, 0, sizeof(planned));
    planned.image_size = (size_t)pe->image_size;
    planned.image_page_size = page_size;
    planned.image_page_count = planned.image_size / page_size;
    if (planned.image_size % page_size != 0u) {
        ++planned.image_page_count;
    }
    planned.loaded_base = pe->image_base;
    *layout = planned;
    return DB_HOST_PE_RUN_PLAN_OK;
}

#if defined(__aarch64__)

enum {
    HOST_CACHE_SLOTS = 32,
    HOST_RELOCATIONS = 2048,
    HOST_MODULES = 64,
    HOST_IMPORTS = 512,
    HOST_PROVIDERS = 8,
    HOST_PROVIDER_SYMBOLS = 32,
    HOST_HANDLERS = 32,
    HOST_SIGNATURES = 32,
    HOST_RUNS = 16,
    HOST_RUN_BLOCK_BUDGET = 64,
    HOST_BLOCK_RECORDS = 32,
    HOST_IR_RECORDS = 64,
    HOST_SELECT_RECORDS = 128,
    HOST_PLAN_RECORDS = 256,
    HOST_SOURCE_WORDS = 256,
    HOST_TRANSLATION_RELOCATIONS = 128,
    HOST_OUTPUT_WORDS = 512,
    HOST_RESOLUTIONS = 128,
    HOST_TRANSLATED_IMAGE_BYTES = 4096
};

typedef struct host_run_owned {
    uint8_t *image;
    uint8_t *staging_image;
    db_map_page_protection *pages;
    db_map_page_protection *staging_pages;
    uint8_t gateway[DB_HOST_PE_RUN_GATEWAY_BYTES];
    uint8_t staging_gateway[DB_HOST_PE_RUN_GATEWAY_BYTES];
    uint8_t stack[DB_HOST_PE_RUN_STACK_BYTES];
    db_reloc_record relocations[HOST_RELOCATIONS];
    db_import_module modules[HOST_MODULES];
    db_import_record imports[HOST_IMPORTS];
    db_bind_provider providers[HOST_PROVIDERS];
    db_bind_provider_symbol symbols[HOST_PROVIDER_SYMBOLS];
    db_gateway_dispatch_handler handlers[HOST_HANDLERS];
    db_gateway_frame_signature signatures[HOST_SIGNATURES];
    db_import_binding bindings[HOST_IMPORTS];
    db_iat_target targets[HOST_IMPORTS];
    db_iat_patch patches[HOST_IMPORTS];

    db_arm64_block_cache_slot cache_slots[HOST_CACHE_SLOTS];
    db_linux_arm64_wx_handle cache_handles[HOST_CACHE_SLOTS];
    uint8_t cache_owned[HOST_CACHE_SLOTS];
    db_x64_block_record block_records[HOST_BLOCK_RECORDS];
    db_ir_instruction ir[HOST_IR_RECORDS];
    db_arm64_select_record selections[HOST_SELECT_RECORDS];
    db_x64_arm64_plan_record plans[HOST_PLAN_RECORDS];
    uint32_t source_words[HOST_SOURCE_WORDS];
    db_x64_arm64_relocation translation_relocations[HOST_TRANSLATION_RELOCATIONS];
    uint32_t output_words[HOST_OUTPUT_WORDS];
    db_x64_arm64_resolution resolutions[HOST_RESOLUTIONS];
    uint8_t translated_image[HOST_TRANSLATED_IMAGE_BYTES];

    db_win32_pe_startup_config startup;
    db_win32_pe_startup_workspace startup_workspace;
    db_arm64_translation_workspace translation_workspace;
    db_win32_pe_translated_session_config config;
    db_win32_pe_translated_session session;
    uint32_t reserved_code_pages;
} host_run_owned;

static int host_reserve_code_base(
    void *context,
    uint64_t guest_source_rip,
    uint64_t source_generation,
    uint64_t translation_key,
    uint64_t page_size,
    uint64_t *code_base)
{
    host_run_owned *owned = (host_run_owned *)context;
    const uint64_t stride = UINT64_C(65536);
    const uint64_t start = UINT64_C(0x0000001000000000);

    (void)guest_source_rip;
    (void)source_generation;
    (void)translation_key;
    if (owned == NULL || code_base == NULL || page_size == 0u ||
        page_size > stride || (stride % page_size) != 0u ||
        owned->reserved_code_pages >= UINT32_C(1024)) {
        return -1;
    }
    *code_base = start + stride * owned->reserved_code_pages;
    ++owned->reserved_code_pages;
    /*
     * This returns an address candidate, NOT a reservation.
     * POSIX W^X publication must acquire it with MAP_FIXED_NOREPLACE,
     * or reject rather than overwrite any existing process mapping.
     */
    return 0;
}

static int host_allocate(host_run_owned *owned,
                         const db_host_pe_run_layout *layout)
{
    owned->image = calloc(layout->image_size, 1u);
    owned->staging_image = calloc(layout->image_size, 1u);
    owned->pages = calloc(layout->image_page_count, sizeof(*owned->pages));
    owned->staging_pages = calloc(
        layout->image_page_count, sizeof(*owned->staging_pages));
    return owned->image != NULL && owned->staging_image != NULL &&
           owned->pages != NULL && owned->staging_pages != NULL;
}

static void host_release(host_run_owned *owned)
{
    if (owned == NULL) {
        return;
    }
    free(owned->staging_pages);
    free(owned->pages);
    free(owned->staging_image);
    free(owned->image);
    free(owned);
}

static void host_build_startup(host_run_owned *owned,
                               const db_host_pe_input *input,
                               const db_host_pe_run_layout *layout)
{
    db_win32_pe_startup_config *start = &owned->startup;
    db_win32_pe_startup_workspace *space = &owned->startup_workspace;
    db_arm64_translation_workspace *translation = &owned->translation_workspace;
    db_win32_pe_translated_session_config *config = &owned->config;

    start->source = input->data;
    start->source_size = input->size;
    start->image = owned->image;
    start->image_size = layout->image_size;
    start->page_plan = owned->pages;
    start->page_capacity = layout->image_page_count;
    start->page_size = layout->image_page_size;
    start->gateway = owned->gateway;
    start->gateway_size = sizeof(owned->gateway);
    start->loaded_base = layout->loaded_base;
    start->gateway_base = DB_HOST_PE_RUN_GATEWAY_BASE;
    start->gateway_region_size = sizeof(owned->gateway);
    start->slot_size = UINT64_C(64);
    start->guest_stack_base = DB_HOST_PE_RUN_STACK_BASE;
    start->guest_stack_size = sizeof(owned->stack);
    start->initial_rsp = DB_HOST_PE_RUN_STACK_BASE +
                         (uint64_t)sizeof(owned->stack);
    start->format_version = DB_WIN32_PE_STARTUP_FORMAT_VERSION;

    space->staging_image = owned->staging_image;
    space->staging_page_plan = owned->staging_pages;
    space->staging_gateway = owned->staging_gateway;
    space->relocations = owned->relocations;
    space->modules = owned->modules;
    space->imports = owned->imports;
    space->providers = owned->providers;
    space->provider_symbols = owned->symbols;
    space->handlers = owned->handlers;
    space->signatures = owned->signatures;
    space->bindings = owned->bindings;
    space->targets = owned->targets;
    space->patches = owned->patches;
    space->staging_image_size = layout->image_size;
    space->staging_page_capacity = layout->image_page_count;
    space->staging_gateway_size = sizeof(owned->staging_gateway);
    space->relocation_capacity = HOST_RELOCATIONS;
    space->module_capacity = HOST_MODULES;
    space->import_capacity = HOST_IMPORTS;
    space->provider_capacity = HOST_PROVIDERS;
    space->provider_symbol_capacity = HOST_PROVIDER_SYMBOLS;
    space->handler_capacity = HOST_HANDLERS;
    space->signature_capacity = HOST_SIGNATURES;
    space->binding_capacity = HOST_IMPORTS;
    space->target_capacity = HOST_IMPORTS;
    space->patch_capacity = HOST_IMPORTS;

    translation->block_records = owned->block_records;
    translation->ir = owned->ir;
    translation->selections = owned->selections;
    translation->plans = owned->plans;
    translation->source_words = owned->source_words;
    translation->relocations = owned->translation_relocations;
    translation->output_words = owned->output_words;
    translation->resolutions = owned->resolutions;
    translation->image_bytes = owned->translated_image;
    translation->block_record_capacity = HOST_BLOCK_RECORDS;
    translation->ir_capacity = HOST_IR_RECORDS;
    translation->selection_capacity = HOST_SELECT_RECORDS;
    translation->plan_capacity = HOST_PLAN_RECORDS;
    translation->source_word_capacity = HOST_SOURCE_WORDS;
    translation->relocation_capacity = HOST_TRANSLATION_RELOCATIONS;
    translation->output_word_capacity = HOST_OUTPUT_WORDS;
    translation->resolution_capacity = HOST_RESOLUTIONS;
    translation->image_capacity = sizeof(owned->translated_image);

    config->startup = start;
    config->startup_workspace = space;
    config->translation_workspace = translation;
    config->cache_slots = owned->cache_slots;
    config->cache_handles = owned->cache_handles;
    config->cache_owned = owned->cache_owned;
    config->guest_stack = owned->stack;
    config->wx_platform = db_linux_arm64_wx_posix_platform();
    config->code_base_context = owned;
    config->reserve_code_base = host_reserve_code_base;
    config->cache_capacity = HOST_CACHE_SLOTS;
    config->source_generation = UINT64_C(1);
    config->translation_key = UINT64_C(1);
    config->translation_page_size = layout->image_page_size;
    config->translation_instruction_budget = 8u;
    config->translation_byte_budget = 64u;
    config->run_block_budget = HOST_RUN_BLOCK_BUDGET;
    config->process_id = (uint32_t)getpid();
    config->thread_id = (uint32_t)getpid();
    config->format_version = DB_WIN32_PE_TRANSLATED_SESSION_FORMAT_VERSION;
}

static int host_execute(host_run_owned *owned)
{
    db_win32_pe_translated_session_run_summary summary;
    db_win32_pe_translated_session_status status;
    uint32_t run;
    const char *trace_env = getenv("DBLYATEM_RUN_TRACE");
    const int trace_enabled = trace_env != NULL &&
                              strcmp(trace_env, "1") == 0;
    int result = 5;

    db_win32_pe_translated_session_init(&owned->session);
    status = db_win32_pe_translated_session_prepare(
        &owned->config, &owned->session);
    if (status != DB_WIN32_PE_TRANSLATED_SESSION_OK) {
        (void)fprintf(stderr, "DblyatEM run: prepare failed: %s\n",
                      db_win32_pe_translated_session_status_name(status));
        if (owned->session.state ==
            (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_CLEANUP_REQUIRED) {
            uint32_t retry;
            for (retry = 0u; retry < 3u; ++retry) {
                if (db_win32_pe_translated_session_dispose(
                        &owned->session) == DB_WIN32_PE_TRANSLATED_SESSION_OK) {
                    break;
                }
            }
            if (retry == 3u) {
                (void)fprintf(stderr,
                              "DblyatEM run: prepare rollback incomplete\n");
            }
        }
        return 4;
    }

    for (run = 0u; run < HOST_RUNS; ++run) {
        (void)memset(&summary, 0, sizeof(summary));
        status = db_win32_pe_translated_session_run(
            &owned->session, &summary);
        if (status != DB_WIN32_PE_TRANSLATED_SESSION_OK) {
            (void)fprintf(stderr, "DblyatEM run: execution failed: %s\n",
                          db_win32_pe_translated_session_status_name(status));
            result = 4;
            break;
        }
        if (trace_enabled) {
            (void)fprintf(stderr,
                          "PE TRACE: run=%u source_rip=0x%016" PRIx64
                          " final_rip=0x%016" PRIx64
                          " stop=%u cache_count=%zu fill_count=%u hit_count=%u\n",
                          summary.session_run_count,
                          summary.source_rip, summary.final_rip,
                          summary.stop_reason, owned->session.store.cache.count,
                          summary.demand.fill_count,
                          summary.demand.cache_hit_fill_count);
        }
        if (summary.stop_reason ==
            (uint32_t)DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TERMINATED) {
            (void)fprintf(stdout, "PE RUN: EXIT code=%u\n",
                          summary.exit_code);
            result = summary.exit_code < UINT32_C(256)
                         ? (int)summary.exit_code : 4;
            break;
        }
        if (summary.stop_reason ==
            (uint32_t)DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TRAP) {
            (void)fprintf(stderr, "PE RUN: GUEST TRAP=%u\n", summary.trap);
            result = 4;
            break;
        }
    }
    if (run == HOST_RUNS) {
        (void)fprintf(stderr, "PE RUN: BLOCK BUDGET EXHAUSTED\n");
    }
    status = db_win32_pe_translated_session_dispose(&owned->session);
    if (status != DB_WIN32_PE_TRANSLATED_SESSION_OK) {
        (void)fprintf(stderr, "DblyatEM run: dispose failed: %s\n",
                      db_win32_pe_translated_session_status_name(status));
        result = 4;
    }
    return result;
}
#endif

int db_host_pe_run_file(const char *path)
{
#if !defined(__aarch64__)
    (void)path;
    (void)fprintf(stderr,
                  "DblyatEM run: native AArch64 Linux host required\n");
    return 3;
#else
    db_host_pe_input input;
    db_host_pe_run_layout layout;
    db_host_pe_run_plan_status plan_status;
    db_pe_status pe_status;
    db_pe_image pe;
    host_run_owned *owned = NULL;
    long host_page;
    int result = 4;

    db_host_pe_input_init(&input);
    if (db_host_pe_input_open(path, DB_HOST_PE_RUN_MAX_FILE, &input) !=
        DB_HOST_PE_INPUT_OK) {
        (void)fprintf(stderr, "DblyatEM run: invalid host input\n");
        return 2;
    }

    pe_status = db_pe_verify(input.data, input.size, &pe);
    if (pe_status != DB_PE_OK) {
        (void)fprintf(stderr, "DblyatEM run: invalid PE: %s\n",
                      db_pe_status_name(pe_status));
        result = 2;
        goto cleanup;
    }

    host_page = sysconf(_SC_PAGESIZE);
    if (host_page <= 0) {
        (void)fprintf(stderr, "DblyatEM run: invalid host page size\n");
        goto cleanup;
    }
    plan_status = db_host_pe_run_plan(&pe, (size_t)host_page, &layout);
    if (plan_status != DB_HOST_PE_RUN_PLAN_OK) {
        (void)fprintf(stderr, "DblyatEM run: image size/range unsupported\n");
        result = 2;
        goto cleanup;
    }

    owned = calloc(1u, sizeof(*owned));
    if (owned == NULL || !host_allocate(owned, &layout)) {
        (void)fprintf(stderr, "DblyatEM run: host allocation failed\n");
        goto cleanup;
    }
    host_build_startup(owned, &input, &layout);
    result = host_execute(owned);

cleanup:
    host_release(owned);
    if (db_host_pe_input_close(&input) != DB_HOST_PE_INPUT_OK) {
        (void)fprintf(stderr, "DblyatEM run: unmap failed\n");
        result = 4;
    }
    return result;
#endif
}
