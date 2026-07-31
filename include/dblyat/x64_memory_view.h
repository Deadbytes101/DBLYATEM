#ifndef DBLYAT_X64_MEMORY_VIEW_H
#define DBLYAT_X64_MEMORY_VIEW_H

#include <stddef.h>
#include <stdint.h>

#define DB_X64_MEMORY_PERMISSION_READ UINT8_C(0x01)
#define DB_X64_MEMORY_PERMISSION_WRITE UINT8_C(0x02)
#define DB_X64_MEMORY_PERMISSION_MASK \
    (DB_X64_MEMORY_PERMISSION_READ | DB_X64_MEMORY_PERMISSION_WRITE)
#define DB_X64_MEMORY_ACCESS_WIDTH UINT8_C(8)

typedef enum db_x64_memory_view_status {
    DB_X64_MEMORY_VIEW_OK = 0,
    DB_X64_MEMORY_VIEW_NULL_ARGUMENT,
    DB_X64_MEMORY_VIEW_BUFFER_OVERLAP,
    DB_X64_MEMORY_VIEW_BAD_VIEW,
    DB_X64_MEMORY_VIEW_PERMISSION_DENIED,
    DB_X64_MEMORY_VIEW_NONCANONICAL_ADDRESS,
    DB_X64_MEMORY_VIEW_OUT_OF_BOUNDS
} db_x64_memory_view_status;

typedef enum db_x64_memory_access_kind {
    DB_X64_MEMORY_ACCESS_READ_U64 = 0,
    DB_X64_MEMORY_ACCESS_WRITE_U64
} db_x64_memory_access_kind;

typedef struct db_x64_memory_view {
    uint8_t *bytes;
    size_t size;
    uint64_t guest_base;
    uint8_t permissions;
    uint8_t reserved[7];
} db_x64_memory_view;

typedef struct db_x64_memory_access_summary {
    uint64_t guest_address;
    uint64_t offset;
    uint64_t value;
    db_x64_memory_access_kind kind;
    uint8_t width;
    uint8_t reserved[3];
} db_x64_memory_access_summary;

db_x64_memory_view_status db_x64_memory_view_read_u64(
    const db_x64_memory_view *view,
    uint64_t guest_address,
    uint64_t *value,
    db_x64_memory_access_summary *summary);

db_x64_memory_view_status db_x64_memory_view_write_u64(
    db_x64_memory_view *view,
    uint64_t guest_address,
    uint64_t value,
    db_x64_memory_access_summary *summary);

const char *db_x64_memory_view_status_name(
    db_x64_memory_view_status status);
const char *db_x64_memory_access_kind_name(
    db_x64_memory_access_kind kind);

#endif
