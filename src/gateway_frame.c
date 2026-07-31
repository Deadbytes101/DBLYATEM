#include "dblyat/gateway_frame.h"

#include <stdbool.h>
#include <stdint.h>

#define DB_GATEWAY_FRAME_STACK_ARGUMENT_OFFSET UINT64_C(0x28)
#define DB_GATEWAY_FRAME_SLOT_SIZE UINT64_C(8)

static bool db_gateway_frame_mul_size(size_t left,
                                      size_t right,
                                      size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_gateway_frame_mul_u64(uint64_t left,
                                     uint64_t right,
                                     uint64_t *result)
{
    if (left != 0u && right > UINT64_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_gateway_frame_add_u64(uint64_t left,
                                     uint64_t right,
                                     uint64_t *result)
{
    if (left > UINT64_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool db_gateway_frame_ranges_overlap(const void *left,
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

static bool db_gateway_frame_address_is_canonical(uint64_t value)
{
    const uint64_t upper = value >> 48u;
    const uint64_t sign = (value >> 47u) & 1u;

    return (sign == 0u && upper == 0u) ||
           (sign != 0u && upper == UINT64_C(0xffff));
}

static uint64_t db_gateway_frame_read_u64(const uint8_t *bytes,
                                          size_t offset)
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

static bool db_gateway_frame_dispatch_is_valid(
    const db_gateway_dispatch_plan *dispatch)
{
    return dispatch->rip != 0u &&
           db_gateway_frame_address_is_canonical(dispatch->rip) &&
           dispatch->slot_address == dispatch->rip &&
           dispatch->slot_index != SIZE_MAX &&
           dispatch->provider_id != 0u &&
           dispatch->symbol_id != 0u &&
           dispatch->handler_id != 0u &&
           dispatch->reserved == 0u;
}

static db_gateway_frame_status db_gateway_frame_validate_catalog(
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    uint32_t handler_id,
    const db_gateway_frame_signature **matched)
{
    size_t index;

    *matched = NULL;

    for (index = 0u; index < signature_count; ++index) {
        const db_gateway_frame_signature *signature =
            &signatures[index];
        size_t other;

        if (signature->handler_id == 0u ||
            signature->argument_count >
                (uint16_t)DB_GATEWAY_FRAME_MAX_ARGUMENTS ||
            signature->reserved != 0u) {
            return DB_GATEWAY_FRAME_BAD_SIGNATURE;
        }

        for (other = 0u; other < index; ++other) {
            if (signatures[other].handler_id ==
                signature->handler_id) {
                return DB_GATEWAY_FRAME_DUPLICATE_HANDLER_ID;
            }
        }

        if (signature->handler_id == handler_id) {
            *matched = signature;
        }
    }

    if (*matched == NULL) {
        return DB_GATEWAY_FRAME_SIGNATURE_NOT_FOUND;
    }

    return DB_GATEWAY_FRAME_OK;
}

static db_gateway_frame_status db_gateway_frame_validate_stack_window(
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t guest_stack_base)
{
    uint64_t size_u64;
    uint64_t last_address;

    if (guest_stack == NULL || guest_stack_size == 0u) {
        return DB_GATEWAY_FRAME_INVALID_STACK_WINDOW;
    }

    size_u64 = (uint64_t)guest_stack_size;
    if ((size_t)size_u64 != guest_stack_size) {
        return DB_GATEWAY_FRAME_COUNT_OVERFLOW;
    }

    if (!db_gateway_frame_address_is_canonical(guest_stack_base) ||
        !db_gateway_frame_add_u64(guest_stack_base,
                                  size_u64 - 1u,
                                  &last_address)) {
        return DB_GATEWAY_FRAME_ADDRESS_OVERFLOW;
    }

    if (!db_gateway_frame_address_is_canonical(last_address)) {
        return DB_GATEWAY_FRAME_NONCANONICAL_ADDRESS;
    }

    return DB_GATEWAY_FRAME_OK;
}

static db_gateway_frame_status db_gateway_frame_guest_span_offset(
    uint64_t address,
    uint64_t length,
    uint64_t guest_stack_base,
    size_t guest_stack_size,
    size_t *offset)
{
    uint64_t size_u64 = (uint64_t)guest_stack_size;
    uint64_t relative;

    if ((size_t)size_u64 != guest_stack_size) {
        return DB_GATEWAY_FRAME_COUNT_OVERFLOW;
    }

    if (address < guest_stack_base) {
        return DB_GATEWAY_FRAME_STACK_OUT_OF_RANGE;
    }

    relative = address - guest_stack_base;
    if (relative > size_u64 || length > size_u64 - relative) {
        return DB_GATEWAY_FRAME_STACK_OUT_OF_RANGE;
    }

    *offset = (size_t)relative;
    if ((uint64_t)*offset != relative) {
        return DB_GATEWAY_FRAME_COUNT_OVERFLOW;
    }

    return DB_GATEWAY_FRAME_OK;
}

static db_gateway_frame_status db_gateway_frame_stack_argument_address(
    uint64_t rsp,
    size_t argument_index,
    uint64_t *address)
{
    const size_t stack_index = argument_index - 4u;
    const uint64_t stack_index_u64 = (uint64_t)stack_index;
    uint64_t offset;
    uint64_t base;

    if ((size_t)stack_index_u64 != stack_index ||
        !db_gateway_frame_mul_u64(stack_index_u64,
                                  DB_GATEWAY_FRAME_SLOT_SIZE,
                                  &offset) ||
        !db_gateway_frame_add_u64(rsp,
                                  DB_GATEWAY_FRAME_STACK_ARGUMENT_OFFSET,
                                  &base) ||
        !db_gateway_frame_add_u64(base, offset, address)) {
        return DB_GATEWAY_FRAME_ADDRESS_OVERFLOW;
    }

    if (!db_gateway_frame_address_is_canonical(*address)) {
        return DB_GATEWAY_FRAME_NONCANONICAL_ADDRESS;
    }

    return DB_GATEWAY_FRAME_OK;
}

static db_gateway_frame_status db_gateway_frame_validate_aliases(
    const db_gateway_dispatch_plan *dispatch,
    const db_gateway_frame_signature *signatures,
    size_t signature_bytes,
    const db_gateway_guest_registers *registers,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t *arguments,
    size_t argument_bytes,
    db_gateway_frame_summary *summary)
{
#define DB_FRAME_OVERLAP(a, as, b, bs) \
    db_gateway_frame_ranges_overlap((a), (as), (b), (bs))

    if (DB_FRAME_OVERLAP(dispatch, sizeof(*dispatch),
                         signatures, signature_bytes) ||
        DB_FRAME_OVERLAP(dispatch, sizeof(*dispatch),
                         registers, sizeof(*registers)) ||
        DB_FRAME_OVERLAP(dispatch, sizeof(*dispatch),
                         guest_stack, guest_stack_size) ||
        DB_FRAME_OVERLAP(dispatch, sizeof(*dispatch),
                         arguments, argument_bytes) ||
        DB_FRAME_OVERLAP(dispatch, sizeof(*dispatch),
                         summary, sizeof(*summary)) ||
        DB_FRAME_OVERLAP(signatures, signature_bytes,
                         registers, sizeof(*registers)) ||
        DB_FRAME_OVERLAP(signatures, signature_bytes,
                         guest_stack, guest_stack_size) ||
        DB_FRAME_OVERLAP(signatures, signature_bytes,
                         arguments, argument_bytes) ||
        DB_FRAME_OVERLAP(signatures, signature_bytes,
                         summary, sizeof(*summary)) ||
        DB_FRAME_OVERLAP(registers, sizeof(*registers),
                         guest_stack, guest_stack_size) ||
        DB_FRAME_OVERLAP(registers, sizeof(*registers),
                         arguments, argument_bytes) ||
        DB_FRAME_OVERLAP(registers, sizeof(*registers),
                         summary, sizeof(*summary)) ||
        DB_FRAME_OVERLAP(guest_stack, guest_stack_size,
                         arguments, argument_bytes) ||
        DB_FRAME_OVERLAP(guest_stack, guest_stack_size,
                         summary, sizeof(*summary)) ||
        DB_FRAME_OVERLAP(arguments, argument_bytes,
                         summary, sizeof(*summary))) {
        return DB_GATEWAY_FRAME_BUFFER_OVERLAP;
    }

#undef DB_FRAME_OVERLAP
    return DB_GATEWAY_FRAME_OK;
}

db_gateway_frame_status db_gateway_frame_plan_call(
    const db_gateway_dispatch_plan *dispatch,
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    const db_gateway_guest_registers *registers,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t guest_stack_base,
    uint64_t *arguments,
    size_t argument_capacity,
    db_gateway_frame_summary *summary)
{
    const uint8_t *stack_bytes = (const uint8_t *)guest_stack;
    const db_gateway_frame_signature *signature;
    db_gateway_frame_status status;
    db_gateway_frame_summary planned;
    size_t signature_bytes;
    size_t argument_bytes;
    size_t return_offset;
    size_t stack_offsets[DB_GATEWAY_FRAME_MAX_ARGUMENTS - 4u];
    size_t argument_count;
    size_t index;

    if (dispatch == NULL || registers == NULL || summary == NULL ||
        (signature_count != 0u && signatures == NULL) ||
        (argument_capacity != 0u && arguments == NULL)) {
        return DB_GATEWAY_FRAME_NULL_ARGUMENT;
    }

    if (!db_gateway_frame_mul_size(signature_count,
                                   sizeof(*signatures),
                                   &signature_bytes) ||
        !db_gateway_frame_mul_size(argument_capacity,
                                   sizeof(*arguments),
                                   &argument_bytes)) {
        return DB_GATEWAY_FRAME_COUNT_OVERFLOW;
    }

    status = db_gateway_frame_validate_aliases(dispatch,
                                               signatures,
                                               signature_bytes,
                                               registers,
                                               guest_stack,
                                               guest_stack_size,
                                               arguments,
                                               argument_bytes,
                                               summary);
    if (status != DB_GATEWAY_FRAME_OK) {
        return status;
    }

    if (!db_gateway_frame_dispatch_is_valid(dispatch)) {
        return DB_GATEWAY_FRAME_BAD_DISPATCH_PLAN;
    }

    status = db_gateway_frame_validate_catalog(signatures,
                                               signature_count,
                                               dispatch->handler_id,
                                               &signature);
    if (status != DB_GATEWAY_FRAME_OK) {
        return status;
    }

    status = db_gateway_frame_validate_stack_window(guest_stack,
                                                    guest_stack_size,
                                                    guest_stack_base);
    if (status != DB_GATEWAY_FRAME_OK) {
        return status;
    }

    if (registers->rsp == 0u ||
        !db_gateway_frame_address_is_canonical(registers->rsp)) {
        return DB_GATEWAY_FRAME_NONCANONICAL_ADDRESS;
    }

    status = db_gateway_frame_guest_span_offset(registers->rsp,
                                                DB_GATEWAY_FRAME_SLOT_SIZE,
                                                guest_stack_base,
                                                guest_stack_size,
                                                &return_offset);
    if (status != DB_GATEWAY_FRAME_OK) {
        return status;
    }

    argument_count = (size_t)signature->argument_count;
    for (index = 4u; index < argument_count; ++index) {
        uint64_t address;
        size_t offset;

        status = db_gateway_frame_stack_argument_address(registers->rsp,
                                                         index,
                                                         &address);
        if (status != DB_GATEWAY_FRAME_OK) {
            return status;
        }

        status = db_gateway_frame_guest_span_offset(
            address,
            DB_GATEWAY_FRAME_SLOT_SIZE,
            guest_stack_base,
            guest_stack_size,
            &offset);
        if (status != DB_GATEWAY_FRAME_OK) {
            return status;
        }

        stack_offsets[index - 4u] = offset;
    }

    if (argument_capacity < argument_count) {
        return DB_GATEWAY_FRAME_OUTPUT_BUFFER_TOO_SMALL;
    }

    for (index = 0u; index < argument_count; ++index) {
        if (index == 0u) {
            arguments[index] = registers->rcx;
        } else if (index == 1u) {
            arguments[index] = registers->rdx;
        } else if (index == 2u) {
            arguments[index] = registers->r8;
        } else if (index == 3u) {
            arguments[index] = registers->r9;
        } else {
            arguments[index] = db_gateway_frame_read_u64(
                stack_bytes,
                stack_offsets[index - 4u]);
        }
    }

    planned.rip = dispatch->rip;
    planned.slot_address = dispatch->slot_address;
    planned.guest_rsp = registers->rsp;
    planned.return_address = db_gateway_frame_read_u64(stack_bytes,
                                                       return_offset);
    planned.slot_index = dispatch->slot_index;
    planned.argument_count = argument_count;
    planned.provider_id = dispatch->provider_id;
    planned.symbol_id = dispatch->symbol_id;
    planned.handler_id = dispatch->handler_id;
    planned.reserved = 0u;
    *summary = planned;

    return DB_GATEWAY_FRAME_OK;
}

const char *db_gateway_frame_status_name(
    db_gateway_frame_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "bad dispatch plan",
        "bad handler signature",
        "duplicate handler id",
        "handler signature not found",
        "invalid guest stack window",
        "noncanonical guest address",
        "guest address overflows",
        "guest stack span is out of range",
        "output buffer is too small"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway frame status";
    }

    return names[(size_t)status];
}
