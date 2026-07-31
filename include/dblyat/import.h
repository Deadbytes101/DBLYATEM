#ifndef DBLYAT_IMPORT_H
#define DBLYAT_IMPORT_H

#include "dblyat/map.h"

#include <stddef.h>
#include <stdint.h>

typedef uint8_t db_import_kind;

enum {
    DB_IMPORT_BY_NAME = 0u,
    DB_IMPORT_BY_ORDINAL = 1u
};

typedef enum db_import_status {
    DB_IMPORT_OK = 0,
    DB_IMPORT_NULL_ARGUMENT,
    DB_IMPORT_INVALID_PAGE_SIZE,
    DB_IMPORT_BAD_MAPPED_IMAGE,
    DB_IMPORT_PAGE_PLAN_TOO_SMALL,
    DB_IMPORT_BAD_PAGE_PLAN,
    DB_IMPORT_MODULE_CAPACITY_OVERFLOW,
    DB_IMPORT_RECORD_CAPACITY_OVERFLOW,
    DB_IMPORT_BUFFER_OVERLAP,
    DB_IMPORT_BAD_DIRECTORY,
    DB_IMPORT_DIRECTORY_OUT_OF_RANGE,
    DB_IMPORT_DIRECTORY_UNMAPPED,
    DB_IMPORT_TOO_MANY_MODULES,
    DB_IMPORT_BAD_DESCRIPTOR,
    DB_IMPORT_DESCRIPTOR_UNTERMINATED,
    DB_IMPORT_DLL_NAME_UNMAPPED,
    DB_IMPORT_DLL_NAME_UNTERMINATED,
    DB_IMPORT_DLL_NAME_INVALID,
    DB_IMPORT_THUNK_UNMAPPED,
    DB_IMPORT_THUNK_UNTERMINATED,
    DB_IMPORT_THUNK_VALUE_INVALID,
    DB_IMPORT_NAME_UNMAPPED,
    DB_IMPORT_NAME_UNTERMINATED,
    DB_IMPORT_NAME_INVALID,
    DB_IMPORT_IAT_OVERLAP,
    DB_IMPORT_MODULE_BUFFER_TOO_SMALL,
    DB_IMPORT_RECORD_BUFFER_TOO_SMALL
} db_import_status;

typedef struct db_import_module {
    uint32_t name_rva;
    uint32_t name_length;
    uint32_t lookup_table_rva;
    uint32_t iat_rva;
    size_t first_record;
    size_t record_count;
} db_import_module;

typedef struct db_import_record {
    uint32_t module_index;
    uint32_t lookup_rva;
    uint32_t iat_rva;
    uint32_t name_rva;
    uint32_t name_length;
    uint16_t hint;
    uint16_t ordinal;
    db_import_kind kind;
    uint8_t reserved[3];
    uint64_t lookup_value;
    uint64_t original_iat_value;
} db_import_record;

typedef struct db_import_summary {
    uint32_t directory_rva;
    uint32_t directory_size;
    size_t module_count;
    size_t record_count;
} db_import_summary;

db_import_status db_import_plan_pe64(
    const void *mapped_image,
    size_t mapped_size,
    const db_mapped_image *mapped,
    size_t page_size,
    const db_map_page_protection *page_plan,
    size_t page_capacity,
    db_import_module *modules,
    size_t module_capacity,
    db_import_record *records,
    size_t record_capacity,
    db_import_summary *summary);

const char *db_import_status_name(db_import_status status);

#endif
