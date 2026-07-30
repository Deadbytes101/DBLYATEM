#include "dblyat/reloc_apply.h"

#include <stdbool.h>
#include <stdint.h>

#define DB_RELOC_APPLY_DOS_E_LFANEW_OFFSET 0x3cu
#define DB_RELOC_APPLY_COFF_HEADER_SIZE 20u
#define DB_RELOC_APPLY_SECTION_HEADER_SIZE 40u
#define DB_RELOC_APPLY_MAX_SECTIONS 96u
#define DB_RELOC_APPLY_OPTIONAL_MAGIC_PE32_PLUS 0x020bu
#define DB_RELOC_APPLY_MACHINE_AMD64 0x8664u
#define DB_RELOC_APPLY_OPTIONAL_NUMBER_OF_RVA_SIZES_OFFSET 108u
#define DB_RELOC_APPLY_OPTIONAL_DATA_DIRECTORIES_OFFSET 112u
#define DB_RELOC_APPLY_DIRECTORY_ENTRY_SIZE 8u
#define DB_RELOC_APPLY_BASE_DIRECTORY_INDEX 5u
#define DB_RELOC_APPLY_BASE_DIRECTORY_OFFSET \
    (DB_RELOC_APPLY_OPTIONAL_DATA_DIRECTORIES_OFFSET + \
     DB_RELOC_APPLY_BASE_DIRECTORY_INDEX * \
         DB_RELOC_APPLY_DIRECTORY_ENTRY_SIZE)
#define DB_RELOC_APPLY_BLOCK_HEADER_SIZE 8u
#define DB_RELOC_APPLY_PE_PAGE_SIZE 0x1000u
#define DB_RELOC_APPLY_TYPE_ABSOLUTE 0u
#define DB_RELOC_APPLY_TYPE_DIR64 10u

typedef struct db_reloc_apply_span {
    uint32_t begin;
    uint32_t end;
} db_reloc_apply_span;

typedef struct db_reloc_apply_view {
    uint8_t *image;
    uint32_t image_size;
    uint32_t headers_size;
    size_t page_size;
    const db_map_page_protection *page_plan;
    size_t page_count;
    db_reloc_apply_span sections[DB_RELOC_APPLY_MAX_SECTIONS];
    size_t section_count;
    uint32_t directory_rva;
    uint32_t directory_size;
} db_reloc_apply_view;

static bool db_reloc_apply_add_size(size_t left,
                                    size_t right,
                                    size_t *result)
{
    if (left > SIZE_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool db_reloc_apply_mul_size(size_t left,
                                    size_t right,
                                    size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_reloc_apply_u32_add(uint32_t left,
                                   uint32_t right,
                                   uint32_t *result)
{
    if (left > UINT32_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool db_reloc_apply_span_fits(size_t total,
                                     size_t offset,
                                     size_t length)
{
    return offset <= total && length <= total - offset;
}

static uint16_t db_reloc_apply_read_u16(const uint8_t *bytes, size_t offset)
{
    return (uint16_t)((uint16_t)bytes[offset] |
                      ((uint16_t)bytes[offset + 1u] << 8u));
}

static uint32_t db_reloc_apply_read_u32(const uint8_t *bytes, size_t offset)
{
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t db_reloc_apply_read_u64(const uint8_t *bytes, size_t offset)
{
    return (uint64_t)db_reloc_apply_read_u32(bytes, offset) |
           ((uint64_t)db_reloc_apply_read_u32(bytes, offset + 4u) << 32u);
}

static void db_reloc_apply_write_u64(uint8_t *bytes,
                                     size_t offset,
                                     uint64_t value)
{
    bytes[offset] = (uint8_t)(value & 0xffu);
    bytes[offset + 1u] = (uint8_t)((value >> 8u) & 0xffu);
    bytes[offset + 2u] = (uint8_t)((value >> 16u) & 0xffu);
    bytes[offset + 3u] = (uint8_t)((value >> 24u) & 0xffu);
    bytes[offset + 4u] = (uint8_t)((value >> 32u) & 0xffu);
    bytes[offset + 5u] = (uint8_t)((value >> 40u) & 0xffu);
    bytes[offset + 6u] = (uint8_t)((value >> 48u) & 0xffu);
    bytes[offset + 7u] = (uint8_t)(value >> 56u);
}

static bool db_reloc_apply_is_power_of_two(size_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static size_t db_reloc_apply_page_count(size_t image_size, size_t page_size)
{
    return image_size / page_size +
           (image_size % page_size != 0u ? 1u : 0u);
}

static bool db_reloc_apply_ranges_overlap(const void *left,
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

static bool db_reloc_apply_page_value_is_valid(
    db_map_page_protection protection)
{
    return protection == DB_MAP_PAGE_NONE ||
           protection == DB_MAP_PAGE_R ||
           protection == DB_MAP_PAGE_RW ||
           protection == DB_MAP_PAGE_RX;
}

static bool db_reloc_apply_span_is_mapped(
    const db_reloc_apply_view *view,
    uint32_t begin,
    uint32_t size)
{
    uint32_t end;
    uint32_t cursor;

    if (size == 0u) {
        return true;
    }

    if (!db_reloc_apply_u32_add(begin, size, &end) ||
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

static db_reloc_apply_status db_reloc_apply_span_has_pages(
    const db_reloc_apply_view *view,
    uint32_t begin,
    uint32_t size)
{
    uint32_t end;
    size_t first_page;
    size_t last_page;
    size_t page;

    if (size == 0u) {
        return DB_RELOC_APPLY_OK;
    }

    if (!db_reloc_apply_u32_add(begin, size, &end) || end == 0u) {
        return DB_RELOC_APPLY_TARGET_OUT_OF_RANGE;
    }

    first_page = (size_t)begin / view->page_size;
    last_page = ((size_t)end - 1u) / view->page_size;

    if (last_page >= view->page_count) {
        return DB_RELOC_APPLY_TARGET_UNMAPPED;
    }

    for (page = first_page; page <= last_page; ++page) {
        if (!db_reloc_apply_page_value_is_valid(view->page_plan[page])) {
            return DB_RELOC_APPLY_BAD_PAGE_PLAN;
        }

        if (view->page_plan[page] == DB_MAP_PAGE_NONE) {
            return DB_RELOC_APPLY_TARGET_UNMAPPED;
        }
    }

    return DB_RELOC_APPLY_OK;
}

static bool db_reloc_apply_sections_overlap(
    const db_reloc_apply_view *view)
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

static db_reloc_apply_status db_reloc_apply_build_view(
    uint8_t *image,
    size_t mapped_size,
    const db_mapped_image *mapped,
    size_t page_size,
    const db_map_page_protection *page_plan,
    size_t page_capacity,
    db_reloc_apply_view *view)
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

    if (!db_reloc_apply_is_power_of_two(page_size)) {
        return DB_RELOC_APPLY_INVALID_PAGE_SIZE;
    }

    if (mapped->image_size == 0u ||
        mapped->headers_size == 0u ||
        mapped->headers_size > mapped->image_size ||
        mapped_size < (size_t)mapped->image_size ||
        mapped->section_count > DB_RELOC_APPLY_MAX_SECTIONS) {
        return DB_RELOC_APPLY_BAD_MAPPED_IMAGE;
    }

    expected_page_count = db_reloc_apply_page_count(
        (size_t)mapped->image_size, page_size);
    if (mapped->page_count != expected_page_count) {
        return DB_RELOC_APPLY_BAD_MAPPED_IMAGE;
    }

    if (page_capacity < expected_page_count) {
        return DB_RELOC_APPLY_PAGE_PLAN_TOO_SMALL;
    }

    if (!db_reloc_apply_span_fits(
            (size_t)mapped->headers_size, 0u, 0x40u) ||
        image[0] != (uint8_t)'M' || image[1] != (uint8_t)'Z') {
        return DB_RELOC_APPLY_BAD_MAPPED_IMAGE;
    }

    pe_offset = (size_t)db_reloc_apply_read_u32(
        image, DB_RELOC_APPLY_DOS_E_LFANEW_OFFSET);
    if (!db_reloc_apply_span_fits((size_t)mapped->headers_size,
                                  pe_offset,
                                  4u) ||
        image[pe_offset] != (uint8_t)'P' ||
        image[pe_offset + 1u] != (uint8_t)'E' ||
        image[pe_offset + 2u] != 0u ||
        image[pe_offset + 3u] != 0u ||
        !db_reloc_apply_add_size(pe_offset, 4u, &coff_offset) ||
        !db_reloc_apply_span_fits((size_t)mapped->headers_size,
                                  coff_offset,
                                  DB_RELOC_APPLY_COFF_HEADER_SIZE)) {
        return DB_RELOC_APPLY_BAD_MAPPED_IMAGE;
    }

    if (db_reloc_apply_read_u16(image, coff_offset) !=
        DB_RELOC_APPLY_MACHINE_AMD64) {
        return DB_RELOC_APPLY_BAD_MAPPED_IMAGE;
    }

    section_count = db_reloc_apply_read_u16(image, coff_offset + 2u);
    optional_size = db_reloc_apply_read_u16(image, coff_offset + 16u);
    if (section_count != mapped->section_count ||
        section_count == 0u ||
        section_count > DB_RELOC_APPLY_MAX_SECTIONS ||
        !db_reloc_apply_add_size(coff_offset,
                                 DB_RELOC_APPLY_COFF_HEADER_SIZE,
                                 &optional_offset) ||
        !db_reloc_apply_add_size(optional_offset,
                                 (size_t)optional_size,
                                 &optional_end) ||
        optional_end > (size_t)mapped->headers_size ||
        optional_size <
            DB_RELOC_APPLY_OPTIONAL_DATA_DIRECTORIES_OFFSET) {
        return DB_RELOC_APPLY_BAD_MAPPED_IMAGE;
    }

    if (db_reloc_apply_read_u16(image, optional_offset) !=
            DB_RELOC_APPLY_OPTIONAL_MAGIC_PE32_PLUS ||
        db_reloc_apply_read_u32(image, optional_offset + 16u) !=
            mapped->entry_rva ||
        db_reloc_apply_read_u64(image, optional_offset + 24u) !=
            mapped->preferred_base ||
        db_reloc_apply_read_u32(image, optional_offset + 56u) !=
            mapped->image_size ||
        db_reloc_apply_read_u32(image, optional_offset + 60u) !=
            mapped->headers_size) {
        return DB_RELOC_APPLY_BAD_MAPPED_IMAGE;
    }

    section_table_offset = optional_end;
    if (!db_reloc_apply_mul_size((size_t)section_count,
                                 DB_RELOC_APPLY_SECTION_HEADER_SIZE,
                                 &section_table_size) ||
        !db_reloc_apply_span_fits((size_t)mapped->headers_size,
                                  section_table_offset,
                                  section_table_size)) {
        return DB_RELOC_APPLY_BAD_MAPPED_IMAGE;
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
        const size_t section_offset = section_table_offset +
            index * DB_RELOC_APPLY_SECTION_HEADER_SIZE;
        const uint32_t virtual_size = db_reloc_apply_read_u32(
            image, section_offset + 8u);
        const uint32_t virtual_address = db_reloc_apply_read_u32(
            image, section_offset + 12u);
        const uint32_t raw_size = db_reloc_apply_read_u32(
            image, section_offset + 16u);
        const uint32_t memory_size =
            virtual_size != 0u ? virtual_size : raw_size;
        uint32_t virtual_end;

        if (memory_size == 0u) {
            if (virtual_address > mapped->image_size) {
                return DB_RELOC_APPLY_BAD_MAPPED_IMAGE;
            }

            view->sections[index].begin = virtual_address;
            view->sections[index].end = virtual_address;
            continue;
        }

        if (virtual_address < mapped->headers_size ||
            !db_reloc_apply_u32_add(virtual_address,
                                    memory_size,
                                    &virtual_end) ||
            virtual_end > mapped->image_size) {
            return DB_RELOC_APPLY_BAD_MAPPED_IMAGE;
        }

        view->sections[index].begin = virtual_address;
        view->sections[index].end = virtual_end;
    }

    if (db_reloc_apply_sections_overlap(view)) {
        return DB_RELOC_APPLY_BAD_MAPPED_IMAGE;
    }

    number_of_directories = db_reloc_apply_read_u32(
        image,
        optional_offset +
            DB_RELOC_APPLY_OPTIONAL_NUMBER_OF_RVA_SIZES_OFFSET);
    if (number_of_directories <= DB_RELOC_APPLY_BASE_DIRECTORY_INDEX) {
        return DB_RELOC_APPLY_OK;
    }

    if ((size_t)optional_size <
        DB_RELOC_APPLY_BASE_DIRECTORY_OFFSET +
            DB_RELOC_APPLY_DIRECTORY_ENTRY_SIZE) {
        return DB_RELOC_APPLY_BAD_DIRECTORY;
    }

    view->directory_rva = db_reloc_apply_read_u32(
        image, optional_offset + DB_RELOC_APPLY_BASE_DIRECTORY_OFFSET);
    view->directory_size = db_reloc_apply_read_u32(
        image,
        optional_offset + DB_RELOC_APPLY_BASE_DIRECTORY_OFFSET + 4u);

    if ((view->directory_rva == 0u) !=
        (view->directory_size == 0u)) {
        return DB_RELOC_APPLY_BAD_DIRECTORY;
    }

    return DB_RELOC_APPLY_OK;
}

static bool db_reloc_apply_spans_overlap(uint32_t left,
                                         uint32_t right)
{
    return left < right + 8u && right < left + 8u;
}

static db_reloc_apply_status db_reloc_apply_adjust_value(
    uint64_t original,
    uint64_t preferred_base,
    uint64_t loaded_base,
    uint64_t *adjusted)
{
    if (loaded_base >= preferred_base) {
        const uint64_t delta = loaded_base - preferred_base;

        if (original > UINT64_MAX - delta) {
            return DB_RELOC_APPLY_VALUE_OVERFLOW;
        }

        *adjusted = original + delta;
    } else {
        const uint64_t delta = preferred_base - loaded_base;

        if (original < delta) {
            return DB_RELOC_APPLY_VALUE_OVERFLOW;
        }

        *adjusted = original - delta;
    }

    return DB_RELOC_APPLY_OK;
}

static db_reloc_apply_status db_reloc_apply_preflight_record(
    const db_reloc_apply_view *view,
    const db_reloc_record *records,
    size_t index,
    uint64_t preferred_base,
    uint64_t loaded_base)
{
    const db_reloc_record *record = &records[index];
    uint32_t target_end;
    uint32_t directory_end;
    uint64_t adjusted;
    size_t other;
    db_reloc_apply_status status;

    if (record->type != DB_RELOC_APPLY_TYPE_DIR64 ||
        record->reserved != 0u) {
        return DB_RELOC_APPLY_RECORD_MISMATCH;
    }

    if (!db_reloc_apply_u32_add(record->target_rva, 8u, &target_end) ||
        target_end > view->image_size) {
        return DB_RELOC_APPLY_TARGET_OUT_OF_RANGE;
    }

    if (!db_reloc_apply_span_is_mapped(view, record->target_rva, 8u)) {
        return DB_RELOC_APPLY_TARGET_UNMAPPED;
    }

    status = db_reloc_apply_span_has_pages(
        view, record->target_rva, 8u);
    if (status != DB_RELOC_APPLY_OK) {
        return status;
    }

    if (view->directory_size != 0u &&
        db_reloc_apply_u32_add(view->directory_rva,
                               view->directory_size,
                               &directory_end) &&
        record->target_rva < directory_end &&
        view->directory_rva < target_end) {
        return DB_RELOC_APPLY_TARGET_OVERLAPS_DIRECTORY;
    }

    for (other = 0u; other < index; ++other) {
        if (records[other].target_rva == record->target_rva) {
            return DB_RELOC_APPLY_DUPLICATE_TARGET;
        }

        if (db_reloc_apply_spans_overlap(records[other].target_rva,
                                          record->target_rva)) {
            return DB_RELOC_APPLY_TARGET_OVERLAP;
        }
    }

    if (db_reloc_apply_read_u64(view->image,
                                (size_t)record->target_rva) !=
        record->original_value) {
        return DB_RELOC_APPLY_STALE_VALUE;
    }

    return db_reloc_apply_adjust_value(record->original_value,
                                       preferred_base,
                                       loaded_base,
                                       &adjusted);
}

static db_reloc_apply_status db_reloc_apply_validate_directory(
    const db_reloc_apply_view *view,
    const db_reloc_record *records,
    size_t record_count)
{
    uint32_t directory_end;
    uint32_t cursor;
    size_t record_index = 0u;
    db_reloc_apply_status status;

    if (view->directory_size == 0u) {
        return record_count == 0u ? DB_RELOC_APPLY_OK :
            DB_RELOC_APPLY_RECORD_COUNT_MISMATCH;
    }

    if (!db_reloc_apply_u32_add(view->directory_rva,
                                view->directory_size,
                                &directory_end) ||
        directory_end > view->image_size) {
        return DB_RELOC_APPLY_BAD_DIRECTORY;
    }

    if (!db_reloc_apply_span_is_mapped(view,
                                       view->directory_rva,
                                       view->directory_size)) {
        return DB_RELOC_APPLY_BAD_DIRECTORY;
    }

    status = db_reloc_apply_span_has_pages(
        view, view->directory_rva, view->directory_size);
    if (status != DB_RELOC_APPLY_OK) {
        return status == DB_RELOC_APPLY_BAD_PAGE_PLAN ? status :
            DB_RELOC_APPLY_BAD_DIRECTORY;
    }

    cursor = view->directory_rva;
    while (cursor < directory_end) {
        uint32_t block_size;
        uint32_t block_end;
        uint32_t page_rva;
        uint32_t entry_cursor;

        if (directory_end - cursor < DB_RELOC_APPLY_BLOCK_HEADER_SIZE) {
            return DB_RELOC_APPLY_BAD_BLOCK;
        }

        page_rva = db_reloc_apply_read_u32(
            view->image, (size_t)cursor);
        block_size = db_reloc_apply_read_u32(
            view->image, (size_t)cursor + 4u);

        if (block_size < DB_RELOC_APPLY_BLOCK_HEADER_SIZE ||
            (block_size & 1u) != 0u ||
            (page_rva % DB_RELOC_APPLY_PE_PAGE_SIZE) != 0u ||
            page_rva >= view->image_size ||
            !db_reloc_apply_u32_add(cursor, block_size, &block_end) ||
            block_end > directory_end) {
            return DB_RELOC_APPLY_BAD_BLOCK;
        }

        entry_cursor = cursor + DB_RELOC_APPLY_BLOCK_HEADER_SIZE;
        while (entry_cursor < block_end) {
            const uint16_t entry = db_reloc_apply_read_u16(
                view->image, (size_t)entry_cursor);
            const uint16_t type = (uint16_t)(entry >> 12u);
            const uint32_t offset = (uint32_t)(entry & 0x0fffu);
            uint32_t target_rva;

            if (type == DB_RELOC_APPLY_TYPE_ABSOLUTE) {
                entry_cursor += 2u;
                continue;
            }

            if (type != DB_RELOC_APPLY_TYPE_DIR64) {
                return DB_RELOC_APPLY_UNSUPPORTED_TYPE;
            }

            if (!db_reloc_apply_u32_add(page_rva,
                                        offset,
                                        &target_rva)) {
                return DB_RELOC_APPLY_TARGET_OUT_OF_RANGE;
            }

            if (record_index >= record_count) {
                return DB_RELOC_APPLY_RECORD_COUNT_MISMATCH;
            }

            if (records[record_index].target_rva != target_rva ||
                records[record_index].type != type ||
                records[record_index].reserved != 0u) {
                return DB_RELOC_APPLY_RECORD_MISMATCH;
            }

            ++record_index;
            entry_cursor += 2u;
        }

        cursor = block_end;
    }

    return record_index == record_count ? DB_RELOC_APPLY_OK :
        DB_RELOC_APPLY_RECORD_COUNT_MISMATCH;
}

db_reloc_apply_status db_reloc_apply_pe64(
    void *mapped_image,
    size_t mapped_size,
    const db_mapped_image *mapped,
    size_t page_size,
    const db_map_page_protection *page_plan,
    size_t page_capacity,
    const db_reloc_record *records,
    size_t record_count,
    const db_reloc_summary *summary,
    uint64_t loaded_base,
    db_reloc_apply_result *result)
{
    uint8_t *image;
    db_reloc_apply_view view;
    db_reloc_apply_result applied;
    db_reloc_apply_status status;
    size_t record_bytes;
    size_t index;

    if (mapped_image == NULL || mapped == NULL || page_plan == NULL ||
        summary == NULL || result == NULL ||
        (records == NULL && record_count != 0u)) {
        return DB_RELOC_APPLY_NULL_ARGUMENT;
    }

    if (!db_reloc_apply_mul_size(record_count,
                                 sizeof(*records),
                                 &record_bytes)) {
        return DB_RELOC_APPLY_RECORD_COUNT_OVERFLOW;
    }

    if (db_reloc_apply_ranges_overlap(mapped_image, mapped_size,
                                      mapped, sizeof(*mapped)) ||
        db_reloc_apply_ranges_overlap(mapped_image, mapped_size,
                                      page_plan, page_capacity) ||
        db_reloc_apply_ranges_overlap(mapped_image, mapped_size,
                                      records, record_bytes) ||
        db_reloc_apply_ranges_overlap(mapped_image, mapped_size,
                                      summary, sizeof(*summary)) ||
        db_reloc_apply_ranges_overlap(mapped_image, mapped_size,
                                      result, sizeof(*result)) ||
        db_reloc_apply_ranges_overlap(mapped, sizeof(*mapped),
                                      page_plan, page_capacity) ||
        db_reloc_apply_ranges_overlap(mapped, sizeof(*mapped),
                                      records, record_bytes) ||
        db_reloc_apply_ranges_overlap(mapped, sizeof(*mapped),
                                      summary, sizeof(*summary)) ||
        db_reloc_apply_ranges_overlap(mapped, sizeof(*mapped),
                                      result, sizeof(*result)) ||
        db_reloc_apply_ranges_overlap(page_plan, page_capacity,
                                      records, record_bytes) ||
        db_reloc_apply_ranges_overlap(page_plan, page_capacity,
                                      summary, sizeof(*summary)) ||
        db_reloc_apply_ranges_overlap(page_plan, page_capacity,
                                      result, sizeof(*result)) ||
        db_reloc_apply_ranges_overlap(records, record_bytes,
                                      summary, sizeof(*summary)) ||
        db_reloc_apply_ranges_overlap(records, record_bytes,
                                      result, sizeof(*result)) ||
        db_reloc_apply_ranges_overlap(summary, sizeof(*summary),
                                      result, sizeof(*result))) {
        return DB_RELOC_APPLY_BUFFER_OVERLAP;
    }

    image = (uint8_t *)mapped_image;
    status = db_reloc_apply_build_view(image,
                                       mapped_size,
                                       mapped,
                                       page_size,
                                       page_plan,
                                       page_capacity,
                                       &view);
    if (status != DB_RELOC_APPLY_OK) {
        return status;
    }

    if (summary->directory_rva != view.directory_rva ||
        summary->directory_size != view.directory_size ||
        summary->record_count != record_count) {
        return DB_RELOC_APPLY_BAD_SUMMARY;
    }

    if ((loaded_base % page_size) != 0u) {
        return DB_RELOC_APPLY_LOADED_BASE_UNALIGNED;
    }

    if (loaded_base > UINT64_MAX - (uint64_t)mapped->image_size ||
        mapped->preferred_base >
            UINT64_MAX - (uint64_t)mapped->image_size) {
        return DB_RELOC_APPLY_IMAGE_RANGE_OVERFLOW;
    }

    status = db_reloc_apply_validate_directory(
        &view, records, record_count);
    if (status != DB_RELOC_APPLY_OK) {
        return status;
    }

    for (index = 0u; index < record_count; ++index) {
        status = db_reloc_apply_preflight_record(&view,
                                                 records,
                                                 index,
                                                 mapped->preferred_base,
                                                 loaded_base);
        if (status != DB_RELOC_APPLY_OK) {
            return status;
        }
    }

    for (index = 0u; index < record_count; ++index) {
        uint64_t adjusted = 0u;

        (void)db_reloc_apply_adjust_value(records[index].original_value,
                                          mapped->preferred_base,
                                          loaded_base,
                                          &adjusted);
        db_reloc_apply_write_u64(image,
                                 (size_t)records[index].target_rva,
                                 adjusted);
    }

    applied.preferred_base = mapped->preferred_base;
    applied.loaded_base = loaded_base;
    applied.applied_count = record_count;
    *result = applied;

    return DB_RELOC_APPLY_OK;
}

const char *db_reloc_apply_status_name(db_reloc_apply_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "invalid host page size",
        "bad mapped image",
        "page plan is too small",
        "invalid page protection value",
        "record count overflows",
        "buffer overlap",
        "relocation summary mismatch",
        "bad relocation directory",
        "bad relocation block",
        "unsupported relocation type",
        "relocation record count mismatch",
        "relocation record mismatch",
        "duplicate relocation target",
        "overlapping relocation targets",
        "relocation target is out of range",
        "relocation target is not mapped",
        "relocation target overlaps the relocation directory",
        "relocation target value is stale",
        "loaded base is not host-page aligned",
        "loaded image range overflows",
        "relocated value overflows"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown relocation-apply status";
    }

    return names[(size_t)status];
}
