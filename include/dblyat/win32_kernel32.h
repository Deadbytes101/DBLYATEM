#ifndef DBLYAT_WIN32_KERNEL32_H
#define DBLYAT_WIN32_KERNEL32_H

#include "dblyat/win32_provider.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_win32_kernel32_status {
    DB_WIN32_KERNEL32_OK = 0,
    DB_WIN32_KERNEL32_NULL_ARGUMENT,
    DB_WIN32_KERNEL32_COUNT_OVERFLOW,
    DB_WIN32_KERNEL32_BUFFER_OVERLAP,
    DB_WIN32_KERNEL32_BAD_STATE,
    DB_WIN32_KERNEL32_BAD_HANDLER,
    DB_WIN32_KERNEL32_ARGUMENT_COUNT_MISMATCH,
    DB_WIN32_KERNEL32_ARGUMENT_OUT_OF_RANGE
} db_win32_kernel32_status;

typedef struct db_win32_kernel32_state {
    uint32_t process_id;
    uint32_t thread_id;
    uint32_t last_error;
    uint32_t reserved;
} db_win32_kernel32_state;

typedef struct db_win32_kernel32_result {
    uint64_t raw_result;
    uint32_t handler_id;
    uint32_t reserved;
} db_win32_kernel32_result;

db_win32_kernel32_status db_win32_kernel32_execute(
    db_win32_kernel32_state *state,
    uint32_t handler_id,
    const uint64_t *arguments,
    size_t argument_count,
    db_win32_kernel32_result *result);

const char *db_win32_kernel32_status_name(db_win32_kernel32_status status);

#endif
