#ifndef DBLYAT_WIN32_PE_STARTUP_H
#define DBLYAT_WIN32_PE_STARTUP_H

#include "dblyat/gateway_emit.h"
#include "dblyat/iat_apply.h"
#include "dblyat/reloc_apply.h"
#include "dblyat/win32_provider_tables.h"
#include "dblyat/x64_interpret.h"
#include "dblyat/x64_run.h"

#include <stddef.h>
#include <stdint.h>

#define DB_WIN32_PE_STARTUP_FORMAT_VERSION UINT32_C(1)

typedef enum db_win32_pe_startup_status {
    DB_WIN32_PE_STARTUP_OK = 0,
    DB_WIN32_PE_STARTUP_NULL_ARGUMENT,
    DB_WIN32_PE_STARTUP_COUNT_OVERFLOW,
    DB_WIN32_PE_STARTUP_BUFFER_OVERLAP,
    DB_WIN32_PE_STARTUP_BAD_CONFIG,
    DB_WIN32_PE_STARTUP_BAD_STACK,
    DB_WIN32_PE_STARTUP_PE_FAILED,
    DB_WIN32_PE_STARTUP_WORKSPACE_TOO_SMALL,
    DB_WIN32_PE_STARTUP_MAP_FAILED,
    DB_WIN32_PE_STARTUP_RELOC_PLAN_FAILED,
    DB_WIN32_PE_STARTUP_RELOC_APPLY_FAILED,
    DB_WIN32_PE_STARTUP_PROVIDER_TABLES_FAILED,
    DB_WIN32_PE_STARTUP_IMPORT_FAILED,
    DB_WIN32_PE_STARTUP_BIND_FAILED,
    DB_WIN32_PE_STARTUP_GATEWAY_PLAN_FAILED,
    DB_WIN32_PE_STARTUP_GATEWAY_EMIT_FAILED,
    DB_WIN32_PE_STARTUP_IAT_PLAN_FAILED,
    DB_WIN32_PE_STARTUP_IAT_APPLY_FAILED,
    DB_WIN32_PE_STARTUP_BAD_ENTRY
} db_win32_pe_startup_status;

typedef struct db_win32_pe_startup_config {
    const void *source;
    void *image;
    db_map_page_protection *page_plan;
    void *gateway;
    size_t source_size;
    size_t image_size;
    size_t page_capacity;
    size_t gateway_size;
    size_t page_size;
    uint64_t loaded_base;
    uint64_t gateway_base;
    uint64_t gateway_region_size;
    uint64_t slot_size;
    uint64_t guest_stack_base;
    size_t guest_stack_size;
    uint64_t initial_rsp;
    uint32_t format_version;
    uint8_t reserved[4];
} db_win32_pe_startup_config;

typedef struct db_win32_pe_startup_workspace {
    void *staging_image;
    db_map_page_protection *staging_page_plan;
    void *staging_gateway;
    db_reloc_record *relocations;
    db_import_module *modules;
    db_import_record *imports;
    db_bind_provider *providers;
    db_bind_provider_symbol *provider_symbols;
    db_gateway_dispatch_handler *handlers;
    db_gateway_frame_signature *signatures;
    db_import_binding *bindings;
    db_iat_target *targets;
    db_iat_patch *patches;
    size_t staging_image_size;
    size_t staging_page_capacity;
    size_t staging_gateway_size;
    size_t relocation_capacity;
    size_t module_capacity;
    size_t import_capacity;
    size_t provider_capacity;
    size_t provider_symbol_capacity;
    size_t handler_capacity;
    size_t signature_capacity;
    size_t binding_capacity;
    size_t target_capacity;
    size_t patch_capacity;
    uint8_t reserved[8];
} db_win32_pe_startup_workspace;

typedef struct db_win32_pe_startup_summary {
    db_pe_image pe;
    db_mapped_image mapped;
    db_reloc_summary reloc_plan;
    db_reloc_apply_result reloc_apply;
    db_win32_provider_tables_summary provider_tables;
    db_import_summary imports;
    db_bind_summary bind;
    db_gateway_plan_summary gateway_plan;
    db_gateway_emit_summary gateway_emit;
    db_iat_plan_summary iat_plan;
    db_iat_apply_summary iat_apply;
    uint64_t loaded_base;
    uint64_t entry_rip;
    uint64_t initial_rsp;
    size_t image_bytes_committed;
    size_t page_entries_committed;
    size_t gateway_bytes_committed;
    uint32_t pe_status;
    uint32_t map_status;
    uint32_t reloc_plan_status;
    uint32_t reloc_apply_status;
    uint32_t provider_tables_status;
    uint32_t import_status;
    uint32_t bind_status;
    uint32_t gateway_plan_status;
    uint32_t gateway_emit_status;
    uint32_t iat_plan_status;
    uint32_t iat_apply_status;
    uint32_t format_version;
    uint8_t reserved[4];
} db_win32_pe_startup_summary;

/*
 * workspace storage must remain alive after success because its derived
 * provider/handler/signature/target tables are runtime inputs for the
 * translated Win32 gateway path. The source provider catalog is the static
 * KERNEL32 catalog owned by win32_provider.c.
 */
db_win32_pe_startup_status db_win32_pe_startup_prepare(
    const db_win32_pe_startup_config *config,
    db_win32_pe_startup_workspace *workspace,
    db_mapped_image *mapped,
    db_x64_scalar_state *scalar,
    db_x64_code_view *code,
    db_win32_pe_startup_summary *summary);

const char *db_win32_pe_startup_status_name(
    db_win32_pe_startup_status status);

#endif
