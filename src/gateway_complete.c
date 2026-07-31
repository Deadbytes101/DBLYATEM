#include "dblyat/gateway_complete.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_gateway_complete_ranges_overlap(const void *left,
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

db_gateway_complete_status db_gateway_complete_raw(
    const db_gateway_frame_summary *frame,
    uint64_t raw_result,
    db_gateway_x64_guest_state *state,
    db_gateway_complete_summary *summary)
{
    db_gateway_complete_summary completed;
    db_gateway_return_status return_status;
    db_gateway_return_apply_status apply_status;

    if (frame == NULL || state == NULL || summary == NULL) {
        return DB_GATEWAY_COMPLETE_NULL_ARGUMENT;
    }

    if (db_gateway_complete_ranges_overlap(frame,
                                           sizeof(*frame),
                                           state,
                                           sizeof(*state)) ||
        db_gateway_complete_ranges_overlap(frame,
                                           sizeof(*frame),
                                           summary,
                                           sizeof(*summary)) ||
        db_gateway_complete_ranges_overlap(state,
                                           sizeof(*state),
                                           summary,
                                           sizeof(*summary))) {
        return DB_GATEWAY_COMPLETE_BUFFER_OVERLAP;
    }

    return_status = db_gateway_return_plan_raw(frame,
                                               raw_result,
                                               &completed.return_plan);
    if (return_status != DB_GATEWAY_RETURN_OK) {
        return DB_GATEWAY_COMPLETE_RETURN_PLAN_FAILED;
    }

    apply_status = db_gateway_return_apply_plan(
        &completed.return_plan,
        state,
        &completed.apply_summary);
    if (apply_status != DB_GATEWAY_RETURN_APPLY_OK) {
        return DB_GATEWAY_COMPLETE_RETURN_APPLY_FAILED;
    }

    *summary = completed;
    return DB_GATEWAY_COMPLETE_OK;
}

const char *db_gateway_complete_status_name(
    db_gateway_complete_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "return planning failed",
        "return application failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway completion status";
    }

    return names[(size_t)status];
}
