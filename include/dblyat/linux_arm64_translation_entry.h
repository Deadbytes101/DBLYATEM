#ifndef DBLYAT_LINUX_ARM64_TRANSLATION_ENTRY_H
#define DBLYAT_LINUX_ARM64_TRANSLATION_ENTRY_H

#include "dblyat/x64_interpret.h"

#include <stdint.h>

#define DB_LINUX_ARM64_TRANSLATION_ENTRY_FORMAT_VERSION UINT32_C(1)
#define DB_LINUX_ARM64_TRANSLATION_ENTRY_AUDIT_VERSION UINT32_C(1)

typedef struct db_linux_arm64_translation_context {
    uint64_t terminal_return_count;
    uint32_t format_version;
    uint8_t reserved[4];
} db_linux_arm64_translation_context;

typedef enum db_linux_arm64_translation_entry_status {
    DB_LINUX_ARM64_TRANSLATION_ENTRY_OK = 0,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_NULL_ARGUMENT,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_BUFFER_OVERLAP,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_ENTRY_ADDRESS,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_CONTEXT,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_UNSUPPORTED_HOST,
    DB_LINUX_ARM64_TRANSLATION_ENTRY_RETURN_NOT_OBSERVED
} db_linux_arm64_translation_entry_status;

typedef struct db_linux_arm64_translation_entry_audit {
    uint64_t entry_address;
    uint64_t state_address;
    uint64_t context_address;
    uint64_t return_helper_address;
    uint64_t terminal_return_count_before;
    uint64_t terminal_return_count_after;
    uint32_t version;
    db_linux_arm64_translation_entry_status status;
    uint8_t entry_invoked;
    uint8_t terminal_return_observed;
    uint8_t reserved[6];
} db_linux_arm64_translation_entry_audit;

db_linux_arm64_translation_entry_status
 db_linux_arm64_translation_context_init(
    db_linux_arm64_translation_context *context);

db_linux_arm64_translation_entry_status
 db_linux_arm64_translation_enter(
    uint64_t entry_address,
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context,
    db_linux_arm64_translation_entry_audit *audit);

uint64_t db_linux_arm64_translation_return_helper_address(void);

const char *db_linux_arm64_translation_entry_status_name(
    db_linux_arm64_translation_entry_status status);

#endif
