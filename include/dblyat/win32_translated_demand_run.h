#ifndef DBLYAT_WIN32_TRANSLATED_DEMAND_RUN_H
#define DBLYAT_WIN32_TRANSLATED_DEMAND_RUN_H

#include "dblyat/arm64_translation_store.h"
#include "dblyat/win32_translated_run.h"

#include <stddef.h>
#include <stdint.h>

#define DB_WIN32_TRANSLATED_DEMAND_RUN_FORMAT_VERSION UINT32_C(2)

typedef enum db_win32_translated_demand_run_status {
    DB_WIN32_TRANSLATED_DEMAND_RUN_OK = 0,
    DB_WIN32_TRANSLATED_DEMAND_RUN_NULL_ARGUMENT,
    DB_WIN32_TRANSLATED_DEMAND_RUN_COUNT_OVERFLOW,
    DB_WIN32_TRANSLATED_DEMAND_RUN_BUFFER_OVERLAP,
    DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_CONFIG,
    DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_BLOCK_BUDGET,
    DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_SCALAR_STATE,
    DB_WIN32_TRANSLATED_DEMAND_RUN_RUN_FAILED,
    DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_RUN_OUTCOME,
    DB_WIN32_TRANSLATED_DEMAND_RUN_STORE_FAILED,
    DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_STORE_OUTCOME,
    DB_WIN32_TRANSLATED_DEMAND_RUN_FILL_STALLED,
    DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_GUEST_MEMORY,
    DB_WIN32_TRANSLATED_DEMAND_RUN_CODE_NOT_MAPPED
} db_win32_translated_demand_run_status;

typedef enum db_win32_translated_demand_run_stop_reason {
    DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_BLOCK_BUDGET = 0,
    DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TRAP,
    DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TERMINATED
} db_win32_translated_demand_run_stop_reason;

typedef enum db_win32_translated_demand_code_region {
    DB_WIN32_TRANSLATED_DEMAND_CODE_NONE = 0,
    DB_WIN32_TRANSLATED_DEMAND_CODE_IMAGE,
    DB_WIN32_TRANSLATED_DEMAND_CODE_GATEWAY
} db_win32_translated_demand_code_region;

typedef struct db_win32_translated_demand_run_config {
    db_arm64_translation_store *store;
    const db_arm64_translation_store_config *translation;
    db_arm64_translation_workspace *workspace;
    void *image_memory;
    const db_map_page_protection *image_page_plan;
    const void *gateway_buffer;
    const db_iat_target *targets;
    const db_gateway_dispatch_handler *handlers;
    const db_gateway_frame_signature *signatures;
    void *guest_stack;
    size_t image_page_count;
    size_t image_page_size;
    size_t gateway_buffer_size;
    size_t target_count;
    size_t handler_count;
    size_t signature_count;
    size_t guest_stack_size;
    uint64_t gateway_base;
    uint64_t slot_size;
    uint64_t guest_stack_base;
    uint32_t block_budget;
    uint32_t format_version;
    uint8_t reserved[8];
} db_win32_translated_demand_run_config;

typedef struct db_win32_translated_demand_run_summary {
    db_win32_translated_run_summary last_run;
    db_arm64_translation_store_summary last_fill;
    uint64_t source_rip;
    uint64_t final_rip;
    uint64_t source_generation;
    uint64_t translation_key;
    uint32_t block_budget;
    uint32_t run_count;
    uint32_t fill_count;
    uint32_t published_fill_count;
    uint32_t cache_hit_fill_count;
    uint32_t lookup_count;
    uint32_t completed_blocks;
    uint32_t stop_reason;
    uint32_t trap;
    uint32_t exit_code;
    uint32_t last_run_status;
    uint32_t last_store_status;
    uint32_t guest_memory_bound;
    uint32_t image_fill_count;
    uint32_t gateway_fill_count;
    uint32_t last_fill_code_region;
    uint32_t format_version;
    uint8_t reserved[4];
} db_win32_translated_demand_run_summary;

db_win32_translated_demand_run_status db_win32_translated_demand_run(
    const db_win32_translated_demand_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_demand_run_summary *summary);

const char *db_win32_translated_demand_run_status_name(
    db_win32_translated_demand_run_status status);

const char *db_win32_translated_demand_run_stop_reason_name(
    db_win32_translated_demand_run_stop_reason reason);

const char *db_win32_translated_demand_code_region_name(
    db_win32_translated_demand_code_region region);

#endif
