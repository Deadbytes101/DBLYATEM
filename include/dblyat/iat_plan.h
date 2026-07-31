#ifndef DBLYAT_IAT_PLAN_H
#define DBLYAT_IAT_PLAN_H

#include "dblyat/bind.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_iat_plan_status {
    DB_IAT_PLAN_OK = 0,
    DB_IAT_PLAN_NULL_ARGUMENT,
    DB_IAT_PLAN_COUNT_OVERFLOW,
    DB_IAT_PLAN_BUFFER_OVERLAP,
    DB_IAT_PLAN_BINDING_COUNT_MISMATCH,
    DB_IAT_PLAN_BAD_IMPORT_RECORD,
    DB_IAT_PLAN_BAD_BINDING,
    DB_IAT_PLAN_BAD_TARGET,
    DB_IAT_PLAN_DUPLICATE_TARGET_KEY,
    DB_IAT_PLAN_TARGET_NOT_FOUND,
    DB_IAT_PLAN_IAT_OUT_OF_RANGE,
    DB_IAT_PLAN_IAT_OVERLAP,
    DB_IAT_PLAN_STALE_IAT,
    DB_IAT_PLAN_OUTPUT_BUFFER_TOO_SMALL
} db_iat_plan_status;

typedef struct db_iat_target {
    uint32_t provider_id;
    uint32_t symbol_id;
    uint64_t guest_value;
} db_iat_target;

typedef struct db_iat_patch {
    size_t record_index;
    uint32_t module_index;
    uint32_t provider_id;
    uint32_t symbol_id;
    uint32_t iat_rva;
    uint32_t reserved;
    uint64_t expected_value;
    uint64_t replacement_value;
} db_iat_patch;

typedef struct db_iat_plan_summary {
    size_t record_count;
    size_t target_count;
    size_t patch_count;
} db_iat_plan_summary;

db_iat_plan_status db_iat_plan_patches(
    const void *mapped_image,
    size_t mapped_size,
    const db_import_record *records,
    size_t record_count,
    const db_import_binding *bindings,
    size_t binding_count,
    const db_iat_target *targets,
    size_t target_count,
    db_iat_patch *patches,
    size_t patch_capacity,
    db_iat_plan_summary *summary);

const char *db_iat_plan_status_name(db_iat_plan_status status);

#endif
