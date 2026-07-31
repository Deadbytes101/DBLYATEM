#include "dblyat/bind.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_IMAGE_SIZE 256u
#define TEST_BINDING_CAPACITY 4u

#define MODULE_NAME_RVA 8u
#define SYMBOL_NAME_RVA 64u
#define SECOND_SYMBOL_NAME_RVA 96u

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr,                                                \
                    "BIND TEST FAIL: %s:%d: %s\n",                         \
                    __FILE__,                                              \
                    __LINE__,                                              \
                    #condition);                                           \
            return 1;                                                      \
        }                                                                  \
    } while (0)

typedef struct test_fixture {
    uint8_t image[TEST_IMAGE_SIZE];
    db_import_module modules[2];
    db_import_record records[4];
    db_bind_provider_symbol symbols[4];
    db_bind_provider providers[2];
    db_import_binding bindings[TEST_BINDING_CAPACITY];
    db_bind_summary summary;
} test_fixture;

static const char provider_kernel32[] = "kernel32.dll";
static const char provider_kernel32_upper[] = "KERNEL32.DLL";
static const char provider_user32[] = "user32.dll";
static const char symbol_create_file[] = "CreateFileW";
static const char symbol_create_file_other_case[] = "createFileW";
static const char symbol_close_handle[] = "CloseHandle";

static void put_text(uint8_t *image, size_t offset, const char *text)
{
    size_t index = 0u;

    while (text[index] != '\0') {
        image[offset + index] = (uint8_t)text[index];
        ++index;
    }
    image[offset + index] = 0u;
}

static void make_valid_fixture(test_fixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    memset(fixture->bindings, 0xa5, sizeof(fixture->bindings));
    memset(&fixture->summary, 0x5a, sizeof(fixture->summary));

    put_text(fixture->image, MODULE_NAME_RVA, "KERNEL32.DLL");
    put_text(fixture->image, SYMBOL_NAME_RVA, "CreateFileW");
    put_text(fixture->image, SECOND_SYMBOL_NAME_RVA, "CloseHandle");

    fixture->modules[0].name_rva = MODULE_NAME_RVA;
    fixture->modules[0].name_length = 12u;
    fixture->modules[0].lookup_table_rva = 0x1000u;
    fixture->modules[0].iat_rva = 0x1100u;
    fixture->modules[0].first_record = 0u;
    fixture->modules[0].record_count = 2u;

    fixture->records[0].module_index = 0u;
    fixture->records[0].lookup_rva = 0x1000u;
    fixture->records[0].iat_rva = 0x1100u;
    fixture->records[0].name_rva = SYMBOL_NAME_RVA;
    fixture->records[0].name_length = 11u;
    fixture->records[0].hint = 4u;
    fixture->records[0].kind = DB_IMPORT_BY_NAME;
    fixture->records[0].lookup_value = SYMBOL_NAME_RVA - 2u;

    fixture->records[1].module_index = 0u;
    fixture->records[1].lookup_rva = 0x1008u;
    fixture->records[1].iat_rva = 0x1108u;
    fixture->records[1].ordinal = 7u;
    fixture->records[1].kind = DB_IMPORT_BY_ORDINAL;
    fixture->records[1].lookup_value =
        UINT64_C(0x8000000000000000) | UINT64_C(7);

    fixture->symbols[0].name = symbol_create_file;
    fixture->symbols[0].name_length = 11u;
    fixture->symbols[0].symbol_id = 0x2001u;
    fixture->symbols[0].ordinal = 3u;
    fixture->symbols[0].flags =
        DB_BIND_SYMBOL_HAS_NAME | DB_BIND_SYMBOL_HAS_ORDINAL;

    fixture->symbols[1].symbol_id = 0x2002u;
    fixture->symbols[1].ordinal = 7u;
    fixture->symbols[1].flags = DB_BIND_SYMBOL_HAS_ORDINAL;

    fixture->providers[0].module_name = provider_kernel32;
    fixture->providers[0].module_name_length = 12u;
    fixture->providers[0].symbols = fixture->symbols;
    fixture->providers[0].symbol_count = 2u;
    fixture->providers[0].provider_id = 0x1001u;
}

static db_bind_status plan_fixture(test_fixture *fixture,
                                   size_t module_count,
                                   size_t record_count,
                                   size_t provider_count,
                                   db_import_binding *bindings,
                                   size_t binding_capacity,
                                   db_bind_summary *summary)
{
    return db_bind_plan_imports(
        fixture->image,
        sizeof(fixture->image),
        module_count != 0u ? fixture->modules : NULL,
        module_count,
        record_count != 0u ? fixture->records : NULL,
        record_count,
        provider_count != 0u ? fixture->providers : NULL,
        provider_count,
        bindings,
        binding_capacity,
        summary);
}

static int output_is_pattern(const test_fixture *fixture)
{
    size_t index;
    const uint8_t *binding_bytes =
        (const uint8_t *)fixture->bindings;
    const uint8_t *summary_bytes =
        (const uint8_t *)&fixture->summary;

    for (index = 0u; index < sizeof(fixture->bindings); ++index) {
        if (binding_bytes[index] != 0xa5u) {
            return 0;
        }
    }

    for (index = 0u; index < sizeof(fixture->summary); ++index) {
        if (summary_bytes[index] != 0x5au) {
            return 0;
        }
    }

    return 1;
}

static int test_valid_plan(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_OK);
    CHECK(fixture.bindings[0].module_index == 0u);
    CHECK(fixture.bindings[0].provider_id == 0x1001u);
    CHECK(fixture.bindings[0].symbol_id == 0x2001u);
    CHECK(fixture.bindings[0].reserved == 0u);
    CHECK(fixture.bindings[1].module_index == 0u);
    CHECK(fixture.bindings[1].provider_id == 0x1001u);
    CHECK(fixture.bindings[1].symbol_id == 0x2002u);
    CHECK(fixture.bindings[1].reserved == 0u);
    CHECK(fixture.summary.module_count == 1u);
    CHECK(fixture.summary.record_count == 2u);
    CHECK(fixture.summary.binding_count == 2u);

    return 0;
}

static int test_name_and_ordinal_alias_same_entry(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.records[1].ordinal = 3u;
    fixture.records[1].lookup_value =
        UINT64_C(0x8000000000000000) | UINT64_C(3);

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_OK);
    CHECK(fixture.bindings[0].symbol_id == 0x2001u);
    CHECK(fixture.bindings[1].symbol_id == 0x2001u);

    return 0;
}

static int test_zero_imports(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    status = plan_fixture(&fixture,
                          0u,
                          0u,
                          1u,
                          NULL,
                          0u,
                          &fixture.summary);

    CHECK(status == DB_BIND_OK);
    CHECK(fixture.summary.module_count == 0u);
    CHECK(fixture.summary.record_count == 0u);
    CHECK(fixture.summary.binding_count == 0u);

    return 0;
}

static int test_symbol_names_are_case_sensitive(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.symbols[0].name = symbol_create_file_other_case;

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_SYMBOL_NOT_FOUND);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_module_matching_does_not_rewrite_suffixes(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.providers[0].module_name = "kernel32";
    fixture.providers[0].module_name_length = 8u;

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_MODULE_NOT_FOUND);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_missing_module(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.providers[0].module_name = provider_user32;
    fixture.providers[0].module_name_length = 10u;

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_MODULE_NOT_FOUND);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_missing_symbol(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.symbols[1].ordinal = 8u;

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_SYMBOL_NOT_FOUND);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_duplicate_provider_module(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.providers[1] = fixture.providers[0];
    fixture.providers[1].module_name = provider_kernel32_upper;
    fixture.providers[1].provider_id = 0x1002u;

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          2u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_DUPLICATE_PROVIDER_MODULE);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_duplicate_provider_id(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.providers[1] = fixture.providers[0];
    fixture.providers[1].module_name = provider_user32;
    fixture.providers[1].module_name_length = 10u;

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          2u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_DUPLICATE_PROVIDER_ID);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_duplicate_symbol_name(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.symbols[2].name = symbol_create_file;
    fixture.symbols[2].name_length = 11u;
    fixture.symbols[2].symbol_id = 0x2003u;
    fixture.symbols[2].flags = DB_BIND_SYMBOL_HAS_NAME;
    fixture.providers[0].symbol_count = 3u;

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_DUPLICATE_SYMBOL_NAME);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_duplicate_symbol_ordinal(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.symbols[2].symbol_id = 0x2003u;
    fixture.symbols[2].ordinal = 7u;
    fixture.symbols[2].flags = DB_BIND_SYMBOL_HAS_ORDINAL;
    fixture.providers[0].symbol_count = 3u;

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_DUPLICATE_SYMBOL_ORDINAL);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_ambiguous_symbol_alias(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.symbols[2].name = symbol_close_handle;
    fixture.symbols[2].name_length = 11u;
    fixture.symbols[2].symbol_id = 0x2001u;
    fixture.symbols[2].ordinal = 9u;
    fixture.symbols[2].flags =
        DB_BIND_SYMBOL_HAS_NAME | DB_BIND_SYMBOL_HAS_ORDINAL;
    fixture.providers[0].symbol_count = 3u;

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_AMBIGUOUS_SYMBOL_ALIAS);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_output_too_small_is_transactional(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          1u,
                          &fixture.summary);

    CHECK(status == DB_BIND_OUTPUT_BUFFER_TOO_SMALL);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_bad_import_layout_is_transactional(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.modules[0].first_record = 1u;

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_BAD_IMPORT_LAYOUT);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_bad_ordinal_encoding(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.records[1].lookup_value |= UINT64_C(0x0001000000000000);

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_BAD_IMPORT_LAYOUT);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_bad_import_module_name(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.image[MODULE_NAME_RVA + 3u] = 0x1fu;

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_IMPORT_MODULE_NAME_INVALID);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_bad_import_symbol_name(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.image[SYMBOL_NAME_RVA + 3u] = 0x7fu;

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_IMPORT_SYMBOL_NAME_INVALID);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_bad_provider_symbol(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    fixture.symbols[0].flags = 4u;

    status = plan_fixture(&fixture,
                          1u,
                          2u,
                          1u,
                          fixture.bindings,
                          TEST_BINDING_CAPACITY,
                          &fixture.summary);

    CHECK(status == DB_BIND_PROVIDER_SYMBOL_INVALID);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_buffer_overlap(void)
{
    test_fixture fixture;
    db_bind_status status;

    make_valid_fixture(&fixture);
    status = plan_fixture(
        &fixture,
        1u,
        2u,
        1u,
        (db_import_binding *)(void *)fixture.records,
        2u,
        &fixture.summary);

    CHECK(status == DB_BIND_BUFFER_OVERLAP);
    CHECK(output_is_pattern(&fixture));

    return 0;
}

static int test_status_names(void)
{
    CHECK(strcmp(db_bind_status_name(DB_BIND_OK), "ok") == 0);
    CHECK(strcmp(db_bind_status_name((db_bind_status)999),
                 "unknown bind status") == 0);

    return 0;
}

int main(void)
{
    CHECK(test_valid_plan() == 0);
    CHECK(test_name_and_ordinal_alias_same_entry() == 0);
    CHECK(test_zero_imports() == 0);
    CHECK(test_symbol_names_are_case_sensitive() == 0);
    CHECK(test_module_matching_does_not_rewrite_suffixes() == 0);
    CHECK(test_missing_module() == 0);
    CHECK(test_missing_symbol() == 0);
    CHECK(test_duplicate_provider_module() == 0);
    CHECK(test_duplicate_provider_id() == 0);
    CHECK(test_duplicate_symbol_name() == 0);
    CHECK(test_duplicate_symbol_ordinal() == 0);
    CHECK(test_ambiguous_symbol_alias() == 0);
    CHECK(test_output_too_small_is_transactional() == 0);
    CHECK(test_bad_import_layout_is_transactional() == 0);
    CHECK(test_bad_ordinal_encoding() == 0);
    CHECK(test_bad_import_module_name() == 0);
    CHECK(test_bad_import_symbol_name() == 0);
    CHECK(test_bad_provider_symbol() == 0);
    CHECK(test_buffer_overlap() == 0);
    CHECK(test_status_names() == 0);

    puts("BIND TEST: PASS");
    return 0;
}
