#ifndef DBLYAT_IAT_APPLY_H
#define DBLYAT_IAT_APPLY_H

#include "dblyat/iat_plan.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_iat_apply_status {
    DB_IAT_APPLY_OK = 0,
    DB_IAT_APPLY_NULL_ARGUMENT,
    DB_IAT_APPLY_INVALID_PAGE_SIZE,
    DB_IAT_APPLY_COUNT_OVERFLOW,
    DB_IAT_APPLY_BUFFER_OVERLAP,
    DB_IAT_APPLY_PAGE_PLAN_TOO_SMALL,
    DB_IAT_APPLY_BAD_PAGE_PLAN,
    DB_IAT_APPLY_BAD_PATCH,
    DB_IAT_APPLY_IAT_OUT_OF_RANGE,
    DB_IAT_APPLY_IAT_PAGE_NOT_WRITABLE,
    DB_IAT_APPLY_IAT_OVERLAP,
    DB_IAT_APPLY_STALE_IAT
} db_iat_apply_status;

typedef struct db_iat_apply_summary {
    size_t patch_count;
    size_t bytes_written;
    uint32_t first_iat_rva;
    uint32_t last_iat_end;
} db_iat_apply_summary;

/*
 * The caller must provide exclusive ownership of mapped_image for the whole
 * call. The function performs complete preflight validation before the first
 * write and never changes page permissions.
 */
db_iat_apply_status db_iat_apply_patches(
    void *mapped_image,
    size_t mapped_size,
    size_t page_size,
    const db_map_page_protection *page_plan,
    size_t page_capacity,
    const db_iat_patch *patches,
    size_t patch_count,
    db_iat_apply_summary *summary);

const char *db_iat_apply_status_name(db_iat_apply_status status);

#endif
