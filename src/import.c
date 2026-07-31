#include "dblyat/import.h"

#include <stdbool.h>
#include <stdint.h>

#define DB_IMPORT_DOS_E_LFANEW_OFFSET 0x3cu
#define DB_IMPORT_COFF_HEADER_SIZE 20u
#define DB_IMPORT_SECTION_HEADER_SIZE 40u
#define DB_IMPORT_MAX_SECTIONS 96u
#define DB_IMPORT_MAX_MODULES 512u
#define DB_IMPORT_OPTIONAL_MAGIC_PE32_PLUS 0x020bu
#define DB_IMPORT_MACHINE_AMD64 0x8664u
#define DB_IMPORT_OPTIONAL_NUMBER_OF_RVA_SIZES_OFFSET 108u
#define DB_IMPORT_OPTIONAL_DATA_DIRECTORIES_OFFSET 112u
#define DB_IMPORT_DIRECTORY_ENTRY_SIZE 8u
#define DB_IMPORT_DIRECTORY_INDEX 1u
#define DB_IMPORT_DIRECTORY_OFFSET \
    (DB_IMPORT_OPTIONAL_DATA_DIRECTORIES_OFFSET + \
     DB_IMPORT_DIRECTORY_INDEX * DB_IMPORT_DIRECTORY_ENTRY_SIZE)
#define DB_IMPORT_DESCRIPTOR_SIZE 20u
#define DB_IMPORT_THUNK_SIZE 8u
#define DB_IMPORT_ORDINAL_FLAG UINT64_C(0x8000000000000000)
#define DB_IMPORT_ORDINAL_MASK UINT64_C(0x000000000000ffff)

#define DB_IMPORT_STRING_DLL 0u
#define DB_IMPORT_STRING_SYMBOL 1u

typedef struct db_import_span {
    uint32_t begin;
    uint32_t end;
} db_import_span;

typedef struct db_import_view {
    const uint8_t *image;
    uint32_t image_size;
    uint32_t headers_size;
    size_t page_size;
    const db_map_page_protection *page_plan;
    size_t page_count;
    db_import_span sections[DB_IMPORT_MAX_SECTIONS];
    size_t section_count;
    uint32_t directory_rva;
    uint32_t directory_size;
} db_import_view;

typedef struct db_import_module_plan {
    uint32_t name_rva;
    uint32_t name_length;
    uint32_t lookup_table_rva;
    uint32_t iat_rva;
    uint32_t iat_end;
    size_t first_record;
    size_t record_count;
} db_import_module_plan;

static bool db_import_add_size(size_t left, size_t right, size_t *result)
{
    if (left > SIZE_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool db_import_mul_size(size_t left, size_t right, size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_import_u32_add(uint32_t left,
                              uint32_t right,
                              uint32_t *result)
{
    if (left > UINT32_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool db_import_span_fits(size_t total,
                                size_t offset,
                                size_t length)
{
    return offset <= total && length <= total - offset;
}

static uint16_t db_import_read_u16(const uint8_t *bytes, size_t offset)
{
    return (uint16_t)((uint16_t)bytes[offset] |
                      ((uint16_t)bytes[offset + 1u] << 8u));
}

static uint32_t db_import_read_u32(const uint8_t *bytes, size_t offset)
{
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t db_import_read_u64(const uint8_t *bytes, size_t offset)
{
    return (uint64_t)db_import_read_u32(bytes, offset) |
           ((uint64_t)db_import_read_u32(bytes, offset + 4u) << 32u);
}

static bool db_import_is_power_of_two(size_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static size_t db_import_page_count(size_t image_size, size_t page_size)
{
    return image_size / page_size +
           (image_size % page_size != 0u ? 1u : 0u);
}

static bool db_import_ranges_overlap(const void *left,
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

static bool db_import_page_value_is_valid(
    db_map_page_protection protection)
{
    return protection == DB_MAP_PAGE_NONE ||
           protection == DB_MAP_PAGE_R ||
           protection == DB_MAP_PAGE_RW ||
           protection == DB_MAP_PAGE_RX;
}

static bool db_import_sections_overlap(const db_import_view *view)
{
    size_t index;

    for (index = 0u; index < view->section_count; ++index) {
        size_t other;

        if (view->sections[index].begin == view->sections[index].end) {
            continue;
        }

        for (other = index + 1u;
             other < view->section_count;
             ++other) {
            if (view->sections[other].begin == view->sections[other].end) {
                continue;
            }

            if (view->sections[index].begin < view->sections[other].end &&
                view->sections[other].begin < view->sections[index].end) {
                return true;
            }
        }
    }

    return false;
}

static bool db_import_span_is_mapped(const db_import_view *view,
                                     uint32_t begin,
                                     uint32_t size)
{
    uint32_t end;
    uint32_t cursor;

    if (size == 0u) {
        return true;
    }

    if (!db_import_u32_add(begin, size, &end) ||
        end > view->image_size) {
        return false;
    }

    cursor = begin;
    while (cursor < end) {
        uint32_t covered_end = cursor;
        size_t index;

        if (cursor < view->headers_size) {
            covered_end = view->headers_size;
        }

        for (index = 0u; index < view->section_count; ++index) {
            if (cursor >= view->sections[index].begin &&
                cursor < view->sections[index].end &&
                view->sections[index].end > covered_end) {
                covered_end = view->sections[index].end;
            }
        }

        if (covered_end <= cursor) {
            return false;
        }

        cursor = covered_end < end ? covered_end : end;
    }

    return true;
}

static db_import_status db_import_span_has_pages(
    const db_import_view *view,
    uint32_t begin,
    uint32_t size,
    db_import_status unmapped_status)
{
    uint32_t end;
    size_t first_page;
    size_t last_page;
    size_t page;

    if (size == 0u) {
        return DB_IMPORT_OK;
    }

    if (!db_import_u32_add(begin, size, &end) || end == 0u) {
        return unmapped_status;
    }

    first_page = (size_t)begin / view->page_size;
    last_page = ((size_t)end - 1u) / view->page_size;

    if (last_page >= view->page_count) {
        return unmapped_status;
    }

    for (page = first_page; page <= last_page; ++page) {
        if (!db_import_page_value_is_valid(view->page_plan[page])) {
            return DB_IMPORT_BAD_PAGE_PLAN;
        }

        if (view->page_plan[page] == DB_MAP_PAGE_NONE) {
            return unmapped_status;
        }
    }

    return DB_IMPORT_OK;
}

static db_import_status db_import_require_span(
    const db_import_view *view,
    uint32_t begin,
    uint32_t size,
    db_import_status unmapped_status)
{
    if (!db_import_span_is_mapped(view, begin, size)) {
        return unmapped_status;
    }

    return db_import_span_has_pages(view,
                                    begin,
                                    size,
                                    unmapped_status);
}

static db_import_status db_import_scan_ascii(
    const db_import_view *view,
    uint32_t rva,
    uint8_t string_kind,
    uint32_t *length)
{
    const db_import_status unmapped_status =
        string_kind == DB_IMPORT_STRING_DLL
            ? DB_IMPORT_DLL_NAME_UNMAPPED
            : DB_IMPORT_NAME_UNMAPPED;
    const db_import_status unterminated_status =
        string_kind == DB_IMPORT_STRING_DLL
            ? DB_IMPORT_DLL_NAME_UNTERMINATED
            : DB_IMPORT_NAME_UNTERMINATED;
    const db_import_status invalid_status =
        string_kind == DB_IMPORT_STRING_DLL
            ? DB_IMPORT_DLL_NAME_INVALID
            : DB_IMPORT_NAME_INVALID;
    uint32_t cursor;

    if (rva >= view->image_size) {
        return unmapped_status;
    }

    cursor = rva;
    while (cursor < view->image_size) {
        db_import_status status = db_import_require_span(view,
                                                         cursor,
                                                         1u,
                                                         unmapped_status);
        uint8_t value;

        if (status != DB_IMPORT_OK) {
            return status;
        }

        value = view->image[cursor];
        if (value == 0u) {
            if (cursor == rva) {
                return invalid_status;
            }

            *length = cursor - rva;
            return DB_IMPORT_OK;
        }

        if (value < 0x20u || value > 0x7eu) {
            return invalid_status;
        }

        ++cursor;
    }

    return unterminated_status;
}

static db_import_status db_import_build_view(
    const uint8_t *image,
    size_t mapped_size,
    const db_mapped_image *mapped,
    size_t page_size,
    const db_map_page_protection *page_plan,
    size_t page_capacity,
    db_import_view *view)
{
    size_t pe_offset;
    size_t coff_offset;
    size_t optional_offset;
    size_t optional_end;
    size_t section_table_offset;
    size_t section_table_size;
    uint16_t optional_size;
    uint16_t section_count;
    uint32_t number_of_directories;
    size_t expected_page_count;
    size_t index;

    if (!db_import_is_power_of_two(page_size)) {
        return DB_IMPORT_INVALID_PAGE_SIZE;
    }

    if (mapped->image_size == 0u ||
        mapped->headers_size == 0u ||
        mapped->headers_size > mapped->image_size ||
        mapped_size < (size_t)mapped->image_size ||
        mapped->section_count > DB_IMPORT_MAX_SECTIONS) {
        return DB_IMPORT_BAD_MAPPED_IMAGE;
    }

    expected_page_count =
        db_import_page_count((size_t)mapped->image_size, page_size);
    if (mapped->page_count != expected_page_count) {
        return DB_IMPORT_BAD_MAPPED_IMAGE;
    }

    if (page_capacity < expected_page_count) {
        return DB_IMPORT_PAGE_PLAN_TOO_SMALL;
    }

    for (index = 0u; index < expected_page_count; ++index) {
        if (!db_import_page_value_is_valid(page_plan[index])) {
            return DB_IMPORT_BAD_PAGE_PLAN;
        }
    }

    if (!db_import_span_fits((size_t)mapped->headers_size, 0u, 0x40u) ||
        image[0] != (uint8_t)'M' || image[1] != (uint8_t)'Z') {
        return DB_IMPORT_BAD_MAPPED_IMAGE;
    }

    pe_offset = (size_t)db_import_read_u32(
        image, DB_IMPORT_DOS_E_LFANEW_OFFSET);
    if (!db_import_span_fits((size_t)mapped->headers_size,
                             pe_offset,
                             4u) ||
        image[pe_offset] != (uint8_t)'P' ||
        image[pe_offset + 1u] != (uint8_t)'E' ||
        image[pe_offset + 2u] != 0u ||
        image[pe_offset + 3u] != 0u ||
        !db_import_add_size(pe_offset, 4u, &coff_offset) ||
        !db_import_span_fits((size_t)mapped->headers_size,
                             coff_offset,
                             DB_IMPORT_COFF_HEADER_SIZE)) {
        return DB_IMPORT_BAD_MAPPED_IMAGE;
    }

    if (db_import_read_u16(image, coff_offset) != DB_IMPORT_MACHINE_AMD64) {
        return DB_IMPORT_BAD_MAPPED_IMAGE;
    }

    section_count = db_import_read_u16(image, coff_offset + 2u);
    optional_size = db_import_read_u16(image, coff_offset + 16u);
    if (section_count != mapped->section_count ||
        section_count == 0u ||
        section_count > DB_IMPORT_MAX_SECTIONS ||
        !db_import_add_size(coff_offset,
                            DB_IMPORT_COFF_HEADER_SIZE,
                            &optional_offset) ||
        !db_import_add_size(optional_offset,
                            (size_t)optional_size,
                            &optional_end) ||
        optional_end > (size_t)mapped->headers_size ||
        optional_size < DB_IMPORT_OPTIONAL_DATA_DIRECTORIES_OFFSET) {
        return DB_IMPORT_BAD_MAPPED_IMAGE;
    }

    if (db_import_read_u16(image, optional_offset) !=
            DB_IMPORT_OPTIONAL_MAGIC_PE32_PLUS ||
        db_import_read_u32(image, optional_offset + 16u) !=
            mapped->entry_rva ||
        db_import_read_u64(image, optional_offset + 24u) !=
            mapped->preferred_base ||
        db_import_read_u32(image, optional_offset + 56u) !=
            mapped->image_size ||
        db_import_read_u32(image, optional_offset + 60u) !=
            mapped->headers_size) {
        return DB_IMPORT_BAD_MAPPED_IMAGE;
    }

    section_table_offset = optional_end;
    if (!db_import_mul_size((size_t)section_count,
                            DB_IMPORT_SECTION_HEADER_SIZE,
                            &section_table_size) ||
        !db_import_span_fits((size_t)mapped->headers_size,
                             section_table_offset,
                             section_table_size)) {
        return DB_IMPORT_BAD_MAPPED_IMAGE;
    }

    view->image = image;
    view->image_size = mapped->image_size;
    view->headers_size = mapped->headers_size;
    view->page_size = page_size;
    view->page_plan = page_plan;
    view->page_count = expected_page_count;
    view->section_count = section_count;
    view->directory_rva = 0u;
    view->directory_size = 0u;

    for (index = 0u; index < (size_t)section_count; ++index) {
        const size_t section_offset =
            section_table_offset + index * DB_IMPORT_SECTION_HEADER_SIZE;
        const uint32_t virtual_size =
            db_import_read_u32(image, section_offset + 8u);
        const uint32_t virtual_address =
            db_import_read_u32(image, section_offset + 12u);
        const uint32_t raw_size =
            db_import_read_u32(image, section_offset + 16u);
        const uint32_t memory_size =
            virtual_size != 0u ? virtual_size : raw_size;
        uint32_t virtual_end;

        if (memory_size == 0u) {
            if (virtual_address > mapped->image_size) {
                return DB_IMPORT_BAD_MAPPED_IMAGE;
            }

            view->sections[index].begin = virtual_address;
            view->sections[index].end = virtual_address;
            continue;
        }

        if (virtual_address < mapped->headers_size ||
            !db_import_u32_add(virtual_address,
                               memory_size,
                               &virtual_end) ||
            virtual_end > mapped->image_size) {
            return DB_IMPORT_BAD_MAPPED_IMAGE;
        }

        view->sections[index].begin = virtual_address;
        view->sections[index].end = virtual_end;
    }

    if (db_import_sections_overlap(view)) {
        return DB_IMPORT_BAD_MAPPED_IMAGE;
    }

    number_of_directories = db_import_read_u32(
        image,
        optional_offset + DB_IMPORT_OPTIONAL_NUMBER_OF_RVA_SIZES_OFFSET);
    if (number_of_directories <= DB_IMPORT_DIRECTORY_INDEX) {
        return DB_IMPORT_OK;
    }

    if ((size_t)optional_size <
        DB_IMPORT_DIRECTORY_OFFSET + DB_IMPORT_DIRECTORY_ENTRY_SIZE) {
        return DB_IMPORT_BAD_DIRECTORY;
    }

    view->directory_rva = db_import_read_u32(
        image, optional_offset + DB_IMPORT_DIRECTORY_OFFSET);
    view->directory_size = db_import_read_u32(
        image,
        optional_offset + DB_IMPORT_DIRECTORY_OFFSET + 4u);

    if ((view->directory_rva == 0u) !=
        (view->directory_size == 0u)) {
        return DB_IMPORT_BAD_DIRECTORY;
    }

    return DB_IMPORT_OK;
}

static bool db_import_descriptor_is_zero(const uint8_t *image,
                                         uint32_t descriptor_rva)
{
    size_t index;

    for (index = 0u; index < DB_IMPORT_DESCRIPTOR_SIZE; ++index) {
        if (image[(size_t)descriptor_rva + index] != 0u) {
            return false;
        }
    }

    return true;
}

static bool db_import_tail_is_zero(const uint8_t *image,
                                   uint32_t begin,
                                   uint32_t end)
{
    uint32_t cursor;

    for (cursor = begin; cursor < end; ++cursor) {
        if (image[cursor] != 0u) {
            return false;
        }
    }

    return true;
}

static bool db_import_spans_overlap_u32(uint32_t left_begin,
                                        uint32_t left_end,
                                        uint32_t right_begin,
                                        uint32_t right_end)
{
    return left_begin < right_end && right_begin < left_end;
}

static db_import_status db_import_validate_thunk_value(
    const db_import_view *view,
    uint64_t lookup_value)
{
    if ((lookup_value & DB_IMPORT_ORDINAL_FLAG) != 0u) {
        if ((lookup_value &
             ~(DB_IMPORT_ORDINAL_FLAG | DB_IMPORT_ORDINAL_MASK)) != 0u) {
            return DB_IMPORT_THUNK_VALUE_INVALID;
        }

        return DB_IMPORT_OK;
    }

    if (lookup_value == 0u || lookup_value > UINT32_MAX) {
        return DB_IMPORT_THUNK_VALUE_INVALID;
    }

    {
        const uint32_t name_struct_rva = (uint32_t)lookup_value;
        uint32_t name_rva;
        uint32_t name_length;
        db_import_status status;

        status = db_import_require_span(view,
                                        name_struct_rva,
                                        2u,
                                        DB_IMPORT_NAME_UNMAPPED);
        if (status != DB_IMPORT_OK) {
            return status;
        }

        if (!db_import_u32_add(name_struct_rva, 2u, &name_rva)) {
            return DB_IMPORT_NAME_UNMAPPED;
        }

        status = db_import_scan_ascii(view,
                                      name_rva,
                                      DB_IMPORT_STRING_SYMBOL,
                                      &name_length);
        if (status != DB_IMPORT_OK) {
            return status;
        }
    }

    return DB_IMPORT_OK;
}

static db_import_status db_import_walk_module(
    const db_import_view *view,
    uint32_t lookup_table_rva,
    uint32_t iat_rva,
    size_t *record_count,
    uint32_t *iat_end)
{
    uint32_t lookup_cursor = lookup_table_rva;
    uint32_t iat_cursor = iat_rva;
    size_t count = 0u;

    for (;;) {
        uint64_t lookup_value;
        uint64_t iat_value;
        db_import_status status;
        uint32_t next_lookup;
        uint32_t next_iat;

        status = db_import_require_span(view,
                                        lookup_cursor,
                                        DB_IMPORT_THUNK_SIZE,
                                        count == 0u
                                            ? DB_IMPORT_THUNK_UNMAPPED
                                            : DB_IMPORT_THUNK_UNTERMINATED);
        if (status != DB_IMPORT_OK) {
            return status;
        }

        status = db_import_require_span(view,
                                        iat_cursor,
                                        DB_IMPORT_THUNK_SIZE,
                                        count == 0u
                                            ? DB_IMPORT_THUNK_UNMAPPED
                                            : DB_IMPORT_THUNK_UNTERMINATED);
        if (status != DB_IMPORT_OK) {
            return status;
        }

        lookup_value = db_import_read_u64(view->image,
                                          (size_t)lookup_cursor);
        iat_value = db_import_read_u64(view->image, (size_t)iat_cursor);

        if (lookup_value == 0u) {
            if (iat_value != 0u) {
                return DB_IMPORT_THUNK_VALUE_INVALID;
            }

            if (!db_import_u32_add(iat_cursor,
                                   DB_IMPORT_THUNK_SIZE,
                                   iat_end)) {
                return DB_IMPORT_THUNK_UNTERMINATED;
            }

            *record_count = count;
            return DB_IMPORT_OK;
        }

        status = db_import_validate_thunk_value(view, lookup_value);
        if (status != DB_IMPORT_OK) {
            return status;
        }

        if (count == SIZE_MAX) {
            return DB_IMPORT_RECORD_CAPACITY_OVERFLOW;
        }
        ++count;

        if (!db_import_u32_add(lookup_cursor,
                               DB_IMPORT_THUNK_SIZE,
                               &next_lookup) ||
            !db_import_u32_add(iat_cursor,
                               DB_IMPORT_THUNK_SIZE,
                               &next_iat)) {
            return DB_IMPORT_THUNK_UNTERMINATED;
        }

        lookup_cursor = next_lookup;
        iat_cursor = next_iat;
    }
}

static db_import_status db_import_plan_modules(
    const db_import_view *view,
    db_import_module_plan *plans,
    size_t *module_count,
    size_t *record_count)
{
    uint32_t directory_end;
    uint32_t cursor;
    size_t modules = 0u;
    size_t records = 0u;
    bool terminated = false;

    if (view->directory_size == 0u) {
        *module_count = 0u;
        *record_count = 0u;
        return DB_IMPORT_OK;
    }

    if (!db_import_u32_add(view->directory_rva,
                           view->directory_size,
                           &directory_end) ||
        directory_end > view->image_size) {
        return DB_IMPORT_DIRECTORY_OUT_OF_RANGE;
    }

    {
        db_import_status status = db_import_require_span(
            view,
            view->directory_rva,
            view->directory_size,
            DB_IMPORT_DIRECTORY_UNMAPPED);

        if (status != DB_IMPORT_OK) {
            return status;
        }
    }

    cursor = view->directory_rva;
    while (cursor < directory_end) {
        uint32_t descriptor_end;
        uint32_t original_first_thunk;
        uint32_t name_rva;
        uint32_t first_thunk;
        uint32_t lookup_table_rva;
        uint32_t name_length;
        uint32_t iat_end;
        size_t module_records;
        size_t index;
        db_import_status status;

        if (directory_end - cursor < DB_IMPORT_DESCRIPTOR_SIZE) {
            return DB_IMPORT_DESCRIPTOR_UNTERMINATED;
        }

        if (!db_import_u32_add(cursor,
                               DB_IMPORT_DESCRIPTOR_SIZE,
                               &descriptor_end)) {
            return DB_IMPORT_BAD_DIRECTORY;
        }

        if (db_import_descriptor_is_zero(view->image, cursor)) {
            terminated = true;
            if (!db_import_tail_is_zero(view->image,
                                        descriptor_end,
                                        directory_end)) {
                return DB_IMPORT_BAD_DIRECTORY;
            }
            break;
        }

        if (modules >= DB_IMPORT_MAX_MODULES) {
            return DB_IMPORT_TOO_MANY_MODULES;
        }

        original_first_thunk = db_import_read_u32(view->image,
                                                  (size_t)cursor);
        name_rva = db_import_read_u32(view->image,
                                      (size_t)cursor + 12u);
        first_thunk = db_import_read_u32(view->image,
                                         (size_t)cursor + 16u);

        if (name_rva == 0u || first_thunk == 0u) {
            return DB_IMPORT_BAD_DESCRIPTOR;
        }

        lookup_table_rva = original_first_thunk != 0u
                               ? original_first_thunk
                               : first_thunk;

        status = db_import_scan_ascii(view,
                                      name_rva,
                                      DB_IMPORT_STRING_DLL,
                                      &name_length);
        if (status != DB_IMPORT_OK) {
            return status;
        }

        status = db_import_walk_module(view,
                                       lookup_table_rva,
                                       first_thunk,
                                       &module_records,
                                       &iat_end);
        if (status != DB_IMPORT_OK) {
            return status;
        }

        if (db_import_spans_overlap_u32(first_thunk,
                                        iat_end,
                                        view->directory_rva,
                                        directory_end)) {
            return DB_IMPORT_IAT_OVERLAP;
        }

        for (index = 0u; index < modules; ++index) {
            if (db_import_spans_overlap_u32(first_thunk,
                                            iat_end,
                                            plans[index].iat_rva,
                                            plans[index].iat_end)) {
                return DB_IMPORT_IAT_OVERLAP;
            }
        }

        if (module_records > SIZE_MAX - records) {
            return DB_IMPORT_RECORD_CAPACITY_OVERFLOW;
        }

        plans[modules].name_rva = name_rva;
        plans[modules].name_length = name_length;
        plans[modules].lookup_table_rva = lookup_table_rva;
        plans[modules].iat_rva = first_thunk;
        plans[modules].iat_end = iat_end;
        plans[modules].first_record = records;
        plans[modules].record_count = module_records;

        records += module_records;
        ++modules;
        cursor = descriptor_end;
    }

    if (!terminated) {
        return DB_IMPORT_DESCRIPTOR_UNTERMINATED;
    }

    *module_count = modules;
    *record_count = records;
    return DB_IMPORT_OK;
}

static void db_import_emit_records(const db_import_view *view,
                                   const db_import_module_plan *plans,
                                   size_t module_count,
                                   db_import_module *modules,
                                   db_import_record *records)
{
    size_t module_index;

    for (module_index = 0u;
         module_index < module_count;
         ++module_index) {
        const db_import_module_plan *plan = &plans[module_index];
        uint32_t lookup_cursor = plan->lookup_table_rva;
        uint32_t iat_cursor = plan->iat_rva;
        size_t record_index;

        modules[module_index].name_rva = plan->name_rva;
        modules[module_index].name_length = plan->name_length;
        modules[module_index].lookup_table_rva =
            plan->lookup_table_rva;
        modules[module_index].iat_rva = plan->iat_rva;
        modules[module_index].first_record = plan->first_record;
        modules[module_index].record_count = plan->record_count;

        for (record_index = 0u;
             record_index < plan->record_count;
             ++record_index) {
            const size_t output_index =
                plan->first_record + record_index;
            const uint64_t lookup_value = db_import_read_u64(
                view->image, (size_t)lookup_cursor);
            db_import_record *record = &records[output_index];

            record->module_index = (uint32_t)module_index;
            record->lookup_rva = lookup_cursor;
            record->iat_rva = iat_cursor;
            record->lookup_value = lookup_value;
            record->original_iat_value = db_import_read_u64(
                view->image, (size_t)iat_cursor);
            record->reserved[0] = 0u;
            record->reserved[1] = 0u;
            record->reserved[2] = 0u;

            if ((lookup_value & DB_IMPORT_ORDINAL_FLAG) != 0u) {
                record->kind = DB_IMPORT_BY_ORDINAL;
                record->name_rva = 0u;
                record->name_length = 0u;
                record->hint = 0u;
                record->ordinal = (uint16_t)(lookup_value &
                                              DB_IMPORT_ORDINAL_MASK);
            } else {
                const uint32_t name_struct_rva =
                    (uint32_t)lookup_value;
                uint32_t name_length = 0u;

                (void)db_import_scan_ascii(view,
                                           name_struct_rva + 2u,
                                           DB_IMPORT_STRING_SYMBOL,
                                           &name_length);

                record->kind = DB_IMPORT_BY_NAME;
                record->name_rva = name_struct_rva + 2u;
                record->name_length = name_length;
                record->hint = db_import_read_u16(
                    view->image, (size_t)name_struct_rva);
                record->ordinal = 0u;
            }

            lookup_cursor += DB_IMPORT_THUNK_SIZE;
            iat_cursor += DB_IMPORT_THUNK_SIZE;
        }
    }
}

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
    db_import_summary *summary)
{
    const uint8_t *image;
    db_import_view view;
    db_import_module_plan plans[DB_IMPORT_MAX_MODULES];
    db_import_summary planned;
    db_import_status status;
    size_t module_bytes;
    size_t record_bytes;
    size_t module_count;
    size_t record_count;

    if (mapped_image == NULL || mapped == NULL || page_plan == NULL ||
        summary == NULL ||
        (modules == NULL && module_capacity != 0u) ||
        (records == NULL && record_capacity != 0u)) {
        return DB_IMPORT_NULL_ARGUMENT;
    }

    if (!db_import_mul_size(module_capacity,
                            sizeof(*modules),
                            &module_bytes)) {
        return DB_IMPORT_MODULE_CAPACITY_OVERFLOW;
    }

    if (!db_import_mul_size(record_capacity,
                            sizeof(*records),
                            &record_bytes)) {
        return DB_IMPORT_RECORD_CAPACITY_OVERFLOW;
    }

    if (db_import_ranges_overlap(mapped_image, mapped_size,
                                 mapped, sizeof(*mapped)) ||
        db_import_ranges_overlap(mapped_image, mapped_size,
                                 page_plan, page_capacity) ||
        db_import_ranges_overlap(mapped_image, mapped_size,
                                 modules, module_bytes) ||
        db_import_ranges_overlap(mapped_image, mapped_size,
                                 records, record_bytes) ||
        db_import_ranges_overlap(mapped_image, mapped_size,
                                 summary, sizeof(*summary)) ||
        db_import_ranges_overlap(mapped, sizeof(*mapped),
                                 page_plan, page_capacity) ||
        db_import_ranges_overlap(mapped, sizeof(*mapped),
                                 modules, module_bytes) ||
        db_import_ranges_overlap(mapped, sizeof(*mapped),
                                 records, record_bytes) ||
        db_import_ranges_overlap(mapped, sizeof(*mapped),
                                 summary, sizeof(*summary)) ||
        db_import_ranges_overlap(page_plan, page_capacity,
                                 modules, module_bytes) ||
        db_import_ranges_overlap(page_plan, page_capacity,
                                 records, record_bytes) ||
        db_import_ranges_overlap(page_plan, page_capacity,
                                 summary, sizeof(*summary)) ||
        db_import_ranges_overlap(modules, module_bytes,
                                 records, record_bytes) ||
        db_import_ranges_overlap(modules, module_bytes,
                                 summary, sizeof(*summary)) ||
        db_import_ranges_overlap(records, record_bytes,
                                 summary, sizeof(*summary))) {
        return DB_IMPORT_BUFFER_OVERLAP;
    }

    image = (const uint8_t *)mapped_image;
    status = db_import_build_view(image,
                                  mapped_size,
                                  mapped,
                                  page_size,
                                  page_plan,
                                  page_capacity,
                                  &view);
    if (status != DB_IMPORT_OK) {
        return status;
    }

    status = db_import_plan_modules(&view,
                                    plans,
                                    &module_count,
                                    &record_count);
    if (status != DB_IMPORT_OK) {
        return status;
    }

    if (module_capacity < module_count) {
        return DB_IMPORT_MODULE_BUFFER_TOO_SMALL;
    }

    if (record_capacity < record_count) {
        return DB_IMPORT_RECORD_BUFFER_TOO_SMALL;
    }

    if (module_count != 0u || record_count != 0u) {
        db_import_emit_records(&view,
                               plans,
                               module_count,
                               modules,
                               records);
    }

    planned.directory_rva = view.directory_rva;
    planned.directory_size = view.directory_size;
    planned.module_count = module_count;
    planned.record_count = record_count;
    *summary = planned;

    return DB_IMPORT_OK;
}

const char *db_import_status_name(db_import_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "invalid host page size",
        "bad mapped image",
        "page plan is too small",
        "bad page plan",
        "module capacity overflows",
        "record capacity overflows",
        "buffer overlap",
        "bad import directory",
        "import directory is out of range",
        "import directory is not mapped",
        "too many import modules",
        "bad import descriptor",
        "import descriptor array is unterminated",
        "DLL name is not mapped",
        "DLL name is unterminated",
        "DLL name is invalid",
        "import thunk is not mapped",
        "import thunk chain is unterminated",
        "import thunk value is invalid",
        "import name is not mapped",
        "import name is unterminated",
        "import name is invalid",
        "IAT ranges overlap",
        "module buffer is too small",
        "record buffer is too small"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown import status";
    }

    return names[(size_t)status];
}
