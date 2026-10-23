#ifndef DBLYAT_WIN32_PE_TRANSLATED_SESSION_H
#define DBLYAT_WIN32_PE_TRANSLATED_SESSION_H

#include "dblyat/win32_pe_startup.h"
#include "dblyat/win32_translated_demand_run.h"

#include <stddef.h>
#include <stdint.h>

#define DB_WIN32_PE_TRANSLATED_SESSION_FORMAT_VERSION UINT32_C(1)
#define DB_WIN32_PE_TRANSLATED_SESSION_RUN_SUMMARY_VERSION UINT32_C(1)

typedef enum db_win32_pe_translated_session_state {
    DB_WIN32_PE_TRANSLATED_SESSION_EMPTY = 0,
    DB_WIN32_PE_TRANSLATED_SESSION_READY,
    DB_WIN32_PE_TRANSLATED_SESSION_TERMINATED,
    DB_WIN32_PE_TRANSLATED_SESSION_CLEANUP_REQUIRED
} db_win32_pe_translated_session_state;

typedef enum db_win32_pe_translated_session_status {
    DB_WIN32_PE_TRANSLATED_SESSION_OK = 0,
    DB_WIN32_PE_TRANSLATED_SESSION_NULL_ARGUMENT,
    DB_WIN32_PE_TRANSLATED_SESSION_COUNT_OVERFLOW,
    DB_WIN32_PE_TRANSLATED_SESSION_BUFFER_OVERLAP,
    DB_WIN32_PE_TRANSLATED_SESSION_BAD_CONFIG,
    DB_WIN32_PE_TRANSLATED_SESSION_BAD_SESSION,
    DB_WIN32_PE_TRANSLATED_SESSION_UNSUPPORTED_HOST,
    DB_WIN32_PE_TRANSLATED_SESSION_BAD_HELPER_TARGETS,
    DB_WIN32_PE_TRANSLATED_SESSION_STORE_INIT_FAILED,
    DB_WIN32_PE_TRANSLATED_SESSION_STARTUP_FAILED,
    DB_WIN32_PE_TRANSLATED_SESSION_BAD_COMPOSITION,
    DB_WIN32_PE_TRANSLATED_SESSION_RUN_FAILED,
    DB_WIN32_PE_TRANSLATED_SESSION_BAD_RUN_OUTCOME,
    DB_WIN32_PE_TRANSLATED_SESSION_ALREADY_TERMINATED,
    DB_WIN32_PE_TRANSLATED_SESSION_DISPOSE_FAILED,
    DB_WIN32_PE_TRANSLATED_SESSION_ROLLBACK_FAILED
} db_win32_pe_translated_session_status;

typedef struct db_win32_pe_translated_session_config {
    const db_win32_pe_startup_config *startup;
    db_win32_pe_startup_workspace *startup_workspace;
    db_arm64_translation_workspace *translation_workspace;
    db_arm64_block_cache_slot *cache_slots;
    db_linux_arm64_wx_handle *cache_handles;
    uint8_t *cache_owned;
    void *guest_stack;
    const db_linux_arm64_wx_platform *wx_platform;
    void *code_base_context;
    db_arm64_translation_code_base_fn reserve_code_base;
    size_t cache_capacity;
    uint64_t source_generation;
    uint64_t translation_key;
    uint64_t translation_page_size;
    uint32_t translation_instruction_budget;
    uint32_t translation_byte_budget;
    uint32_t run_block_budget;
    uint32_t process_id;
    uint32_t thread_id;
    uint32_t format_version;
    uint8_t reserved[4];
} db_win32_pe_translated_session_config;

typedef struct db_win32_pe_translated_session {
    db_arm64_translation_store store;
    db_x64_arm64_helper_targets helper_targets;
    db_arm64_translation_store_config translation;
    db_win32_translated_demand_run_config demand;
    db_mapped_image mapped;
    db_x64_code_view code;
    db_x64_scalar_state scalar;
    db_win32_kernel32_state kernel32;
    db_win32_pe_startup_summary startup;
    db_win32_translated_demand_run_summary last_run;
    const db_linux_arm64_wx_platform *wx_platform;
    uint32_t run_count;
    uint32_t exit_code;
    uint32_t state;
    uint32_t format_version;
    uint8_t reserved[8];
} db_win32_pe_translated_session;

typedef struct db_win32_pe_translated_session_run_summary {
    db_win32_translated_demand_run_summary demand;
    uint64_t source_rip;
    uint64_t final_rip;
    uint32_t session_run_count;
    uint32_t state_before;
    uint32_t state_after;
    uint32_t stop_reason;
    uint32_t trap;
    uint32_t exit_code;
    uint32_t demand_status;
    uint32_t format_version;
    uint8_t reserved[4];
} db_win32_pe_translated_session_run_summary;

void db_win32_pe_translated_session_init(
    db_win32_pe_translated_session *session);

db_win32_pe_translated_session_status
 db_win32_pe_translated_session_prepare(
    const db_win32_pe_translated_session_config *config,
    db_win32_pe_translated_session *session);

db_win32_pe_translated_session_status
 db_win32_pe_translated_session_run(
    db_win32_pe_translated_session *session,
    db_win32_pe_translated_session_run_summary *summary);

db_win32_pe_translated_session_status
 db_win32_pe_translated_session_dispose(
    db_win32_pe_translated_session *session);

const char *db_win32_pe_translated_session_status_name(
    db_win32_pe_translated_session_status status);

const char *db_win32_pe_translated_session_state_name(
    db_win32_pe_translated_session_state state);

#endif
