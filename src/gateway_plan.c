#include "dblyat/gateway_plan.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_gateway_plan_mul_size(size_t left,
                                     size_t right,
                                     size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_gateway_plan_mul_u64(uint64_t left,
                                    uint64_t right,
                                    uint64_t *result)
{
    if (left != 0u && right > UINT64_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_gateway_plan_add_u64(uint64_t left,
                                    uint64_t right,
                                    uint64_t *result)
{
    if (left > UINT64_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool db_gateway_plan_ranges_overlap(const void *left,
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

static bool db_gateway_plan_is_power_of_two(uint64_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool db_gateway_plan_address_is_canonical(uint64_t value)
{
    const uint64_t upper = value >> 48u;
    const uint64_t sign = (value >> 47u) & 1u;

    return (sign == 0u && upper == 0u) ||
           (sign != 0u && upper == UINT64_C(0xffff));
}

static bool db_gateway_plan_binding_is_valid(
    const db_import_binding *binding)
{
    return binding->provider_id != 0u &&
           binding->symbol_id != 0u &&
           binding->reserved == 0u;
}

static bool db_gateway_plan_keys_equal(const db_import_binding *left,
                                       const db_import_binding *right)
{
    return left->provider_id == right->provider_id &&
           left->symbol_id == right->symbol_id;
}

static bool db_gateway_plan_key_is_first(
    const db_import_binding *bindings,
    size_t index)
{
    size_t other;

    for (other = 0u; other < index; ++other) {
        if (db_gateway_plan_keys_equal(&bindings[index],
                                       &bindings[other])) {
            return false;
        }
    }

    return true;
}

static db_gateway_plan_status db_gateway_plan_validate_layout(
    uint64_t gateway_base,
    uint64_t region_size,
    uint64_t slot_size)
{
    uint64_t region_end;

    if (!db_gateway_plan_is_power_of_two(slot_size)) {
        return DB_GATEWAY_PLAN_INVALID_SLOT_SIZE;
    }

    if (gateway_base == 0u ||
        !db_gateway_plan_address_is_canonical(gateway_base) ||
        (gateway_base & (slot_size - 1u)) != 0u) {
        return DB_GATEWAY_PLAN_INVALID_GATEWAY_BASE;
    }

    if (region_size < slot_size ||
        !db_gateway_plan_add_u64(gateway_base,
                                 region_size,
                                 &region_end) ||
        region_end <= gateway_base) {
        return DB_GATEWAY_PLAN_INVALID_REGION;
    }

    return DB_GATEWAY_PLAN_OK;
}

static db_gateway_plan_status db_gateway_plan_validate_aliases(
    const db_import_binding *bindings,
    size_t binding_bytes,
    db_iat_target *targets,
    size_t target_bytes,
    db_gateway_plan_summary *summary)
{
    if (db_gateway_plan_ranges_overlap(bindings, binding_bytes,
                                       targets, target_bytes) ||
        db_gateway_plan_ranges_overlap(bindings, binding_bytes,
                                       summary, sizeof(*summary)) ||
        db_gateway_plan_ranges_overlap(targets, target_bytes,
                                       summary, sizeof(*summary))) {
        return DB_GATEWAY_PLAN_BUFFER_OVERLAP;
    }

    return DB_GATEWAY_PLAN_OK;
}

static db_gateway_plan_status db_gateway_plan_validate_slot(
    uint64_t gateway_base,
    uint64_t region_size,
    uint64_t slot_size,
    size_t slot_index)
{
    uint64_t offset;
    uint64_t slot_end_offset;
    uint64_t address;
    uint64_t address_end;

    const uint64_t slot_number = (uint64_t)slot_index;

    if ((size_t)slot_number != slot_index) {
        return DB_GATEWAY_PLAN_ADDRESS_OVERFLOW;
    }

    if (!db_gateway_plan_mul_u64(slot_number,
                                 slot_size,
                                 &offset) ||
        !db_gateway_plan_add_u64(offset,
                                 slot_size,
                                 &slot_end_offset)) {
        return DB_GATEWAY_PLAN_ADDRESS_OVERFLOW;
    }

    if (slot_end_offset > region_size) {
        return DB_GATEWAY_PLAN_INVALID_REGION;
    }

    if (!db_gateway_plan_add_u64(gateway_base, offset, &address) ||
        !db_gateway_plan_add_u64(address,
                                 slot_size - 1u,
                                 &address_end)) {
        return DB_GATEWAY_PLAN_ADDRESS_OVERFLOW;
    }

    if (address == 0u ||
        !db_gateway_plan_address_is_canonical(address) ||
        !db_gateway_plan_address_is_canonical(address_end)) {
        return DB_GATEWAY_PLAN_NONCANONICAL_ADDRESS;
    }

    return DB_GATEWAY_PLAN_OK;
}

db_gateway_plan_status db_gateway_plan_targets(
    const db_import_binding *bindings,
    size_t binding_count,
    uint64_t gateway_base,
    uint64_t region_size,
    uint64_t slot_size,
    db_iat_target *targets,
    size_t target_capacity,
    db_gateway_plan_summary *summary)
{
    db_gateway_plan_summary planned;
    db_gateway_plan_status status;
    size_t binding_bytes;
    size_t target_bytes;
    size_t unique_count = 0u;
    size_t index;
    uint64_t used_size;

    if (summary == NULL ||
        (binding_count != 0u && bindings == NULL) ||
        (target_capacity != 0u && targets == NULL)) {
        return DB_GATEWAY_PLAN_NULL_ARGUMENT;
    }

    if (!db_gateway_plan_mul_size(binding_count,
                                  sizeof(*bindings),
                                  &binding_bytes) ||
        !db_gateway_plan_mul_size(target_capacity,
                                  sizeof(*targets),
                                  &target_bytes)) {
        return DB_GATEWAY_PLAN_COUNT_OVERFLOW;
    }

    status = db_gateway_plan_validate_aliases(bindings,
                                              binding_bytes,
                                              targets,
                                              target_bytes,
                                              summary);
    if (status != DB_GATEWAY_PLAN_OK) {
        return status;
    }

    status = db_gateway_plan_validate_layout(gateway_base,
                                             region_size,
                                             slot_size);
    if (status != DB_GATEWAY_PLAN_OK) {
        return status;
    }

    for (index = 0u; index < binding_count; ++index) {
        if (!db_gateway_plan_binding_is_valid(&bindings[index])) {
            return DB_GATEWAY_PLAN_BAD_BINDING;
        }

        if (db_gateway_plan_key_is_first(bindings, index)) {
            status = db_gateway_plan_validate_slot(gateway_base,
                                                   region_size,
                                                   slot_size,
                                                   unique_count);
            if (status != DB_GATEWAY_PLAN_OK) {
                return status;
            }

            if (unique_count == SIZE_MAX) {
                return DB_GATEWAY_PLAN_COUNT_OVERFLOW;
            }
            ++unique_count;
        }
    }

    {
        const uint64_t unique_count_u64 = (uint64_t)unique_count;

        if ((size_t)unique_count_u64 != unique_count ||
            !db_gateway_plan_mul_u64(unique_count_u64,
                                     slot_size,
                                     &used_size)) {
            return DB_GATEWAY_PLAN_ADDRESS_OVERFLOW;
        }
    }

    if (target_capacity < unique_count) {
        return DB_GATEWAY_PLAN_OUTPUT_BUFFER_TOO_SMALL;
    }

    unique_count = 0u;
    for (index = 0u; index < binding_count; ++index) {
        if (db_gateway_plan_key_is_first(bindings, index)) {
            const uint64_t offset =
                (uint64_t)unique_count * slot_size;
            db_iat_target target;

            target.provider_id = bindings[index].provider_id;
            target.symbol_id = bindings[index].symbol_id;
            target.guest_value = gateway_base + offset;
            targets[unique_count] = target;
            ++unique_count;
        }
    }

    planned.binding_count = binding_count;
    planned.target_count = unique_count;
    planned.gateway_base = gateway_base;
    planned.region_size = region_size;
    planned.slot_size = slot_size;
    planned.used_size = used_size;
    *summary = planned;

    return DB_GATEWAY_PLAN_OK;
}

const char *db_gateway_plan_status_name(db_gateway_plan_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "bad import binding",
        "invalid gateway slot size",
        "invalid gateway base",
        "invalid gateway region",
        "gateway address overflows",
        "gateway address is noncanonical",
        "output buffer is too small"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway plan status";
    }

    return names[(size_t)status];
}
