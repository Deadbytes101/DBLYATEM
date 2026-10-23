#ifndef DBLYAT_WIN32_TRANSLATED_EXECUTION_H
#define DBLYAT_WIN32_TRANSLATED_EXECUTION_H

#include "dblyat/arm64_published_block.h"
#include "dblyat/linux_arm64_translation_entry.h"
#include "dblyat/win32_translated_gateway_bridge.h"

#include <stddef.h>
#include <stdint.h>

#define DB_WIN32_TRANSLATED_EXECUTION_FORMAT_VERSION UINT32_C(1)

typedef enum db_win32_translated_execution_outcome {
    DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_NONE = 0,
    DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
    DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP,
    DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED
} db_win32_translated_execution_outcome;

typedef enum db_win32_translated_execution_status {
    DB_WIN32_TRANSLATED_EXECUTION_OK = 0,
    DB_WIN32_TRANSLATED_EXECUTION_NULL_ARGUMENT,
    DB_WIN32_TRANSLATED_EXECUTION_COUNT_OVERFLOW,
    DB_WIN32_TRANSLATED_EXECUTION_BUFFER_OVERLAP,
    DB_WIN32_TRANSLATED_EXECUTION_BAD_PUBLISHED_BLOCK,
    DB_WIN32_TRANSLATED_EXECUTION_BAD_SCALAR_STATE,
    DB_WIN32_TRANSLATED_EXECUTION_BAD_TERMINAL_EXIT,
    DB_WIN32_TRANSLATED_EXECUTION_ENTRY_FAILED,
    DB_WIN32_TRANSLATED_EXECUTION_GATEWAY_FAILED,
    DB_WIN32_TRANSLATED_EXECUTION_BAD_GATEWAY_OUTCOME,
    DB_WIN32_TRANSLATED_EXECUTION_BAD_GUEST_MEMORY
} db_win32_translated_execution_status;

typedef struct db_win32_translated_execution_summary {
    db_linux_arm64_translation_entry_audit entry_audit;
    db_translated_terminal_exit observed_exit;
    db_win32_translated_gateway_bridge_summary gateway;
    uint64_t next_rip;
    uint32_t outcome;
    uint32_t trap;
    uint32_t exit_code;
    uint32_t format_version;
    uint8_t entry_executed;
    uint8_t gateway_attempted;
    uint8_t gateway_consumed;
    uint8_t reserved[5];
} db_win32_translated_execution_summary;

db_win32_translated_execution_status
 db_win32_translated_execution_resolve_exit(
    const db_translated_terminal_exit *observed_exit,
    const void *gateway_buffer,
    size_t gateway_buffer_size,
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t guest_stack_base,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_execution_summary *summary);

db_win32_translated_execution_status
 db_win32_translated_execution_step_with_guest_memory(
    const db_arm64_published_block *published,
    const db_linux_arm64_guest_memory *guest_memory,
    const void *gateway_buffer,
    size_t gateway_buffer_size,
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t guest_stack_base,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_execution_summary *summary);

db_win32_translated_execution_status
 db_win32_translated_execution_step(
    const db_arm64_published_block *published,
    const void *gateway_buffer,
    size_t gateway_buffer_size,
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t guest_stack_base,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_execution_summary *summary);

const char *db_win32_translated_execution_status_name(
    db_win32_translated_execution_status status);

const char *db_win32_translated_execution_outcome_name(
    db_win32_translated_execution_outcome outcome);

#endif
