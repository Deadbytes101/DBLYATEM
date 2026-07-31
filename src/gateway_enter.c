#include "dblyat/gateway_enter.h"

#include <stdbool.h>
#include <stdint.h>


static void db_gateway_enter_zero_bytes(void *data, size_t size)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        bytes[index] = 0u;
    }
}

static bool db_gateway_enter_mul_size(size_t left,
                                      size_t right,
                                      size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_gateway_enter_ranges_overlap(const void *left,
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

static db_gateway_enter_status db_gateway_enter_validate_aliases(
    const db_iat_target *targets,
    size_t target_bytes,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_bytes,
    const db_gateway_frame_signature *signatures,
    size_t signature_bytes,
    const db_gateway_x64_guest_state *state,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t *arguments,
    size_t argument_bytes,
    db_gateway_enter_summary *summary)
{
#define DB_ENTER_OVERLAP(a, as, b, bs) \
    db_gateway_enter_ranges_overlap((a), (as), (b), (bs))

    if (DB_ENTER_OVERLAP(targets, target_bytes,
                         handlers, handler_bytes) ||
        DB_ENTER_OVERLAP(targets, target_bytes,
                         signatures, signature_bytes) ||
        DB_ENTER_OVERLAP(targets, target_bytes,
                         state, sizeof(*state)) ||
        DB_ENTER_OVERLAP(targets, target_bytes,
                         guest_stack, guest_stack_size) ||
        DB_ENTER_OVERLAP(targets, target_bytes,
                         arguments, argument_bytes) ||
        DB_ENTER_OVERLAP(targets, target_bytes,
                         summary, sizeof(*summary)) ||
        DB_ENTER_OVERLAP(handlers, handler_bytes,
                         signatures, signature_bytes) ||
        DB_ENTER_OVERLAP(handlers, handler_bytes,
                         state, sizeof(*state)) ||
        DB_ENTER_OVERLAP(handlers, handler_bytes,
                         guest_stack, guest_stack_size) ||
        DB_ENTER_OVERLAP(handlers, handler_bytes,
                         arguments, argument_bytes) ||
        DB_ENTER_OVERLAP(handlers, handler_bytes,
                         summary, sizeof(*summary)) ||
        DB_ENTER_OVERLAP(signatures, signature_bytes,
                         state, sizeof(*state)) ||
        DB_ENTER_OVERLAP(signatures, signature_bytes,
                         guest_stack, guest_stack_size) ||
        DB_ENTER_OVERLAP(signatures, signature_bytes,
                         arguments, argument_bytes) ||
        DB_ENTER_OVERLAP(signatures, signature_bytes,
                         summary, sizeof(*summary)) ||
        DB_ENTER_OVERLAP(state, sizeof(*state),
                         guest_stack, guest_stack_size) ||
        DB_ENTER_OVERLAP(state, sizeof(*state),
                         arguments, argument_bytes) ||
        DB_ENTER_OVERLAP(state, sizeof(*state),
                         summary, sizeof(*summary)) ||
        DB_ENTER_OVERLAP(guest_stack, guest_stack_size,
                         arguments, argument_bytes) ||
        DB_ENTER_OVERLAP(guest_stack, guest_stack_size,
                         summary, sizeof(*summary)) ||
        DB_ENTER_OVERLAP(arguments, argument_bytes,
                         summary, sizeof(*summary))) {
        return DB_GATEWAY_ENTER_BUFFER_OVERLAP;
    }

#undef DB_ENTER_OVERLAP
    return DB_GATEWAY_ENTER_OK;
}

db_gateway_enter_status db_gateway_enter_plan(
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    const db_gateway_x64_guest_state *state,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t guest_stack_base,
    uint64_t *arguments,
    size_t argument_capacity,
    db_gateway_enter_summary *summary)
{
    db_gateway_enter_summary entered;
    db_gateway_guest_registers registers;
    db_gateway_lookup_status lookup_status;
    db_gateway_dispatch_status dispatch_status;
    db_gateway_frame_status frame_status;
    db_gateway_enter_status status;
    size_t target_bytes;
    size_t handler_bytes;
    size_t signature_bytes;
    size_t argument_bytes;

    if (state == NULL || guest_stack == NULL || summary == NULL ||
        (target_count != 0u && targets == NULL) ||
        (handler_count != 0u && handlers == NULL) ||
        (signature_count != 0u && signatures == NULL) ||
        (argument_capacity != 0u && arguments == NULL)) {
        return DB_GATEWAY_ENTER_NULL_ARGUMENT;
    }

    if (!db_gateway_enter_mul_size(target_count,
                                   sizeof(*targets),
                                   &target_bytes) ||
        !db_gateway_enter_mul_size(handler_count,
                                   sizeof(*handlers),
                                   &handler_bytes) ||
        !db_gateway_enter_mul_size(signature_count,
                                   sizeof(*signatures),
                                   &signature_bytes) ||
        !db_gateway_enter_mul_size(argument_capacity,
                                   sizeof(*arguments),
                                   &argument_bytes)) {
        return DB_GATEWAY_ENTER_COUNT_OVERFLOW;
    }

    status = db_gateway_enter_validate_aliases(targets,
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
    if (status != DB_GATEWAY_ENTER_OK) {
        return status;
    }

    db_gateway_enter_zero_bytes(&entered, sizeof(entered));

    lookup_status = db_gateway_lookup_rip(targets,
                                          target_count,
                                          gateway_base,
                                          slot_size,
                                          state->rip,
                                          &entered.lookup);
    if (lookup_status != DB_GATEWAY_LOOKUP_OK) {
        return DB_GATEWAY_ENTER_LOOKUP_FAILED;
    }

    if (entered.lookup.kind != DB_GATEWAY_LOOKUP_ENTRY) {
        return DB_GATEWAY_ENTER_NOT_ENTRY;
    }

    dispatch_status = db_gateway_dispatch_plan_entry(&entered.lookup,
                                                      handlers,
                                                      handler_count,
                                                      &entered.dispatch);
    if (dispatch_status != DB_GATEWAY_DISPATCH_OK) {
        return DB_GATEWAY_ENTER_DISPATCH_FAILED;
    }

    registers.rcx = state->rcx;
    registers.rdx = state->rdx;
    registers.r8 = state->r8;
    registers.r9 = state->r9;
    registers.rsp = state->rsp;

    frame_status = db_gateway_frame_plan_call(&entered.dispatch,
                                              signatures,
                                              signature_count,
                                              &registers,
                                              guest_stack,
                                              guest_stack_size,
                                              guest_stack_base,
                                              arguments,
                                              argument_capacity,
                                              &entered.frame);
    if (frame_status != DB_GATEWAY_FRAME_OK) {
        return DB_GATEWAY_ENTER_FRAME_FAILED;
    }

    *summary = entered;
    return DB_GATEWAY_ENTER_OK;
}

const char *db_gateway_enter_status_name(db_gateway_enter_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "gateway lookup failed",
        "gateway RIP is not an exact entry",
        "gateway dispatch planning failed",
        "gateway call-frame planning failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway entry status";
    }

    return names[(size_t)status];
}
