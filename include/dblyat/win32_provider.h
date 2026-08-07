#ifndef DBLYAT_WIN32_PROVIDER_H
#define DBLYAT_WIN32_PROVIDER_H

#include <stddef.h>
#include <stdint.h>

#define DB_WIN32_PROVIDER_MODULE_CAPACITY 32u
#define DB_WIN32_PROVIDER_SYMBOL_CAPACITY 64u
#define DB_WIN32_PROVIDER_MAX_ARGUMENTS 64u

typedef uint16_t db_win32_provider_flags;

enum {
    DB_WIN32_PROVIDER_FLAG_NONE = 0u,
    DB_WIN32_PROVIDER_FLAG_PROCESS_STATE = 1u << 0u,
    DB_WIN32_PROVIDER_FLAG_THREAD_STATE = 1u << 1u,
    DB_WIN32_PROVIDER_FLAG_MAY_BLOCK = 1u << 2u
};

enum {
    DB_WIN32_PROVIDER_KERNEL32 = 1u,

    DB_WIN32_SYMBOL_KERNEL32_GET_CURRENT_PROCESS_ID = 1u,
    DB_WIN32_SYMBOL_KERNEL32_GET_CURRENT_THREAD_ID = 2u,
    DB_WIN32_SYMBOL_KERNEL32_GET_LAST_ERROR = 3u,
    DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR = 4u,
    DB_WIN32_SYMBOL_KERNEL32_EXIT_PROCESS = 5u,

    DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_PROCESS_ID = 0x1001u,
    DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_THREAD_ID = 0x1002u,
    DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR = 0x1003u,
    DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR = 0x1004u,
    DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS = 0x1005u
};

typedef enum db_win32_provider_status {
    DB_WIN32_PROVIDER_OK = 0,
    DB_WIN32_PROVIDER_NULL_ARGUMENT,
    DB_WIN32_PROVIDER_COUNT_OVERFLOW,
    DB_WIN32_PROVIDER_BUFFER_OVERLAP,
    DB_WIN32_PROVIDER_INVALID_QUERY,
    DB_WIN32_PROVIDER_BAD_RECORD,
    DB_WIN32_PROVIDER_DUPLICATE_NAME,
    DB_WIN32_PROVIDER_DUPLICATE_KEY,
    DB_WIN32_PROVIDER_DUPLICATE_HANDLER_ID,
    DB_WIN32_PROVIDER_NOT_FOUND
} db_win32_provider_status;

typedef struct db_win32_provider_record {
    char module[DB_WIN32_PROVIDER_MODULE_CAPACITY];
    char symbol[DB_WIN32_PROVIDER_SYMBOL_CAPACITY];
    uint32_t provider_id;
    uint32_t symbol_id;
    uint32_t handler_id;
    uint16_t argument_count;
    db_win32_provider_flags flags;
    uint32_t reserved;
} db_win32_provider_record;

typedef struct db_win32_provider_match {
    size_t catalog_index;
    uint32_t provider_id;
    uint32_t symbol_id;
    uint32_t handler_id;
    uint16_t argument_count;
    db_win32_provider_flags flags;
    uint32_t reserved;
} db_win32_provider_match;

const db_win32_provider_record *db_win32_kernel32_catalog(size_t *count);

db_win32_provider_status db_win32_provider_lookup(
    const db_win32_provider_record *catalog,
    size_t catalog_count,
    const char *module,
    size_t module_length,
    const char *symbol,
    size_t symbol_length,
    db_win32_provider_match *match);

const char *db_win32_provider_status_name(db_win32_provider_status status);

#endif
