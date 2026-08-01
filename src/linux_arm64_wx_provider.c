#include "dblyat/linux_arm64_wx_provider.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool zero_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (bytes[index] != 0u) {
            return false;
        }
    }
    return true;
}

static bool regions_overlap(const void *left,
                            size_t left_size,
                            const void *right,
                            size_t right_size)
{
    const uintptr_t left_address = (uintptr_t)left;
    const uintptr_t right_address = (uintptr_t)right;

    if (left_size == 0u || right_size == 0u) {
        return false;
    }
    if (left_address <= right_address) {
        return right_address - left_address < left_size;
    }
    return left_address - right_address < right_size;
}

static bool canonical_nonzero(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return address != 0u &&
           upper == (sign != 0u ? UINT64_C(0xffff) : UINT64_C(0));
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (UINT64_MAX - left < right) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool round_up_u64(uint64_t value,
                         uint64_t alignment,
                         uint64_t *result)
{
    const uint64_t mask = alignment - UINT64_C(1);

    if (UINT64_MAX - value < mask) {
        return false;
    }
    *result = (value + mask) & ~mask;
    return true;
}

static bool platform_valid(const db_linux_arm64_wx_platform *platform)
{
    return platform != NULL &&
           platform->map_rw != NULL &&
           platform->copy_bytes != NULL &&
           platform->protect_rx != NULL &&
           platform->sync_icache != NULL &&
           platform->unmap != NULL &&
           zero_bytes(platform->reserved, sizeof(platform->reserved));
}

static bool plan_valid(const db_arm64_code_image_plan *plan)
{
    uint64_t expected_byte_count;
    uint64_t expected_entry_offset;
    uint64_t expected_entry_address;
    uint64_t expected_image_end;
    uint64_t expected_allocation_size;
    uint64_t expected_allocation_end;

    if (plan == NULL ||
        plan->format_version != DB_ARM64_CODE_IMAGE_FORMAT_VERSION ||
        plan->word_count == 0u ||
        !zero_bytes(plan->reserved, sizeof(plan->reserved)) ||
        !canonical_nonzero(plan->code_base) ||
        plan->page_size < UINT64_C(4096) ||
        (plan->page_size & (plan->page_size - UINT64_C(1))) != 0u ||
        (plan->code_base & (plan->page_size - UINT64_C(1))) != 0u ||
        plan->entry_word_index >= plan->word_count ||
        plan->initial_permissions !=
            (DB_ARM64_CODE_PERMISSION_READ |
             DB_ARM64_CODE_PERMISSION_WRITE) ||
        plan->final_permissions !=
            (DB_ARM64_CODE_PERMISSION_READ |
             DB_ARM64_CODE_PERMISSION_EXECUTE)) {
        return false;
    }

    expected_byte_count = (uint64_t)plan->word_count * UINT64_C(4);
    expected_entry_offset =
        (uint64_t)plan->entry_word_index * UINT64_C(4);

    if (!round_up_u64(expected_byte_count,
                      plan->page_size,
                      &expected_allocation_size) ||
        expected_allocation_size == 0u ||
        plan->byte_count != expected_byte_count ||
        plan->entry_byte_offset != expected_entry_offset ||
        plan->allocation_size != expected_allocation_size ||
        plan->instruction_cache_offset != 0u ||
        plan->instruction_cache_size != expected_byte_count ||
        !add_u64(plan->code_base,
                 plan->entry_byte_offset,
                 &expected_entry_address) ||
        !add_u64(plan->code_base,
                 plan->byte_count - UINT64_C(1),
                 &expected_image_end) ||
        !add_u64(plan->code_base,
                 plan->allocation_size - UINT64_C(1),
                 &expected_allocation_end) ||
        plan->entry_address != expected_entry_address ||
        plan->image_end_address != expected_image_end ||
        plan->allocation_end_address != expected_allocation_end ||
        !canonical_nonzero(plan->entry_address) ||
        !canonical_nonzero(plan->image_end_address) ||
        !canonical_nonzero(plan->allocation_end_address)) {
        return false;
    }

    return true;
}

static bool handle_empty(const db_linux_arm64_wx_handle *handle)
{
    return handle->mapping == NULL &&
           handle->entry == NULL &&
           handle->allocation_size == 0u &&
           handle->state == DB_LINUX_ARM64_WX_EMPTY &&
           handle->reserved == 0u;
}

static bool handle_releasable(const db_linux_arm64_wx_handle *handle)
{
    const bool published =
        handle->state == DB_LINUX_ARM64_WX_PUBLISHED;
    const bool rollback_pending =
        handle->state == DB_LINUX_ARM64_WX_ROLLBACK_PENDING;

    return (published || rollback_pending) &&
           handle->mapping != NULL &&
           handle->allocation_size != 0u &&
           handle->reserved == 0u &&
           ((published && handle->entry != NULL) ||
            (rollback_pending && handle->entry == NULL));
}

static void audit_init(db_linux_arm64_wx_audit *audit,
                       const db_arm64_code_image_plan *plan)
{
    memset(audit, 0, sizeof(*audit));
    audit->version = DB_LINUX_ARM64_WX_AUDIT_VERSION;
    audit->completed_state = DB_LINUX_ARM64_WX_EMPTY;
    audit->failure_state = DB_LINUX_ARM64_WX_EMPTY;
    audit->status = DB_LINUX_ARM64_WX_OK;
    audit->primary_status = DB_LINUX_ARM64_WX_OK;

    if (plan != NULL) {
        audit->requested_base = plan->code_base;
        audit->entry_address = plan->entry_address;
        audit->allocation_size = plan->allocation_size;
        audit->image_size = plan->byte_count;
        audit->icache_address =
            plan->code_base + plan->instruction_cache_offset;
        audit->icache_size = plan->instruction_cache_size;
    }
}

static void preserve_rollback_handle(
    db_linux_arm64_wx_handle *handle,
    void *mapping,
    size_t allocation_size)
{
    db_linux_arm64_wx_handle pending;

    memset(&pending, 0, sizeof(pending));
    pending.mapping = mapping;
    pending.allocation_size = allocation_size;
    pending.state = DB_LINUX_ARM64_WX_ROLLBACK_PENDING;
    *handle = pending;
}

static db_linux_arm64_wx_status rollback(
    const db_linux_arm64_wx_platform *platform,
    void *mapping,
    size_t allocation_size,
    db_linux_arm64_wx_status primary,
    db_linux_arm64_wx_state failed_state,
    db_linux_arm64_wx_handle *handle,
    db_linux_arm64_wx_audit *audit)
{
    audit->failure_state = (uint32_t)failed_state;
    audit->primary_status = (uint32_t)primary;

    if (mapping != NULL) {
        if (platform->unmap(platform->context,
                            mapping,
                            allocation_size) != 0) {
            preserve_rollback_handle(handle, mapping, allocation_size);
            audit->completed_state =
                DB_LINUX_ARM64_WX_ROLLBACK_PENDING;
            audit->mapped_base = (uint64_t)(uintptr_t)mapping;
            audit->allocation_size = allocation_size;
            audit->status = DB_LINUX_ARM64_WX_ROLLBACK_FAILED;
            return DB_LINUX_ARM64_WX_ROLLBACK_FAILED;
        }
        audit->rollback_observed = 1u;
    }

    audit->status = (uint32_t)primary;
    return primary;
}

void db_linux_arm64_wx_handle_init(db_linux_arm64_wx_handle *handle)
{
    if (handle != NULL) {
        memset(handle, 0, sizeof(*handle));
    }
}

db_linux_arm64_wx_status db_linux_arm64_wx_publish(
    const db_linux_arm64_wx_platform *platform,
    const db_arm64_code_image_plan *plan,
    const uint8_t *image_bytes,
    size_t image_size,
    db_linux_arm64_wx_handle *handle,
    db_linux_arm64_wx_audit *audit)
{
    db_linux_arm64_wx_audit observed;
    db_linux_arm64_wx_handle completed;
    void *mapping;
    void *entry;
    void *cache_begin;
    const size_t allocation_size =
        plan != NULL && plan->allocation_size <= SIZE_MAX
            ? (size_t)plan->allocation_size
            : 0u;
    const size_t byte_count =
        plan != NULL && plan->byte_count <= SIZE_MAX
            ? (size_t)plan->byte_count
            : 0u;
    const size_t cache_size =
        plan != NULL && plan->instruction_cache_size <= SIZE_MAX
            ? (size_t)plan->instruction_cache_size
            : 0u;

    if (platform == NULL || plan == NULL || image_bytes == NULL ||
        handle == NULL || audit == NULL) {
        return DB_LINUX_ARM64_WX_NULL_ARGUMENT;
    }
    if (regions_overlap(handle, sizeof(*handle),
                        audit, sizeof(*audit)) ||
        regions_overlap(handle, sizeof(*handle),
                        platform, sizeof(*platform)) ||
        regions_overlap(audit, sizeof(*audit),
                        platform, sizeof(*platform)) ||
        regions_overlap(handle, sizeof(*handle),
                        plan, sizeof(*plan)) ||
        regions_overlap(audit, sizeof(*audit),
                        plan, sizeof(*plan)) ||
        regions_overlap(handle, sizeof(*handle),
                        image_bytes, image_size) ||
        regions_overlap(audit, sizeof(*audit),
                        image_bytes, image_size)) {
        return DB_LINUX_ARM64_WX_BUFFER_OVERLAP;
    }

    audit_init(&observed, plan);

    if (!platform_valid(platform)) {
        observed.status = DB_LINUX_ARM64_WX_BAD_PLATFORM;
        *audit = observed;
        return DB_LINUX_ARM64_WX_BAD_PLATFORM;
    }
    if (!handle_empty(handle)) {
        observed.status = DB_LINUX_ARM64_WX_HANDLE_NOT_EMPTY;
        *audit = observed;
        return DB_LINUX_ARM64_WX_HANDLE_NOT_EMPTY;
    }
    if (!plan_valid(plan) || plan->allocation_size > SIZE_MAX ||
        plan->byte_count > SIZE_MAX ||
        plan->instruction_cache_size > SIZE_MAX) {
        observed.status = DB_LINUX_ARM64_WX_BAD_PLAN;
        *audit = observed;
        return DB_LINUX_ARM64_WX_BAD_PLAN;
    }
    if (image_size != byte_count) {
        observed.status = DB_LINUX_ARM64_WX_BAD_IMAGE_SIZE;
        *audit = observed;
        return DB_LINUX_ARM64_WX_BAD_IMAGE_SIZE;
    }
    if (regions_overlap((const void *)(uintptr_t)plan->code_base,
                        allocation_size,
                        platform, sizeof(*platform)) ||
        regions_overlap((const void *)(uintptr_t)plan->code_base,
                        allocation_size,
                        plan, sizeof(*plan)) ||
        regions_overlap((const void *)(uintptr_t)plan->code_base,
                        allocation_size,
                        image_bytes, image_size) ||
        regions_overlap((const void *)(uintptr_t)plan->code_base,
                        allocation_size,
                        handle, sizeof(*handle)) ||
        regions_overlap((const void *)(uintptr_t)plan->code_base,
                        allocation_size,
                        audit, sizeof(*audit))) {
        observed.status = DB_LINUX_ARM64_WX_BUFFER_OVERLAP;
        *audit = observed;
        return DB_LINUX_ARM64_WX_BUFFER_OVERLAP;
    }

    mapping = platform->map_rw(platform->context,
                               (void *)(uintptr_t)plan->code_base,
                               allocation_size);
    if (mapping == NULL) {
        observed.failure_state = DB_LINUX_ARM64_WX_RW_ALLOCATED;
        observed.status = DB_LINUX_ARM64_WX_MAP_FAILED;
        observed.primary_status = DB_LINUX_ARM64_WX_MAP_FAILED;
        *audit = observed;
        return DB_LINUX_ARM64_WX_MAP_FAILED;
    }

    observed.map_observed = 1u;
    observed.mapped_base = (uint64_t)(uintptr_t)mapping;
    observed.completed_state = DB_LINUX_ARM64_WX_RW_ALLOCATED;

    if ((uint64_t)(uintptr_t)mapping != plan->code_base) {
        const db_linux_arm64_wx_status status =
            rollback(platform,
                     mapping,
                     allocation_size,
                     DB_LINUX_ARM64_WX_ADDRESS_MISMATCH,
                     DB_LINUX_ARM64_WX_RW_ALLOCATED,
                     handle,
                     &observed);
        *audit = observed;
        return status;
    }

    if (platform->copy_bytes(platform->context,
                             mapping,
                             image_bytes,
                             byte_count) != 0) {
        const db_linux_arm64_wx_status status =
            rollback(platform,
                     mapping,
                     allocation_size,
                     DB_LINUX_ARM64_WX_COPY_FAILED,
                     DB_LINUX_ARM64_WX_IMAGE_COPIED,
                     handle,
                     &observed);
        *audit = observed;
        return status;
    }
    if (memcmp(mapping, image_bytes, byte_count) != 0) {
        const db_linux_arm64_wx_status status =
            rollback(platform,
                     mapping,
                     allocation_size,
                     DB_LINUX_ARM64_WX_COPY_MISMATCH,
                     DB_LINUX_ARM64_WX_IMAGE_COPIED,
                     handle,
                     &observed);
        *audit = observed;
        return status;
    }

    observed.copy_verified = 1u;
    observed.completed_state = DB_LINUX_ARM64_WX_IMAGE_COPIED;

    if (platform->protect_rx(platform->context,
                             mapping,
                             allocation_size) != 0) {
        const db_linux_arm64_wx_status status =
            rollback(platform,
                     mapping,
                     allocation_size,
                     DB_LINUX_ARM64_WX_PROTECT_FAILED,
                     DB_LINUX_ARM64_WX_RX_SEALED,
                     handle,
                     &observed);
        *audit = observed;
        return status;
    }

    observed.rx_observed = 1u;
    observed.completed_state = DB_LINUX_ARM64_WX_RX_SEALED;

    cache_begin = (uint8_t *)mapping +
                  (size_t)plan->instruction_cache_offset;
    if (platform->sync_icache(platform->context,
                              cache_begin,
                              cache_size) != 0) {
        const db_linux_arm64_wx_status status =
            rollback(platform,
                     mapping,
                     allocation_size,
                     DB_LINUX_ARM64_WX_ICACHE_FAILED,
                     DB_LINUX_ARM64_WX_ICACHE_SYNCHRONIZED,
                     handle,
                     &observed);
        *audit = observed;
        return status;
    }

    observed.icache_observed = 1u;
    observed.completed_state = DB_LINUX_ARM64_WX_ICACHE_SYNCHRONIZED;

    entry = (uint8_t *)mapping + (size_t)plan->entry_byte_offset;
    if ((uint64_t)(uintptr_t)entry != plan->entry_address) {
        const db_linux_arm64_wx_status status =
            rollback(platform,
                     mapping,
                     allocation_size,
                     DB_LINUX_ARM64_WX_ENTRY_MISMATCH,
                     DB_LINUX_ARM64_WX_PUBLISHED,
                     handle,
                     &observed);
        *audit = observed;
        return status;
    }

    memset(&completed, 0, sizeof(completed));
    completed.mapping = mapping;
    completed.entry = entry;
    completed.allocation_size = allocation_size;
    completed.state = DB_LINUX_ARM64_WX_PUBLISHED;

    observed.completed_state = DB_LINUX_ARM64_WX_PUBLISHED;
    observed.status = DB_LINUX_ARM64_WX_OK;
    observed.primary_status = DB_LINUX_ARM64_WX_OK;
    *handle = completed;
    *audit = observed;
    return DB_LINUX_ARM64_WX_OK;
}

db_linux_arm64_wx_status db_linux_arm64_wx_release(
    const db_linux_arm64_wx_platform *platform,
    db_linux_arm64_wx_handle *handle,
    db_linux_arm64_wx_audit *audit)
{
    db_linux_arm64_wx_audit observed;

    if (platform == NULL || handle == NULL || audit == NULL) {
        return DB_LINUX_ARM64_WX_NULL_ARGUMENT;
    }
    if (regions_overlap(handle, sizeof(*handle),
                        audit, sizeof(*audit)) ||
        regions_overlap(handle, sizeof(*handle),
                        platform, sizeof(*platform)) ||
        regions_overlap(audit, sizeof(*audit),
                        platform, sizeof(*platform)) ||
        (handle->mapping != NULL &&
         handle->allocation_size != 0u &&
         (regions_overlap(handle, sizeof(*handle),
                          handle->mapping,
                          handle->allocation_size) ||
          regions_overlap(audit, sizeof(*audit),
                          handle->mapping,
                          handle->allocation_size)))) {
        return DB_LINUX_ARM64_WX_BUFFER_OVERLAP;
    }

    audit_init(&observed, NULL);

    if (!platform_valid(platform)) {
        observed.status = DB_LINUX_ARM64_WX_BAD_PLATFORM;
        *audit = observed;
        return DB_LINUX_ARM64_WX_BAD_PLATFORM;
    }
    if (!handle_releasable(handle)) {
        observed.status = DB_LINUX_ARM64_WX_HANDLE_NOT_PUBLISHED;
        *audit = observed;
        return DB_LINUX_ARM64_WX_HANDLE_NOT_PUBLISHED;
    }

    observed.mapped_base = (uint64_t)(uintptr_t)handle->mapping;
    observed.entry_address = (uint64_t)(uintptr_t)handle->entry;
    observed.allocation_size = handle->allocation_size;
    observed.completed_state = handle->state;

    if (platform->unmap(platform->context,
                        handle->mapping,
                        handle->allocation_size) != 0) {
        observed.failure_state = DB_LINUX_ARM64_WX_EMPTY;
        observed.status = DB_LINUX_ARM64_WX_UNMAP_FAILED;
        observed.primary_status = DB_LINUX_ARM64_WX_UNMAP_FAILED;
        *audit = observed;
        return DB_LINUX_ARM64_WX_UNMAP_FAILED;
    }

    observed.release_observed = 1u;
    observed.completed_state = DB_LINUX_ARM64_WX_EMPTY;
    observed.status = DB_LINUX_ARM64_WX_OK;
    observed.primary_status = DB_LINUX_ARM64_WX_OK;
    db_linux_arm64_wx_handle_init(handle);
    *audit = observed;
    return DB_LINUX_ARM64_WX_OK;
}

const char *db_linux_arm64_wx_status_name(db_linux_arm64_wx_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad platform operations",
        "bad sealed image plan",
        "bad image byte size",
        "publication handle is not empty",
        "publication handle is not releasable",
        "read-write mapping failed",
        "mapping address does not match sealed code base",
        "image copy failed",
        "copied image bytes do not match",
        "read-execute transition failed",
        "instruction cache synchronization failed",
        "entry address does not match sealed plan",
        "mapping release failed",
        "rollback mapping release failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown Linux ARM64 W^X provider status";
    }
    return names[(size_t)status];
}
