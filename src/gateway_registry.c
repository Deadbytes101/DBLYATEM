#include "dblyat/gateway_registry.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_gateway_registry_mul_size(size_t left,
                                         size_t right,
                                         size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_gateway_registry_overlap(const void *left,
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

static db_gateway_result_status db_gateway_registry_validate_request(
    const db_gateway_invoke_request *request)
{
    db_gateway_invoke_result probe = {0};
    db_gateway_validated_result validated;

    probe.rip = request->rip;
    probe.slot_address = request->slot_address;
    probe.slot_index = request->slot_index;
    probe.provider_id = request->provider_id;
    probe.symbol_id = request->symbol_id;
    probe.handler_id = request->handler_id;

    return db_gateway_result_validate(request, &probe, &validated);
}

static db_gateway_registry_status db_gateway_registry_validate_entries(
    const db_gateway_executor_entry *entries,
    size_t entry_count,
    const db_gateway_invoke_request *request,
    db_gateway_x64_guest_state *state,
    db_gateway_registry_summary *summary,
    size_t *matched_index)
{
    size_t index;

    *matched_index = SIZE_MAX;
    for (index = 0u; index < entry_count; ++index) {
        const db_gateway_executor_entry *entry = &entries[index];
        size_t other;

        if (entry->handler_id == 0u ||
            entry->reserved != 0u ||
            entry->callback == NULL ||
            entry->context == (void *)request ||
            entry->context == (void *)state ||
            entry->context == (void *)summary) {
            return DB_GATEWAY_REGISTRY_BAD_ENTRY;
        }

        for (other = 0u; other < index; ++other) {
            if (entries[other].handler_id == entry->handler_id) {
                return DB_GATEWAY_REGISTRY_DUPLICATE_HANDLER_ID;
            }
        }

        if (entry->handler_id == request->handler_id) {
            *matched_index = index;
        }
    }

    if (*matched_index == SIZE_MAX) {
        return DB_GATEWAY_REGISTRY_HANDLER_NOT_FOUND;
    }

    return DB_GATEWAY_REGISTRY_OK;
}

db_gateway_registry_status db_gateway_registry_run(
    const db_gateway_invoke_request *request,
    const db_gateway_executor_entry *entries,
    size_t entry_count,
    db_gateway_x64_guest_state *state,
    db_gateway_registry_summary *summary)
{
    db_gateway_registry_summary completed;
    db_gateway_x64_guest_state planned_state;
    db_gateway_registry_status status;
    db_gateway_executor_status executor_status;
    size_t entry_bytes;
    size_t matched_index;

    if (request == NULL || state == NULL || summary == NULL ||
        (entry_count != 0u && entries == NULL)) {
        return DB_GATEWAY_REGISTRY_NULL_ARGUMENT;
    }

    if (!db_gateway_registry_mul_size(entry_count,
                                      sizeof(*entries),
                                      &entry_bytes)) {
        return DB_GATEWAY_REGISTRY_COUNT_OVERFLOW;
    }

    if (db_gateway_registry_overlap(request, sizeof(*request),
                                    entries, entry_bytes) ||
        db_gateway_registry_overlap(request, sizeof(*request),
                                    state, sizeof(*state)) ||
        db_gateway_registry_overlap(request, sizeof(*request),
                                    summary, sizeof(*summary)) ||
        db_gateway_registry_overlap(entries, entry_bytes,
                                    state, sizeof(*state)) ||
        db_gateway_registry_overlap(entries, entry_bytes,
                                    summary, sizeof(*summary)) ||
        db_gateway_registry_overlap(state, sizeof(*state),
                                    summary, sizeof(*summary))) {
        return DB_GATEWAY_REGISTRY_BUFFER_OVERLAP;
    }

    if (db_gateway_registry_validate_request(request) !=
        DB_GATEWAY_RESULT_OK) {
        return DB_GATEWAY_REGISTRY_BAD_REQUEST;
    }

    status = db_gateway_registry_validate_entries(entries,
                                                  entry_count,
                                                  request,
                                                  state,
                                                  summary,
                                                  &matched_index);
    if (status != DB_GATEWAY_REGISTRY_OK) {
        return status;
    }

    planned_state = *state;
    executor_status = db_gateway_executor_run(
        request,
        entries[matched_index].callback,
        entries[matched_index].context,
        &planned_state,
        &completed.executor);
    if (executor_status != DB_GATEWAY_EXECUTOR_OK) {
        return DB_GATEWAY_REGISTRY_EXECUTOR_FAILED;
    }

    completed.entry_index = matched_index;
    completed.handler_id = entries[matched_index].handler_id;
    completed.reserved = 0u;
    *state = planned_state;
    *summary = completed;
    return DB_GATEWAY_REGISTRY_OK;
}

const char *db_gateway_registry_status_name(db_gateway_registry_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "bad invocation request",
        "bad executor entry",
        "duplicate handler id",
        "handler not found",
        "executor failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown gateway registry status";
    }

    return names[(size_t)status];
}
