#include "dblyat/win32_provider.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define CHECK(value) do { if (!(value)) return 0; } while (0)

static void fill(void *data, size_t size, uint8_t value)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        bytes[index] = value;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    uint8_t *output = (uint8_t *)destination;
    const uint8_t *input = (const uint8_t *)source;
    size_t index;

    for (index = 0u; index < size; ++index) {
        output[index] = input[index];
    }
}

static int same(const void *left, const void *right, size_t size)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (a[index] != b[index]) {
            return 0;
        }
    }

    return 1;
}

static int preserved(const db_win32_provider_record *catalog,
                     size_t count,
                     const char *module,
                     size_t module_length,
                     const char *symbol,
                     size_t symbol_length,
                     db_win32_provider_status expected)
{
    db_win32_provider_match match;
    db_win32_provider_match before;

    fill(&match, sizeof(match), UINT8_C(0xa5));
    copy_bytes(&before, &match, sizeof(before));
    return db_win32_provider_lookup(catalog,
                                    count,
                                    module,
                                    module_length,
                                    symbol,
                                    symbol_length,
                                    &match) == expected &&
           same(&match, &before, sizeof(match));
}

static int test_builtin_catalog(void)
{
    const db_win32_provider_record *catalog;
    db_win32_provider_match match;
    size_t count = 0u;

    catalog = db_win32_kernel32_catalog(&count);
    CHECK(catalog != NULL);
    CHECK(count == 4u);

    CHECK(db_win32_provider_lookup(catalog,
                                   count,
                                   "kernel32.dll",
                                   12u,
                                   "GetCurrentProcessId",
                                   19u,
                                   &match) == DB_WIN32_PROVIDER_OK);
    CHECK(match.catalog_index == 0u);
    CHECK(match.provider_id == DB_WIN32_PROVIDER_KERNEL32);
    CHECK(match.symbol_id ==
          DB_WIN32_SYMBOL_KERNEL32_GET_CURRENT_PROCESS_ID);
    CHECK(match.handler_id ==
          DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_PROCESS_ID);
    CHECK(match.argument_count == 0u);
    CHECK(match.flags == DB_WIN32_PROVIDER_FLAG_PROCESS_STATE);
    CHECK(match.reserved == 0u);

    CHECK(db_win32_provider_lookup(catalog,
                                   count,
                                   "KeRnEl32.DlL",
                                   12u,
                                   "SetLastError",
                                   12u,
                                   &match) == DB_WIN32_PROVIDER_OK);
    CHECK(match.catalog_index == 3u);
    CHECK(match.symbol_id == DB_WIN32_SYMBOL_KERNEL32_SET_LAST_ERROR);
    CHECK(match.handler_id == DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR);
    CHECK(match.argument_count == 1u);
    CHECK(match.flags == DB_WIN32_PROVIDER_FLAG_THREAD_STATE);

    CHECK(preserved(catalog,
                    count,
                    "kernel32.dll",
                    12u,
                    "setlasterror",
                    12u,
                    DB_WIN32_PROVIDER_NOT_FOUND));
    return 1;
}

static void copy_record(db_win32_provider_record *destination,
                        const db_win32_provider_record *source)
{
    *destination = *source;
}

static int test_catalog_failures(void)
{
    const db_win32_provider_record *builtin;
    db_win32_provider_record catalog[4];
    size_t count;

    builtin = db_win32_kernel32_catalog(&count);
    CHECK(count == 4u);
    copy_record(&catalog[0], &builtin[0]);
    copy_record(&catalog[1], &builtin[1]);
    copy_record(&catalog[2], &builtin[2]);
    copy_record(&catalog[3], &builtin[3]);

    catalog[1].handler_id = catalog[0].handler_id;
    CHECK(preserved(catalog, 4u,
                    "kernel32.dll", 12u,
                    "GetCurrentProcessId", 19u,
                    DB_WIN32_PROVIDER_DUPLICATE_HANDLER_ID));

    copy_record(&catalog[1], &builtin[1]);
    catalog[1].provider_id = catalog[0].provider_id;
    catalog[1].symbol_id = catalog[0].symbol_id;
    CHECK(preserved(catalog, 4u,
                    "kernel32.dll", 12u,
                    "GetCurrentProcessId", 19u,
                    DB_WIN32_PROVIDER_DUPLICATE_KEY));

    copy_record(&catalog[1], &builtin[1]);
    fill(catalog[1].module, sizeof(catalog[1].module), 0u);
    fill(catalog[1].symbol, sizeof(catalog[1].symbol), 0u);
    {
        const char module[] = "kernel32.dll";
        const char symbol[] = "GetCurrentProcessId";
        size_t index;
        for (index = 0u; index < sizeof(module); ++index) {
            catalog[1].module[index] = module[index];
        }
        for (index = 0u; index < sizeof(symbol); ++index) {
            catalog[1].symbol[index] = symbol[index];
        }
    }
    CHECK(preserved(catalog, 4u,
                    "kernel32.dll", 12u,
                    "GetCurrentProcessId", 19u,
                    DB_WIN32_PROVIDER_DUPLICATE_NAME));

    copy_record(&catalog[1], &builtin[1]);
    catalog[1].reserved = 1u;
    CHECK(preserved(catalog, 4u,
                    "kernel32.dll", 12u,
                    "GetCurrentProcessId", 19u,
                    DB_WIN32_PROVIDER_BAD_RECORD));

    copy_record(&catalog[1], &builtin[1]);
    catalog[1].argument_count = 65u;
    CHECK(preserved(catalog, 4u,
                    "kernel32.dll", 12u,
                    "GetCurrentProcessId", 19u,
                    DB_WIN32_PROVIDER_BAD_RECORD));

    copy_record(&catalog[1], &builtin[1]);
    catalog[1].flags = (db_win32_provider_flags)0x8000u;
    CHECK(preserved(catalog, 4u,
                    "kernel32.dll", 12u,
                    "GetCurrentProcessId", 19u,
                    DB_WIN32_PROVIDER_BAD_RECORD));

    copy_record(&catalog[1], &builtin[1]);
    catalog[1].module[sizeof(catalog[1].module) - 1u] = 'X';
    CHECK(preserved(catalog, 4u,
                    "kernel32.dll", 12u,
                    "GetCurrentProcessId", 19u,
                    DB_WIN32_PROVIDER_BAD_RECORD));
    return 1;
}

static int test_query_and_alias_guards(void)
{
    const db_win32_provider_record *catalog;
    db_win32_provider_match match;
    size_t count;
    union catalog_match_alias {
        db_win32_provider_record catalog[4];
        db_win32_provider_match match;
    } alias;
    char shared[32] = "kernel32.dllGetCurrentProcessId";

    catalog = db_win32_kernel32_catalog(&count);

    CHECK(preserved(catalog, count,
                    "", 0u,
                    "GetCurrentProcessId", 19u,
                    DB_WIN32_PROVIDER_INVALID_QUERY));
    CHECK(preserved(catalog, count,
                    "kernel32.dll", 12u,
                    "Get CurrentProcessId", 20u,
                    DB_WIN32_PROVIDER_INVALID_QUERY));
    CHECK(preserved(catalog, count,
                    "kernel32.dll", 32u,
                    "GetCurrentProcessId", 19u,
                    DB_WIN32_PROVIDER_INVALID_QUERY));

    copy_record(&alias.catalog[0], &catalog[0]);
    copy_record(&alias.catalog[1], &catalog[1]);
    copy_record(&alias.catalog[2], &catalog[2]);
    copy_record(&alias.catalog[3], &catalog[3]);
    CHECK(db_win32_provider_lookup(alias.catalog,
                                   4u,
                                   "kernel32.dll",
                                   12u,
                                   "GetCurrentProcessId",
                                   19u,
                                   &alias.match) ==
          DB_WIN32_PROVIDER_BUFFER_OVERLAP);

    CHECK(db_win32_provider_lookup(catalog,
                                   count,
                                   shared,
                                   12u,
                                   shared + 8u,
                                   19u,
                                   &match) == DB_WIN32_PROVIDER_BUFFER_OVERLAP);

    CHECK(db_win32_provider_lookup(NULL, count,
                                   "kernel32.dll", 12u,
                                   "GetCurrentProcessId", 19u,
                                   &match) == DB_WIN32_PROVIDER_NULL_ARGUMENT);
    CHECK(db_win32_provider_lookup(catalog, count,
                                   NULL, 12u,
                                   "GetCurrentProcessId", 19u,
                                   &match) == DB_WIN32_PROVIDER_NULL_ARGUMENT);
    CHECK(db_win32_provider_lookup(catalog, count,
                                   "kernel32.dll", 12u,
                                   NULL, 19u,
                                   &match) == DB_WIN32_PROVIDER_NULL_ARGUMENT);
    CHECK(db_win32_provider_lookup(catalog, count,
                                   "kernel32.dll", 12u,
                                   "GetCurrentProcessId", 19u,
                                   NULL) == DB_WIN32_PROVIDER_NULL_ARGUMENT);

    CHECK(preserved(catalog,
                    DB_WIN32_PROVIDER_MAX_ARGUMENTS + 1u,
                    "kernel32.dll", 12u,
                    "GetCurrentProcessId", 19u,
                    DB_WIN32_PROVIDER_COUNT_OVERFLOW));
    return 1;
}

static int test_names(void)
{
    CHECK(db_win32_provider_status_name(DB_WIN32_PROVIDER_OK)[0] == 'o');
    CHECK(db_win32_provider_status_name(DB_WIN32_PROVIDER_NOT_FOUND)[0] == 'p');
    CHECK(db_win32_provider_status_name((db_win32_provider_status)99)[0] == 'u');
    return 1;
}

int main(void)
{
    static const char pass[] = "WIN32 PROVIDER TEST: PASS\n";
    static const char fail[] = "WIN32 PROVIDER TEST: FAIL\n";

    if (test_builtin_catalog() &&
        test_catalog_failures() &&
        test_query_and_alias_guards() &&
        test_names()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
