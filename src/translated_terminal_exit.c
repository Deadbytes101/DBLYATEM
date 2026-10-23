#include "dblyat/translated_terminal_exit.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool zero_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (bytes[index] != 0u) {
            return false;
        }
    }

    return true;
}

static bool x64_address_is_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return sign == 0u ? upper == 0u : upper == UINT64_C(0xffff);
}

static bool condition_valid(uint32_t condition)
{
    return condition <= UINT32_C(15);
}

static bool trap_valid(uint32_t trap)
{
    return trap >= UINT32_C(1) && trap <= UINT32_C(3);
}

static bool static_shape_valid(const db_translated_terminal_exit *exit_record)
{
    if (exit_record->format_version !=
            DB_TRANSLATED_TERMINAL_EXIT_FORMAT_VERSION ||
        exit_record->source_rip == 0u ||
        !x64_address_is_canonical(exit_record->source_rip) ||
        !zero_bytes(exit_record->reserved,
                    sizeof(exit_record->reserved))) {
        return false;
    }

    switch ((db_translated_terminal_exit_kind)exit_record->kind) {
    case DB_TRANSLATED_TERMINAL_EXIT_DIRECT:
        return x64_address_is_canonical(exit_record->direct_target) &&
               exit_record->fallthrough_target == 0u &&
               exit_record->trap == 0u &&
               exit_record->condition ==
                   DB_TRANSLATED_TERMINAL_EXIT_CONDITION_NONE;

    case DB_TRANSLATED_TERMINAL_EXIT_TARGET:
        return exit_record->direct_target == 0u &&
               exit_record->fallthrough_target == 0u &&
               exit_record->trap == 0u &&
               exit_record->condition ==
                   DB_TRANSLATED_TERMINAL_EXIT_CONDITION_NONE;

    case DB_TRANSLATED_TERMINAL_EXIT_CONDITIONAL:
        return x64_address_is_canonical(exit_record->direct_target) &&
               x64_address_is_canonical(exit_record->fallthrough_target) &&
               exit_record->trap == 0u &&
               condition_valid(exit_record->condition);

    case DB_TRANSLATED_TERMINAL_EXIT_TRAP:
        return exit_record->direct_target == 0u &&
               exit_record->fallthrough_target == 0u &&
               trap_valid(exit_record->trap) &&
               exit_record->condition ==
                   DB_TRANSLATED_TERMINAL_EXIT_CONDITION_NONE;

    case DB_TRANSLATED_TERMINAL_EXIT_NONE:
        break;
    }

    return false;
}

static db_translated_terminal_exit base_record(
    uint64_t source_rip,
    db_translated_terminal_exit_kind kind)
{
    db_translated_terminal_exit completed;

    memset(&completed, 0, sizeof(completed));
    completed.source_rip = source_rip;
    completed.kind = (uint32_t)kind;
    completed.condition = DB_TRANSLATED_TERMINAL_EXIT_CONDITION_NONE;
    completed.format_version = DB_TRANSLATED_TERMINAL_EXIT_FORMAT_VERSION;
    return completed;
}

db_translated_terminal_exit_status
 db_translated_terminal_exit_plan_direct(
    uint64_t source_rip,
    uint64_t direct_target,
    db_translated_terminal_exit *exit_record)
{
    db_translated_terminal_exit completed;

    if (exit_record == NULL) {
        return DB_TRANSLATED_TERMINAL_EXIT_NULL_ARGUMENT;
    }

    completed = base_record(source_rip,
                            DB_TRANSLATED_TERMINAL_EXIT_DIRECT);
    completed.direct_target = direct_target;

    if (!static_shape_valid(&completed)) {
        return DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD;
    }

    *exit_record = completed;
    return DB_TRANSLATED_TERMINAL_EXIT_OK;
}

db_translated_terminal_exit_status
 db_translated_terminal_exit_plan_target(
    uint64_t source_rip,
    db_translated_terminal_exit *exit_record)
{
    db_translated_terminal_exit completed;

    if (exit_record == NULL) {
        return DB_TRANSLATED_TERMINAL_EXIT_NULL_ARGUMENT;
    }

    completed = base_record(source_rip,
                            DB_TRANSLATED_TERMINAL_EXIT_TARGET);

    if (!static_shape_valid(&completed)) {
        return DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD;
    }

    *exit_record = completed;
    return DB_TRANSLATED_TERMINAL_EXIT_OK;
}

db_translated_terminal_exit_status
 db_translated_terminal_exit_plan_conditional(
    uint64_t source_rip,
    uint64_t direct_target,
    uint64_t fallthrough_target,
    uint32_t condition,
    db_translated_terminal_exit *exit_record)
{
    db_translated_terminal_exit completed;

    if (exit_record == NULL) {
        return DB_TRANSLATED_TERMINAL_EXIT_NULL_ARGUMENT;
    }

    completed = base_record(source_rip,
                            DB_TRANSLATED_TERMINAL_EXIT_CONDITIONAL);
    completed.direct_target = direct_target;
    completed.fallthrough_target = fallthrough_target;
    completed.condition = condition;

    if (!static_shape_valid(&completed)) {
        return DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD;
    }

    *exit_record = completed;
    return DB_TRANSLATED_TERMINAL_EXIT_OK;
}

db_translated_terminal_exit_status
 db_translated_terminal_exit_plan_trap(
    uint64_t source_rip,
    uint32_t trap,
    db_translated_terminal_exit *exit_record)
{
    db_translated_terminal_exit completed;

    if (exit_record == NULL) {
        return DB_TRANSLATED_TERMINAL_EXIT_NULL_ARGUMENT;
    }

    completed = base_record(source_rip,
                            DB_TRANSLATED_TERMINAL_EXIT_TRAP);
    completed.trap = trap;

    if (!static_shape_valid(&completed)) {
        return DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD;
    }

    *exit_record = completed;
    return DB_TRANSLATED_TERMINAL_EXIT_OK;
}

db_translated_terminal_exit_status
 db_translated_terminal_exit_validate_planned(
    const db_translated_terminal_exit *exit_record)
{
    if (exit_record == NULL) {
        return DB_TRANSLATED_TERMINAL_EXIT_NULL_ARGUMENT;
    }

    if (!static_shape_valid(exit_record) ||
        exit_record->runtime_target != 0u ||
        exit_record->runtime_value != 0u ||
        exit_record->runtime_target_valid != 0u ||
        exit_record->runtime_value_valid != 0u ||
        exit_record->condition_taken != 0u ||
        exit_record->condition_taken_valid != 0u) {
        return DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD;
    }

    return DB_TRANSLATED_TERMINAL_EXIT_OK;
}

db_translated_terminal_exit_status
 db_translated_terminal_exit_validate_observed(
    const db_translated_terminal_exit *exit_record)
{
    if (exit_record == NULL) {
        return DB_TRANSLATED_TERMINAL_EXIT_NULL_ARGUMENT;
    }

    if (!static_shape_valid(exit_record)) {
        return DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD;
    }

    switch ((db_translated_terminal_exit_kind)exit_record->kind) {
    case DB_TRANSLATED_TERMINAL_EXIT_DIRECT:
    case DB_TRANSLATED_TERMINAL_EXIT_TRAP:
        if (exit_record->runtime_target != 0u ||
            exit_record->runtime_value != 0u ||
            exit_record->runtime_target_valid != 0u ||
            exit_record->runtime_value_valid != 0u ||
            exit_record->condition_taken != 0u ||
            exit_record->condition_taken_valid != 0u) {
            return DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD;
        }
        return DB_TRANSLATED_TERMINAL_EXIT_OK;

    case DB_TRANSLATED_TERMINAL_EXIT_TARGET:
        if (exit_record->runtime_target_valid != 1u ||
            !x64_address_is_canonical(exit_record->runtime_target) ||
            exit_record->runtime_value != 0u ||
            exit_record->runtime_value_valid != 0u ||
            exit_record->condition_taken != 0u ||
            exit_record->condition_taken_valid != 0u) {
            return DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD;
        }
        return DB_TRANSLATED_TERMINAL_EXIT_OK;

    case DB_TRANSLATED_TERMINAL_EXIT_CONDITIONAL: {
        const uint64_t selected =
            exit_record->condition_taken != 0u
                ? exit_record->direct_target
                : exit_record->fallthrough_target;

        if (exit_record->runtime_target_valid != 1u ||
            exit_record->runtime_value_valid != 1u ||
            exit_record->condition_taken_valid != 1u ||
            exit_record->runtime_value > UINT64_C(1) ||
            exit_record->condition_taken > 1u ||
            exit_record->condition_taken !=
                (uint8_t)exit_record->runtime_value ||
            exit_record->runtime_target != selected ||
            !x64_address_is_canonical(exit_record->runtime_target)) {
            return DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD;
        }
        return DB_TRANSLATED_TERMINAL_EXIT_OK;
    }

    case DB_TRANSLATED_TERMINAL_EXIT_NONE:
        break;
    }

    return DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD;
}

db_translated_terminal_exit_status
 db_translated_terminal_exit_match(
    const db_translated_terminal_exit *planned,
    const db_translated_terminal_exit *observed)
{
    if (planned == NULL || observed == NULL) {
        return DB_TRANSLATED_TERMINAL_EXIT_NULL_ARGUMENT;
    }

    if (db_translated_terminal_exit_validate_planned(planned) !=
            DB_TRANSLATED_TERMINAL_EXIT_OK ||
        db_translated_terminal_exit_validate_observed(observed) !=
            DB_TRANSLATED_TERMINAL_EXIT_OK) {
        return DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD;
    }

    if (planned->source_rip != observed->source_rip ||
        planned->direct_target != observed->direct_target ||
        planned->fallthrough_target != observed->fallthrough_target ||
        planned->kind != observed->kind ||
        planned->trap != observed->trap ||
        planned->condition != observed->condition ||
        planned->format_version != observed->format_version) {
        return DB_TRANSLATED_TERMINAL_EXIT_MISMATCH;
    }

    return DB_TRANSLATED_TERMINAL_EXIT_OK;
}

const char *db_translated_terminal_exit_kind_name(
    db_translated_terminal_exit_kind kind)
{
    static const char *const names[] = {
        "none",
        "direct",
        "runtime target",
        "conditional",
        "trap"
    };

    if ((size_t)kind >= sizeof(names) / sizeof(names[0])) {
        return "unknown translated terminal exit kind";
    }

    return names[(size_t)kind];
}

const char *db_translated_terminal_exit_status_name(
    db_translated_terminal_exit_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "bad translated terminal exit record",
        "translated terminal exit mismatch"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown translated terminal exit status";
    }

    return names[(size_t)status];
}
