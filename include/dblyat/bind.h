#ifndef DBLYAT_BIND_H
#define DBLYAT_BIND_H

#include "dblyat/import.h"

#include <stddef.h>
#include <stdint.h>

typedef uint8_t db_bind_symbol_flags;

enum {
    DB_BIND_SYMBOL_HAS_NAME = 1u,
    DB_BIND_SYMBOL_HAS_ORDINAL = 2u
};

typedef enum db_bind_status {
    DB_BIND_OK = 0,
    DB_BIND_NULL_ARGUMENT,
    DB_BIND_COUNT_OVERFLOW,
    DB_BIND_BUFFER_OVERLAP,
    DB_BIND_BAD_MAPPED_IMAGE,
    DB_BIND_BAD_IMPORT_LAYOUT,
    DB_BIND_IMPORT_MODULE_NAME_INVALID,
    DB_BIND_IMPORT_SYMBOL_NAME_INVALID,
    DB_BIND_PROVIDER_MODULE_NAME_INVALID,
    DB_BIND_PROVIDER_ID_INVALID,
    DB_BIND_PROVIDER_SYMBOL_INVALID,
    DB_BIND_SYMBOL_ID_INVALID,
    DB_BIND_DUPLICATE_PROVIDER_MODULE,
    DB_BIND_DUPLICATE_PROVIDER_ID,
    DB_BIND_DUPLICATE_SYMBOL_NAME,
    DB_BIND_DUPLICATE_SYMBOL_ORDINAL,
    DB_BIND_AMBIGUOUS_SYMBOL_ALIAS,
    DB_BIND_MODULE_NOT_FOUND,
    DB_BIND_SYMBOL_NOT_FOUND,
    DB_BIND_OUTPUT_BUFFER_TOO_SMALL
} db_bind_status;

typedef struct db_bind_provider_symbol {
    const char *name;
    size_t name_length;
    uint32_t symbol_id;
    uint16_t ordinal;
    db_bind_symbol_flags flags;
    uint8_t reserved;
} db_bind_provider_symbol;

typedef struct db_bind_provider {
    const char *module_name;
    size_t module_name_length;
    const db_bind_provider_symbol *symbols;
    size_t symbol_count;
    uint32_t provider_id;
    uint32_t reserved;
} db_bind_provider;

typedef struct db_import_binding {
    uint32_t module_index;
    uint32_t provider_id;
    uint32_t symbol_id;
    uint32_t reserved;
} db_import_binding;

typedef struct db_bind_summary {
    size_t module_count;
    size_t record_count;
    size_t binding_count;
} db_bind_summary;

/*
 * Module names use an explicit ASCII A-Z to a-z fold. No path, suffix,
 * locale, or Unicode rewriting is performed. Symbol names are byte-exact.
 */
db_bind_status db_bind_plan_imports(
    const void *mapped_image,
    size_t mapped_size,
    const db_import_module *modules,
    size_t module_count,
    const db_import_record *records,
    size_t record_count,
    const db_bind_provider *providers,
    size_t provider_count,
    db_import_binding *bindings,
    size_t binding_capacity,
    db_bind_summary *summary);

const char *db_bind_status_name(db_bind_status status);

#endif
