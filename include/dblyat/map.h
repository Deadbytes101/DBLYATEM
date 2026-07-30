#ifndef DBLYAT_MAP_H
#define DBLYAT_MAP_H

#include "dblyat/pe.h"

#include <stddef.h>
#include <stdint.h>

typedef uint8_t db_map_page_protection;

enum {
    DB_MAP_PAGE_NONE = 0u,
    DB_MAP_PAGE_R = 1u,
    DB_MAP_PAGE_RW = 3u,
    DB_MAP_PAGE_RX = 5u
};

typedef enum db_map_status {
    DB_MAP_OK = 0,
    DB_MAP_NULL_ARGUMENT,
    DB_MAP_BAD_PE_IMAGE,
    DB_MAP_INVALID_PAGE_SIZE,
    DB_MAP_DESTINATION_TOO_SMALL,
    DB_MAP_PAGE_PLAN_TOO_SMALL,
    DB_MAP_BUFFER_OVERLAP,
    DB_MAP_BAD_IMAGE_LAYOUT,
    DB_MAP_SECTION_OVERLAP,
    DB_MAP_WRITE_EXECUTE_SECTION,
    DB_MAP_WRITE_EXECUTE_PAGE,
    DB_MAP_ENTRY_UNMAPPED
} db_map_status;

typedef struct db_mapped_image {
    uint64_t preferred_base;
    uint32_t image_size;
    uint32_t headers_size;
    uint32_t entry_rva;
    uint16_t section_count;
    size_t page_count;
} db_mapped_image;

db_map_status db_map_pe64(const void *source,
                          size_t source_size,
                          void *destination,
                          size_t destination_size,
                          size_t page_size,
                          db_map_page_protection *page_plan,
                          size_t page_capacity,
                          db_mapped_image *mapped_image);

const char *db_map_status_name(db_map_status status);

#endif
