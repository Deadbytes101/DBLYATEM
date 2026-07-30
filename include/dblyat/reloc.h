#ifndef DBLYAT_RELOC_H
#define DBLYAT_RELOC_H

#include "dblyat/map.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_reloc_status {
    DB_RELOC_OK = 0,
    DB_RELOC_NULL_ARGUMENT,
    DB_RELOC_INVALID_PAGE_SIZE,
    DB_RELOC_BAD_MAPPED_IMAGE,
    DB_RELOC_PAGE_PLAN_TOO_SMALL,
    DB_RELOC_RECORD_CAPACITY_OVERFLOW,
    DB_RELOC_BUFFER_OVERLAP,
    DB_RELOC_BAD_DIRECTORY,
    DB_RELOC_DIRECTORY_OUT_OF_RANGE,
    DB_RELOC_DIRECTORY_UNMAPPED,
    DB_RELOC_BAD_BLOCK,
    DB_RELOC_UNSUPPORTED_TYPE,
    DB_RELOC_TARGET_OUT_OF_RANGE,
    DB_RELOC_TARGET_UNMAPPED,
    DB_RELOC_RECORD_BUFFER_TOO_SMALL
} db_reloc_status;

typedef struct db_reloc_record {
    uint32_t target_rva;
    uint16_t type;
    uint16_t reserved;
    uint64_t original_value;
} db_reloc_record;

typedef struct db_reloc_summary {
    uint32_t directory_rva;
    uint32_t directory_size;
    size_t record_count;
} db_reloc_summary;

db_reloc_status db_reloc_plan_pe64(
    const void *mapped_image,
    size_t mapped_size,
    const db_mapped_image *mapped,
    size_t page_size,
    const db_map_page_protection *page_plan,
    size_t page_capacity,
    db_reloc_record *records,
    size_t record_capacity,
    db_reloc_summary *summary);

const char *db_reloc_status_name(db_reloc_status status);

#endif
