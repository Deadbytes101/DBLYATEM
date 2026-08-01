#ifndef DBLYAT_LINUX_ARM64_WX_PROVIDER_H
#define DBLYAT_LINUX_ARM64_WX_PROVIDER_H

#include "dblyat/arm64_code_image.h"

#include <stddef.h>
#include <stdint.h>

#define DB_LINUX_ARM64_WX_AUDIT_VERSION UINT32_C(1)

typedef enum db_linux_arm64_wx_state {
    DB_LINUX_ARM64_WX_EMPTY = 0,
    DB_LINUX_ARM64_WX_RW_ALLOCATED,
    DB_LINUX_ARM64_WX_IMAGE_COPIED,
    DB_LINUX_ARM64_WX_RX_SEALED,
    DB_LINUX_ARM64_WX_ICACHE_SYNCHRONIZED,
    DB_LINUX_ARM64_WX_PUBLISHED,
    DB_LINUX_ARM64_WX_ROLLBACK_PENDING
} db_linux_arm64_wx_state;

typedef enum db_linux_arm64_wx_status {
    DB_LINUX_ARM64_WX_OK = 0,
    DB_LINUX_ARM64_WX_NULL_ARGUMENT,
    DB_LINUX_ARM64_WX_BUFFER_OVERLAP,
    DB_LINUX_ARM64_WX_BAD_PLATFORM,
    DB_LINUX_ARM64_WX_BAD_PLAN,
    DB_LINUX_ARM64_WX_BAD_IMAGE_SIZE,
    DB_LINUX_ARM64_WX_HANDLE_NOT_EMPTY,
    DB_LINUX_ARM64_WX_HANDLE_NOT_PUBLISHED,
    DB_LINUX_ARM64_WX_MAP_FAILED,
    DB_LINUX_ARM64_WX_ADDRESS_MISMATCH,
    DB_LINUX_ARM64_WX_COPY_FAILED,
    DB_LINUX_ARM64_WX_COPY_MISMATCH,
    DB_LINUX_ARM64_WX_PROTECT_FAILED,
    DB_LINUX_ARM64_WX_ICACHE_FAILED,
    DB_LINUX_ARM64_WX_ENTRY_MISMATCH,
    DB_LINUX_ARM64_WX_UNMAP_FAILED,
    DB_LINUX_ARM64_WX_ROLLBACK_FAILED
} db_linux_arm64_wx_status;

typedef void *(*db_linux_arm64_wx_map_rw_fn)(
    void *context,
    void *requested_address,
    size_t allocation_size);

typedef int (*db_linux_arm64_wx_copy_fn)(
    void *context,
    void *destination,
    const void *source,
    size_t byte_count);

typedef int (*db_linux_arm64_wx_protect_rx_fn)(
    void *context,
    void *mapping,
    size_t allocation_size);

typedef int (*db_linux_arm64_wx_sync_icache_fn)(
    void *context,
    void *begin,
    size_t byte_count);

typedef int (*db_linux_arm64_wx_unmap_fn)(
    void *context,
    void *mapping,
    size_t allocation_size);

typedef struct db_linux_arm64_wx_platform {
    void *context;
    db_linux_arm64_wx_map_rw_fn map_rw;
    db_linux_arm64_wx_copy_fn copy_bytes;
    db_linux_arm64_wx_protect_rx_fn protect_rx;
    db_linux_arm64_wx_sync_icache_fn sync_icache;
    db_linux_arm64_wx_unmap_fn unmap;
    uint64_t reserved[2];
} db_linux_arm64_wx_platform;

typedef struct db_linux_arm64_wx_handle {
    void *mapping;
    void *entry;
    size_t allocation_size;
    uint32_t state;
    uint32_t reserved;
} db_linux_arm64_wx_handle;

typedef struct db_linux_arm64_wx_audit {
    uint64_t requested_base;
    uint64_t mapped_base;
    uint64_t entry_address;
    uint64_t allocation_size;
    uint64_t image_size;
    uint64_t icache_address;
    uint64_t icache_size;
    uint32_t version;
    uint32_t completed_state;
    uint32_t failure_state;
    uint32_t status;
    uint32_t primary_status;
    uint8_t map_observed;
    uint8_t copy_verified;
    uint8_t rx_observed;
    uint8_t icache_observed;
    uint8_t rollback_observed;
    uint8_t release_observed;
    uint8_t reserved[2];
} db_linux_arm64_wx_audit;

void db_linux_arm64_wx_handle_init(db_linux_arm64_wx_handle *handle);

db_linux_arm64_wx_status db_linux_arm64_wx_publish(
    const db_linux_arm64_wx_platform *platform,
    const db_arm64_code_image_plan *plan,
    const uint8_t *image_bytes,
    size_t image_size,
    db_linux_arm64_wx_handle *handle,
    db_linux_arm64_wx_audit *audit);

db_linux_arm64_wx_status db_linux_arm64_wx_release(
    const db_linux_arm64_wx_platform *platform,
    db_linux_arm64_wx_handle *handle,
    db_linux_arm64_wx_audit *audit);

const db_linux_arm64_wx_platform *db_linux_arm64_wx_posix_platform(void);
const char *db_linux_arm64_wx_status_name(db_linux_arm64_wx_status status);

#endif
