#ifndef DBLYAT_RELOC_APPLY_H
#define DBLYAT_RELOC_APPLY_H

#include "dblyat/reloc.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_reloc_apply_status {
    DB_RELOC_APPLY_OK = 0,
    DB_RELOC_APPLY_NULL_ARGUMENT,
    DB_RELOC_APPLY_INVALID_PAGE_SIZE,
    DB_RELOC_APPLY_BAD_MAPPED_IMAGE,
    DB_RELOC_APPLY_PAGE_PLAN_TOO_SMALL,
    DB_RELOC_APPLY_BAD_PAGE_PLAN,
    DB_RELOC_APPLY_RECORD_COUNT_OVERFLOW,
    DB_RELOC_APPLY_BUFFER_OVERLAP,
    DB_RELOC_APPLY_BAD_SUMMARY,
    DB_RELOC_APPLY_BAD_DIRECTORY,
    DB_RELOC_APPLY_BAD_BLOCK,
    DB_RELOC_APPLY_UNSUPPORTED_TYPE,
    DB_RELOC_APPLY_RECORD_COUNT_MISMATCH,
    DB_RELOC_APPLY_RECORD_MISMATCH,
    DB_RELOC_APPLY_DUPLICATE_TARGET,
    DB_RELOC_APPLY_TARGET_OVERLAP,
    DB_RELOC_APPLY_TARGET_OUT_OF_RANGE,
    DB_RELOC_APPLY_TARGET_UNMAPPED,
    DB_RELOC_APPLY_TARGET_OVERLAPS_DIRECTORY,
    DB_RELOC_APPLY_STALE_VALUE,
    DB_RELOC_APPLY_LOADED_BASE_UNALIGNED,
    DB_RELOC_APPLY_IMAGE_RANGE_OVERFLOW,
    DB_RELOC_APPLY_VALUE_OVERFLOW
} db_reloc_apply_status;

typedef struct db_reloc_apply_result {
    uint64_t preferred_base;
    uint64_t loaded_base;
    size_t applied_count;
} db_reloc_apply_result;

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
    db_reloc_apply_result *result);

const char *db_reloc_apply_status_name(db_reloc_apply_status status);

#endif
