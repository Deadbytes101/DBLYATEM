#include "x64_arm64_plan_internal.h"

#include <stddef.h>
#include <stdint.h>

static bool zero_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;
    for (index = 0u; index < size; ++index) {
        if (bytes[index] != 0u) return false;
    }
    return true;
}

static bool canonical_nonzero(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);
    return address != 0u &&
           upper == (sign != 0u ? UINT64_C(0xffff) : UINT64_C(0));
}

static bool guest(uint8_t value)
{
    return value < UINT8_C(16);
}

static bool guest_or_none(uint8_t value)
{
    return guest(value) || value == DB_IR_REGISTER_NONE;
}

static bool width_valid(db_ir_width width)
{
    return width == DB_IR_WIDTH_NONE || width == DB_IR_WIDTH_32 ||
           width == DB_IR_WIDTH_64;
}

static bool condition_valid(db_ir_condition condition)
{
    return (unsigned int)condition < UINT8_C(16) ||
           condition == (db_ir_condition)DB_IR_CONDITION_NONE;
}

static bool trap_valid(db_ir_trap trap)
{
    return trap >= DB_IR_TRAP_NONE && trap <= DB_IR_TRAP_SYSCALL;
}

static bool mode_valid(db_ir_address_mode mode)
{
    return mode >= DB_IR_ADDRESS_NONE && mode <= DB_IR_ADDRESS_ABSOLUTE;
}

static bool temp_valid(db_arm64_select_temp temp)
{
    return temp >= DB_ARM64_SELECT_TEMP_NONE &&
           temp <= DB_ARM64_SELECT_TEMP_PREDICATE;
}

static bool defaults(const db_arm64_select_record *record)
{
    return record->value == 0u && record->displacement == 0 &&
           record->width == DB_IR_WIDTH_NONE &&
           record->condition == (db_ir_condition)DB_IR_CONDITION_NONE &&
           record->trap == DB_IR_TRAP_NONE &&
           record->address_mode == DB_IR_ADDRESS_NONE &&
           record->destination_register == DB_IR_REGISTER_NONE &&
           record->source_register == DB_IR_REGISTER_NONE &&
           record->base_register == DB_IR_REGISTER_NONE &&
           record->index_register == DB_IR_REGISTER_NONE &&
           record->scale == UINT8_C(1) &&
           record->temp == DB_ARM64_SELECT_TEMP_NONE;
}

static bool address_valid(const db_arm64_select_record *record)
{
    if (record->address_mode == DB_IR_ADDRESS_NONE ||
        !guest_or_none(record->base_register) ||
        !guest_or_none(record->index_register) ||
        (record->scale != UINT8_C(1) && record->scale != UINT8_C(2) &&
         record->scale != UINT8_C(4) && record->scale != UINT8_C(8)) ||
        record->temp != DB_ARM64_SELECT_TEMP_ADDRESS) {
        return false;
    }

    if (record->address_mode == DB_IR_ADDRESS_BASE_INDEX) {
        return record->value == 0u &&
               (record->base_register != DB_IR_REGISTER_NONE ||
                record->index_register != DB_IR_REGISTER_NONE);
    }

    if (record->address_mode == DB_IR_ADDRESS_IP_RELATIVE) {
        return canonical_nonzero(record->value) &&
               record->base_register == DB_IR_REGISTER_NONE &&
               record->index_register == DB_IR_REGISTER_NONE &&
               record->scale == UINT8_C(1);
    }

    return record->address_mode == DB_IR_ADDRESS_ABSOLUTE &&
           record->value == 0u &&
           record->base_register == DB_IR_REGISTER_NONE &&
           record->index_register == DB_IR_REGISTER_NONE &&
           record->scale == UINT8_C(1);
}

static bool target_load_valid(const db_arm64_select_record *record)
{
    return record->value == 0u && record->displacement == 0 &&
           record->width == DB_IR_WIDTH_64 &&
           record->condition == (db_ir_condition)DB_IR_CONDITION_NONE &&
           record->trap == DB_IR_TRAP_NONE &&
           record->address_mode == DB_IR_ADDRESS_NONE &&
           record->destination_register == DB_IR_REGISTER_NONE &&
           record->source_register == DB_IR_REGISTER_NONE &&
           record->base_register == DB_IR_REGISTER_NONE &&
           record->index_register == DB_IR_REGISTER_NONE &&
           record->scale == UINT8_C(1) &&
           record->temp == DB_ARM64_SELECT_TEMP_ADDRESS;
}

bool db_x64_arm64_plan_selection_valid_internal(
    const db_arm64_select_record *record)
{
    if (!canonical_nonzero(record->source_rip) ||
        !width_valid(record->width) ||
        !condition_valid(record->condition) ||
        !trap_valid(record->trap) || !mode_valid(record->address_mode) ||
        !guest_or_none(record->destination_register) ||
        !guest_or_none(record->source_register) ||
        !guest_or_none(record->base_register) ||
        !guest_or_none(record->index_register) ||
        !temp_valid(record->temp) ||
        !zero_bytes(record->reserved, sizeof(record->reserved))) {
        return false;
    }

    switch (record->opcode) {
    case DB_ARM64_SELECT_NOP:
        return defaults(record);
    case DB_ARM64_SELECT_MATERIALIZE_U64:
        return record->width == DB_IR_WIDTH_64 &&
               guest(record->destination_register) &&
               record->source_register == DB_IR_REGISTER_NONE &&
               record->address_mode == DB_IR_ADDRESS_NONE &&
               record->condition == (db_ir_condition)DB_IR_CONDITION_NONE &&
               record->trap == DB_IR_TRAP_NONE &&
               record->base_register == DB_IR_REGISTER_NONE &&
               record->index_register == DB_IR_REGISTER_NONE &&
               record->scale == UINT8_C(1) &&
               record->temp == DB_ARM64_SELECT_TEMP_NONE;
    case DB_ARM64_SELECT_MOVE_GUEST_GPR:
        return record->value == 0u && record->displacement == 0 &&
               record->width == DB_IR_WIDTH_64 &&
               guest(record->destination_register) &&
               guest(record->source_register) &&
               record->address_mode == DB_IR_ADDRESS_NONE &&
               record->condition == (db_ir_condition)DB_IR_CONDITION_NONE &&
               record->trap == DB_IR_TRAP_NONE &&
               record->base_register == DB_IR_REGISTER_NONE &&
               record->index_register == DB_IR_REGISTER_NONE &&
               record->scale == UINT8_C(1) &&
               record->temp == DB_ARM64_SELECT_TEMP_NONE;
    case DB_ARM64_SELECT_EOR_GUEST_GPR:
        return record->value == 0u && record->displacement == 0 &&
               (record->width == DB_IR_WIDTH_32 ||
                record->width == DB_IR_WIDTH_64) &&
               guest(record->destination_register) &&
               guest(record->source_register) &&
               record->address_mode == DB_IR_ADDRESS_NONE &&
               record->condition == (db_ir_condition)DB_IR_CONDITION_NONE &&
               record->trap == DB_IR_TRAP_NONE &&
               record->base_register == DB_IR_REGISTER_NONE &&
               record->index_register == DB_IR_REGISTER_NONE &&
               record->scale == UINT8_C(1) &&
               record->temp == DB_ARM64_SELECT_TEMP_NONE;
    case DB_ARM64_SELECT_UPDATE_LOGIC_FLAGS:
        return record->value == 0u && record->displacement == 0 &&
               (record->width == DB_IR_WIDTH_32 ||
                record->width == DB_IR_WIDTH_64) &&
               record->destination_register == DB_IR_REGISTER_NONE &&
               guest(record->source_register) &&
               record->address_mode == DB_IR_ADDRESS_NONE &&
               record->condition == (db_ir_condition)DB_IR_CONDITION_NONE &&
               record->trap == DB_IR_TRAP_NONE &&
               record->temp == DB_ARM64_SELECT_TEMP_NONE;
    case DB_ARM64_SELECT_FORM_GUEST_ADDRESS:
        return record->width == DB_IR_WIDTH_NONE &&
               record->condition == (db_ir_condition)DB_IR_CONDITION_NONE &&
               record->trap == DB_IR_TRAP_NONE &&
               record->destination_register == DB_IR_REGISTER_NONE &&
               record->source_register == DB_IR_REGISTER_NONE &&
               address_valid(record);
    case DB_ARM64_SELECT_LOAD_U64:
        return record->value == 0u && record->displacement == 0 &&
               record->width == DB_IR_WIDTH_64 &&
               guest(record->destination_register) &&
               record->source_register == DB_IR_REGISTER_NONE &&
               record->address_mode == DB_IR_ADDRESS_NONE &&
               record->temp == DB_ARM64_SELECT_TEMP_ADDRESS;
    case DB_ARM64_SELECT_LOAD_U64_TARGET:
        return target_load_valid(record);
    case DB_ARM64_SELECT_STORE_U64:
        return record->value == 0u && record->displacement == 0 &&
               record->width == DB_IR_WIDTH_64 &&
               record->destination_register == DB_IR_REGISTER_NONE &&
               guest(record->source_register) &&
               record->address_mode == DB_IR_ADDRESS_NONE &&
               record->temp == DB_ARM64_SELECT_TEMP_ADDRESS;
    case DB_ARM64_SELECT_PUSH_GUEST_U64:
        return canonical_nonzero(record->value) &&
               record->displacement == 0 &&
               record->width == DB_IR_WIDTH_NONE &&
               record->destination_register == DB_IR_REGISTER_NONE &&
               record->source_register == DB_IR_REGISTER_NONE &&
               record->address_mode == DB_IR_ADDRESS_NONE &&
               record->temp == DB_ARM64_SELECT_TEMP_NONE;
    case DB_ARM64_SELECT_POP_GUEST_U64:
        return record->value == 0u && record->displacement == 0 &&
               record->width == DB_IR_WIDTH_NONE &&
               record->destination_register == DB_IR_REGISTER_NONE &&
               record->source_register == DB_IR_REGISTER_NONE &&
               record->address_mode == DB_IR_ADDRESS_NONE &&
               record->temp == DB_ARM64_SELECT_TEMP_TARGET;
    case DB_ARM64_SELECT_BRANCH_GUEST_DIRECT:
        return canonical_nonzero(record->value) &&
               record->displacement == 0 &&
               record->width == DB_IR_WIDTH_NONE &&
               record->condition == (db_ir_condition)DB_IR_CONDITION_NONE &&
               record->trap == DB_IR_TRAP_NONE &&
               record->temp == DB_ARM64_SELECT_TEMP_NONE;
    case DB_ARM64_SELECT_BRANCH_GUEST_TARGET:
        return record->value == 0u && record->displacement == 0 &&
               record->width == DB_IR_WIDTH_NONE &&
               record->temp == DB_ARM64_SELECT_TEMP_TARGET;
    case DB_ARM64_SELECT_TEST_GUEST_CONDITION:
        return record->value == 0u && record->displacement == 0 &&
               (unsigned int)record->condition < UINT8_C(16) &&
               record->trap == DB_IR_TRAP_NONE &&
               record->temp == DB_ARM64_SELECT_TEMP_PREDICATE;
    case DB_ARM64_SELECT_BRANCH_IF_TRUE:
        return canonical_nonzero(record->value) &&
               record->displacement == 0 &&
               (unsigned int)record->condition < UINT8_C(16) &&
               record->trap == DB_IR_TRAP_NONE &&
               record->temp == DB_ARM64_SELECT_TEMP_PREDICATE;
    case DB_ARM64_SELECT_RAISE_TRAP:
        return record->value == 0u && record->displacement == 0 &&
               record->condition == (db_ir_condition)DB_IR_CONDITION_NONE &&
               record->trap != DB_IR_TRAP_NONE &&
               record->temp == DB_ARM64_SELECT_TEMP_NONE;
    }
    return false;
}
