#include "dblyat/bind.h"

#include <stdbool.h>
#include <stdint.h>

#define DB_BIND_ORDINAL_FLAG UINT64_C(0x8000000000000000)
#define DB_BIND_ORDINAL_MASK UINT64_C(0x000000000000ffff)

static bool db_bind_mul_size(size_t left, size_t right, size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_bind_span_fits(size_t total,
                              size_t offset,
                              size_t length)
{
    return offset <= total && length <= total - offset;
}

static bool db_bind_ranges_overlap(const void *left,
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

static bool db_bind_ascii_is_printable(const uint8_t *bytes, size_t length)
{
    size_t index;

    if (length == 0u) {
        return false;
    }

    for (index = 0u; index < length; ++index) {
        if (bytes[index] < 0x20u || bytes[index] > 0x7eu) {
            return false;
        }
    }

    return true;
}

static uint8_t db_bind_ascii_fold(uint8_t value)
{
    if (value >= (uint8_t)'A' && value <= (uint8_t)'Z') {
        return (uint8_t)(value + ((uint8_t)'a' - (uint8_t)'A'));
    }

    return value;
}

static bool db_bind_module_names_equal(const uint8_t *left,
                                       size_t left_length,
                                       const uint8_t *right,
                                       size_t right_length)
{
    size_t index;

    if (left_length != right_length) {
        return false;
    }

    for (index = 0u; index < left_length; ++index) {
        if (db_bind_ascii_fold(left[index]) !=
            db_bind_ascii_fold(right[index])) {
            return false;
        }
    }

    return true;
}

static bool db_bind_symbol_names_equal(const uint8_t *left,
                                       size_t left_length,
                                       const uint8_t *right,
                                       size_t right_length)
{
    size_t index;

    if (left_length != right_length) {
        return false;
    }

    for (index = 0u; index < left_length; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }

    return true;
}

static db_bind_status db_bind_validate_image_string(
    const uint8_t *image,
    size_t mapped_size,
    uint32_t rva,
    uint32_t length,
    db_bind_status invalid_status)
{
    const size_t offset = (size_t)rva;
    const size_t string_length = (size_t)length;

    if (!db_bind_span_fits(mapped_size, offset, string_length) ||
        string_length == mapped_size - offset ||
        image[offset + string_length] != 0u ||
        !db_bind_ascii_is_printable(image + offset, string_length)) {
        return invalid_status;
    }

    return DB_BIND_OK;
}

static db_bind_status db_bind_validate_aliases(
    const void *mapped_image,
    size_t mapped_size,
    const db_import_module *modules,
    size_t module_bytes,
    const db_import_record *records,
    size_t record_bytes,
    const db_bind_provider *providers,
    size_t provider_bytes,
    db_import_binding *bindings,
    size_t binding_bytes,
    db_bind_summary *summary)
{
    if (db_bind_ranges_overlap(bindings, binding_bytes,
                               summary, sizeof(*summary)) ||
        db_bind_ranges_overlap(bindings, binding_bytes,
                               mapped_image, mapped_size) ||
        db_bind_ranges_overlap(bindings, binding_bytes,
                               modules, module_bytes) ||
        db_bind_ranges_overlap(bindings, binding_bytes,
                               records, record_bytes) ||
        db_bind_ranges_overlap(bindings, binding_bytes,
                               providers, provider_bytes) ||
        db_bind_ranges_overlap(summary, sizeof(*summary),
                               mapped_image, mapped_size) ||
        db_bind_ranges_overlap(summary, sizeof(*summary),
                               modules, module_bytes) ||
        db_bind_ranges_overlap(summary, sizeof(*summary),
                               records, record_bytes) ||
        db_bind_ranges_overlap(summary, sizeof(*summary),
                               providers, provider_bytes)) {
        return DB_BIND_BUFFER_OVERLAP;
    }

    return DB_BIND_OK;
}

static db_bind_status db_bind_validate_catalog(
    const db_bind_provider *providers,
    size_t provider_count,
    db_import_binding *bindings,
    size_t binding_bytes,
    db_bind_summary *summary)
{
    size_t provider_index;

    for (provider_index = 0u;
         provider_index < provider_count;
         ++provider_index) {
        const db_bind_provider *provider = &providers[provider_index];
        size_t symbol_bytes;
        size_t symbol_index;
        size_t other_provider;

        if (provider->module_name == NULL ||
            provider->module_name_length == 0u ||
            !db_bind_ascii_is_printable(
                (const uint8_t *)provider->module_name,
                provider->module_name_length)) {
            return DB_BIND_PROVIDER_MODULE_NAME_INVALID;
        }

        if (provider->provider_id == 0u || provider->reserved != 0u) {
            return DB_BIND_PROVIDER_ID_INVALID;
        }

        if (provider->symbol_count != 0u && provider->symbols == NULL) {
            return DB_BIND_NULL_ARGUMENT;
        }

        if (!db_bind_mul_size(provider->symbol_count,
                              sizeof(*provider->symbols),
                              &symbol_bytes)) {
            return DB_BIND_COUNT_OVERFLOW;
        }

        if (db_bind_ranges_overlap(bindings, binding_bytes,
                                   provider->module_name,
                                   provider->module_name_length) ||
            db_bind_ranges_overlap(summary, sizeof(*summary),
                                   provider->module_name,
                                   provider->module_name_length) ||
            db_bind_ranges_overlap(bindings, binding_bytes,
                                   provider->symbols,
                                   symbol_bytes) ||
            db_bind_ranges_overlap(summary, sizeof(*summary),
                                   provider->symbols,
                                   symbol_bytes)) {
            return DB_BIND_BUFFER_OVERLAP;
        }

        for (other_provider = 0u;
             other_provider < provider_index;
             ++other_provider) {
            const db_bind_provider *other = &providers[other_provider];

            if (provider->provider_id == other->provider_id) {
                return DB_BIND_DUPLICATE_PROVIDER_ID;
            }

            if (db_bind_module_names_equal(
                    (const uint8_t *)provider->module_name,
                    provider->module_name_length,
                    (const uint8_t *)other->module_name,
                    other->module_name_length)) {
                return DB_BIND_DUPLICATE_PROVIDER_MODULE;
            }
        }

        for (symbol_index = 0u;
             symbol_index < provider->symbol_count;
             ++symbol_index) {
            const db_bind_provider_symbol *symbol =
                &provider->symbols[symbol_index];
            const db_bind_symbol_flags valid_flags =
                DB_BIND_SYMBOL_HAS_NAME |
                DB_BIND_SYMBOL_HAS_ORDINAL;
            size_t other_symbol;

            if (symbol->flags == 0u ||
                (symbol->flags & (db_bind_symbol_flags)~valid_flags) != 0u ||
                symbol->reserved != 0u) {
                return DB_BIND_PROVIDER_SYMBOL_INVALID;
            }

            if (symbol->symbol_id == 0u) {
                return DB_BIND_SYMBOL_ID_INVALID;
            }

            if ((symbol->flags & DB_BIND_SYMBOL_HAS_NAME) != 0u) {
                if (symbol->name == NULL ||
                    symbol->name_length == 0u ||
                    !db_bind_ascii_is_printable(
                        (const uint8_t *)symbol->name,
                        symbol->name_length)) {
                    return DB_BIND_PROVIDER_SYMBOL_INVALID;
                }

                if (db_bind_ranges_overlap(bindings, binding_bytes,
                                           symbol->name,
                                           symbol->name_length) ||
                    db_bind_ranges_overlap(summary, sizeof(*summary),
                                           symbol->name,
                                           symbol->name_length)) {
                    return DB_BIND_BUFFER_OVERLAP;
                }
            } else if (symbol->name != NULL ||
                       symbol->name_length != 0u) {
                return DB_BIND_PROVIDER_SYMBOL_INVALID;
            }

            if ((symbol->flags & DB_BIND_SYMBOL_HAS_ORDINAL) == 0u &&
                symbol->ordinal != 0u) {
                return DB_BIND_PROVIDER_SYMBOL_INVALID;
            }

            for (other_symbol = 0u;
                 other_symbol < symbol_index;
                 ++other_symbol) {
                const db_bind_provider_symbol *other =
                    &provider->symbols[other_symbol];

                if (symbol->symbol_id == other->symbol_id) {
                    return DB_BIND_AMBIGUOUS_SYMBOL_ALIAS;
                }

                if ((symbol->flags & DB_BIND_SYMBOL_HAS_NAME) != 0u &&
                    (other->flags & DB_BIND_SYMBOL_HAS_NAME) != 0u &&
                    db_bind_symbol_names_equal(
                        (const uint8_t *)symbol->name,
                        symbol->name_length,
                        (const uint8_t *)other->name,
                        other->name_length)) {
                    return DB_BIND_DUPLICATE_SYMBOL_NAME;
                }

                if ((symbol->flags & DB_BIND_SYMBOL_HAS_ORDINAL) != 0u &&
                    (other->flags & DB_BIND_SYMBOL_HAS_ORDINAL) != 0u &&
                    symbol->ordinal == other->ordinal) {
                    return DB_BIND_DUPLICATE_SYMBOL_ORDINAL;
                }
            }
        }
    }

    return DB_BIND_OK;
}

static db_bind_status db_bind_validate_imports(
    const uint8_t *image,
    size_t mapped_size,
    const db_import_module *modules,
    size_t module_count,
    const db_import_record *records,
    size_t record_count)
{
    size_t expected_record = 0u;
    size_t module_index;

    if (module_count > (size_t)UINT32_MAX) {
        return DB_BIND_COUNT_OVERFLOW;
    }

    for (module_index = 0u;
         module_index < module_count;
         ++module_index) {
        const db_import_module *module = &modules[module_index];
        size_t record_end;
        size_t record_index;
        db_bind_status status;

        if (module->first_record != expected_record ||
            module->record_count > record_count - expected_record) {
            return DB_BIND_BAD_IMPORT_LAYOUT;
        }

        record_end = expected_record + module->record_count;

        status = db_bind_validate_image_string(
            image,
            mapped_size,
            module->name_rva,
            module->name_length,
            DB_BIND_IMPORT_MODULE_NAME_INVALID);
        if (status != DB_BIND_OK) {
            return status;
        }

        for (record_index = expected_record;
             record_index < record_end;
             ++record_index) {
            const db_import_record *record = &records[record_index];

            if (record->module_index != (uint32_t)module_index ||
                record->reserved[0] != 0u ||
                record->reserved[1] != 0u ||
                record->reserved[2] != 0u) {
                return DB_BIND_BAD_IMPORT_LAYOUT;
            }

            if (record->kind == DB_IMPORT_BY_NAME) {
                const uint64_t name_struct_rva =
                    (uint64_t)record->name_rva - UINT64_C(2);

                if (record->ordinal != 0u ||
                    record->name_rva < 2u ||
                    record->lookup_value != name_struct_rva) {
                    return DB_BIND_BAD_IMPORT_LAYOUT;
                }

                status = db_bind_validate_image_string(
                    image,
                    mapped_size,
                    record->name_rva,
                    record->name_length,
                    DB_BIND_IMPORT_SYMBOL_NAME_INVALID);
                if (status != DB_BIND_OK) {
                    return status;
                }
            } else if (record->kind == DB_IMPORT_BY_ORDINAL) {
                const uint64_t allowed =
                    DB_BIND_ORDINAL_FLAG | DB_BIND_ORDINAL_MASK;

                if (record->name_rva != 0u ||
                    record->name_length != 0u ||
                    record->hint != 0u ||
                    (record->lookup_value & DB_BIND_ORDINAL_FLAG) == 0u ||
                    (record->lookup_value & ~allowed) != 0u ||
                    (uint16_t)(record->lookup_value &
                               DB_BIND_ORDINAL_MASK) !=
                        record->ordinal) {
                    return DB_BIND_BAD_IMPORT_LAYOUT;
                }
            } else {
                return DB_BIND_BAD_IMPORT_LAYOUT;
            }
        }

        expected_record = record_end;
    }

    if (expected_record != record_count) {
        return DB_BIND_BAD_IMPORT_LAYOUT;
    }

    return DB_BIND_OK;
}

static size_t db_bind_find_provider(
    const uint8_t *image,
    const db_import_module *module,
    const db_bind_provider *providers,
    size_t provider_count)
{
    const uint8_t *module_name = image + module->name_rva;
    size_t provider_index;

    for (provider_index = 0u;
         provider_index < provider_count;
         ++provider_index) {
        const db_bind_provider *provider = &providers[provider_index];

        if (db_bind_module_names_equal(
                module_name,
                module->name_length,
                (const uint8_t *)provider->module_name,
                provider->module_name_length)) {
            return provider_index;
        }
    }

    return SIZE_MAX;
}

static size_t db_bind_find_symbol(
    const uint8_t *image,
    const db_import_record *record,
    const db_bind_provider *provider)
{
    size_t symbol_index;

    for (symbol_index = 0u;
         symbol_index < provider->symbol_count;
         ++symbol_index) {
        const db_bind_provider_symbol *symbol =
            &provider->symbols[symbol_index];

        if (record->kind == DB_IMPORT_BY_NAME) {
            if ((symbol->flags & DB_BIND_SYMBOL_HAS_NAME) != 0u &&
                db_bind_symbol_names_equal(
                    image + record->name_rva,
                    record->name_length,
                    (const uint8_t *)symbol->name,
                    symbol->name_length)) {
                return symbol_index;
            }
        } else if ((symbol->flags & DB_BIND_SYMBOL_HAS_ORDINAL) != 0u &&
                   record->ordinal == symbol->ordinal) {
            return symbol_index;
        }
    }

    return SIZE_MAX;
}

static db_bind_status db_bind_resolve_all(
    const uint8_t *image,
    const db_import_module *modules,
    size_t module_count,
    const db_import_record *records,
    const db_bind_provider *providers,
    size_t provider_count)
{
    size_t module_index;

    for (module_index = 0u;
         module_index < module_count;
         ++module_index) {
        const db_import_module *module = &modules[module_index];
        const size_t provider_index =
            db_bind_find_provider(image,
                                  module,
                                  providers,
                                  provider_count);
        size_t record_index;
        size_t record_end;

        if (provider_index == SIZE_MAX) {
            return DB_BIND_MODULE_NOT_FOUND;
        }

        record_end = module->first_record + module->record_count;
        for (record_index = module->first_record;
             record_index < record_end;
             ++record_index) {
            if (db_bind_find_symbol(
                    image,
                    &records[record_index],
                    &providers[provider_index]) == SIZE_MAX) {
                return DB_BIND_SYMBOL_NOT_FOUND;
            }
        }
    }

    return DB_BIND_OK;
}

static void db_bind_emit(
    const uint8_t *image,
    const db_import_module *modules,
    size_t module_count,
    const db_import_record *records,
    const db_bind_provider *providers,
    size_t provider_count,
    db_import_binding *bindings)
{
    size_t module_index;

    for (module_index = 0u;
         module_index < module_count;
         ++module_index) {
        const db_import_module *module = &modules[module_index];
        const size_t provider_index =
            db_bind_find_provider(image,
                                  module,
                                  providers,
                                  provider_count);
        const db_bind_provider *provider =
            &providers[provider_index];
        size_t record_index;
        const size_t record_end =
            module->first_record + module->record_count;

        for (record_index = module->first_record;
             record_index < record_end;
             ++record_index) {
            const size_t symbol_index =
                db_bind_find_symbol(image,
                                    &records[record_index],
                                    provider);
            db_import_binding planned;

            planned.module_index = (uint32_t)module_index;
            planned.provider_id = provider->provider_id;
            planned.symbol_id =
                provider->symbols[symbol_index].symbol_id;
            planned.reserved = 0u;
            bindings[record_index] = planned;
        }
    }
}

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
    db_bind_summary *summary)
{
    const uint8_t *image;
    size_t module_bytes;
    size_t record_bytes;
    size_t provider_bytes;
    size_t binding_bytes;
    db_bind_summary planned;
    db_bind_status status;

    if (mapped_image == NULL ||
        summary == NULL ||
        (module_count != 0u && modules == NULL) ||
        (record_count != 0u && records == NULL) ||
        (provider_count != 0u && providers == NULL) ||
        (binding_capacity != 0u && bindings == NULL)) {
        return DB_BIND_NULL_ARGUMENT;
    }

    if (mapped_size == 0u ||
        mapped_size > (size_t)UINT32_MAX) {
        return DB_BIND_BAD_MAPPED_IMAGE;
    }

    if (!db_bind_mul_size(module_count,
                          sizeof(*modules),
                          &module_bytes) ||
        !db_bind_mul_size(record_count,
                          sizeof(*records),
                          &record_bytes) ||
        !db_bind_mul_size(provider_count,
                          sizeof(*providers),
                          &provider_bytes) ||
        !db_bind_mul_size(binding_capacity,
                          sizeof(*bindings),
                          &binding_bytes)) {
        return DB_BIND_COUNT_OVERFLOW;
    }

    status = db_bind_validate_aliases(mapped_image,
                                      mapped_size,
                                      modules,
                                      module_bytes,
                                      records,
                                      record_bytes,
                                      providers,
                                      provider_bytes,
                                      bindings,
                                      binding_bytes,
                                      summary);
    if (status != DB_BIND_OK) {
        return status;
    }

    status = db_bind_validate_catalog(providers,
                                      provider_count,
                                      bindings,
                                      binding_bytes,
                                      summary);
    if (status != DB_BIND_OK) {
        return status;
    }

    image = (const uint8_t *)mapped_image;
    status = db_bind_validate_imports(image,
                                      mapped_size,
                                      modules,
                                      module_count,
                                      records,
                                      record_count);
    if (status != DB_BIND_OK) {
        return status;
    }

    status = db_bind_resolve_all(image,
                                 modules,
                                 module_count,
                                 records,
                                 providers,
                                 provider_count);
    if (status != DB_BIND_OK) {
        return status;
    }

    if (binding_capacity < record_count) {
        return DB_BIND_OUTPUT_BUFFER_TOO_SMALL;
    }

    if (record_count != 0u) {
        db_bind_emit(image,
                     modules,
                     module_count,
                     records,
                     providers,
                     provider_count,
                     bindings);
    }

    planned.module_count = module_count;
    planned.record_count = record_count;
    planned.binding_count = record_count;
    *summary = planned;

    return DB_BIND_OK;
}

const char *db_bind_status_name(db_bind_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "bad mapped image",
        "bad import layout",
        "import module name is invalid",
        "import symbol name is invalid",
        "provider module name is invalid",
        "provider identifier is invalid",
        "provider symbol is invalid",
        "symbol identifier is invalid",
        "duplicate provider module",
        "duplicate provider identifier",
        "duplicate symbol name",
        "duplicate symbol ordinal",
        "ambiguous symbol alias",
        "import module was not found",
        "import symbol was not found",
        "binding output buffer is too small"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown bind status";
    }

    return names[(size_t)status];
}
