#include "dblyat/map.h"

#include <stdbool.h>
#include <stdint.h>

#define DB_MAP_DOS_E_LFANEW_OFFSET 0x3cu
#define DB_MAP_COFF_HEADER_SIZE 20u
#define DB_MAP_SECTION_HEADER_SIZE 40u
#define DB_MAP_MAX_SECTIONS 96u

#define DB_MAP_SCN_MEM_EXECUTE 0x20000000u
#define DB_MAP_SCN_MEM_READ 0x40000000u
#define DB_MAP_SCN_MEM_WRITE 0x80000000u

typedef struct db_map_section {
    uint32_t virtual_address;
    uint32_t memory_size;
    uint32_t raw_size;
    uint32_t raw_offset;
    uint32_t copy_size;
    uint32_t virtual_end;
    size_t first_page;
    size_t page_end;
    db_map_page_protection protection;
} db_map_section;

static bool db_map_add_size(size_t left, size_t right, size_t *result)
{
    if (left > SIZE_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static uint16_t db_map_read_u16(const uint8_t *bytes, size_t offset)
{
    return (uint16_t)((uint16_t)bytes[offset] |
                      ((uint16_t)bytes[offset + 1u] << 8u));
}

static uint32_t db_map_read_u32(const uint8_t *bytes, size_t offset)
{
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static bool db_map_is_power_of_two(size_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static size_t db_map_page_end(size_t end, size_t page_size)
{
    return end / page_size + (end % page_size != 0u ? 1u : 0u);
}

static bool db_map_ranges_overlap(const void *left,
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

static void db_map_zero(uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0u; index < size; ++index) {
        bytes[index] = 0u;
    }
}

static void db_map_copy(uint8_t *destination,
                        const uint8_t *source,
                        size_t size)
{
    size_t index;

    for (index = 0u; index < size; ++index) {
        destination[index] = source[index];
    }
}

static db_map_status db_map_section_protection(uint32_t characteristics,
                                               db_map_page_protection *result)
{
    const bool executable =
        (characteristics & DB_MAP_SCN_MEM_EXECUTE) != 0u;
    const bool readable =
        (characteristics & DB_MAP_SCN_MEM_READ) != 0u;
    const bool writable =
        (characteristics & DB_MAP_SCN_MEM_WRITE) != 0u;

    if (writable && executable) {
        return DB_MAP_WRITE_EXECUTE_SECTION;
    }

    if (writable) {
        *result = DB_MAP_PAGE_RW;
    } else if (executable) {
        *result = DB_MAP_PAGE_RX;
    } else if (readable) {
        *result = DB_MAP_PAGE_R;
    } else {
        *result = DB_MAP_PAGE_NONE;
    }

    return DB_MAP_OK;
}

static bool db_map_pages_overlap(const db_map_section *left,
                                 const db_map_section *right)
{
    return left->first_page < right->page_end &&
           right->first_page < left->page_end;
}

static bool db_map_bytes_overlap(const db_map_section *left,
                                 const db_map_section *right)
{
    return left->virtual_address < right->virtual_end &&
           right->virtual_address < left->virtual_end;
}

static bool db_map_is_writable(db_map_page_protection protection)
{
    return (protection & 2u) != 0u;
}

static bool db_map_is_executable(db_map_page_protection protection)
{
    return (protection & 4u) != 0u;
}

db_map_status db_map_pe64(const void *source,
                          size_t source_size,
                          void *destination,
                          size_t destination_size,
                          size_t page_size,
                          db_map_page_protection *page_plan,
                          size_t page_capacity,
                          db_mapped_image *mapped_image)
{
    const uint8_t *source_bytes;
    uint8_t *destination_bytes;
    db_pe_image image;
    db_map_section sections[DB_MAP_MAX_SECTIONS];
    db_mapped_image mapped;
    db_pe_status pe_status;
    size_t pe_offset;
    size_t coff_offset;
    size_t optional_offset;
    size_t section_table_offset;
    size_t page_count;
    size_t index;
    bool entry_is_mapped;

    if (source == NULL || destination == NULL || page_plan == NULL ||
        mapped_image == NULL) {
        return DB_MAP_NULL_ARGUMENT;
    }

    if (!db_map_is_power_of_two(page_size)) {
        return DB_MAP_INVALID_PAGE_SIZE;
    }

    pe_status = db_pe_verify(source, source_size, &image);
    if (pe_status != DB_PE_OK) {
        return DB_MAP_BAD_PE_IMAGE;
    }

#if SIZE_MAX < UINT32_MAX
    if (image.image_size > (uint32_t)SIZE_MAX) {
        return DB_MAP_DESTINATION_TOO_SMALL;
    }
#endif

    if (destination_size < (size_t)image.image_size) {
        return DB_MAP_DESTINATION_TOO_SMALL;
    }

    if (image.headers_size > image.image_size ||
        image.section_count > DB_MAP_MAX_SECTIONS) {
        return DB_MAP_BAD_IMAGE_LAYOUT;
    }

    page_count = (size_t)image.image_size / page_size;
    if ((size_t)image.image_size % page_size != 0u) {
        ++page_count;
    }

    if (page_capacity < page_count) {
        return DB_MAP_PAGE_PLAN_TOO_SMALL;
    }

    if (db_map_ranges_overlap(source, source_size,
                              destination, (size_t)image.image_size) ||
        db_map_ranges_overlap(source, source_size,
                              page_plan, page_count) ||
        db_map_ranges_overlap(source, source_size,
                              mapped_image, sizeof(*mapped_image)) ||
        db_map_ranges_overlap(destination, (size_t)image.image_size,
                              page_plan, page_count) ||
        db_map_ranges_overlap(destination, (size_t)image.image_size,
                              mapped_image, sizeof(*mapped_image)) ||
        db_map_ranges_overlap(page_plan, page_count,
                              mapped_image, sizeof(*mapped_image))) {
        return DB_MAP_BUFFER_OVERLAP;
    }

    source_bytes = (const uint8_t *)source;
    destination_bytes = (uint8_t *)destination;

    pe_offset = (size_t)db_map_read_u32(source_bytes,
                                        DB_MAP_DOS_E_LFANEW_OFFSET);
    if (!db_map_add_size(pe_offset, 4u, &coff_offset) ||
        !db_map_add_size(coff_offset, DB_MAP_COFF_HEADER_SIZE,
                         &optional_offset) ||
        !db_map_add_size(optional_offset,
                         (size_t)db_map_read_u16(source_bytes,
                                                coff_offset + 16u),
                         &section_table_offset)) {
        return DB_MAP_BAD_IMAGE_LAYOUT;
    }

    entry_is_mapped = image.entry_rva == 0u ||
                      image.entry_rva < image.headers_size;

    for (index = 0u; index < (size_t)image.section_count; ++index) {
        const size_t section_offset =
            section_table_offset + index * DB_MAP_SECTION_HEADER_SIZE;
        const uint32_t virtual_size =
            db_map_read_u32(source_bytes, section_offset + 8u);
        const uint32_t virtual_address =
            db_map_read_u32(source_bytes, section_offset + 12u);
        const uint32_t raw_size =
            db_map_read_u32(source_bytes, section_offset + 16u);
        const uint32_t raw_offset =
            db_map_read_u32(source_bytes, section_offset + 20u);
        const uint32_t characteristics =
            db_map_read_u32(source_bytes, section_offset + 36u);
        const uint32_t memory_size =
            virtual_size != 0u ? virtual_size : raw_size;
        db_map_status status;

        sections[index].virtual_address = virtual_address;
        sections[index].memory_size = memory_size;
        sections[index].raw_size = raw_size;
        sections[index].raw_offset = raw_offset;
        sections[index].copy_size =
            raw_size < memory_size ? raw_size : memory_size;

        if (virtual_address > image.image_size) {
            return DB_MAP_BAD_IMAGE_LAYOUT;
        }

        if (memory_size == 0u) {
            sections[index].virtual_end = virtual_address;
            sections[index].first_page = 0u;
            sections[index].page_end = 0u;
        } else {
            if (virtual_address < image.headers_size ||
                virtual_address > UINT32_MAX - memory_size ||
                virtual_address + memory_size > image.image_size) {
                return DB_MAP_BAD_IMAGE_LAYOUT;
            }

            sections[index].virtual_end = virtual_address + memory_size;
            sections[index].first_page =
                (size_t)virtual_address / page_size;
            sections[index].page_end =
                db_map_page_end((size_t)sections[index].virtual_end,
                                page_size);
        }

        status = db_map_section_protection(characteristics,
                                           &sections[index].protection);
        if (status != DB_MAP_OK) {
            return status;
        }

        if (memory_size != 0u &&
            image.entry_rva >= virtual_address &&
            image.entry_rva < sections[index].virtual_end) {
            entry_is_mapped = true;
        }
    }

    if (!entry_is_mapped) {
        return DB_MAP_ENTRY_UNMAPPED;
    }

    for (index = 0u; index < (size_t)image.section_count; ++index) {
        size_t other;

        if (sections[index].memory_size == 0u) {
            continue;
        }

        for (other = index + 1u;
             other < (size_t)image.section_count;
             ++other) {
            if (sections[other].memory_size == 0u) {
                continue;
            }

            if (db_map_bytes_overlap(&sections[index], &sections[other])) {
                return DB_MAP_SECTION_OVERLAP;
            }

            if (db_map_pages_overlap(&sections[index], &sections[other]) &&
                ((db_map_is_writable(sections[index].protection) &&
                  db_map_is_executable(sections[other].protection)) ||
                 (db_map_is_executable(sections[index].protection) &&
                  db_map_is_writable(sections[other].protection)))) {
                return DB_MAP_WRITE_EXECUTE_PAGE;
            }
        }
    }

    db_map_zero(destination_bytes, (size_t)image.image_size);
    db_map_zero(page_plan, page_count);

    db_map_copy(destination_bytes, source_bytes, (size_t)image.headers_size);

    for (index = 0u;
         index < db_map_page_end((size_t)image.headers_size, page_size);
         ++index) {
        page_plan[index] = DB_MAP_PAGE_R;
    }

    for (index = 0u; index < (size_t)image.section_count; ++index) {
        size_t page;

        if (sections[index].copy_size != 0u) {
            db_map_copy(destination_bytes + sections[index].virtual_address,
                        source_bytes + sections[index].raw_offset,
                        (size_t)sections[index].copy_size);
        }

        for (page = sections[index].first_page;
             page < sections[index].page_end;
             ++page) {
            page_plan[page] = (db_map_page_protection)(
                page_plan[page] | sections[index].protection);
        }
    }

    mapped.preferred_base = image.image_base;
    mapped.image_size = image.image_size;
    mapped.headers_size = image.headers_size;
    mapped.entry_rva = image.entry_rva;
    mapped.section_count = image.section_count;
    mapped.page_count = page_count;
    *mapped_image = mapped;

    return DB_MAP_OK;
}

const char *db_map_status_name(db_map_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "bad PE image",
        "invalid host page size",
        "destination buffer is too small",
        "page plan is too small",
        "overlapping input or output buffers",
        "bad image layout",
        "overlapping section virtual ranges",
        "section requests write and execute",
        "host page would become writable and executable",
        "entry point is not mapped"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown map status";
    }

    return names[(size_t)status];
}
