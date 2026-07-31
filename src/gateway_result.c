#include "dblyat/gateway_result.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_gateway_result_ranges_overlap(const void *left,
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

static bool db_gateway_result_address_is_canonical(uint64_t value)
{
    const uint64_t upper = value >> 48u;
    const uint64_t sign = (value >> 47u) & 1u;

    return (sign == 0u && upper == 0u) ||
           (sign != 0u && upper == UINT64_C(0xffff));
}

static bool db_gateway_result_request_is_valid(
    const db_gateway_invoke_request *request)
{
    size_t index;

    if (request->rip == 0u ||
        request->slot_address == 0u ||
        request->rip != request->slot_address ||
        request->guest_rsp == 0u ||
        request->return_address == 0u ||
        !db_gateway_result_address_is_canonical(request->rip) ||
        !db_gateway_result_address_is_canonical(request->guest_rsp) ||
        !db_gateway_result_address_is_canonical(request->return_address) ||
        request->slot_index == SIZE_MAX ||
        request->argument_count > DB_GATEWAY_FRAME_MAX_ARGUMENTS ||
        request->provider_id == 0u ||
        request->symbol_id == 0u ||
        request->handler_id == 0u ||
        request->reserved != 0u) {
        return false;
    }

    for (index = request->argument_count;
         index < DB_GATEWAY_FRAME_MAX_ARGUMENTS;
         ++index) {
        if (request->arguments[index] != 0u) {
            return false;
        }
    }

    return true;
}

static bool db_gateway_result_envelope_is_valid(
    const db_gateway_invoke_result *result)
{
    return result->rip != 0u &&
           result->slot_address != 0u &&
           result->rip == result->slot_address &&
           db_gateway_result_address_is_canonical(result->rip) &&
           result->slot_index != SIZE_MAX &&
           result->provider_id != 0u &&
           result->symbol_id != 0u &&
           result->handler_id != 0u &&
           result->reserved == 0u;
}

db_gateway_result_status db_gateway_result_validate(
    const db_gateway_invoke_request *request,
    const db_gateway_invoke_result *result,
    db_gateway_validated_result *validated)
{
    db_gateway_validated_result planned;

    if (request == NULL || result == NULL || validated == NULL) {
        return DB_GATEWAY_RESULT_NULL_ARGUMENT;
    }

    if (db_gateway_result_ranges_overlap(request,
                                         sizeof(*request),
                                         result,
                                         sizeof(*result)) ||
        db_gateway_result_ranges_overlap(request,
                                         sizeof(*request),
                                         validated,
                                         sizeof(*validated)) ||
        db_gateway_result_ranges_overlap(result,
                                         sizeof(*result),
                                         validated,
                                         sizeof(*validated))) {
        return DB_GATEWAY_RESULT_BUFFER_OVERLAP;
    }

    if (!db_gateway_result_request_is_valid(request)) {
        return DB_GATEWAY_RESULT_BAD_REQUEST;
    }

    if (!db_gateway_result_envelope_is_valid(result)) {
        return DB_GATEWAY_RESULT_BAD_RESULT;
    }

    if (result->rip != request->rip ||
        result->slot_address != request->slot_address ||
        result->slot_index != request->slot_index ||
        result->provider_id != request->provider_id ||
        result->symbol_id != request->symbol_id ||
        result->handler_id != request->handler_id) {
        return DB_GATEWAY_RESULT_IDENTITY_MISMATCH;
    }

    planned.rip = request->rip;
    planned.slot_address = request->slot_address;
    planned.guest_rsp = request->guest_rsp;
    planned.return_address = request->return_address;
    planned.raw_result = result->raw_result;
    planned.slot_index = request->slot_index;
    planned.argument_count = request->argument_count;
    planned.provider_id = request->provider_id;
    planned.symbol_id = request->symbol_id;
    planned.handler_id = request->handler_id;
    planned.reserved = 0u;
    *validated = planned;

    return DB_GATEWAY_RESULT_OK;
}

const char *db_gateway_result_status_name(db_gateway_result_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad invocation request",
        "bad invocation result",
        "invocation identity mismatch"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway result status";
    }

    return names[(size_t)status];
}
