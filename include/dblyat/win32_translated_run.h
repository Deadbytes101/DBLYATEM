#ifndef DBLYAT_WIN32_TRANSLATED_RUN_H
#define DBLYAT_WIN32_TRANSLATED_RUN_H

#include "dblyat/arm64_block_cache.h"
#include "dblyat/win32_translated_execution.h"

#include <stddef.h>
#include <stdint.h>

#define DB_WIN32_TRANSLATED_RUN_FORMAT_VERSION UINT32_C(1)

typedef enum db_win32_translated_run_status {
    DB_WIN32_TRANSLATED_RUN_OK = 0,
    DB_WIN32_TRANSLATED_RUN_NULL_ARGUMENT,
    DB_WIN32_TRANSLATED_RUN_COUNT_OVERFLOW,
    DB_WIN32_TRANSLATED_RUN_BUFFER_OVERLAP,
    DB_WIN32_TRANSLATED_RUN_BAD_CONFIG,
    DB_WIN32_TRANSLATED_RUN_BAD_BLOCK_BUDGET,
    DB_WIN32_TRANSLATED_RUN_BAD_SCALAR_STATE,
    DB_WIN32_TRANSLATED_RUN_CACHE_FAILED,
    DB_WIN32_TRANSLATED_RUN_EXECUTION_FAILED,
    DB_WIN32_TRANSLATED_RUN_BAD_EXECUTION_OUTCOME
} db_win32_translated_run_status;

typedef enum db_win32_translated_run_stop_reason {
    DB_WIN32_TRANSLATED_RUN_STOP_BLOCK_BUDGET = 0,
    DB_WIN32_TRANSLATED_RUN_STOP_CACHE_MISS,
    DB_WIN32_TRANSLATED_RUN_STOP_GUEST_TRAP,
    DB_WIN32_TRANSLATED_RUN_STOP_GUEST_TERMINATED
} db_win32_translated_run_stop_reason;

typedef struct db_win32_translated_run_config {
    const db_arm64_block_cache *cache;
    const void *gateway_buffer;
    const db_iat_target *targets;
    const db_gateway_dispatch_handler *handlers;
    const db_gateway_frame_signature *signatures;
    const void *guest_stack;
    size_t gateway_buffer_size;
    size_t target_count;
    size_t handler_count;
    size_t signature_count;
    size_t guest_stack_size;
    uint64_t source_generation;
    uint64_t translation_key;
    uint64_t gateway_base;
    uint64_t slot_size;
    uint64_t guest_stack_base;
    uint32_t block_budget;
    uint32_t format_version;
    uint8_t reserved[8];
} db_win32_translated_run_config;

typedef struct db_win32_translated_run_summary {
    db_arm64_block_cache_result last_lookup;
    db_win32_translated_execution_summary last_execution;
    uint64_t source_rip;
    uint64_t final_rip;
    uint64_t source_generation;
    uint64_t translation_key;
    uint32_t block_budget;
    uint32_t lookup_count;
    uint32_t completed_blocks;
    uint32_t stop_reason;
    uint32_t trap;
    uint32_t exit_code;
    uint32_t last_cache_status;
    uint32_t last_execution_status;
    uint32_t format_version;
    uint8_t reserved[4];
} db_win32_translated_run_summary;

db_win32_translated_run_status db_win32_translated_run(
    const db_win32_translated_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_run_summary *summary);

const char *db_win32_translated_run_status_name(
    db_win32_translated_run_status status);

const char *db_win32_translated_run_stop_reason_name(
    db_win32_translated_run_stop_reason reason);

#endif
