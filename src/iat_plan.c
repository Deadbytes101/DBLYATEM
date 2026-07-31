#include "dblyat/iat_plan.h"

#include <stdbool.h>
#include <stdint.h>

#define DB_IAT_PLAN_ENTRY_SIZE 8u
#define DB_IAT_PLAN_ORDINAL_FLAG UINT64_C(0x8000000000000000)
#define DB_IAT_PLAN_ORDINAL_MASK UINT64_C(0x000000000000ffff)

static bool db_iat_plan_mul_size(size_t left,
                                 size_t right,
                                 size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_iat_plan_span_fits(size_t total,
                                  size_t offset,
                                  size_t length)
{
    return offset <= total && length <= total - offset;
}

static bool db_iat_plan_ranges_overlap(const void *left,
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

static uint64_t db_iat_plan_read_u64(const uint8_t *bytes, size_t offset)
{
    return (uint64_t)bytes[offset] |
           ((uint64_t)bytes[offset + 1u] << 8u) |
           ((uint64_t)bytes[offset + 2u] << 16u) |
           ((uint64_t)bytes[offset + 3u] << 24u) |
           ((uint64_t)bytes[offset + 4u] << 32u) |
           ((uint64_t)bytes[offset + 5u] << 40u) |
           ((uint64_t)bytes[offset + 6u] << 48u) |
           ((uint64_t)bytes[offset + 7u] << 56u);
}

static bool db_iat_plan_record_is_valid(const db_import_record *record)
{
    if (record->iat_rva == 0u ||
        (record->kind != DB_IMPORT_BY_NAME &&
         record->kind != DB_IMPORT_BY_ORDINAL) ||
        record->reserved[0] != 0u ||
        record->reserved[1] != 0u ||
        record->reserved[2] != 0u) {
        return false;
    }

    if (record->kind == DB_IMPORT_BY_NAME) {
        return record->name_rva != 0u &&
               record->name_length != 0u &&
               record->ordinal == 0u &&
               (record->lookup_value & DB_IAT_PLAN_ORDINAL_FLAG) == 0u &&
               record->lookup_value <= UINT32_MAX - 2u &&
               record->name_rva == (uint32_t)record->lookup_value + 2u;
    }

    return record->name_rva == 0u &&
           record->name_length == 0u &&
           record->hint == 0u &&
           (record->lookup_value & DB_IAT_PLAN_ORDINAL_FLAG) != 0u &&
           (record->lookup_value &
            ~(DB_IAT_PLAN_ORDINAL_FLAG | DB_IAT_PLAN_ORDINAL_MASK)) == 0u &&
           (uint16_t)(record->lookup_value & DB_IAT_PLAN_ORDINAL_MASK) ==
               record->ordinal;
}

static bool db_iat_plan_binding_is_valid(
    const db_import_record *record,
    const db_import_binding *binding)
{
    return binding->module_index == record->module_index &&
           binding->provider_id != 0u &&
           binding->symbol_id != 0u &&
           binding->reserved == 0u;
}

static bool db_iat_plan_guest_value_is_valid(uint64_t value)
{
    const uint64_t upper = value >> 48u;
    const uint64_t sign = (value >> 47u) & 1u;

    return value != 0u &&
           ((sign == 0u && upper == 0u) ||
            (sign != 0u && upper == UINT64_C(0xffff)));
}

static bool db_iat_plan_target_is_valid(const db_iat_target *target)
{
    return target->provider_id != 0u &&
           target->symbol_id != 0u &&
           db_iat_plan_guest_value_is_valid(target->guest_value);
}

static const db_iat_target *db_iat_plan_find_target(
    const db_iat_target *targets,
    size_t target_count,
    const db_import_binding *binding)
{
    size_t index;

    for (index = 0u; index < target_count; ++index) {
        if (targets[index].provider_id == binding->provider_id &&
            targets[index].symbol_id == binding->symbol_id) {
            return &targets[index];
        }
    }

    return NULL;
}

static db_iat_plan_status db_iat_plan_validate_aliases(
    const void *mapped_image,
    size_t mapped_size,
    const db_import_record *records,
    size_t record_bytes,
    const db_import_binding *bindings,
    size_t binding_bytes,
    const db_iat_target *targets,
    size_t target_bytes,
    db_iat_patch *patches,
    size_t patch_bytes,
    db_iat_plan_summary *summary)
{
    if (db_iat_plan_ranges_overlap(patches, patch_bytes,
                                   summary, sizeof(*summary)) ||
        db_iat_plan_ranges_overlap(patches, patch_bytes,
                                   mapped_image, mapped_size) ||
        db_iat_plan_ranges_overlap(patches, patch_bytes,
                                   records, record_bytes) ||
        db_iat_plan_ranges_overlap(patches, patch_bytes,
                                   bindings, binding_bytes) ||
        db_iat_plan_ranges_overlap(patches, patch_bytes,
                                   targets, target_bytes) ||
        db_iat_plan_ranges_overlap(summary, sizeof(*summary),
                                   mapped_image, mapped_size) ||
        db_iat_plan_ranges_overlap(summary, sizeof(*summary),
                                   records, record_bytes) ||
        db_iat_plan_ranges_overlap(summary, sizeof(*summary),
                                   bindings, binding_bytes) ||
        db_iat_plan_ranges_overlap(summary, sizeof(*summary),
                                   targets, target_bytes)) {
        return DB_IAT_PLAN_BUFFER_OVERLAP;
    }

    return DB_IAT_PLAN_OK;
}

static db_iat_plan_status db_iat_plan_validate_targets(
    const db_iat_target *targets,
    size_t target_count)
{
    size_t index;

    for (index = 0u; index < target_count; ++index) {
        size_t other;

        if (!db_iat_plan_target_is_valid(&targets[index])) {
            return DB_IAT_PLAN_BAD_TARGET;
        }

        for (other = 0u; other < index; ++other) {
            if (targets[index].provider_id == targets[other].provider_id &&
                targets[index].symbol_id == targets[other].symbol_id) {
                return DB_IAT_PLAN_DUPLICATE_TARGET_KEY;
            }
        }
    }

    return DB_IAT_PLAN_OK;
}

static db_iat_plan_status db_iat_plan_validate_records(
    const uint8_t *image,
    size_t mapped_size,
    const db_import_record *records,
    size_t record_count,
    const db_import_binding *bindings,
    const db_iat_target *targets,
    size_t target_count)
{
    size_t index;

    for (index = 0u; index < record_count; ++index) {
        const db_import_record *record = &records[index];
        const db_import_binding *binding = &bindings[index];
        size_t other;
        size_t iat_offset;

        if (!db_iat_plan_record_is_valid(record)) {
            return DB_IAT_PLAN_BAD_IMPORT_RECORD;
        }

        if (!db_iat_plan_binding_is_valid(record, binding)) {
            return DB_IAT_PLAN_BAD_BINDING;
        }

        iat_offset = (size_t)record->iat_rva;
        if (!db_iat_plan_span_fits(mapped_size,
                                   iat_offset,
                                   DB_IAT_PLAN_ENTRY_SIZE)) {
            return DB_IAT_PLAN_IAT_OUT_OF_RANGE;
        }

        for (other = 0u; other < index; ++other) {
            const uint32_t left = record->iat_rva;
            const uint32_t right = records[other].iat_rva;
            const uint32_t distance = left >= right
                                          ? left - right
                                          : right - left;

            if (distance < DB_IAT_PLAN_ENTRY_SIZE) {
                return DB_IAT_PLAN_IAT_OVERLAP;
            }
        }

        if (db_iat_plan_read_u64(image, iat_offset) !=
            record->original_iat_value) {
            return DB_IAT_PLAN_STALE_IAT;
        }

        if (db_iat_plan_find_target(targets,
                                    target_count,
                                    binding) == NULL) {
            return DB_IAT_PLAN_TARGET_NOT_FOUND;
        }
    }

    return DB_IAT_PLAN_OK;
}

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
    db_iat_plan_summary *summary)
{
    const uint8_t *image = (const uint8_t *)mapped_image;
    db_iat_plan_summary planned;
    db_iat_plan_status status;
    size_t record_bytes;
    size_t binding_bytes;
    size_t target_bytes;
    size_t patch_bytes;
    size_t index;

    if (mapped_image == NULL || summary == NULL ||
        (record_count != 0u && records == NULL) ||
        (binding_count != 0u && bindings == NULL) ||
        (target_count != 0u && targets == NULL) ||
        (patch_capacity != 0u && patches == NULL)) {
        return DB_IAT_PLAN_NULL_ARGUMENT;
    }

    if (binding_count != record_count) {
        return DB_IAT_PLAN_BINDING_COUNT_MISMATCH;
    }

    if (!db_iat_plan_mul_size(record_count,
                              sizeof(*records),
                              &record_bytes) ||
        !db_iat_plan_mul_size(binding_count,
                              sizeof(*bindings),
                              &binding_bytes) ||
        !db_iat_plan_mul_size(target_count,
                              sizeof(*targets),
                              &target_bytes) ||
        !db_iat_plan_mul_size(patch_capacity,
                              sizeof(*patches),
                              &patch_bytes)) {
        return DB_IAT_PLAN_COUNT_OVERFLOW;
    }

    status = db_iat_plan_validate_aliases(mapped_image,
                                          mapped_size,
                                          records,
                                          record_bytes,
                                          bindings,
                                          binding_bytes,
                                          targets,
                                          target_bytes,
                                          patches,
                                          patch_bytes,
                                          summary);
    if (status != DB_IAT_PLAN_OK) {
        return status;
    }

    status = db_iat_plan_validate_targets(targets, target_count);
    if (status != DB_IAT_PLAN_OK) {
        return status;
    }

    status = db_iat_plan_validate_records(image,
                                          mapped_size,
                                          records,
                                          record_count,
                                          bindings,
                                          targets,
                                          target_count);
    if (status != DB_IAT_PLAN_OK) {
        return status;
    }

    if (patch_capacity < record_count) {
        return DB_IAT_PLAN_OUTPUT_BUFFER_TOO_SMALL;
    }

    for (index = 0u; index < record_count; ++index) {
        const db_import_record *record = &records[index];
        const db_import_binding *binding = &bindings[index];
        const db_iat_target *target = db_iat_plan_find_target(targets,
                                                               target_count,
                                                               binding);
        db_iat_patch patch;

        patch.record_index = index;
        patch.module_index = record->module_index;
        patch.provider_id = binding->provider_id;
        patch.symbol_id = binding->symbol_id;
        patch.iat_rva = record->iat_rva;
        patch.reserved = 0u;
        patch.expected_value = record->original_iat_value;
        patch.replacement_value = target->guest_value;
        patches[index] = patch;
    }

    planned.record_count = record_count;
    planned.target_count = target_count;
    planned.patch_count = record_count;
    *summary = planned;

    return DB_IAT_PLAN_OK;
}

const char *db_iat_plan_status_name(db_iat_plan_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "binding count mismatch",
        "bad import record",
        "bad import binding",
        "bad target",
        "duplicate target key",
        "target not found",
        "IAT span is out of range",
        "IAT spans overlap",
        "stale IAT value",
        "output buffer is too small"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown IAT plan status";
    }

    return names[(size_t)status];
}
