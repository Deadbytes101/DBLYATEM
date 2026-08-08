#ifndef DBLYAT_LINUX_ARM64_GUEST_RUNTIME_INTERNAL_H
#define DBLYAT_LINUX_ARM64_GUEST_RUNTIME_INTERNAL_H

#include "dblyat/linux_arm64_translation_entry.h"

#include <stdint.h>

uint64_t db_linux_arm64_helper_form_guest_address_core(
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context,
    uint64_t value,
    uint64_t displacement_bits,
    uint64_t address_mode,
    uint64_t base_guest_register,
    uint64_t index_guest_register,
    uint64_t scale);

uint64_t db_linux_arm64_helper_read_guest_u64_core(
    db_linux_arm64_translation_context *context,
    uint64_t guest_address);

void db_linux_arm64_helper_write_guest_u64_core(
    db_linux_arm64_translation_context *context,
    uint64_t guest_address,
    uint64_t value);

void db_linux_arm64_helper_push_guest_u64_core(
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context,
    uint64_t value);

uint64_t db_linux_arm64_helper_pop_guest_u64_core(
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context);

void db_linux_arm64_helper_update_logic_flags_core(
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context,
    uint64_t width,
    uint64_t guest_register);

uint64_t db_linux_arm64_helper_test_guest_condition_core(
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context,
    uint64_t condition);

#endif
