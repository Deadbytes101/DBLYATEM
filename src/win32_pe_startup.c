#include "dblyat/win32_pe_startup.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct db_win32_pe_startup_region {
    const void *pointer;
    size_t size;
} db_win32_pe_startup_region;

static bool db_win32_pe_startup_mul(size_t left,
                                    size_t right,
                                    size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }
    *result = left * right;
    return true;
}

static bool db_win32_pe_startup_overlap(const void *left,
                                        size_t left_size,
                                        const void *right,
                                        size_t right_size)
{
    const uintptr_t left_address = (uintptr_t)left;
    const uintptr_t right_address = (uintptr_t)right;

    if (left_size == 0u || right_size == 0u) {
        return false;
    }
    if (left_address <= right_address) {
        return right_address - left_address < left_size;
    }
    return left_address - right_address < right_size;
}

static bool db_win32_pe_startup_zero(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (bytes[index] != 0u) {
            return false;
        }
    }
    return true;
}

static bool db_win32_pe_startup_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return address != 0u &&
           upper == (sign != 0u ? UINT64_C(0xffff) : UINT64_C(0));
}

static bool db_win32_pe_startup_add(uint64_t base,
                                    uint64_t amount,
                                    uint64_t *result)
{
    if (UINT64_MAX - base < amount) {
        return false;
    }
    *result = base + amount;
    return db_win32_pe_startup_canonical(*result);
}

static bool db_win32_pe_startup_stack_valid(
    const db_win32_pe_startup_config *config)
{
    uint64_t end;

    if (config->guest_stack_size == 0u ||
        !db_win32_pe_startup_canonical(config->guest_stack_base) ||
        !db_win32_pe_startup_canonical(config->initial_rsp) ||
        UINT64_MAX - config->guest_stack_base < config->guest_stack_size) {
        return false;
    }

    end = config->guest_stack_base + (uint64_t)config->guest_stack_size;
    if (!db_win32_pe_startup_canonical(end)) {
        return false;
    }

    return config->initial_rsp >= config->guest_stack_base &&
           config->initial_rsp <= end &&
           (config->initial_rsp & UINT64_C(7)) == 0u;
}

static bool db_win32_pe_startup_add_region(
    db_win32_pe_startup_region *regions,
    size_t capacity,
    size_t *count,
    const void *pointer,
    size_t size)
{
    size_t index;

    if (pointer == NULL || size == 0u || *count >= capacity) {
        return false;
    }
    for (index = 0u; index < *count; ++index) {
        if (db_win32_pe_startup_overlap(pointer,
                                        size,
                                        regions[index].pointer,
                                        regions[index].size)) {
            return false;
        }
    }
    regions[*count].pointer = pointer;
    regions[*count].size = size;
    ++*count;
    return true;
}

static db_win32_pe_startup_status db_win32_pe_startup_validate_regions(
    const db_win32_pe_startup_config *config,
    db_win32_pe_startup_workspace *workspace,
    db_mapped_image *mapped,
    db_x64_scalar_state *scalar,
    db_x64_code_view *code,
    db_win32_pe_startup_summary *summary)
{
    db_win32_pe_startup_region regions[32];
    size_t count = 0u;
    size_t bytes;

#define DB_ADD_REGION(pointer_value, size_value)                              \
    do {                                                                       \
        if (!db_win32_pe_startup_add_region(                                   \
                regions,                                                       \
                sizeof(regions) / sizeof(regions[0]),                          \
                &count,                                                        \
                (pointer_value),                                               \
                (size_value))) {                                               \
            return DB_WIN32_PE_STARTUP_BUFFER_OVERLAP;                         \
        }                                                                      \
    } while (0)

    DB_ADD_REGION(config, sizeof(*config));
    DB_ADD_REGION(workspace, sizeof(*workspace));
    DB_ADD_REGION(mapped, sizeof(*mapped));
    DB_ADD_REGION(scalar, sizeof(*scalar));
    DB_ADD_REGION(code, sizeof(*code));
    DB_ADD_REGION(summary, sizeof(*summary));
    DB_ADD_REGION(config->source, config->source_size);
    DB_ADD_REGION(config->image, config->image_size);

    if (!db_win32_pe_startup_mul(config->page_capacity,
                                 sizeof(*config->page_plan),
                                 &bytes)) {
        return DB_WIN32_PE_STARTUP_COUNT_OVERFLOW;
    }
    DB_ADD_REGION(config->page_plan, bytes);
    DB_ADD_REGION(config->gateway, config->gateway_size);
    DB_ADD_REGION(workspace->staging_image, workspace->staging_image_size);

    if (!db_win32_pe_startup_mul(workspace->staging_page_capacity,
                                 sizeof(*workspace->staging_page_plan),
                                 &bytes)) {
        return DB_WIN32_PE_STARTUP_COUNT_OVERFLOW;
    }
    DB_ADD_REGION(workspace->staging_page_plan, bytes);
    DB_ADD_REGION(workspace->staging_gateway,
                  workspace->staging_gateway_size);

    if (!db_win32_pe_startup_mul(workspace->relocation_capacity,
                                 sizeof(*workspace->relocations),
                                 &bytes)) {
        return DB_WIN32_PE_STARTUP_COUNT_OVERFLOW;
    }
    DB_ADD_REGION(workspace->relocations, bytes);

    if (!db_win32_pe_startup_mul(workspace->module_capacity,
                                 sizeof(*workspace->modules),
                                 &bytes)) {
        return DB_WIN32_PE_STARTUP_COUNT_OVERFLOW;
    }
    DB_ADD_REGION(workspace->modules, bytes);

    if (!db_win32_pe_startup_mul(workspace->import_capacity,
                                 sizeof(*workspace->imports),
                                 &bytes)) {
        return DB_WIN32_PE_STARTUP_COUNT_OVERFLOW;
    }
    DB_ADD_REGION(workspace->imports, bytes);

    if (!db_win32_pe_startup_mul(workspace->provider_capacity,
                                 sizeof(*workspace->providers),
                                 &bytes)) {
        return DB_WIN32_PE_STARTUP_COUNT_OVERFLOW;
    }
    DB_ADD_REGION(workspace->providers, bytes);

    if (!db_win32_pe_startup_mul(workspace->provider_symbol_capacity,
                                 sizeof(*workspace->provider_symbols),
                                 &bytes)) {
        return DB_WIN32_PE_STARTUP_COUNT_OVERFLOW;
    }
    DB_ADD_REGION(workspace->provider_symbols, bytes);

    if (!db_win32_pe_startup_mul(workspace->handler_capacity,
                                 sizeof(*workspace->handlers),
                                 &bytes)) {
        return DB_WIN32_PE_STARTUP_COUNT_OVERFLOW;
    }
    DB_ADD_REGION(workspace->handlers, bytes);

    if (!db_win32_pe_startup_mul(workspace->signature_capacity,
                                 sizeof(*workspace->signatures),
                                 &bytes)) {
        return DB_WIN32_PE_STARTUP_COUNT_OVERFLOW;
    }
    DB_ADD_REGION(workspace->signatures, bytes);

    if (!db_win32_pe_startup_mul(workspace->binding_capacity,
                                 sizeof(*workspace->bindings),
                                 &bytes)) {
        return DB_WIN32_PE_STARTUP_COUNT_OVERFLOW;
    }
    DB_ADD_REGION(workspace->bindings, bytes);

    if (!db_win32_pe_startup_mul(workspace->target_capacity,
                                 sizeof(*workspace->targets),
                                 &bytes)) {
        return DB_WIN32_PE_STARTUP_COUNT_OVERFLOW;
    }
    DB_ADD_REGION(workspace->targets, bytes);

    if (!db_win32_pe_startup_mul(workspace->patch_capacity,
                                 sizeof(*workspace->patches),
                                 &bytes)) {
        return DB_WIN32_PE_STARTUP_COUNT_OVERFLOW;
    }
    DB_ADD_REGION(workspace->patches, bytes);

#undef DB_ADD_REGION

    return DB_WIN32_PE_STARTUP_OK;
}

static bool db_win32_pe_startup_config_valid(
    const db_win32_pe_startup_config *config,
    const db_win32_pe_startup_workspace *workspace)
{
    return config->format_version == DB_WIN32_PE_STARTUP_FORMAT_VERSION &&
           db_win32_pe_startup_zero(config->reserved,
                                    sizeof(config->reserved)) &&
           db_win32_pe_startup_zero(workspace->reserved,
                                    sizeof(workspace->reserved)) &&
           config->source_size != 0u &&
           config->image_size != 0u &&
           config->page_capacity != 0u &&
           config->gateway_size != 0u &&
           config->page_size != 0u &&
           db_win32_pe_startup_canonical(config->loaded_base) &&
           workspace->staging_image_size != 0u &&
           workspace->staging_page_capacity != 0u &&
           workspace->staging_gateway_size != 0u &&
           workspace->relocation_capacity != 0u &&
           workspace->module_capacity != 0u &&
           workspace->import_capacity != 0u &&
           workspace->provider_capacity != 0u &&
           workspace->provider_symbol_capacity != 0u &&
           workspace->handler_capacity != 0u &&
           workspace->signature_capacity != 0u &&
           workspace->binding_capacity != 0u &&
           workspace->target_capacity != 0u &&
           workspace->patch_capacity != 0u;
}

db_win32_pe_startup_status db_win32_pe_startup_prepare(
    const db_win32_pe_startup_config *config,
    db_win32_pe_startup_workspace *workspace,
    db_mapped_image *mapped,
    db_x64_scalar_state *scalar,
    db_x64_code_view *code,
    db_win32_pe_startup_summary *summary)
{
    db_win32_pe_startup_summary completed;
    db_x64_scalar_state planned_scalar;
    db_x64_code_view planned_code;
    db_mapped_image planned_mapped;
    const db_win32_provider_record *catalog;
    size_t catalog_count;
    size_t required_pages;
    uint64_t entry_rip;
    db_win32_pe_startup_status validation;

    if (config == NULL || workspace == NULL || mapped == NULL ||
        scalar == NULL || code == NULL || summary == NULL ||
        config->source == NULL || config->image == NULL ||
        config->page_plan == NULL || config->gateway == NULL ||
        workspace->staging_image == NULL ||
        workspace->staging_page_plan == NULL ||
        workspace->staging_gateway == NULL ||
        workspace->relocations == NULL || workspace->modules == NULL ||
        workspace->imports == NULL || workspace->providers == NULL ||
        workspace->provider_symbols == NULL || workspace->handlers == NULL ||
        workspace->signatures == NULL || workspace->bindings == NULL ||
        workspace->targets == NULL || workspace->patches == NULL) {
        return DB_WIN32_PE_STARTUP_NULL_ARGUMENT;
    }

    if (!db_win32_pe_startup_config_valid(config, workspace)) {
        return DB_WIN32_PE_STARTUP_BAD_CONFIG;
    }
    if (!db_win32_pe_startup_stack_valid(config)) {
        return DB_WIN32_PE_STARTUP_BAD_STACK;
    }

    validation = db_win32_pe_startup_validate_regions(config,
                                                      workspace,
                                                      mapped,
                                                      scalar,
                                                      code,
                                                      summary);
    if (validation != DB_WIN32_PE_STARTUP_OK) {
        return validation;
    }

    memset(&completed, 0, sizeof(completed));
    completed.pe_status = (uint32_t)db_pe_verify(config->source,
                                                 config->source_size,
                                                 &completed.pe);
    if (completed.pe_status != (uint32_t)DB_PE_OK) {
        return DB_WIN32_PE_STARTUP_PE_FAILED;
    }

    required_pages =
        ((size_t)completed.pe.image_size + config->page_size - 1u) /
        config->page_size;
    if (config->page_size > SIZE_MAX -
            ((size_t)completed.pe.image_size % config->page_size) ||
        completed.pe.image_size > config->image_size ||
        completed.pe.image_size > workspace->staging_image_size ||
        required_pages > config->page_capacity ||
        required_pages > workspace->staging_page_capacity) {
        return DB_WIN32_PE_STARTUP_WORKSPACE_TOO_SMALL;
    }

    completed.map_status = (uint32_t)db_map_pe64(
        config->source,
        config->source_size,
        workspace->staging_image,
        workspace->staging_image_size,
        config->page_size,
        workspace->staging_page_plan,
        workspace->staging_page_capacity,
        &planned_mapped);
    if (completed.map_status != (uint32_t)DB_MAP_OK) {
        return DB_WIN32_PE_STARTUP_MAP_FAILED;
    }
    completed.mapped = planned_mapped;

    completed.reloc_plan_status = (uint32_t)db_reloc_plan_pe64(
        workspace->staging_image,
        workspace->staging_image_size,
        &planned_mapped,
        config->page_size,
        workspace->staging_page_plan,
        workspace->staging_page_capacity,
        workspace->relocations,
        workspace->relocation_capacity,
        &completed.reloc_plan);
    if (completed.reloc_plan_status != (uint32_t)DB_RELOC_OK) {
        return DB_WIN32_PE_STARTUP_RELOC_PLAN_FAILED;
    }

    completed.reloc_apply_status = (uint32_t)db_reloc_apply_pe64(
        workspace->staging_image,
        workspace->staging_image_size,
        &planned_mapped,
        config->page_size,
        workspace->staging_page_plan,
        workspace->staging_page_capacity,
        workspace->relocations,
        completed.reloc_plan.record_count,
        &completed.reloc_plan,
        config->loaded_base,
        &completed.reloc_apply);
    if (completed.reloc_apply_status != (uint32_t)DB_RELOC_APPLY_OK) {
        return DB_WIN32_PE_STARTUP_RELOC_APPLY_FAILED;
    }

    catalog = db_win32_kernel32_catalog(&catalog_count);
    completed.provider_tables_status =
        (uint32_t)db_win32_provider_tables_build(
            catalog,
            catalog_count,
            workspace->providers,
            workspace->provider_capacity,
            workspace->provider_symbols,
            workspace->provider_symbol_capacity,
            workspace->handlers,
            workspace->handler_capacity,
            workspace->signatures,
            workspace->signature_capacity,
            &completed.provider_tables);
    if (completed.provider_tables_status !=
        (uint32_t)DB_WIN32_PROVIDER_TABLES_OK) {
        return DB_WIN32_PE_STARTUP_PROVIDER_TABLES_FAILED;
    }

    completed.import_status = (uint32_t)db_import_plan_pe64(
        workspace->staging_image,
        workspace->staging_image_size,
        &planned_mapped,
        config->page_size,
        workspace->staging_page_plan,
        workspace->staging_page_capacity,
        workspace->modules,
        workspace->module_capacity,
        workspace->imports,
        workspace->import_capacity,
        &completed.imports);
    if (completed.import_status != (uint32_t)DB_IMPORT_OK) {
        return DB_WIN32_PE_STARTUP_IMPORT_FAILED;
    }

    completed.bind_status = (uint32_t)db_bind_plan_imports(
        workspace->staging_image,
        workspace->staging_image_size,
        workspace->modules,
        completed.imports.module_count,
        workspace->imports,
        completed.imports.record_count,
        workspace->providers,
        completed.provider_tables.provider_count,
        workspace->bindings,
        workspace->binding_capacity,
        &completed.bind);
    if (completed.bind_status != (uint32_t)DB_BIND_OK) {
        return DB_WIN32_PE_STARTUP_BIND_FAILED;
    }

    completed.gateway_plan_status = (uint32_t)db_gateway_plan_targets(
        workspace->bindings,
        completed.bind.binding_count,
        config->gateway_base,
        config->gateway_region_size,
        config->slot_size,
        workspace->targets,
        workspace->target_capacity,
        &completed.gateway_plan);
    if (completed.gateway_plan_status != (uint32_t)DB_GATEWAY_PLAN_OK) {
        return DB_WIN32_PE_STARTUP_GATEWAY_PLAN_FAILED;
    }

    completed.gateway_emit_status = (uint32_t)db_gateway_emit_slots(
        workspace->targets,
        completed.gateway_plan.target_count,
        config->gateway_base,
        config->slot_size,
        workspace->staging_gateway,
        workspace->staging_gateway_size,
        &completed.gateway_emit);
    if (completed.gateway_emit_status != (uint32_t)DB_GATEWAY_EMIT_OK) {
        return DB_WIN32_PE_STARTUP_GATEWAY_EMIT_FAILED;
    }
    if (completed.gateway_emit.bytes_written > config->gateway_size) {
        return DB_WIN32_PE_STARTUP_WORKSPACE_TOO_SMALL;
    }

    completed.iat_plan_status = (uint32_t)db_iat_plan_patches(
        workspace->staging_image,
        workspace->staging_image_size,
        workspace->imports,
        completed.imports.record_count,
        workspace->bindings,
        completed.bind.binding_count,
        workspace->targets,
        completed.gateway_plan.target_count,
        workspace->patches,
        workspace->patch_capacity,
        &completed.iat_plan);
    if (completed.iat_plan_status != (uint32_t)DB_IAT_PLAN_OK) {
        return DB_WIN32_PE_STARTUP_IAT_PLAN_FAILED;
    }

    completed.iat_apply_status = (uint32_t)db_iat_apply_patches(
        workspace->staging_image,
        workspace->staging_image_size,
        config->page_size,
        workspace->staging_page_plan,
        workspace->staging_page_capacity,
        workspace->patches,
        completed.iat_plan.patch_count,
        &completed.iat_apply);
    if (completed.iat_apply_status != (uint32_t)DB_IAT_APPLY_OK) {
        return DB_WIN32_PE_STARTUP_IAT_APPLY_FAILED;
    }

    if (!db_win32_pe_startup_add(config->loaded_base,
                                 completed.pe.entry_rva,
                                 &entry_rip)) {
        return DB_WIN32_PE_STARTUP_BAD_ENTRY;
    }

    memset(&planned_scalar, 0, sizeof(planned_scalar));
    planned_scalar.rip = entry_rip;
    planned_scalar.registers[4] = config->initial_rsp;
    planned_scalar.rflags = DB_X64_RFLAGS_FIXED_ONE;

    memset(&planned_code, 0, sizeof(planned_code));
    planned_code.bytes = (const uint8_t *)config->image;
    planned_code.size = completed.pe.image_size;
    planned_code.guest_base = config->loaded_base;

    completed.loaded_base = config->loaded_base;
    completed.entry_rip = entry_rip;
    completed.initial_rsp = config->initial_rsp;
    completed.image_bytes_committed = completed.pe.image_size;
    completed.page_entries_committed = planned_mapped.page_count;
    completed.gateway_bytes_committed = completed.gateway_emit.bytes_written;
    completed.format_version = DB_WIN32_PE_STARTUP_FORMAT_VERSION;

    memcpy(config->image,
           workspace->staging_image,
           completed.image_bytes_committed);
    memcpy(config->page_plan,
           workspace->staging_page_plan,
           completed.page_entries_committed * sizeof(*config->page_plan));
    memcpy(config->gateway,
           workspace->staging_gateway,
           completed.gateway_bytes_committed);

    *mapped = planned_mapped;
    *scalar = planned_scalar;
    *code = planned_code;
    *summary = completed;
    return DB_WIN32_PE_STARTUP_OK;
}

const char *db_win32_pe_startup_status_name(
    db_win32_pe_startup_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflow",
        "buffer overlap",
        "bad configuration",
        "bad guest stack",
        "PE verification failed",
        "workspace too small",
        "image mapping failed",
        "relocation planning failed",
        "relocation application failed",
        "provider table derivation failed",
        "import planning failed",
        "import binding failed",
        "gateway planning failed",
        "gateway emission failed",
        "IAT planning failed",
        "IAT application failed",
        "bad entry address"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown Win32 PE startup status";
    }
    return names[(size_t)status];
}
