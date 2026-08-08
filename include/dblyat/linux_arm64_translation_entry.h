#ifndef DBLYAT_LINUX_ARM64_TRANSLATION_ENTRY_H
#define DBLYAT_LINUX_ARM64_TRANSLATION_ENTRY_H

#include "dblyat/linux_arm64_guest_runtime.h"
#include "dblyat/translated_terminal_exit.h"
#include "dblyat/x64_interpret.h"

#include <stdint.h>

#define DB_LINUX_ARM64_TRANSLATION_ENTRY_FORMAT_VERSION UINT32_C(3)
#define DB_LINUX_ARM64_TRANSLATION_ENTRY_AUDIT_VERSION UINT32_C(2)

typedef enum db_linux_arm64_helper_fault {
    DB_LINUX_ARM64_HELPER_FAULT_NONE = 0,
    DB_LINUX_ARM64_HELPER_FAULT_BAD_OPERAND,
    DB_LINUX_ARM64_HELPER_FAULT_ADDRESS,
    DB_LINUX_ARM64_HELPER_FAULT_READ,
    DB_LINUX_ARM64_HELPER_FAULT_WRITE,
    DB_LINUX_ARM64_HELPER_FAULT_STACK,
    DB_LINUX_ARM64_HELPER_FAULT_UNBOUND_MEMORY
} db_linux_arm64_helper_fault;

typedef struct db_linux_arm64_translation_context {
    uint64_t terminal_return_count;
    db_translated_terminal_exit planned_exit;
    db_translated_terminal_exit observed_exit;
    uint32_t format_version;
    uint8_t reserved[4];
    db_linux_arm64_guest_memory guest_memory;
    uint64_t helper_fault_address;
    uint32_t helper_fault;
    uint32_t helper_access;
    uint8_t guest_memory_bound;
    uint8_t helper_reserved[7];
} db_linux_arm64_translation_context;

typedef enum db_linux_arm64_translation_entry_status {
    DB_LINUX_ARM64_TRANSLATION_ENTRY_OK = 0,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_NULL_ARGUMENT,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_BUFFER_OVERLAP,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_ENTRY_ADDRESS,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_CONTEXT,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_UNSUPPORTED_HOST,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_RETURN_NOT_OBSERVED,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_TERMINAL_EXIT,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_TERMINAL_EXIT_MISMATCH,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_GUEST_MEMORY,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_HELPER_FAILED
} db_linux_arm64_translation_entry_status;

typedef struct db_linux_arm64_translation_entry_audit {
    uint64_t entry_address;
    uint64_t state_address;
    uint64_t context_address;
    uint64_t return_helper_address;
    uint64_t terminal_return_count_before;
    uint64_t terminal_return_count_after;
    uint32_t planned_kind;
    uint32_t observed_kind;
    uint32_t version;
    db_linux_arm64_translation_entry_status status;
    uint8_t entry_invoked;
    uint8_t terminal_return_observed;
    uint8_t terminal_exit_matched;
    uint8_t reserved[5];
} db_linux_arm64_translation_entry_audit;

db_linux_arm64_translation_entry_status
 db_linux_arm64_translation_context_init(
    db_linux_arm64_translation_context *context);

db_linux_arm64_translation_entry_status
 db_linux_arm64_translation_context_bind_terminal(
    db_linux_arm64_translation_context *context,
    const db_translated_terminal_exit *planned_exit);

db_linux_arm64_translation_entry_status
 db_linux_arm64_translation_context_bind_guest_memory(
    db_linux_arm64_translation_context *context,
    const db_linux_arm64_guest_memory *memory);

db_linux_arm64_translation_entry_status
 db_linux_arm64_translation_enter(
    uint64_t entry_address,
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context,
    db_linux_arm64_translation_entry_audit *audit);

uint64_t db_linux_arm64_translation_terminal_helper_address(
    db_translated_terminal_exit_kind kind);

uint64_t db_linux_arm64_translation_return_helper_address(void);

const char *db_linux_arm64_translation_entry_status_name(
    db_linux_arm64_translation_entry_status status);

#endif
