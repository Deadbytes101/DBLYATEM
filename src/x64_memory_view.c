#include "dblyat/x64_memory_view.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_x64_memory_view_overlap(const void *left,
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

static bool db_x64_memory_view_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return upper == (sign != 0u ? UINT64_C(0xffff) : UINT64_C(0));
}

static bool db_x64_memory_view_reserved_zero(
    const db_x64_memory_view *view)
{
    size_t index;

    for (index = 0u; index < sizeof(view->reserved); ++index) {
        if (view->reserved[index] != 0u) {
            return false;
        }
    }

    return true;
}

static bool db_x64_memory_view_valid(const db_x64_memory_view *view)
{
    uint64_t last_address;

    if (view->bytes == NULL ||
        view->size == 0u ||
        view->permissions == 0u ||
        (view->permissions & (uint8_t)~DB_X64_MEMORY_PERMISSION_MASK) != 0u ||
        !db_x64_memory_view_reserved_zero(view) ||
        !db_x64_memory_view_canonical(view->guest_base) ||
        db_x64_memory_view_overlap(view->bytes,
                                   view->size,
                                   view,
                                   sizeof(*view))) {
        return false;
    }

    if (view->size - 1u > (size_t)(UINT64_MAX - view->guest_base)) {
        return false;
    }

    last_address = view->guest_base + (uint64_t)(view->size - 1u);
    return db_x64_memory_view_canonical(last_address);
}

static db_x64_memory_view_status db_x64_memory_view_plan(
    const db_x64_memory_view *view,
    uint64_t guest_address,
    uint8_t permission,
    size_t *offset)
{
    uint64_t delta;
    uint64_t end_address;

    if (!db_x64_memory_view_valid(view)) {
        return DB_X64_MEMORY_VIEW_BAD_VIEW;
    }

    if ((view->permissions & permission) == 0u) {
        return DB_X64_MEMORY_VIEW_PERMISSION_DENIED;
    }

    if (!db_x64_memory_view_canonical(guest_address)) {
        return DB_X64_MEMORY_VIEW_NONCANONICAL_ADDRESS;
    }

    if (guest_address < view->guest_base) {
        return DB_X64_MEMORY_VIEW_OUT_OF_BOUNDS;
    }

    delta = guest_address - view->guest_base;
    if (delta > (uint64_t)SIZE_MAX ||
        view->size < (size_t)DB_X64_MEMORY_ACCESS_WIDTH ||
        (size_t)delta >
            view->size - (size_t)DB_X64_MEMORY_ACCESS_WIDTH) {
        return DB_X64_MEMORY_VIEW_OUT_OF_BOUNDS;
    }

    if (guest_address >
        UINT64_MAX - ((uint64_t)DB_X64_MEMORY_ACCESS_WIDTH - UINT64_C(1))) {
        return DB_X64_MEMORY_VIEW_NONCANONICAL_ADDRESS;
    }

    end_address = guest_address +
                  ((uint64_t)DB_X64_MEMORY_ACCESS_WIDTH - UINT64_C(1));
    if (!db_x64_memory_view_canonical(end_address)) {
        return DB_X64_MEMORY_VIEW_NONCANONICAL_ADDRESS;
    }

    *offset = (size_t)delta;
    return DB_X64_MEMORY_VIEW_OK;
}

static db_x64_memory_access_summary db_x64_memory_view_summary(
    uint64_t guest_address,
    size_t offset,
    uint64_t value,
    db_x64_memory_access_kind kind)
{
    db_x64_memory_access_summary summary;

    summary.guest_address = guest_address;
    summary.offset = (uint64_t)offset;
    summary.value = value;
    summary.kind = kind;
    summary.width = DB_X64_MEMORY_ACCESS_WIDTH;
    summary.reserved[0] = 0u;
    summary.reserved[1] = 0u;
    summary.reserved[2] = 0u;
    return summary;
}

db_x64_memory_view_status db_x64_memory_view_read_u64(
    const db_x64_memory_view *view,
    uint64_t guest_address,
    uint64_t *value,
    db_x64_memory_access_summary *summary)
{
    db_x64_memory_access_summary planned_summary;
    db_x64_memory_view_status status;
    uint64_t planned_value;
    size_t offset;
    size_t index;

    if (view == NULL || value == NULL || summary == NULL) {
        return DB_X64_MEMORY_VIEW_NULL_ARGUMENT;
    }

    status = db_x64_memory_view_plan(view,
                                     guest_address,
                                     DB_X64_MEMORY_PERMISSION_READ,
                                     &offset);
    if (status != DB_X64_MEMORY_VIEW_OK) {
        return status;
    }

    if (db_x64_memory_view_overlap(view, sizeof(*view),
                                   value, sizeof(*value)) ||
        db_x64_memory_view_overlap(view, sizeof(*view),
                                   summary, sizeof(*summary)) ||
        db_x64_memory_view_overlap(value, sizeof(*value),
                                   summary, sizeof(*summary)) ||
        db_x64_memory_view_overlap(view->bytes, view->size,
                                   value, sizeof(*value)) ||
        db_x64_memory_view_overlap(view->bytes, view->size,
                                   summary, sizeof(*summary))) {
        return DB_X64_MEMORY_VIEW_BUFFER_OVERLAP;
    }

    planned_value = 0u;
    for (index = 0u; index < (size_t)DB_X64_MEMORY_ACCESS_WIDTH; ++index) {
        planned_value |= (uint64_t)view->bytes[offset + index] <<
                         (unsigned int)(index * 8u);
    }

    planned_summary = db_x64_memory_view_summary(
        guest_address,
        offset,
        planned_value,
        DB_X64_MEMORY_ACCESS_READ_U64);

    *value = planned_value;
    *summary = planned_summary;
    return DB_X64_MEMORY_VIEW_OK;
}

db_x64_memory_view_status db_x64_memory_view_write_u64(
    db_x64_memory_view *view,
    uint64_t guest_address,
    uint64_t value,
    db_x64_memory_access_summary *summary)
{
    db_x64_memory_access_summary planned_summary;
    db_x64_memory_view_status status;
    uint8_t planned_bytes[DB_X64_MEMORY_ACCESS_WIDTH];
    size_t offset;
    size_t index;

    if (view == NULL || summary == NULL) {
        return DB_X64_MEMORY_VIEW_NULL_ARGUMENT;
    }

    status = db_x64_memory_view_plan(view,
                                     guest_address,
                                     DB_X64_MEMORY_PERMISSION_WRITE,
                                     &offset);
    if (status != DB_X64_MEMORY_VIEW_OK) {
        return status;
    }

    if (db_x64_memory_view_overlap(view, sizeof(*view),
                                   summary, sizeof(*summary)) ||
        db_x64_memory_view_overlap(view->bytes, view->size,
                                   summary, sizeof(*summary))) {
        return DB_X64_MEMORY_VIEW_BUFFER_OVERLAP;
    }

    for (index = 0u; index < (size_t)DB_X64_MEMORY_ACCESS_WIDTH; ++index) {
        planned_bytes[index] =
            (uint8_t)(value >> (unsigned int)(index * 8u));
    }

    planned_summary = db_x64_memory_view_summary(
        guest_address,
        offset,
        value,
        DB_X64_MEMORY_ACCESS_WRITE_U64);

    for (index = 0u; index < (size_t)DB_X64_MEMORY_ACCESS_WIDTH; ++index) {
        view->bytes[offset + index] = planned_bytes[index];
    }
    *summary = planned_summary;
    return DB_X64_MEMORY_VIEW_OK;
}

const char *db_x64_memory_view_status_name(
    db_x64_memory_view_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad memory view",
        "permission denied",
        "noncanonical address",
        "out of bounds"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 memory view status";
    }

    return names[(size_t)status];
}

const char *db_x64_memory_access_kind_name(
    db_x64_memory_access_kind kind)
{
    static const char *const names[] = {
        "read u64",
        "write u64"
    };

    if ((size_t)kind >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 memory access kind";
    }

    return names[(size_t)kind];
}
