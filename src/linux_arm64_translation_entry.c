#include "dblyat/linux_arm64_translation_entry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__aarch64__)
extern void db_linux_arm64_translation_enter_asm(
    uint64_t entry_address,
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context);

extern void db_linux_arm64_translation_return_helper(void);
extern void db_linux_arm64_translation_target_helper(void);
extern void db_linux_arm64_translation_conditional_helper(void);
extern void db_linux_arm64_translation_trap_helper(void);
#endif

_Static_assert(sizeof(db_translated_terminal_exit) == 64u,
               "translated terminal exit ABI size changed");
_Static_assert(offsetof(db_translated_terminal_exit, runtime_target) == 24u,
               "translated terminal runtime target offset changed");
_Static_assert(offsetof(db_translated_terminal_exit, runtime_value) == 32u,
               "translated terminal runtime value offset changed");
_Static_assert(offsetof(db_translated_terminal_exit, kind) == 40u,
               "translated terminal kind offset changed");
_Static_assert(offsetof(db_translated_terminal_exit,
                        runtime_target_valid) == 56u,
               "translated terminal runtime target flag offset changed");
_Static_assert(offsetof(db_linux_arm64_translation_context,
                        terminal_return_count) == 0u,
               "translated entry return count offset changed");
_Static_assert(offsetof(db_linux_arm64_translation_context,
                        planned_exit) == 8u,
               "translated entry planned exit offset changed");
_Static_assert(offsetof(db_linux_arm64_translation_context,
                        observed_exit) == 72u,
               "translated entry observed exit offset changed");
_Static_assert(offsetof(db_linux_arm64_translation_context,
                        format_version) == 136u,
               "translated entry context version offset changed");
_Static_assert(sizeof(db_linux_arm64_translation_context) == 144u,
               "translated entry context ABI size changed");

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

static bool regions_overlap(const void *left,
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

static bool arm64_host_address_nonzero(uint64_t address)
{
    return address != 0u &&
           (address >> 48u) == UINT64_C(0);
}

static bool entry_address_valid(uint64_t entry_address)
{
    return arm64_host_address_nonzero(entry_address) &&
           (entry_address & UINT64_C(3)) == 0u;
}

static bool context_base_valid(
    const db_linux_arm64_translation_context *context)
{
    return context->format_version ==
               DB_LINUX_ARM64_TRANSLATION_ENTRY_FORMAT_VERSION &&
           context->terminal_return_count != UINT64_MAX &&
           zero_bytes(context->reserved, sizeof(context->reserved));
}

static bool context_valid(
    const db_linux_arm64_translation_context *context)
{
    if (!context_base_valid(context) ||
        db_translated_terminal_exit_validate_planned(
            &context->planned_exit) !=
            DB_TRANSLATED_TERMINAL_EXIT_OK) {
        return false;
    }

    if (zero_bytes(&context->observed_exit,
                   sizeof(context->observed_exit))) {
        return true;
    }

    return db_translated_terminal_exit_validate_observed(
               &context->observed_exit) ==
               DB_TRANSLATED_TERMINAL_EXIT_OK &&
           db_translated_terminal_exit_match(
               &context->planned_exit,
               &context->observed_exit) ==
               DB_TRANSLATED_TERMINAL_EXIT_OK;
}

#if defined(__aarch64__)
static uint64_t function_address(void (*function)(void))
{
    uintptr_t address = 0u;

    _Static_assert(sizeof(function) == sizeof(address),
                   "function pointer and uintptr_t sizes differ");
    (void)memcpy(&address, &function, sizeof(address));
    return (uint64_t)address;
}
#endif

db_linux_arm64_translation_entry_status
 db_linux_arm64_translation_context_init(
    db_linux_arm64_translation_context *context)
{
    db_linux_arm64_translation_context initialized;

    if (context == NULL) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_NULL_ARGUMENT;
    }

    (void)memset(&initialized, 0, sizeof(initialized));
    initialized.format_version =
        DB_LINUX_ARM64_TRANSLATION_ENTRY_FORMAT_VERSION;
    *context = initialized;
    return DB_LINUX_ARM64_TRANSLATION_ENTRY_OK;
}

db_linux_arm64_translation_entry_status
 db_linux_arm64_translation_context_bind_terminal(
    db_linux_arm64_translation_context *context,
    const db_translated_terminal_exit *planned_exit)
{
    db_linux_arm64_translation_context completed;

    if (context == NULL || planned_exit == NULL) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_NULL_ARGUMENT;
    }

    if (regions_overlap(context, sizeof(*context),
                        planned_exit, sizeof(*planned_exit))) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_BUFFER_OVERLAP;
    }

    if (!context_base_valid(context)) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_CONTEXT;
    }

    if (db_translated_terminal_exit_validate_planned(planned_exit) !=
        DB_TRANSLATED_TERMINAL_EXIT_OK) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_TERMINAL_EXIT;
    }

    completed = *context;
    completed.planned_exit = *planned_exit;
    (void)memset(&completed.observed_exit,
                 0,
                 sizeof(completed.observed_exit));
    *context = completed;
    return DB_LINUX_ARM64_TRANSLATION_ENTRY_OK;
}

db_linux_arm64_translation_entry_status
 db_linux_arm64_translation_enter(
    uint64_t entry_address,
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context,
    db_linux_arm64_translation_entry_audit *audit)
{
    if (state == NULL || context == NULL || audit == NULL) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_NULL_ARGUMENT;
    }

    if (regions_overlap(state, sizeof(*state),
                        context, sizeof(*context)) ||
        regions_overlap(state, sizeof(*state),
                        audit, sizeof(*audit)) ||
        regions_overlap(context, sizeof(*context),
                        audit, sizeof(*audit))) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_BUFFER_OVERLAP;
    }

    if (!entry_address_valid(entry_address)) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_ENTRY_ADDRESS;
    }

    if (!context_valid(context)) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_CONTEXT;
    }

#if !defined(__aarch64__)
    (void)entry_address;
    return DB_LINUX_ARM64_TRANSLATION_ENTRY_UNSUPPORTED_HOST;
#else
    db_x64_scalar_state planned_state = *state;
    db_linux_arm64_translation_context planned_context = *context;
    db_linux_arm64_translation_entry_audit completed;
    const uint64_t expected_return_count =
        planned_context.terminal_return_count + UINT64_C(1);
    const db_translated_terminal_exit_kind planned_kind =
        (db_translated_terminal_exit_kind)
            planned_context.planned_exit.kind;
    const uint64_t helper_address =
        db_linux_arm64_translation_terminal_helper_address(planned_kind);
    db_translated_terminal_exit_status exit_status;

    if (!arm64_host_address_nonzero(helper_address) ||
        (helper_address & UINT64_C(3)) != 0u) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_TERMINAL_EXIT;
    }

    (void)memset(&planned_context.observed_exit,
                 0,
                 sizeof(planned_context.observed_exit));
    (void)memset(&completed, 0, sizeof(completed));
    completed.entry_address = entry_address;
    completed.state_address = (uint64_t)(uintptr_t)state;
    completed.context_address = (uint64_t)(uintptr_t)context;
    completed.return_helper_address = helper_address;
    completed.terminal_return_count_before =
        context->terminal_return_count;
    completed.planned_kind = context->planned_exit.kind;
    completed.version =
        DB_LINUX_ARM64_TRANSLATION_ENTRY_AUDIT_VERSION;
    completed.status = DB_LINUX_ARM64_TRANSLATION_ENTRY_OK;
    completed.entry_invoked = 1u;

    db_linux_arm64_translation_enter_asm(
        entry_address,
        &planned_state,
        &planned_context);

    completed.terminal_return_count_after =
        planned_context.terminal_return_count;

    if (planned_context.terminal_return_count !=
        expected_return_count) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_RETURN_NOT_OBSERVED;
    }

    completed.terminal_return_observed = 1u;
    completed.observed_kind = planned_context.observed_exit.kind;

    if (planned_context.observed_exit.kind !=
        planned_context.planned_exit.kind) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_TERMINAL_EXIT_MISMATCH;
    }

    exit_status = db_translated_terminal_exit_validate_observed(
        &planned_context.observed_exit);
    if (exit_status != DB_TRANSLATED_TERMINAL_EXIT_OK) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_TERMINAL_EXIT;
    }

    exit_status = db_translated_terminal_exit_match(
        &planned_context.planned_exit,
        &planned_context.observed_exit);
    if (exit_status != DB_TRANSLATED_TERMINAL_EXIT_OK) {
        return DB_LINUX_ARM64_TRANSLATION_ENTRY_TERMINAL_EXIT_MISMATCH;
    }

    completed.terminal_exit_matched = 1u;
    *state = planned_state;
    *context = planned_context;
    *audit = completed;
    return DB_LINUX_ARM64_TRANSLATION_ENTRY_OK;
#endif
}

uint64_t db_linux_arm64_translation_terminal_helper_address(
    db_translated_terminal_exit_kind kind)
{
#if !defined(__aarch64__)
    (void)kind;
    return UINT64_C(0);
#else
    switch (kind) {
    case DB_TRANSLATED_TERMINAL_EXIT_DIRECT:
        return function_address(
            db_linux_arm64_translation_return_helper);
    case DB_TRANSLATED_TERMINAL_EXIT_TARGET:
        return function_address(
            db_linux_arm64_translation_target_helper);
    case DB_TRANSLATED_TERMINAL_EXIT_CONDITIONAL:
        return function_address(
            db_linux_arm64_translation_conditional_helper);
    case DB_TRANSLATED_TERMINAL_EXIT_TRAP:
        return function_address(
            db_linux_arm64_translation_trap_helper);
    case DB_TRANSLATED_TERMINAL_EXIT_NONE:
        break;
    }

    return UINT64_C(0);
#endif
}

uint64_t db_linux_arm64_translation_return_helper_address(void)
{
    return db_linux_arm64_translation_terminal_helper_address(
        DB_TRANSLATED_TERMINAL_EXIT_DIRECT);
}

const char *db_linux_arm64_translation_entry_status_name(
    db_linux_arm64_translation_entry_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad entry address",
        "bad context",
        "unsupported host",
        "terminal return not observed",
        "bad translated terminal exit",
        "translated terminal exit mismatch"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown ARM64 translated entry status";
    }

    return names[(size_t)status];
}
