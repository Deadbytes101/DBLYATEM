#include "dblyat/win32_provider_tables.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_win32_provider_tables_mul(size_t left,
                                         size_t right,
                                         size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }
    *result = left * right;
    return true;
}

static bool db_win32_provider_tables_overlap(const void *left,
                                             size_t left_size,
                                             const void *right,
                                             size_t right_size)
{
    const uintptr_t left_address = (uintptr_t)left;
    const uintptr_t right_address = (uintptr_t)right;

    if (left_size == 0u || right_size == 0u) {
        return false;
    }
    if (left_address <= right_address) {
        return right_address - left_address < left_size;
    }
    return left_address - right_address < right_size;
}

static bool db_win32_provider_tables_fixed_length(const char *text,
                                                  size_t capacity,
                                                  size_t *length)
{
    size_t index;

    for (index = 0u; index < capacity; ++index) {
        if (text[index] == '\0') {
            if (index == 0u) {
                return false;
            }
            *length = index;
            return true;
        }
    }
    return false;
}

static unsigned char db_win32_provider_tables_fold(unsigned char value)
{
    if (value >= (unsigned char)'A' && value <= (unsigned char)'Z') {
        return (unsigned char)(value + ((unsigned char)'a' -
                                       (unsigned char)'A'));
    }
    return value;
}

static bool db_win32_provider_tables_module_equal(const char *left,
                                                   size_t left_length,
                                                   const char *right,
                                                   size_t right_length)
{
    size_t index;

    if (left_length != right_length) {
        return false;
    }
    for (index = 0u; index < left_length; ++index) {
        if (db_win32_provider_tables_fold((unsigned char)left[index]) !=
            db_win32_provider_tables_fold((unsigned char)right[index])) {
            return false;
        }
    }
    return true;
}

static db_win32_provider_tables_status db_win32_provider_tables_validate_catalog(
    const db_win32_provider_record *catalog,
    size_t catalog_count,
    size_t *module_lengths,
    size_t *symbol_lengths)
{
    size_t index;

    for (index = 0u; index < catalog_count; ++index) {
        char module[DB_WIN32_PROVIDER_MODULE_CAPACITY];
        char symbol[DB_WIN32_PROVIDER_SYMBOL_CAPACITY];
        db_win32_provider_match match;
        db_win32_provider_status status;
        size_t cursor;

        if (!db_win32_provider_tables_fixed_length(
                catalog[index].module,
                sizeof(catalog[index].module),
                &module_lengths[index]) ||
            !db_win32_provider_tables_fixed_length(
                catalog[index].symbol,
                sizeof(catalog[index].symbol),
                &symbol_lengths[index])) {
            return DB_WIN32_PROVIDER_TABLES_BAD_CATALOG;
        }

        for (cursor = 0u; cursor < sizeof(module); ++cursor) {
            module[cursor] = catalog[index].module[cursor];
        }
        for (cursor = 0u; cursor < sizeof(symbol); ++cursor) {
            symbol[cursor] = catalog[index].symbol[cursor];
        }

        status = db_win32_provider_lookup(catalog,
                                          catalog_count,
                                          module,
                                          module_lengths[index],
                                          symbol,
                                          symbol_lengths[index],
                                          &match);
        if (status != DB_WIN32_PROVIDER_OK ||
            match.catalog_index != index ||
            match.provider_id != catalog[index].provider_id ||
            match.symbol_id != catalog[index].symbol_id ||
            match.handler_id != catalog[index].handler_id ||
            match.argument_count != catalog[index].argument_count ||
            match.flags != catalog[index].flags ||
            match.reserved != 0u) {
            return DB_WIN32_PROVIDER_TABLES_BAD_CATALOG;
        }
    }

    return DB_WIN32_PROVIDER_TABLES_OK;
}

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
    db_win32_provider_tables_summary *summary)
{
    size_t module_lengths[DB_WIN32_PROVIDER_MAX_ARGUMENTS];
    size_t symbol_lengths[DB_WIN32_PROVIDER_MAX_ARGUMENTS];
    size_t provider_first[DB_WIN32_PROVIDER_MAX_ARGUMENTS];
    size_t provider_symbol_count[DB_WIN32_PROVIDER_MAX_ARGUMENTS];
    size_t provider_symbol_offset[DB_WIN32_PROVIDER_MAX_ARGUMENTS];
    uint32_t provider_ids[DB_WIN32_PROVIDER_MAX_ARGUMENTS];
    db_win32_provider_tables_summary completed;
    size_t catalog_bytes;
    size_t provider_bytes;
    size_t symbol_bytes;
    size_t handler_bytes;
    size_t signature_bytes;
    size_t provider_count = 0u;
    size_t index;
    size_t symbol_cursor = 0u;
    db_win32_provider_tables_status status;

    if (catalog == NULL || providers == NULL || symbols == NULL ||
        handlers == NULL || signatures == NULL || summary == NULL) {
        return DB_WIN32_PROVIDER_TABLES_NULL_ARGUMENT;
    }
    if (catalog_count == 0u ||
        catalog_count > DB_WIN32_PROVIDER_MAX_ARGUMENTS ||
        !db_win32_provider_tables_mul(catalog_count,
                                      sizeof(*catalog),
                                      &catalog_bytes) ||
        !db_win32_provider_tables_mul(provider_capacity,
                                      sizeof(*providers),
                                      &provider_bytes) ||
        !db_win32_provider_tables_mul(symbol_capacity,
                                      sizeof(*symbols),
                                      &symbol_bytes) ||
        !db_win32_provider_tables_mul(handler_capacity,
                                      sizeof(*handlers),
                                      &handler_bytes) ||
        !db_win32_provider_tables_mul(signature_capacity,
                                      sizeof(*signatures),
                                      &signature_bytes)) {
        return DB_WIN32_PROVIDER_TABLES_COUNT_OVERFLOW;
    }

    if (db_win32_provider_tables_overlap(catalog, catalog_bytes,
                                         providers, provider_bytes) ||
        db_win32_provider_tables_overlap(catalog, catalog_bytes,
                                         symbols, symbol_bytes) ||
        db_win32_provider_tables_overlap(catalog, catalog_bytes,
                                         handlers, handler_bytes) ||
        db_win32_provider_tables_overlap(catalog, catalog_bytes,
                                         signatures, signature_bytes) ||
        db_win32_provider_tables_overlap(catalog, catalog_bytes,
                                         summary, sizeof(*summary)) ||
        db_win32_provider_tables_overlap(providers, provider_bytes,
                                         symbols, symbol_bytes) ||
        db_win32_provider_tables_overlap(providers, provider_bytes,
                                         handlers, handler_bytes) ||
        db_win32_provider_tables_overlap(providers, provider_bytes,
                                         signatures, signature_bytes) ||
        db_win32_provider_tables_overlap(providers, provider_bytes,
                                         summary, sizeof(*summary)) ||
        db_win32_provider_tables_overlap(symbols, symbol_bytes,
                                         handlers, handler_bytes) ||
        db_win32_provider_tables_overlap(symbols, symbol_bytes,
                                         signatures, signature_bytes) ||
        db_win32_provider_tables_overlap(symbols, symbol_bytes,
                                         summary, sizeof(*summary)) ||
        db_win32_provider_tables_overlap(handlers, handler_bytes,
                                         signatures, signature_bytes) ||
        db_win32_provider_tables_overlap(handlers, handler_bytes,
                                         summary, sizeof(*summary)) ||
        db_win32_provider_tables_overlap(signatures, signature_bytes,
                                         summary, sizeof(*summary))) {
        return DB_WIN32_PROVIDER_TABLES_BUFFER_OVERLAP;
    }

    status = db_win32_provider_tables_validate_catalog(catalog,
                                                       catalog_count,
                                                       module_lengths,
                                                       symbol_lengths);
    if (status != DB_WIN32_PROVIDER_TABLES_OK) {
        return status;
    }

    for (index = 0u; index < catalog_count; ++index) {
        size_t provider_index = provider_count;
        size_t other;

        for (other = 0u; other < provider_count; ++other) {
            if (provider_ids[other] == catalog[index].provider_id) {
                provider_index = other;
                break;
            }
        }

        if (provider_index == provider_count) {
            for (other = 0u; other < provider_count; ++other) {
                const size_t first = provider_first[other];
                if (db_win32_provider_tables_module_equal(
                        catalog[first].module,
                        module_lengths[first],
                        catalog[index].module,
                        module_lengths[index])) {
                    return DB_WIN32_PROVIDER_TABLES_INCONSISTENT_PROVIDER;
                }
            }
            provider_ids[provider_count] = catalog[index].provider_id;
            provider_first[provider_count] = index;
            provider_symbol_count[provider_count] = 1u;
            ++provider_count;
        } else {
            const size_t first = provider_first[provider_index];
            if (!db_win32_provider_tables_module_equal(
                    catalog[first].module,
                    module_lengths[first],
                    catalog[index].module,
                    module_lengths[index])) {
                return DB_WIN32_PROVIDER_TABLES_INCONSISTENT_PROVIDER;
            }
            ++provider_symbol_count[provider_index];
        }
    }

    if (provider_count > provider_capacity) {
        return DB_WIN32_PROVIDER_TABLES_PROVIDER_CAPACITY;
    }
    if (catalog_count > symbol_capacity) {
        return DB_WIN32_PROVIDER_TABLES_SYMBOL_CAPACITY;
    }
    if (catalog_count > handler_capacity) {
        return DB_WIN32_PROVIDER_TABLES_HANDLER_CAPACITY;
    }
    if (catalog_count > signature_capacity) {
        return DB_WIN32_PROVIDER_TABLES_SIGNATURE_CAPACITY;
    }

    for (index = 0u; index < provider_count; ++index) {
        provider_symbol_offset[index] = symbol_cursor;
        symbol_cursor += provider_symbol_count[index];
    }

    symbol_cursor = 0u;
    for (index = 0u; index < provider_count; ++index) {
        size_t catalog_index;
        for (catalog_index = 0u;
             catalog_index < catalog_count;
             ++catalog_index) {
            if (catalog[catalog_index].provider_id != provider_ids[index]) {
                continue;
            }
            symbols[symbol_cursor].name = catalog[catalog_index].symbol;
            symbols[symbol_cursor].name_length = symbol_lengths[catalog_index];
            symbols[symbol_cursor].symbol_id = catalog[catalog_index].symbol_id;
            symbols[symbol_cursor].ordinal = 0u;
            symbols[symbol_cursor].flags = DB_BIND_SYMBOL_HAS_NAME;
            symbols[symbol_cursor].reserved = 0u;
            ++symbol_cursor;
        }
    }

    for (index = 0u; index < provider_count; ++index) {
        const size_t first = provider_first[index];
        providers[index].module_name = catalog[first].module;
        providers[index].module_name_length = module_lengths[first];
        providers[index].symbols = &symbols[provider_symbol_offset[index]];
        providers[index].symbol_count = provider_symbol_count[index];
        providers[index].provider_id = provider_ids[index];
        providers[index].reserved = 0u;
    }

    for (index = 0u; index < catalog_count; ++index) {
        handlers[index].provider_id = catalog[index].provider_id;
        handlers[index].symbol_id = catalog[index].symbol_id;
        handlers[index].handler_id = catalog[index].handler_id;
        handlers[index].reserved = 0u;

        signatures[index].handler_id = catalog[index].handler_id;
        signatures[index].argument_count = catalog[index].argument_count;
        signatures[index].reserved = 0u;
    }

    completed.catalog_count = catalog_count;
    completed.provider_count = provider_count;
    completed.symbol_count = catalog_count;
    completed.handler_count = catalog_count;
    completed.signature_count = catalog_count;
    completed.format_version = DB_WIN32_PROVIDER_TABLES_FORMAT_VERSION;
    completed.reserved[0] = 0u;
    completed.reserved[1] = 0u;
    completed.reserved[2] = 0u;
    completed.reserved[3] = 0u;
    *summary = completed;
    return DB_WIN32_PROVIDER_TABLES_OK;
}

const char *db_win32_provider_tables_status_name(
    db_win32_provider_tables_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflow",
        "buffer overlap",
        "bad provider catalog",
        "provider output capacity",
        "symbol output capacity",
        "handler output capacity",
        "signature output capacity",
        "inconsistent provider module identity"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown Win32 provider tables status";
    }
    return names[(size_t)status];
}
