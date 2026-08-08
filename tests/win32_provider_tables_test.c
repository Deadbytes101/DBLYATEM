#include "dblyat/win32_provider_tables.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define CHECK(value) do { if (!(value)) return 0; } while (0)

static int same(const void *left, const void *right, size_t size)
{
    return memcmp(left, right, size) == 0;
}

static int test_kernel32_tables(void)
{
    const db_win32_provider_record *catalog;
    size_t catalog_count = 0u;
    db_bind_provider providers[4];
    db_bind_provider_symbol symbols[16];
    db_gateway_dispatch_handler handlers[16];
    db_gateway_frame_signature signatures[16];
    db_win32_provider_tables_summary summary;
    size_t index;

    catalog = db_win32_kernel32_catalog(&catalog_count);
    CHECK(catalog != NULL);
    CHECK(catalog_count == 5u);
    CHECK(db_win32_provider_tables_build(
              catalog, catalog_count,
              providers, 4u,
              symbols, 16u,
              handlers, 16u,
              signatures, 16u,
              &summary) == DB_WIN32_PROVIDER_TABLES_OK);
    CHECK(summary.catalog_count == catalog_count);
    CHECK(summary.provider_count == 1u);
    CHECK(summary.symbol_count == catalog_count);
    CHECK(summary.handler_count == catalog_count);
    CHECK(summary.signature_count == catalog_count);
    CHECK(summary.format_version == DB_WIN32_PROVIDER_TABLES_FORMAT_VERSION);
    CHECK(providers[0].module_name == catalog[0].module);
    CHECK(providers[0].module_name_length == 12u);
    CHECK(providers[0].provider_id == DB_WIN32_PROVIDER_KERNEL32);
    CHECK(providers[0].symbols == symbols);
    CHECK(providers[0].symbol_count == catalog_count);

    for (index = 0u; index < catalog_count; ++index) {
        CHECK(symbols[index].name == catalog[index].symbol);
        CHECK(symbols[index].symbol_id == catalog[index].symbol_id);
        CHECK(symbols[index].flags == DB_BIND_SYMBOL_HAS_NAME);
        CHECK(symbols[index].ordinal == 0u);
        CHECK(symbols[index].reserved == 0u);
        CHECK(handlers[index].provider_id == catalog[index].provider_id);
        CHECK(handlers[index].symbol_id == catalog[index].symbol_id);
        CHECK(handlers[index].handler_id == catalog[index].handler_id);
        CHECK(handlers[index].reserved == 0u);
        CHECK(signatures[index].handler_id == catalog[index].handler_id);
        CHECK(signatures[index].argument_count == catalog[index].argument_count);
        CHECK(signatures[index].reserved == 0u);
    }
    return 1;
}

static int test_failures_are_transactional(void)
{
    const db_win32_provider_record *catalog;
    size_t catalog_count = 0u;
    db_bind_provider providers[4];
    db_bind_provider providers_before[4];
    db_bind_provider_symbol symbols[16];
    db_bind_provider_symbol symbols_before[16];
    db_gateway_dispatch_handler handlers[16];
    db_gateway_dispatch_handler handlers_before[16];
    db_gateway_frame_signature signatures[16];
    db_gateway_frame_signature signatures_before[16];
    db_win32_provider_tables_summary summary;
    db_win32_provider_tables_summary summary_before;

    catalog = db_win32_kernel32_catalog(&catalog_count);
    memset(providers, 0xa5, sizeof(providers));
    memset(symbols, 0x5a, sizeof(symbols));
    memset(handlers, 0x3c, sizeof(handlers));
    memset(signatures, 0xc3, sizeof(signatures));
    memset(&summary, 0x96, sizeof(summary));
    memcpy(providers_before, providers, sizeof(providers));
    memcpy(symbols_before, symbols, sizeof(symbols));
    memcpy(handlers_before, handlers, sizeof(handlers));
    memcpy(signatures_before, signatures, sizeof(signatures));
    summary_before = summary;

    CHECK(db_win32_provider_tables_build(
              catalog, catalog_count,
              providers, 0u,
              symbols, 16u,
              handlers, 16u,
              signatures, 16u,
              &summary) == DB_WIN32_PROVIDER_TABLES_PROVIDER_CAPACITY);
    CHECK(same(providers, providers_before, sizeof(providers)));
    CHECK(same(symbols, symbols_before, sizeof(symbols)));
    CHECK(same(handlers, handlers_before, sizeof(handlers)));
    CHECK(same(signatures, signatures_before, sizeof(signatures)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));

    CHECK(db_win32_provider_tables_build(
              catalog, catalog_count,
              providers, 4u,
              symbols, 4u,
              handlers, 16u,
              signatures, 16u,
              &summary) == DB_WIN32_PROVIDER_TABLES_SYMBOL_CAPACITY);
    CHECK(same(providers, providers_before, sizeof(providers)));
    CHECK(same(symbols, symbols_before, sizeof(symbols)));
    CHECK(same(handlers, handlers_before, sizeof(handlers)));
    CHECK(same(signatures, signatures_before, sizeof(signatures)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}

int main(void)
{
    static const char pass[] = "WIN32 PROVIDER TABLES TEST: PASS\n";
    static const char fail[] = "WIN32 PROVIDER TABLES TEST: FAIL\n";

    if (test_kernel32_tables() && test_failures_are_transactional()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }
    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
