#include "dblyat/win32_provider.h"

#include <stdbool.h>
#include <stdint.h>

#define DB_WIN32_PROVIDER_ALLOWED_FLAGS \
    ((db_win32_provider_flags)(DB_WIN32_PROVIDER_FLAG_PROCESS_STATE | \
                               DB_WIN32_PROVIDER_FLAG_THREAD_STATE | \
                               DB_WIN32_PROVIDER_FLAG_MAY_BLOCK))

static const db_win32_provider_record db_win32_kernel32_records[] = {
    {
        "KERNEL32.DLL",
        "GetCurrentProcessId",
        DB_WIN32_PROVIDER_KERNEL32,
        DB_WIN32_SYMBOL_KERNEL32_GET_CURRENT_PROCESS_ID,
        DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_PROCESS_ID,
        0u,
        DB_WIN32_PROVIDER_FLAG_PROCESS_STATE,
        0u
    },
    {
        "KERNEL32.DLL",
        "GetCurrentThreadId",
        DB_WIN32_PROVIDER_KERNEL32,
        DB_WIN32_SYMBOL_KERNEL32_GET_CURRENT_THREAD_ID,
        DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_THREAD_ID,
        0u,
        DB_WIN32_PROVIDER_FLAG_THREAD_STATE,
        0u
    },
    {
        "KERNEL32.DLL",
        "GetLastError",
        DB_WIN32_PROVIDER_KERNEL32,
        DB_WIN32_SYMBOL_KERNEL32_GET_LAST_ERROR,
        DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR,
        0u,
        DB_WIN32_PROVIDER_FLAG_THREAD_STATE,
        0u
    },
    {
        "KERNEL32.DLL",
        "SetLastError",
        DB_WIN32_PROVIDER_KERNEL32,
        DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR,
        DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
        1u,
        DB_WIN32_PROVIDER_FLAG_THREAD_STATE,
        0u
    },
    {
        "KERNEL32.DLL",
        "ExitProcess",
        DB_WIN32_PROVIDER_KERNEL32,
        DB_WIN32_SYMBOL_KERNEL32_EXIT_PROCESS,
        DB_WIN32_HANDLER_KERNEL32_EXIT_PROCESS,
        1u,
        DB_WIN32_PROVIDER_FLAG_PROCESS_STATE,
        0u
    }
};

static bool db_win32_provider_mul_size(size_t left,
                                       size_t right,
                                       size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_win32_provider_overlap(const void *left,
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

static bool db_win32_provider_ascii_printable(unsigned char value)
{
    return value >= 0x21u && value <= 0x7eu;
}

static unsigned char db_win32_provider_ascii_fold(unsigned char value)
{
    if (value >= (unsigned char)'A' && value <= (unsigned char)'Z') {
        return (unsigned char)(value + ((unsigned char)'a' -
                                       (unsigned char)'A'));
    }

    return value;
}

static bool db_win32_provider_fixed_string_valid(const char *text,
                                                  size_t capacity,
                                                  size_t *length)
{
    size_t index;
    size_t terminator = capacity;

    for (index = 0u; index < capacity; ++index) {
        const unsigned char value = (unsigned char)text[index];

        if (value == 0u) {
            terminator = index;
            break;
        }

        if (!db_win32_provider_ascii_printable(value)) {
            return false;
        }
    }

    if (terminator == 0u || terminator == capacity) {
        return false;
    }

    for (index = terminator + 1u; index < capacity; ++index) {
        if (text[index] != '\0') {
            return false;
        }
    }

    *length = terminator;
    return true;
}

static bool db_win32_provider_module_equal(const char *left,
                                           size_t left_length,
                                           const char *right,
                                           size_t right_length)
{
    size_t index;

    if (left_length != right_length) {
        return false;
    }

    for (index = 0u; index < left_length; ++index) {
        if (db_win32_provider_ascii_fold((unsigned char)left[index]) !=
            db_win32_provider_ascii_fold((unsigned char)right[index])) {
            return false;
        }
    }

    return true;
}

static bool db_win32_provider_symbol_equal(const char *left,
                                           size_t left_length,
                                           const char *right,
                                           size_t right_length)
{
    size_t index;

    if (left_length != right_length) {
        return false;
    }

    for (index = 0u; index < left_length; ++index) {
        if ((unsigned char)left[index] != (unsigned char)right[index]) {
            return false;
        }
    }

    return true;
}

static bool db_win32_provider_query_valid(const char *text,
                                          size_t length,
                                          size_t capacity)
{
    size_t index;

    if (length == 0u || length >= capacity) {
        return false;
    }

    for (index = 0u; index < length; ++index) {
        if (!db_win32_provider_ascii_printable((unsigned char)text[index])) {
            return false;
        }
    }

    return true;
}

static db_win32_provider_status db_win32_provider_validate_catalog(
    const db_win32_provider_record *catalog,
    size_t catalog_count,
    size_t *module_lengths,
    size_t *symbol_lengths)
{
    size_t index;

    for (index = 0u; index < catalog_count; ++index) {
        const db_win32_provider_record *record = &catalog[index];
        size_t other;

        if (!db_win32_provider_fixed_string_valid(
                record->module,
                sizeof(record->module),
                &module_lengths[index]) ||
            !db_win32_provider_fixed_string_valid(
                record->symbol,
                sizeof(record->symbol),
                &symbol_lengths[index]) ||
            record->provider_id == 0u ||
            record->symbol_id == 0u ||
            record->handler_id == 0u ||
            record->argument_count > DB_WIN32_PROVIDER_MAX_ARGUMENTS ||
            (record->flags &
             (db_win32_provider_flags)~DB_WIN32_PROVIDER_ALLOWED_FLAGS) != 0u ||
            record->reserved != 0u) {
            return DB_WIN32_PROVIDER_BAD_RECORD;
        }

        for (other = 0u; other < index; ++other) {
            const db_win32_provider_record *previous = &catalog[other];

            if (db_win32_provider_module_equal(
                    record->module,
                    module_lengths[index],
                    previous->module,
                    module_lengths[other]) &&
                db_win32_provider_symbol_equal(
                    record->symbol,
                    symbol_lengths[index],
                    previous->symbol,
                    symbol_lengths[other])) {
                return DB_WIN32_PROVIDER_DUPLICATE_NAME;
            }

            if (record->provider_id == previous->provider_id &&
                record->symbol_id == previous->symbol_id) {
                return DB_WIN32_PROVIDER_DUPLICATE_KEY;
            }

            if (record->handler_id == previous->handler_id) {
                return DB_WIN32_PROVIDER_DUPLICATE_HANDLER_ID;
            }
        }
    }

    return DB_WIN32_PROVIDER_OK;
}

const db_win32_provider_record *db_win32_kernel32_catalog(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(db_win32_kernel32_records) /
                 sizeof(db_win32_kernel32_records[0]);
    }

    return db_win32_kernel32_records;
}

db_win32_provider_status db_win32_provider_lookup(
    const db_win32_provider_record *catalog,
    size_t catalog_count,
    const char *module,
    size_t module_length,
    const char *symbol,
    size_t symbol_length,
    db_win32_provider_match *match)
{
    db_win32_provider_match planned;
    db_win32_provider_status status;
    size_t catalog_bytes;
    size_t module_lengths[DB_WIN32_PROVIDER_MAX_ARGUMENTS];
    size_t symbol_lengths[DB_WIN32_PROVIDER_MAX_ARGUMENTS];
    size_t index;

    if (catalog == NULL || module == NULL || symbol == NULL || match == NULL) {
        return DB_WIN32_PROVIDER_NULL_ARGUMENT;
    }

    if (catalog_count > DB_WIN32_PROVIDER_MAX_ARGUMENTS ||
        !db_win32_provider_mul_size(catalog_count,
                                    sizeof(*catalog),
                                    &catalog_bytes)) {
        return DB_WIN32_PROVIDER_COUNT_OVERFLOW;
    }

    if (db_win32_provider_overlap(catalog, catalog_bytes,
                                  module, module_length) ||
        db_win32_provider_overlap(catalog, catalog_bytes,
                                  symbol, symbol_length) ||
        db_win32_provider_overlap(catalog, catalog_bytes,
                                  match, sizeof(*match)) ||
        db_win32_provider_overlap(module, module_length,
                                  symbol, symbol_length) ||
        db_win32_provider_overlap(module, module_length,
                                  match, sizeof(*match)) ||
        db_win32_provider_overlap(symbol, symbol_length,
                                  match, sizeof(*match))) {
        return DB_WIN32_PROVIDER_BUFFER_OVERLAP;
    }

    if (!db_win32_provider_query_valid(module,
                                       module_length,
                                       DB_WIN32_PROVIDER_MODULE_CAPACITY) ||
        !db_win32_provider_query_valid(symbol,
                                       symbol_length,
                                       DB_WIN32_PROVIDER_SYMBOL_CAPACITY)) {
        return DB_WIN32_PROVIDER_INVALID_QUERY;
    }

    status = db_win32_provider_validate_catalog(catalog,
                                                catalog_count,
                                                module_lengths,
                                                symbol_lengths);
    if (status != DB_WIN32_PROVIDER_OK) {
        return status;
    }

    for (index = 0u; index < catalog_count; ++index) {
        if (db_win32_provider_module_equal(module,
                                           module_length,
                                           catalog[index].module,
                                           module_lengths[index]) &&
            db_win32_provider_symbol_equal(symbol,
                                           symbol_length,
                                           catalog[index].symbol,
                                           symbol_lengths[index])) {
            planned.catalog_index = index;
            planned.provider_id = catalog[index].provider_id;
            planned.symbol_id = catalog[index].symbol_id;
            planned.handler_id = catalog[index].handler_id;
            planned.argument_count = catalog[index].argument_count;
            planned.flags = catalog[index].flags;
            planned.reserved = 0u;
            *match = planned;
            return DB_WIN32_PROVIDER_OK;
        }
    }

    return DB_WIN32_PROVIDER_NOT_FOUND;
}

const char *db_win32_provider_status_name(db_win32_provider_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "invalid query",
        "bad provider record",
        "duplicate module and symbol name",
        "duplicate provider and symbol key",
        "duplicate handler id",
        "provider symbol not found"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown Win32 provider status";
    }

    return names[(size_t)status];
}
