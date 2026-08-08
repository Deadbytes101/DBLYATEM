#ifndef DBLYAT_LINUX_ARM64_GUEST_RUNTIME_H
#define DBLYAT_LINUX_ARM64_GUEST_RUNTIME_H

#include "dblyat/map.h"
#include "dblyat/x64_arm64_relocate.h"

#include <stddef.h>
#include <stdint.h>

#define DB_LINUX_ARM64_GUEST_MEMORY_FORMAT_VERSION UINT32_C(1)

typedef enum db_linux_arm64_guest_access {
    DB_LINUX_ARM64_GUEST_ACCESS_NONE = 0,
    DB_LINUX_ARM64_GUEST_ACCESS_READ,
    DB_LINUX_ARM64_GUEST_ACCESS_WRITE
} db_linux_arm64_guest_access;

typedef enum db_linux_arm64_guest_runtime_status {
    DB_LINUX_ARM64_GUEST_RUNTIME_OK = 0,
    DB_LINUX_ARM64_GUEST_RUNTIME_NULL_ARGUMENT,
    DB_LINUX_ARM64_GUEST_RUNTIME_BAD_MEMORY,
    DB_LINUX_ARM64_GUEST_RUNTIME_NONCANONICAL_ADDRESS,
    DB_LINUX_ARM64_GUEST_RUNTIME_ADDRESS_OVERFLOW,
    DB_LINUX_ARM64_GUEST_RUNTIME_NOT_MAPPED,
    DB_LINUX_ARM64_GUEST_RUNTIME_ACCESS_DENIED,
    DB_LINUX_ARM64_GUEST_RUNTIME_UNSUPPORTED_HOST
} db_linux_arm64_guest_runtime_status;

typedef struct db_linux_arm64_guest_memory {
    uint64_t image_host_address;
    uint64_t image_guest_base;
    uint64_t image_size;
    uint64_t image_page_plan_address;
    uint64_t image_page_count;
    uint64_t image_page_size;
    uint64_t stack_host_address;
    uint64_t stack_guest_base;
    uint64_t stack_size;
    uint64_t gateway_host_address;
    uint64_t gateway_guest_base;
    uint64_t gateway_size;
    uint32_t format_version;
    uint8_t reserved[4];
} db_linux_arm64_guest_memory;

db_linux_arm64_guest_runtime_status db_linux_arm64_guest_memory_init(
    db_linux_arm64_guest_memory *memory,
    void *image,
    size_t image_size,
    const db_map_page_protection *image_page_plan,
    size_t image_page_count,
    size_t image_page_size,
    uint64_t image_guest_base,
    void *stack,
    size_t stack_size,
    uint64_t stack_guest_base,
    const void *gateway,
    size_t gateway_size,
    uint64_t gateway_guest_base);

db_linux_arm64_guest_runtime_status db_linux_arm64_guest_memory_validate(
    const db_linux_arm64_guest_memory *memory);

db_linux_arm64_guest_runtime_status db_linux_arm64_guest_memory_read_u64(
    const db_linux_arm64_guest_memory *memory,
    uint64_t guest_address,
    uint64_t *value);

db_linux_arm64_guest_runtime_status db_linux_arm64_guest_memory_write_u64(
    const db_linux_arm64_guest_memory *memory,
    uint64_t guest_address,
    uint64_t value);

db_linux_arm64_guest_runtime_status
 db_linux_arm64_guest_runtime_helper_targets(
    db_x64_arm64_helper_targets *targets);

const char *db_linux_arm64_guest_runtime_status_name(
    db_linux_arm64_guest_runtime_status status);

#endif
