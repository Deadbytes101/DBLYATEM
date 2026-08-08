#ifndef DBLYAT_WIN32_PROVIDER_TABLES_H
#define DBLYAT_WIN32_PROVIDER_TABLES_H

#include "dblyat/bind.h"
#include "dblyat/gateway_frame.h"
#include "dblyat/win32_provider.h"

#include <stddef.h>
#include <stdint.h>

#define DB_WIN32_PROVIDER_TABLES_FORMAT_VERSION UINT32_C(1)

typedef enum db_win32_provider_tables_status {
    DB_WIN32_PROVIDER_TABLES_OK = 0,
    DB_WIN32_PROVIDER_TABLES_NULL_ARGUMENT,
    DB_WIN32_PROVIDER_TABLES_COUNT_OVERFLOW,
    DB_WIN32_PROVIDER_TABLES_BUFFER_OVERLAP,
    DB_WIN32_PROVIDER_TABLES_BAD_CATALOG,
    DB_WIN32_PROVIDER_TABLES_PROVIDER_CAPACITY,
    DB_WIN32_PROVIDER_TABLES_SYMBOL_CAPACITY,
    DB_WIN32_PROVIDER_TABLES_HANDLER_CAPACITY,
    DB_WIN32_PROVIDER_TABLES_SIGNATURE_CAPACITY,
    DB_WIN32_PROVIDER_TABLES_INCONSISTENT_PROVIDER
} db_win32_provider_tables_status;

typedef struct db_win32_provider_tables_summary {
    size_t catalog_count;
    size_t provider_count;
    size_t symbol_count;
    size_t handler_count;
    size_t signature_count;
    uint32_t format_version;
    uint8_t reserved[4];
} db_win32_provider_tables_summary;

db_win32_provider_tables_status db_win32_provider_tables_build(
    const db_win32_provider_record *catalog,
    size_t catalog_count,
    db_bind_provider *providers,
    size_t provider_capacity,
    db_bind_provider_symbol *symbols,
    size_t symbol_capacity,
    db_gateway_dispatch_handler *handlers,
    size_t handler_capacity,
    db_gateway_frame_signature *signatures,
    size_t signature_capacity,
    db_win32_provider_tables_summary *summary);

const char *db_win32_provider_tables_status_name(
    db_win32_provider_tables_status status);

#endif
