#include "dblyat/gateway_return_apply.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_gateway_return_apply_ranges_overlap(
    const void *left,
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

static bool db_gateway_return_apply_address_is_canonical(uint64_t value)
{
    const uint64_t upper = value >> 48u;
    const uint64_t sign = (value >> 47u) & 1u;

    return (sign == 0u && upper == 0u) ||
           (sign != 0u && upper == UINT64_C(0xffff));
}

static bool db_gateway_return_apply_add_u64(uint64_t left,
                                            uint64_t right,
                                            uint64_t *result)
{
    if (left > UINT64_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static db_gateway_return_apply_status db_gateway_return_apply_validate_plan(
    const db_gateway_return_plan *plan)
{
    uint64_t expected_next_rsp;

    if (plan->source_rip == 0u ||
        plan->slot_address != plan->source_rip ||
        plan->next_rip == 0u ||
        plan->previous_rsp == 0u ||
        plan->next_rsp == 0u ||
        plan->slot_index == SIZE_MAX ||
        plan->argument_count > DB_GATEWAY_FRAME_MAX_ARGUMENTS ||
        plan->provider_id == 0u ||
        plan->symbol_id == 0u ||
        plan->handler_id == 0u ||
        plan->reserved != 0u) {
        return DB_GATEWAY_RETURN_APPLY_BAD_RETURN_PLAN;
    }

    if (!db_gateway_return_apply_address_is_canonical(plan->source_rip) ||
        !db_gateway_return_apply_address_is_canonical(plan->next_rip) ||
        !db_gateway_return_apply_address_is_canonical(plan->previous_rsp)) {
        return DB_GATEWAY_RETURN_APPLY_NONCANONICAL_ADDRESS;
    }

    if (!db_gateway_return_apply_add_u64(plan->previous_rsp,
                                         UINT64_C(8),
                                         &expected_next_rsp)) {
        return DB_GATEWAY_RETURN_APPLY_ADDRESS_OVERFLOW;
    }

    if (plan->next_rsp != expected_next_rsp) {
        return DB_GATEWAY_RETURN_APPLY_BAD_RETURN_PLAN;
    }

    if (!db_gateway_return_apply_address_is_canonical(plan->next_rsp)) {
        return DB_GATEWAY_RETURN_APPLY_NONCANONICAL_ADDRESS;
    }

    return DB_GATEWAY_RETURN_APPLY_OK;
}

db_gateway_return_apply_status db_gateway_return_apply_plan(
    const db_gateway_return_plan *plan,
    db_gateway_x64_guest_state *state,
    db_gateway_return_apply_summary *summary)
{
    db_gateway_return_apply_summary applied;
    db_gateway_return_apply_status status;

    if (plan == NULL || state == NULL || summary == NULL) {
        return DB_GATEWAY_RETURN_APPLY_NULL_ARGUMENT;
    }

    if (db_gateway_return_apply_ranges_overlap(plan,
                                               sizeof(*plan),
                                               state,
                                               sizeof(*state)) ||
        db_gateway_return_apply_ranges_overlap(plan,
                                               sizeof(*plan),
                                               summary,
                                               sizeof(*summary)) ||
        db_gateway_return_apply_ranges_overlap(state,
                                               sizeof(*state),
                                               summary,
                                               sizeof(*summary))) {
        return DB_GATEWAY_RETURN_APPLY_BUFFER_OVERLAP;
    }

    status = db_gateway_return_apply_validate_plan(plan);
    if (status != DB_GATEWAY_RETURN_APPLY_OK) {
        return status;
    }

    if (state->rip != plan->source_rip ||
        state->rsp != plan->previous_rsp) {
        return DB_GATEWAY_RETURN_APPLY_STALE_STATE;
    }

    applied.source_rip = plan->source_rip;
    applied.next_rip = plan->next_rip;
    applied.previous_rsp = plan->previous_rsp;
    applied.next_rsp = plan->next_rsp;
    applied.previous_rax = state->rax;
    applied.next_rax = plan->rax;
    applied.slot_index = plan->slot_index;
    applied.argument_count = plan->argument_count;
    applied.provider_id = plan->provider_id;
    applied.symbol_id = plan->symbol_id;
    applied.handler_id = plan->handler_id;
    applied.reserved = 0u;

    state->rax = plan->rax;
    state->rsp = plan->next_rsp;
    state->rip = plan->next_rip;
    *summary = applied;

    return DB_GATEWAY_RETURN_APPLY_OK;
}

const char *db_gateway_return_apply_status_name(
    db_gateway_return_apply_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad return plan",
        "noncanonical guest address",
        "guest address overflows",
        "stale guest state"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway return apply status";
    }

    return names[(size_t)status];
}
