#include "dblyat/win32_translated_execution.h"

#include "dblyat/ir.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_execution_zero_bytes(const void *data, size_t size)
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

static bool db_execution_overlap(const void *left,
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

static bool db_execution_mul_size(size_t left,
                                  size_t right,
                                  size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_execution_add_u64(uint64_t left,
                                 uint64_t right,
                                 uint64_t *result)
{
    if (UINT64_MAX - left < right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool db_execution_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return address != 0u &&
           (sign == 0u ? upper == 0u : upper == UINT64_C(0xffff));
}

static bool db_execution_scalar_base_valid(
    const db_x64_scalar_state *state)
{
    return db_execution_canonical(state->rip) &&
           (state->rflags & DB_X64_RFLAGS_FIXED_ONE) != 0u;
}

static bool db_execution_scalar_at_rip_valid(
    const db_x64_scalar_state *state,
    uint64_t expected_rip)
{
    return db_execution_scalar_base_valid(state) &&
           state->rip == expected_rip;
}

static bool db_execution_published_valid(
    const db_arm64_published_block *published)
{
    uint64_t expected_guest_end;
    uint64_t expected_host_end;
    uint64_t expected_image_bytes;

    if (published->format_version !=
            DB_ARM64_PUBLISHED_BLOCK_FORMAT_VERSION ||
        published->guest_source_rip == 0u ||
        published->guest_byte_count == 0u ||
        published->guest_instruction_count == 0u ||
        published->arm64_word_count == 0u ||
        published->arm64_image_byte_count == 0u ||
        published->source_generation == 0u ||
        published->translation_key == 0u ||
        published->host_mapping_address == 0u ||
        published->host_entry_address == 0u ||
        published->host_allocation_end == 0u ||
        published->host_allocation_size == 0u ||
        published->block_stop_reason !=
            (uint32_t)DB_X64_BLOCK_STOP_TERMINATOR ||
        !db_execution_canonical(published->guest_source_rip) ||
        !db_execution_zero_bytes(published->reserved,
                                 sizeof(published->reserved)) ||
        !db_execution_add_u64(published->guest_source_rip,
                              (uint64_t)published->guest_byte_count,
                              &expected_guest_end) ||
        expected_guest_end != published->guest_end_rip ||
        !db_execution_canonical(published->guest_end_rip) ||
        (published->guest_fallthrough_rip != 0u &&
         !db_execution_canonical(published->guest_fallthrough_rip))) {
        return false;
    }

    expected_image_bytes =
        (uint64_t)published->arm64_word_count * UINT64_C(4);
    if (expected_image_bytes != published->arm64_image_byte_count ||
        published->arm64_image_byte_count >
            published->host_allocation_size ||
        !db_execution_add_u64(published->host_mapping_address,
                              published->host_allocation_size - UINT64_C(1),
                              &expected_host_end) ||
        expected_host_end != published->host_allocation_end ||
        published->host_entry_address < published->host_mapping_address ||
        published->host_entry_address > published->host_allocation_end ||
        (published->host_entry_address & UINT64_C(3)) != 0u ||
        db_translated_terminal_exit_validate_planned(
            &published->terminal_exit) !=
            DB_TRANSLATED_TERMINAL_EXIT_OK ||
        published->terminal_exit.source_rip <
            published->guest_source_rip ||
        published->terminal_exit.source_rip >= published->guest_end_rip) {
        return false;
    }

    return true;
}

static db_win32_translated_execution_status db_execution_validate_aliases(
    const void *primary_input,
    size_t primary_input_size,
    const void *gateway_buffer,
    size_t gateway_buffer_size,
    const db_iat_target *targets,
    size_t target_count,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    const void *guest_stack,
    size_t guest_stack_size,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_execution_summary *summary)
{
    size_t target_bytes;
    size_t handler_bytes;
    size_t signature_bytes;

    if (!db_execution_mul_size(target_count, sizeof(*targets),
                               &target_bytes) ||
        !db_execution_mul_size(handler_count, sizeof(*handlers),
                               &handler_bytes) ||
        !db_execution_mul_size(signature_count, sizeof(*signatures),
                               &signature_bytes)) {
        return DB_WIN32_TRANSLATED_EXECUTION_COUNT_OVERFLOW;
    }

#define DB_EXECUTION_OUTPUT_INPUT(out, out_size, in, in_size) \
    db_execution_overlap((out), (out_size), (in), (in_size))
#define DB_EXECUTION_CHECK_OUTPUT(out, out_size) \
    do { \
        if (DB_EXECUTION_OUTPUT_INPUT((out), (out_size), \
                                      primary_input, primary_input_size) || \
            DB_EXECUTION_OUTPUT_INPUT((out), (out_size), \
                                      gateway_buffer, gateway_buffer_size) || \
            DB_EXECUTION_OUTPUT_INPUT((out), (out_size), \
                                      targets, target_bytes) || \
            DB_EXECUTION_OUTPUT_INPUT((out), (out_size), \
                                      handlers, handler_bytes) || \
            DB_EXECUTION_OUTPUT_INPUT((out), (out_size), \
                                      signatures, signature_bytes) || \
            DB_EXECUTION_OUTPUT_INPUT((out), (out_size), \
                                      guest_stack, guest_stack_size)) { \
            return DB_WIN32_TRANSLATED_EXECUTION_BUFFER_OVERLAP; \
        } \
    } while (0)

    if (db_execution_overlap(kernel32_state, sizeof(*kernel32_state),
                             scalar_state, sizeof(*scalar_state)) ||
        db_execution_overlap(kernel32_state, sizeof(*kernel32_state),
                             summary, sizeof(*summary)) ||
        db_execution_overlap(scalar_state, sizeof(*scalar_state),
                             summary, sizeof(*summary))) {
        return DB_WIN32_TRANSLATED_EXECUTION_BUFFER_OVERLAP;
    }

    DB_EXECUTION_CHECK_OUTPUT(kernel32_state, sizeof(*kernel32_state));
    DB_EXECUTION_CHECK_OUTPUT(scalar_state, sizeof(*scalar_state));
    DB_EXECUTION_CHECK_OUTPUT(summary, sizeof(*summary));

#undef DB_EXECUTION_CHECK_OUTPUT
#undef DB_EXECUTION_OUTPUT_INPUT
    return DB_WIN32_TRANSLATED_EXECUTION_OK;
}

static void db_execution_finish_common(
    const db_translated_terminal_exit *observed_exit,
    db_win32_translated_execution_summary *summary)
{
    summary->observed_exit = *observed_exit;
    summary->format_version = DB_WIN32_TRANSLATED_EXECUTION_FORMAT_VERSION;
    summary->reserved[0] = 0u;
    summary->reserved[1] = 0u;
    summary->reserved[2] = 0u;
    summary->reserved[3] = 0u;
    summary->reserved[4] = 0u;
}

db_win32_translated_execution_status
 db_win32_translated_execution_resolve_exit(
    const db_translated_terminal_exit *observed_exit,
    const void *gateway_buffer,
    size_t gateway_buffer_size,
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t guest_stack_base,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_execution_summary *summary)
{
    db_win32_translated_execution_summary completed = {0};
    db_win32_kernel32_state planned_kernel32;
    db_x64_scalar_state planned_scalar;
    db_win32_translated_gateway_bridge_status bridge_status;
    db_win32_translated_execution_status alias_status;
    uint64_t next_rip;

    if (observed_exit == NULL || kernel32_state == NULL ||
        scalar_state == NULL || summary == NULL ||
        (gateway_buffer_size != 0u && gateway_buffer == NULL) ||
        (target_count != 0u && targets == NULL) ||
        (handler_count != 0u && handlers == NULL) ||
        (signature_count != 0u && signatures == NULL) ||
        (guest_stack_size != 0u && guest_stack == NULL)) {
        return DB_WIN32_TRANSLATED_EXECUTION_NULL_ARGUMENT;
    }

    alias_status = db_execution_validate_aliases(
        observed_exit,
        sizeof(*observed_exit),
        gateway_buffer,
        gateway_buffer_size,
        targets,
        target_count,
        handlers,
        handler_count,
        signatures,
        signature_count,
        guest_stack,
        guest_stack_size,
        kernel32_state,
        scalar_state,
        summary);
    if (alias_status != DB_WIN32_TRANSLATED_EXECUTION_OK) {
        return alias_status;
    }

    if (db_translated_terminal_exit_validate_observed(observed_exit) !=
        DB_TRANSLATED_TERMINAL_EXIT_OK) {
        return DB_WIN32_TRANSLATED_EXECUTION_BAD_TERMINAL_EXIT;
    }

    if (!db_execution_scalar_base_valid(scalar_state)) {
        return DB_WIN32_TRANSLATED_EXECUTION_BAD_SCALAR_STATE;
    }

    planned_kernel32 = *kernel32_state;
    planned_scalar = *scalar_state;
    planned_scalar.rip = observed_exit->source_rip;
    db_execution_finish_common(observed_exit, &completed);

    switch ((db_translated_terminal_exit_kind)observed_exit->kind) {
    case DB_TRANSLATED_TERMINAL_EXIT_DIRECT:
        next_rip = observed_exit->direct_target;
        planned_scalar.rip = next_rip;
        completed.next_rip = next_rip;
        completed.outcome = DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE;
        break;

    case DB_TRANSLATED_TERMINAL_EXIT_TARGET:
    case DB_TRANSLATED_TERMINAL_EXIT_CONDITIONAL:
        next_rip = observed_exit->runtime_target;
        planned_scalar.rip = next_rip;
        completed.next_rip = next_rip;
        completed.outcome = DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE;
        break;

    case DB_TRANSLATED_TERMINAL_EXIT_TRAP:
        if (observed_exit->trap != (uint32_t)DB_IR_TRAP_INVALID_OPCODE) {
            completed.next_rip = observed_exit->source_rip;
            completed.outcome =
                DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP;
            completed.trap = observed_exit->trap;
            break;
        }

        completed.gateway_attempted = 1u;
        bridge_status = db_win32_translated_gateway_bridge_run(
            observed_exit,
            gateway_buffer,
            gateway_buffer_size,
            targets,
            target_count,
            gateway_base,
            slot_size,
            handlers,
            handler_count,
            signatures,
            signature_count,
            guest_stack,
            guest_stack_size,
            guest_stack_base,
            &planned_kernel32,
            &planned_scalar,
            &completed.gateway);

        if (bridge_status ==
            DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_NOT_GATEWAY) {
            completed.next_rip = observed_exit->source_rip;
            completed.outcome =
                DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP;
            completed.trap = observed_exit->trap;
            break;
        }

        if (bridge_status != DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_OK) {
            return DB_WIN32_TRANSLATED_EXECUTION_GATEWAY_FAILED;
        }

        if (completed.gateway.gateway_consumed != 1u) {
            return DB_WIN32_TRANSLATED_EXECUTION_BAD_GATEWAY_OUTCOME;
        }

        completed.gateway_consumed = 1u;
        if (completed.gateway.dispatch.outcome ==
            (uint32_t)DB_WIN32_RUNTIME_OUTCOME_CONTINUE) {
            if (!db_execution_canonical(planned_scalar.rip)) {
                return DB_WIN32_TRANSLATED_EXECUTION_BAD_GATEWAY_OUTCOME;
            }
            completed.next_rip = planned_scalar.rip;
            completed.outcome =
                DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE;
        } else if (completed.gateway.dispatch.outcome ==
                   (uint32_t)DB_WIN32_RUNTIME_OUTCOME_GUEST_TERMINATED) {
            completed.next_rip = 0u;
            completed.outcome =
                DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED;
            completed.exit_code = completed.gateway.dispatch.exit_code;
        } else {
            return DB_WIN32_TRANSLATED_EXECUTION_BAD_GATEWAY_OUTCOME;
        }
        break;

    case DB_TRANSLATED_TERMINAL_EXIT_NONE:
    default:
        return DB_WIN32_TRANSLATED_EXECUTION_BAD_TERMINAL_EXIT;
    }

    *kernel32_state = planned_kernel32;
    *scalar_state = planned_scalar;
    *summary = completed;
    return DB_WIN32_TRANSLATED_EXECUTION_OK;
}

db_win32_translated_execution_status
 db_win32_translated_execution_step(
    const db_arm64_published_block *published,
    const void *gateway_buffer,
    size_t gateway_buffer_size,
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t guest_stack_base,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_execution_summary *summary)
{
    db_linux_arm64_translation_context context;
    db_linux_arm64_translation_entry_audit entry_audit;
    db_win32_translated_execution_summary completed;
    db_win32_kernel32_state planned_kernel32;
    db_x64_scalar_state planned_scalar;
    db_linux_arm64_translation_entry_status entry_status;
    db_win32_translated_execution_status status;

    if (published == NULL || kernel32_state == NULL ||
        scalar_state == NULL || summary == NULL ||
        (gateway_buffer_size != 0u && gateway_buffer == NULL) ||
        (target_count != 0u && targets == NULL) ||
        (handler_count != 0u && handlers == NULL) ||
        (signature_count != 0u && signatures == NULL) ||
        (guest_stack_size != 0u && guest_stack == NULL)) {
        return DB_WIN32_TRANSLATED_EXECUTION_NULL_ARGUMENT;
    }

    status = db_execution_validate_aliases(
        published,
        sizeof(*published),
        gateway_buffer,
        gateway_buffer_size,
        targets,
        target_count,
        handlers,
        handler_count,
        signatures,
        signature_count,
        guest_stack,
        guest_stack_size,
        kernel32_state,
        scalar_state,
        summary);
    if (status != DB_WIN32_TRANSLATED_EXECUTION_OK) {
        return status;
    }

    if (!db_execution_published_valid(published)) {
        return DB_WIN32_TRANSLATED_EXECUTION_BAD_PUBLISHED_BLOCK;
    }

    if (!db_execution_scalar_at_rip_valid(
            scalar_state, published->guest_source_rip)) {
        return DB_WIN32_TRANSLATED_EXECUTION_BAD_SCALAR_STATE;
    }

    planned_kernel32 = *kernel32_state;
    planned_scalar = *scalar_state;

    if (db_linux_arm64_translation_context_init(&context) !=
            DB_LINUX_ARM64_TRANSLATION_ENTRY_OK ||
        db_linux_arm64_translation_context_bind_terminal(
            &context, &published->terminal_exit) !=
            DB_LINUX_ARM64_TRANSLATION_ENTRY_OK) {
        return DB_WIN32_TRANSLATED_EXECUTION_BAD_PUBLISHED_BLOCK;
    }

    entry_status = db_linux_arm64_translation_enter(
        published->host_entry_address,
        &planned_scalar,
        &context,
        &entry_audit);
    if (entry_status != DB_LINUX_ARM64_TRANSLATION_ENTRY_OK) {
        return DB_WIN32_TRANSLATED_EXECUTION_ENTRY_FAILED;
    }

    status = db_win32_translated_execution_resolve_exit(
        &context.observed_exit,
        gateway_buffer,
        gateway_buffer_size,
        targets,
        target_count,
        gateway_base,
        slot_size,
        handlers,
        handler_count,
        signatures,
        signature_count,
        guest_stack,
        guest_stack_size,
        guest_stack_base,
        &planned_kernel32,
        &planned_scalar,
        &completed);
    if (status != DB_WIN32_TRANSLATED_EXECUTION_OK) {
        return status;
    }

    completed.entry_audit = entry_audit;
    completed.entry_executed = 1u;

    *kernel32_state = planned_kernel32;
    *scalar_state = planned_scalar;
    *summary = completed;
    return DB_WIN32_TRANSLATED_EXECUTION_OK;
}

const char *db_win32_translated_execution_status_name(
    db_win32_translated_execution_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "bad published ARM64 block",
        "bad translated scalar state",
        "bad observed terminal exit",
        "translated ARM64 entry failed",
        "translated Win32 gateway bridge failed",
        "bad translated Win32 gateway outcome"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown translated Win32 execution status";
    }

    return names[(size_t)status];
}

const char *db_win32_translated_execution_outcome_name(
    db_win32_translated_execution_outcome outcome)
{
    static const char *const names[] = {
        "none",
        "continue",
        "guest trap",
        "guest terminated"
    };

    if ((size_t)outcome >= sizeof(names) / sizeof(names[0])) {
        return "unknown translated Win32 execution outcome";
    }

    return names[(size_t)outcome];
}
