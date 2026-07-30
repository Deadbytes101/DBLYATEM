#include "dblyat/pe.h"

#include <stdbool.h>

#define DB_PE_DOS_E_LFANEW_OFFSET 0x3cu
#define DB_PE_COFF_HEADER_SIZE 20u
#define DB_PE_SECTION_HEADER_SIZE 40u
#define DB_PE64_OPTIONAL_MIN_SIZE 112u
#define DB_PE_MAX_SECTIONS 96u

#define DB_PE_MACHINE_AMD64 0x8664u
#define DB_PE_OPTIONAL_MAGIC_PE32_PLUS 0x020bu
#define DB_PE_FILE_EXECUTABLE_IMAGE 0x0002u

static bool db_span_fits(size_t total, size_t offset, size_t length)
{
    return offset <= total && length <= total - offset;
}

static bool db_add_size(size_t left, size_t right, size_t *result)
{
    if (left > SIZE_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool db_mul_size(size_t left, size_t right, size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static uint16_t db_read_u16(const uint8_t *bytes, size_t offset)
{
    return (uint16_t)((uint16_t)bytes[offset] |
                      ((uint16_t)bytes[offset + 1u] << 8u));
}

static uint32_t db_read_u32(const uint8_t *bytes, size_t offset)
{
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t db_read_u64(const uint8_t *bytes, size_t offset)
{
    return (uint64_t)db_read_u32(bytes, offset) |
           ((uint64_t)db_read_u32(bytes, offset + 4u) << 32u);
}

static bool db_is_power_of_two_u32(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool db_u32_add_fits(uint32_t left, uint32_t right, uint32_t *result)
{
    if (left > UINT32_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool db_pe_alignment_is_valid(uint32_t section_alignment,
                                     uint32_t file_alignment)
{
    if (!db_is_power_of_two_u32(section_alignment) ||
        !db_is_power_of_two_u32(file_alignment)) {
        return false;
    }

    if (section_alignment < file_alignment) {
        return false;
    }

    if (section_alignment < 0x1000u) {
        return file_alignment == section_alignment;
    }

    return file_alignment >= 0x200u && file_alignment <= 0x10000u;
}

db_pe_status db_pe_verify(const void *data, size_t size, db_pe_image *image)
{
    const uint8_t *bytes;
    size_t pe_offset;
    size_t coff_offset;
    size_t optional_offset;
    size_t section_table_offset;
    size_t section_table_size;
    uint16_t machine;
    uint16_t section_count;
    uint16_t optional_size;
    uint16_t characteristics;
    uint32_t entry_rva;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint32_t image_size;
    uint32_t headers_size;
    bool entry_is_mapped;
    size_t index;

    if (data == NULL || image == NULL) {
        return DB_PE_NULL_ARGUMENT;
    }

    bytes = (const uint8_t *)data;

    if (!db_span_fits(size, 0u, 0x40u)) {
        return DB_PE_FILE_TOO_SMALL;
    }

    if (bytes[0] != (uint8_t)'M' || bytes[1] != (uint8_t)'Z') {
        return DB_PE_BAD_DOS_SIGNATURE;
    }

    pe_offset = (size_t)db_read_u32(bytes, DB_PE_DOS_E_LFANEW_OFFSET);
    if (!db_span_fits(size, pe_offset, 4u)) {
        return DB_PE_BAD_PE_OFFSET;
    }

    if (bytes[pe_offset] != (uint8_t)'P' ||
        bytes[pe_offset + 1u] != (uint8_t)'E' ||
        bytes[pe_offset + 2u] != 0u || bytes[pe_offset + 3u] != 0u) {
        return DB_PE_BAD_PE_SIGNATURE;
    }

    if (!db_add_size(pe_offset, 4u, &coff_offset) ||
        !db_span_fits(size, coff_offset, DB_PE_COFF_HEADER_SIZE)) {
        return DB_PE_BAD_PE_OFFSET;
    }

    machine = db_read_u16(bytes, coff_offset);
    if (machine != DB_PE_MACHINE_AMD64) {
        return DB_PE_UNSUPPORTED_MACHINE;
    }

    section_count = db_read_u16(bytes, coff_offset + 2u);
    if (section_count == 0u || section_count > DB_PE_MAX_SECTIONS) {
        return DB_PE_BAD_SECTION_COUNT;
    }

    optional_size = db_read_u16(bytes, coff_offset + 16u);
    characteristics = db_read_u16(bytes, coff_offset + 18u);
    if ((characteristics & DB_PE_FILE_EXECUTABLE_IMAGE) == 0u) {
        return DB_PE_NOT_EXECUTABLE;
    }

    if (optional_size < DB_PE64_OPTIONAL_MIN_SIZE) {
        return DB_PE_BAD_OPTIONAL_HEADER_SIZE;
    }

    if (!db_add_size(coff_offset, DB_PE_COFF_HEADER_SIZE, &optional_offset) ||
        !db_span_fits(size, optional_offset, (size_t)optional_size)) {
        return DB_PE_BAD_OPTIONAL_HEADER_SIZE;
    }

    if (db_read_u16(bytes, optional_offset) != DB_PE_OPTIONAL_MAGIC_PE32_PLUS) {
        return DB_PE_BAD_OPTIONAL_MAGIC;
    }

    entry_rva = db_read_u32(bytes, optional_offset + 16u);
    section_alignment = db_read_u32(bytes, optional_offset + 32u);
    file_alignment = db_read_u32(bytes, optional_offset + 36u);
    image_size = db_read_u32(bytes, optional_offset + 56u);
    headers_size = db_read_u32(bytes, optional_offset + 60u);

    if (!db_pe_alignment_is_valid(section_alignment, file_alignment)) {
        return DB_PE_BAD_ALIGNMENT;
    }

    if (image_size == 0u || (image_size % section_alignment) != 0u) {
        return DB_PE_BAD_IMAGE_SIZE;
    }

    if (!db_add_size(optional_offset, (size_t)optional_size,
                     &section_table_offset) ||
        !db_mul_size((size_t)section_count, DB_PE_SECTION_HEADER_SIZE,
                     &section_table_size) ||
        !db_span_fits(size, section_table_offset, section_table_size)) {
        return DB_PE_BAD_SECTION_TABLE;
    }

    if (headers_size == 0u || (headers_size % file_alignment) != 0u ||
        (size_t)headers_size > size ||
        (size_t)headers_size < section_table_offset + section_table_size) {
        return DB_PE_BAD_HEADER_SIZE;
    }

    if (entry_rva >= image_size) {
        return DB_PE_ENTRY_OUT_OF_RANGE;
    }

    entry_is_mapped = entry_rva == 0u || entry_rva < headers_size;

    for (index = 0u; index < (size_t)section_count; ++index) {
        const size_t section_offset =
            section_table_offset + index * DB_PE_SECTION_HEADER_SIZE;
        const uint32_t virtual_size = db_read_u32(bytes, section_offset + 8u);
        const uint32_t virtual_address = db_read_u32(bytes, section_offset + 12u);
        const uint32_t raw_size = db_read_u32(bytes, section_offset + 16u);
        const uint32_t raw_offset = db_read_u32(bytes, section_offset + 20u);
        const uint32_t mapped_size = virtual_size > raw_size ? virtual_size : raw_size;
        uint32_t virtual_end;

        if ((virtual_address % section_alignment) != 0u) {
            return DB_PE_BAD_SECTION_VIRTUAL_RANGE;
        }

        if (mapped_size != 0u) {
            if (!db_u32_add_fits(virtual_address, mapped_size, &virtual_end) ||
                virtual_end > image_size) {
                return DB_PE_BAD_SECTION_VIRTUAL_RANGE;
            }

            if (entry_rva >= virtual_address && entry_rva < virtual_end) {
                entry_is_mapped = true;
            }
        }

        if (raw_size != 0u) {
            if ((raw_offset % file_alignment) != 0u ||
                raw_offset < headers_size ||
                !db_span_fits(size, (size_t)raw_offset, (size_t)raw_size)) {
                return DB_PE_BAD_SECTION_RAW_RANGE;
            }
        }
    }

    if (!entry_is_mapped) {
        return DB_PE_ENTRY_OUT_OF_RANGE;
    }

    image->machine = machine;
    image->section_count = section_count;
    image->timestamp = db_read_u32(bytes, coff_offset + 4u);
    image->entry_rva = entry_rva;
    image->image_base = db_read_u64(bytes, optional_offset + 24u);
    image->section_alignment = section_alignment;
    image->file_alignment = file_alignment;
    image->image_size = image_size;
    image->headers_size = headers_size;
    image->subsystem = db_read_u16(bytes, optional_offset + 68u);
    image->dll_characteristics = db_read_u16(bytes, optional_offset + 70u);

    return DB_PE_OK;
}

const char *db_pe_status_name(db_pe_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "file too small",
        "bad DOS signature",
        "bad PE offset",
        "bad PE signature",
        "unsupported machine",
        "not an executable image",
        "bad section count",
        "bad optional-header size",
        "bad optional-header magic",
        "bad image alignment",
        "bad header size",
        "bad image size",
        "bad section table",
        "bad section raw-data range",
        "bad section virtual range",
        "entry point is not mapped"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown PE status";
    }

    return names[(size_t)status];
}
