#include "x64_arm64_relocate_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DB_ARM64_BL_ZERO UINT32_C(0x94000000)
#define DB_ARM64_B_ZERO UINT32_C(0x14000000)

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

static bool x64_canonical_nonzero(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return address != 0u &&
           upper == (sign != 0u ? UINT64_C(0xffff) : UINT64_C(0));
}

bool db_x64_arm64_relocate_canonical_internal(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return address != 0u &&
           (upper == UINT64_C(0) ||
            (upper == UINT64_C(0xffff) && sign != 0u));
}

bool db_x64_arm64_relocate_add_scaled_internal(
    uint64_t base,
    uint32_t index,
    uint64_t *result)
{
    const uint64_t amount = (uint64_t)index * UINT64_C(4);

    if (UINT64_MAX - base < amount) {
        return false;
    }
    *result = base + amount;
    return db_x64_arm64_relocate_canonical_internal(*result);
}

static bool width_valid(db_ir_width width)
{
    return width == DB_IR_WIDTH_NONE ||
           width == DB_IR_WIDTH_32 ||
           width == DB_IR_WIDTH_64;
}

static bool condition_valid(db_ir_condition condition)
{
    return (unsigned int)condition < UINT8_C(16) ||
           condition == (db_ir_condition)UINT8_C(0xff);
}

static bool trap_valid(db_ir_trap trap)
{
    return trap >= DB_IR_TRAP_NONE && trap <= DB_IR_TRAP_SYSCALL;
}

static bool address_mode_valid(db_ir_address_mode mode)
{
    return mode >= DB_IR_ADDRESS_NONE && mode <= DB_IR_ADDRESS_ABSOLUTE;
}

static bool host_or_none(uint8_t value)
{
    return value < UINT8_C(31) || value == UINT8_C(0xff);
}

static bool guest_or_none(uint8_t value)
{
    return value < UINT8_C(16) || value == UINT8_C(0xff);
}

static bool scale_valid(uint8_t value)
{
    return value == UINT8_C(1) ||
           value == UINT8_C(2) ||
           value == UINT8_C(4) ||
           value == UINT8_C(8);
}

bool db_x64_arm64_relocate_record_valid_internal(
    const db_x64_arm64_relocation *relocation)
{
    return x64_canonical_nonzero(relocation->source_rip) &&
           (relocation->kind == DB_X64_ARM64_RELOCATION_CALL26 ||
            relocation->kind == DB_X64_ARM64_RELOCATION_JUMP26) &&
           relocation->helper > DB_X64_ARM64_HELPER_NONE &&
           relocation->helper <= DB_X64_ARM64_HELPER_RAISE_TRAP &&
           width_valid(relocation->width) &&
           condition_valid(relocation->condition) &&
           trap_valid(relocation->trap) &&
           address_mode_valid(relocation->address_mode) &&
           host_or_none(relocation->host_destination) &&
           host_or_none(relocation->host_source) &&
           host_or_none(relocation->host_auxiliary) &&
           guest_or_none(relocation->guest_register) &&
           guest_or_none(relocation->base_guest_register) &&
           guest_or_none(relocation->index_guest_register) &&
           scale_valid(relocation->scale) &&
           zero_bytes(relocation->reserved,
                      sizeof(relocation->reserved));
}

bool db_x64_arm64_relocate_target_table_valid_internal(
    const db_x64_arm64_helper_targets *targets)
{
    return targets->addresses[DB_X64_ARM64_HELPER_NONE] == 0u &&
           zero_bytes(targets->reserved, sizeof(targets->reserved));
}

bool db_x64_arm64_relocate_placeholder_matches_internal(
    uint32_t word,
    db_x64_arm64_relocation_kind kind)
{
    return word == (kind == DB_X64_ARM64_RELOCATION_CALL26
                        ? DB_ARM64_BL_ZERO
                        : DB_ARM64_B_ZERO);
}
