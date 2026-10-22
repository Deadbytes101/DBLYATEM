#ifndef DBLYAT_HOST_PE_RUN_H
#define DBLYAT_HOST_PE_RUN_H

#include "dblyat/pe.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Host-side limits, not promises of support for arbitrary Windows binaries.
 * Files outside these bounds are rejected before guest state is allocated.
 */
#define DB_HOST_PE_RUN_MAX_FILE (16u * 1024u * 1024u)
#define DB_HOST_PE_RUN_MAX_IMAGE (16u * 1024u * 1024u)
#define DB_HOST_PE_RUN_GATEWAY_BASE UINT64_C(0x0000000180000000)
#define DB_HOST_PE_RUN_GATEWAY_BYTES 65536u
#define DB_HOST_PE_RUN_STACK_BASE UINT64_C(0x0000000070000000)
#define DB_HOST_PE_RUN_STACK_BYTES 65536u

typedef enum db_host_pe_run_plan_status {
    DB_HOST_PE_RUN_PLAN_OK = 0,
    DB_HOST_PE_RUN_PLAN_NULL_ARGUMENT,
    DB_HOST_PE_RUN_PLAN_BAD_PAGE_SIZE,
    DB_HOST_PE_RUN_PLAN_BAD_IMAGE_SIZE,
    DB_HOST_PE_RUN_PLAN_BAD_GUEST_RANGE
} db_host_pe_run_plan_status;

typedef struct db_host_pe_run_layout {
    size_t image_size;
    size_t image_page_count;
    size_t image_page_size;
    uint64_t loaded_base;
} db_host_pe_run_layout;

db_host_pe_run_plan_status db_host_pe_run_plan(
    const db_pe_image *pe,
    size_t page_size,
    db_host_pe_run_layout *layout);

/* Returns process exit status or explicit infrastructure/guest failure. */
int db_host_pe_run_file(const char *path);

#endif
