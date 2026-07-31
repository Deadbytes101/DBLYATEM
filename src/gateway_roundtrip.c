#include "dblyat/gateway_roundtrip.h"

#include <stdbool.h>
#include <stdint.h>

static void db_gateway_roundtrip_zero(void *data, size_t size)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        bytes[index] = 0u;
    }
}

static bool db_gateway_roundtrip_mul_size(size_t left,
                                          size_t right,
                                          size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_gateway_roundtrip_overlap(const void *left,
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

static db_gateway_roundtrip_status db_gateway_roundtrip_validate_outputs(
    const db_iat_target *targets,
    size_t target_bytes,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_bytes,
    const db_gateway_frame_signature *signatures,
    size_t signature_bytes,
    db_gateway_x64_guest_state *state,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t *arguments,
    size_t argument_bytes,
    db_gateway_roundtrip_summary *summary)
{
#define DB_RT_OVERLAP(a, as, b, bs) \
    db_gateway_roundtrip_overlap((a), (as), (b), (bs))

    if (DB_RT_OVERLAP(arguments, argument_bytes,
                      targets, target_bytes) ||
        DB_RT_OVERLAP(arguments, argument_bytes,
                      handlers, handler_bytes) ||
        DB_RT_OVERLAP(arguments, argument_bytes,
                      signatures, signature_bytes) ||
        DB_RT_OVERLAP(arguments, argument_bytes,
                      state, sizeof(*state)) ||
        DB_RT_OVERLAP(arguments, argument_bytes,
                      guest_stack, guest_stack_size) ||
        DB_RT_OVERLAP(arguments, argument_bytes,
                      summary, sizeof(*summary)) ||
        DB_RT_OVERLAP(summary, sizeof(*summary),
                      targets, target_bytes) ||
        DB_RT_OVERLAP(summary, sizeof(*summary),
                      handlers, handler_bytes) ||
        DB_RT_OVERLAP(summary, sizeof(*summary),
                      signatures, signature_bytes) ||
        DB_RT_OVERLAP(summary, sizeof(*summary),
                      state, sizeof(*state)) ||
        DB_RT_OVERLAP(summary, sizeof(*summary),
                      guest_stack, guest_stack_size) ||
        DB_RT_OVERLAP(state, sizeof(*state),
                      targets, target_bytes) ||
        DB_RT_OVERLAP(state, sizeof(*state),
                      handlers, handler_bytes) ||
        DB_RT_OVERLAP(state, sizeof(*state),
                      signatures, signature_bytes) ||
        DB_RT_OVERLAP(state, sizeof(*state),
                      guest_stack, guest_stack_size)) {
        return DB_GATEWAY_ROUNDTRIP_BUFFER_OVERLAP;
    }

#undef DB_RT_OVERLAP
    return DB_GATEWAY_ROUNDTRIP_OK;
}

db_gateway_roundtrip_status db_gateway_roundtrip_raw(
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    db_gateway_x64_guest_state *state,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t guest_stack_base,
    uint64_t raw_result,
    uint64_t *arguments,
    size_t argument_capacity,
    db_gateway_roundtrip_summary *summary)
{
    uint64_t planned_arguments[DB_GATEWAY_FRAME_MAX_ARGUMENTS];
    db_gateway_x64_guest_state next_state;
    db_gateway_roundtrip_summary completed;
    db_gateway_enter_status enter_status;
    db_gateway_complete_status complete_status;
    db_gateway_roundtrip_status status;
    size_t target_bytes;
    size_t handler_bytes;
    size_t signature_bytes;
    size_t argument_bytes;
    size_t argument_count;
    size_t index;

    if (state == NULL || guest_stack == NULL || summary == NULL ||
        (target_count != 0u && targets == NULL) ||
        (handler_count != 0u && handlers == NULL) ||
        (signature_count != 0u && signatures == NULL) ||
        (argument_capacity != 0u && arguments == NULL)) {
        return DB_GATEWAY_ROUNDTRIP_NULL_ARGUMENT;
    }

    if (!db_gateway_roundtrip_mul_size(target_count,
                                       sizeof(*targets),
                                       &target_bytes) ||
        !db_gateway_roundtrip_mul_size(handler_count,
                                       sizeof(*handlers),
                                       &handler_bytes) ||
        !db_gateway_roundtrip_mul_size(signature_count,
                                       sizeof(*signatures),
                                       &signature_bytes) ||
        !db_gateway_roundtrip_mul_size(argument_capacity,
                                       sizeof(*arguments),
                                       &argument_bytes)) {
        return DB_GATEWAY_ROUNDTRIP_COUNT_OVERFLOW;
    }

    status = db_gateway_roundtrip_validate_outputs(targets,
                                                   target_bytes,
                                                   handlers,
                                                   handler_bytes,
                                                   signatures,
                                                   signature_bytes,
                                                   state,
                                                   guest_stack,
                                                   guest_stack_size,
                                                   arguments,
                                                   argument_bytes,
                                                   summary);
    if (status != DB_GATEWAY_ROUNDTRIP_OK) {
        return status;
    }

    db_gateway_roundtrip_zero(planned_arguments,
                              sizeof(planned_arguments));
    db_gateway_roundtrip_zero(&completed, sizeof(completed));

    enter_status = db_gateway_enter_plan(
        targets,
        target_count,
        gateway_base,
        slot_size,
        handlers,
        handler_count,
        signatures,
        signature_count,
        state,
        guest_stack,
        guest_stack_size,
        guest_stack_base,
        planned_arguments,
        DB_GATEWAY_FRAME_MAX_ARGUMENTS,
        &completed.enter);
    if (enter_status != DB_GATEWAY_ENTER_OK) {
        return DB_GATEWAY_ROUNDTRIP_ENTRY_FAILED;
    }

    argument_count = completed.enter.frame.argument_count;
    if (argument_count > DB_GATEWAY_FRAME_MAX_ARGUMENTS ||
        argument_capacity < argument_count) {
        return DB_GATEWAY_ROUNDTRIP_ARGUMENT_BUFFER_TOO_SMALL;
    }

    next_state = *state;
    complete_status = db_gateway_complete_raw(&completed.enter.frame,
                                              raw_result,
                                              &next_state,
                                              &completed.complete);
    if (complete_status != DB_GATEWAY_COMPLETE_OK) {
        return DB_GATEWAY_ROUNDTRIP_COMPLETION_FAILED;
    }

    for (index = 0u; index < argument_count; ++index) {
        arguments[index] = planned_arguments[index];
    }
    *state = next_state;
    *summary = completed;
    return DB_GATEWAY_ROUNDTRIP_OK;
}

const char *db_gateway_roundtrip_status_name(
    db_gateway_roundtrip_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "gateway entry planning failed",
        "argument buffer is too small",
        "gateway completion failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway round-trip status";
    }

    return names[(size_t)status];
}
