#include "dblyat/win32_translated_gateway_bridge.h"

#include "dblyat/ir.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DB_BRIDGE_RAX 0u
#define DB_BRIDGE_RCX 1u
#define DB_BRIDGE_RDX 2u
#define DB_BRIDGE_RBX 3u
#define DB_BRIDGE_RSP 4u
#define DB_BRIDGE_RBP 5u
#define DB_BRIDGE_RSI 6u
#define DB_BRIDGE_RDI 7u
#define DB_BRIDGE_R8 8u
#define DB_BRIDGE_R9 9u
#define DB_BRIDGE_R10 10u
#define DB_BRIDGE_R11 11u
#define DB_BRIDGE_R12 12u
#define DB_BRIDGE_R13 13u
#define DB_BRIDGE_R14 14u
#define DB_BRIDGE_R15 15u

static bool db_bridge_mul_size(size_t left,
                               size_t right,
                               size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_bridge_overlap(const void *left,
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

static bool db_bridge_canonical(uint64_t value)
{
    const uint64_t upper = value >> 48u;
    const uint64_t sign = (value >> 47u) & UINT64_C(1);

    return value != 0u &&
           ((sign == 0u && upper == 0u) ||
            (sign != 0u && upper == UINT64_C(0xffff)));
}

static bool db_bridge_scalar_valid(
    const db_x64_scalar_state *state,
    uint64_t expected_rip)
{
    return db_bridge_canonical(state->rip) &&
           state->rip == expected_rip &&
           (state->rflags & DB_X64_RFLAGS_FIXED_ONE) != 0u;
}

static void db_bridge_scalar_to_gateway(
    const db_x64_scalar_state *scalar,
    db_gateway_x64_guest_state *gateway)
{
    gateway->rax = scalar->registers[DB_BRIDGE_RAX];
    gateway->rbx = scalar->registers[DB_BRIDGE_RBX];
    gateway->rcx = scalar->registers[DB_BRIDGE_RCX];
    gateway->rdx = scalar->registers[DB_BRIDGE_RDX];
    gateway->rsi = scalar->registers[DB_BRIDGE_RSI];
    gateway->rdi = scalar->registers[DB_BRIDGE_RDI];
    gateway->rbp = scalar->registers[DB_BRIDGE_RBP];
    gateway->rsp = scalar->registers[DB_BRIDGE_RSP];
    gateway->r8 = scalar->registers[DB_BRIDGE_R8];
    gateway->r9 = scalar->registers[DB_BRIDGE_R9];
    gateway->r10 = scalar->registers[DB_BRIDGE_R10];
    gateway->r11 = scalar->registers[DB_BRIDGE_R11];
    gateway->r12 = scalar->registers[DB_BRIDGE_R12];
    gateway->r13 = scalar->registers[DB_BRIDGE_R13];
    gateway->r14 = scalar->registers[DB_BRIDGE_R14];
    gateway->r15 = scalar->registers[DB_BRIDGE_R15];
    gateway->rip = scalar->rip;
}

static void db_bridge_gateway_to_scalar(
    const db_gateway_x64_guest_state *gateway,
    db_x64_scalar_state *scalar)
{
    scalar->registers[DB_BRIDGE_RAX] = gateway->rax;
    scalar->registers[DB_BRIDGE_RBX] = gateway->rbx;
    scalar->registers[DB_BRIDGE_RCX] = gateway->rcx;
    scalar->registers[DB_BRIDGE_RDX] = gateway->rdx;
    scalar->registers[DB_BRIDGE_RSI] = gateway->rsi;
    scalar->registers[DB_BRIDGE_RDI] = gateway->rdi;
    scalar->registers[DB_BRIDGE_RBP] = gateway->rbp;
    scalar->registers[DB_BRIDGE_RSP] = gateway->rsp;
    scalar->registers[DB_BRIDGE_R8] = gateway->r8;
    scalar->registers[DB_BRIDGE_R9] = gateway->r9;
    scalar->registers[DB_BRIDGE_R10] = gateway->r10;
    scalar->registers[DB_BRIDGE_R11] = gateway->r11;
    scalar->registers[DB_BRIDGE_R12] = gateway->r12;
    scalar->registers[DB_BRIDGE_R13] = gateway->r13;
    scalar->registers[DB_BRIDGE_R14] = gateway->r14;
    scalar->registers[DB_BRIDGE_R15] = gateway->r15;
    scalar->rip = gateway->rip;
}

static bool db_bridge_output_aliases(
    const db_translated_terminal_exit *terminal_exit,
    const void *gateway_buffer,
    size_t gateway_buffer_size,
    const db_iat_target *targets,
    size_t target_bytes,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_bytes,
    const db_gateway_frame_signature *signatures,
    size_t signature_bytes,
    const void *guest_stack,
    size_t guest_stack_size,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_gateway_bridge_summary *summary)
{
#define DB_BRIDGE_OUTPUT_INPUT(out, out_size, in, in_size) \
    db_bridge_overlap((out), (out_size), (in), (in_size))

    if (db_bridge_overlap(kernel32_state, sizeof(*kernel32_state),
                          scalar_state, sizeof(*scalar_state)) ||
        db_bridge_overlap(kernel32_state, sizeof(*kernel32_state),
                          summary, sizeof(*summary)) ||
        db_bridge_overlap(scalar_state, sizeof(*scalar_state),
                          summary, sizeof(*summary))) {
        return true;
    }

#define DB_BRIDGE_CHECK_OUTPUT(out, out_size) \
    do { \
        if (DB_BRIDGE_OUTPUT_INPUT((out), (out_size), \
                                   terminal_exit, sizeof(*terminal_exit)) || \
            DB_BRIDGE_OUTPUT_INPUT((out), (out_size), \
                                   gateway_buffer, gateway_buffer_size) || \
            DB_BRIDGE_OUTPUT_INPUT((out), (out_size), \
                                   targets, target_bytes) || \
            DB_BRIDGE_OUTPUT_INPUT((out), (out_size), \
                                   handlers, handler_bytes) || \
            DB_BRIDGE_OUTPUT_INPUT((out), (out_size), \
                                   signatures, signature_bytes) || \
            DB_BRIDGE_OUTPUT_INPUT((out), (out_size), \
                                   guest_stack, guest_stack_size)) { \
            return true; \
        } \
    } while (0)

    DB_BRIDGE_CHECK_OUTPUT(kernel32_state, sizeof(*kernel32_state));
    DB_BRIDGE_CHECK_OUTPUT(scalar_state, sizeof(*scalar_state));
    DB_BRIDGE_CHECK_OUTPUT(summary, sizeof(*summary));

#undef DB_BRIDGE_CHECK_OUTPUT
#undef DB_BRIDGE_OUTPUT_INPUT
    return false;
}

db_win32_translated_gateway_bridge_status
 db_win32_translated_gateway_bridge_run(
    const db_translated_terminal_exit *terminal_exit,
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
    db_win32_translated_gateway_bridge_summary *summary)
{
    db_win32_translated_gateway_bridge_summary completed = {0};
    db_gateway_lookup_result lookup;
    db_iat_target authenticated_target;
    db_gateway_x64_guest_state gateway_state;
    db_gateway_x64_guest_state planned_gateway;
    db_win32_kernel32_state planned_kernel32;
    db_x64_scalar_state planned_scalar;
    uint64_t arguments[DB_GATEWAY_FRAME_MAX_ARGUMENTS] = {0};
    db_gateway_lookup_status lookup_status;
    db_gateway_decode_status decode_status;
    db_gateway_enter_status enter_status;
    db_gateway_invoke_status invoke_status;
    db_win32_runtime_dispatch_status dispatch_status;
    size_t target_bytes;
    size_t handler_bytes;
    size_t signature_bytes;
    size_t slot_size_host;
    size_t gateway_required;
    size_t slot_offset;
    const uint8_t *slot;
    const db_iat_target *expected_target;

    if (terminal_exit == NULL || kernel32_state == NULL ||
        scalar_state == NULL || summary == NULL ||
        (gateway_buffer_size != 0u && gateway_buffer == NULL) ||
        (target_count != 0u && targets == NULL) ||
        (handler_count != 0u && handlers == NULL) ||
        (signature_count != 0u && signatures == NULL) ||
        (guest_stack_size != 0u && guest_stack == NULL)) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_NULL_ARGUMENT;
    }

    if (!db_bridge_mul_size(target_count, sizeof(*targets),
                            &target_bytes) ||
        !db_bridge_mul_size(handler_count, sizeof(*handlers),
                            &handler_bytes) ||
        !db_bridge_mul_size(signature_count, sizeof(*signatures),
                            &signature_bytes)) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_COUNT_OVERFLOW;
    }

    if (db_bridge_output_aliases(terminal_exit,
                                 gateway_buffer,
                                 gateway_buffer_size,
                                 targets,
                                 target_bytes,
                                 handlers,
                                 handler_bytes,
                                 signatures,
                                 signature_bytes,
                                 guest_stack,
                                 guest_stack_size,
                                 kernel32_state,
                                 scalar_state,
                                 summary)) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_BUFFER_OVERLAP;
    }

    if (db_translated_terminal_exit_validate_observed(terminal_exit) !=
        DB_TRANSLATED_TERMINAL_EXIT_OK) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_BAD_TERMINAL_EXIT;
    }

    if (terminal_exit->kind !=
            (uint32_t)DB_TRANSLATED_TERMINAL_EXIT_TRAP ||
        terminal_exit->trap != (uint32_t)DB_IR_TRAP_INVALID_OPCODE) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_NOT_GATEWAY;
    }

    if (!db_bridge_scalar_valid(scalar_state, terminal_exit->source_rip)) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_BAD_SCALAR_STATE;
    }

    lookup_status = db_gateway_lookup_rip(targets,
                                          target_count,
                                          gateway_base,
                                          slot_size,
                                          terminal_exit->source_rip,
                                          &lookup);
    if (lookup_status != DB_GATEWAY_LOOKUP_OK) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_LOOKUP_FAILED;
    }

    if (lookup.kind != DB_GATEWAY_LOOKUP_ENTRY) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_NOT_GATEWAY;
    }

    if (slot_size > (uint64_t)SIZE_MAX) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_COUNT_OVERFLOW;
    }
    slot_size_host = (size_t)slot_size;

    if (!db_bridge_mul_size(target_count,
                            slot_size_host,
                            &gateway_required) ||
        !db_bridge_mul_size(lookup.slot_index,
                            slot_size_host,
                            &slot_offset)) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_COUNT_OVERFLOW;
    }

    if (gateway_buffer == NULL ||
        gateway_buffer_size < gateway_required ||
        slot_offset > gateway_buffer_size ||
        slot_size_host > gateway_buffer_size - slot_offset) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_GATEWAY_BUFFER_TOO_SMALL;
    }

    slot = (const uint8_t *)gateway_buffer + slot_offset;
    decode_status = db_gateway_decode_slots(slot,
                                            slot_size_host,
                                            1u,
                                            lookup.slot_address,
                                            slot_size,
                                            &authenticated_target,
                                            1u,
                                            &completed.decode);
    if (decode_status != DB_GATEWAY_DECODE_OK) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_AUTHENTICATION_FAILED;
    }

    if (lookup.slot_index >= target_count) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_IDENTITY_MISMATCH;
    }
    expected_target = &targets[lookup.slot_index];

    if (authenticated_target.provider_id != expected_target->provider_id ||
        authenticated_target.symbol_id != expected_target->symbol_id ||
        authenticated_target.guest_value != expected_target->guest_value ||
        authenticated_target.provider_id != lookup.provider_id ||
        authenticated_target.symbol_id != lookup.symbol_id ||
        authenticated_target.guest_value != lookup.slot_address) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_IDENTITY_MISMATCH;
    }

    db_bridge_scalar_to_gateway(scalar_state, &gateway_state);

    enter_status = db_gateway_enter_plan(targets,
                                         target_count,
                                         gateway_base,
                                         slot_size,
                                         handlers,
                                         handler_count,
                                         signatures,
                                         signature_count,
                                         &gateway_state,
                                         guest_stack,
                                         guest_stack_size,
                                         guest_stack_base,
                                         arguments,
                                         DB_GATEWAY_FRAME_MAX_ARGUMENTS,
                                         &completed.entry);
    if (enter_status != DB_GATEWAY_ENTER_OK) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_ENTRY_FAILED;
    }

    invoke_status = db_gateway_invoke_plan(&completed.entry,
                                           arguments,
                                           completed.entry.frame.argument_count,
                                           &completed.request);
    if (invoke_status != DB_GATEWAY_INVOKE_OK) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_INVOKE_FAILED;
    }

    planned_kernel32 = *kernel32_state;
    planned_gateway = gateway_state;
    dispatch_status = db_win32_runtime_dispatch_run(&completed.request,
                                                    &planned_kernel32,
                                                    &planned_gateway,
                                                    &completed.dispatch);
    if (dispatch_status != DB_WIN32_RUNTIME_DISPATCH_OK) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_RUNTIME_DISPATCH_FAILED;
    }

    if (!db_bridge_canonical(planned_gateway.rip) ||
        !db_bridge_canonical(planned_gateway.rsp)) {
        return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_RUNTIME_DISPATCH_FAILED;
    }

    planned_scalar = *scalar_state;
    db_bridge_gateway_to_scalar(&planned_gateway, &planned_scalar);

    completed.authenticated_target = authenticated_target;
    completed.slot_index = lookup.slot_index;
    completed.format_version =
        DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_FORMAT_VERSION;
    completed.gateway_consumed = 1u;
    completed.reserved[0] = 0u;
    completed.reserved[1] = 0u;
    completed.reserved[2] = 0u;

    *kernel32_state = planned_kernel32;
    *scalar_state = planned_scalar;
    *summary = completed;
    return DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_OK;
}

const char *db_win32_translated_gateway_bridge_status_name(
    db_win32_translated_gateway_bridge_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflows",
        "buffer overlap",
        "bad translated terminal exit",
        "bad translated scalar state",
        "gateway lookup failed",
        "translated invalid opcode is not a gateway",
        "gateway buffer is too small",
        "gateway slot authentication failed",
        "gateway slot identity mismatch",
        "gateway entry planning failed",
        "gateway invocation planning failed",
        "Win32 runtime dispatch failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown translated Win32 gateway bridge status";
    }

    return names[(size_t)status];
}
