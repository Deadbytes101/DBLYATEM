#ifndef DBLYAT_PE_H
#define DBLYAT_PE_H

#include <stddef.h>
#include <stdint.h>

typedef enum db_pe_status {
    DB_PE_OK = 0,
    DB_PE_NULL_ARGUMENT,
    DB_PE_FILE_TOO_SMALL,
    DB_PE_BAD_DOS_SIGNATURE,
    DB_PE_BAD_PE_OFFSET,
    DB_PE_BAD_PE_SIGNATURE,
    DB_PE_UNSUPPORTED_MACHINE,
    DB_PE_NOT_EXECUTABLE,
    DB_PE_BAD_SECTION_COUNT,
    DB_PE_BAD_OPTIONAL_HEADER_SIZE,
    DB_PE_BAD_OPTIONAL_MAGIC,
    DB_PE_BAD_ALIGNMENT,
    DB_PE_BAD_HEADER_SIZE,
    DB_PE_BAD_IMAGE_SIZE,
    DB_PE_BAD_SECTION_TABLE,
    DB_PE_BAD_SECTION_RAW_RANGE,
    DB_PE_BAD_SECTION_VIRTUAL_RANGE,
    DB_PE_ENTRY_OUT_OF_RANGE
} db_pe_status;

typedef struct db_pe_image {
    uint16_t machine;
    uint16_t section_count;
    uint32_t timestamp;
    uint32_t entry_rva;
    uint64_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint32_t image_size;
    uint32_t headers_size;
    uint16_t subsystem;
    uint16_t dll_characteristics;
} db_pe_image;

db_pe_status db_pe_verify(const void *data, size_t size, db_pe_image *image);
const char *db_pe_status_name(db_pe_status status);

#endif
