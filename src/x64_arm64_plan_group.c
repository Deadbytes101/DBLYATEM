#include "x64_arm64_plan_internal.h"

#include <stddef.h>
#include <stdint.h>

static bool guest_register_valid(uint8_t value)
{
    return value < UINT8_C(16);
}

bool db_x64_arm64_plan_terminal_internal(db_arm64_select_opcode opcode)
{
    return opcode == DB_ARM64_SELECT_BRANCH_GUEST_DIRECT ||
           opcode == DB_ARM64_SELECT_BRANCH_GUEST_TARGET ||
           opcode == DB_ARM64_SELECT_BRANCH_IF_TRUE ||
           opcode == DB_ARM64_SELECT_RAISE_TRAP;
}

bool db_x64_arm64_plan_group_pair_internal(
    db_arm64_select_opcode producer,
    db_arm64_select_opcode consumer)
{
    return (producer == DB_ARM64_SELECT_EOR_GUEST_GPR &&
            consumer == DB_ARM64_SELECT_UPDATE_LOGIC_FLAGS) ||
           (producer == DB_ARM64_SELECT_FORM_GUEST_ADDRESS &&
            (consumer == DB_ARM64_SELECT_LOAD_U64 ||
             consumer == DB_ARM64_SELECT_LOAD_U64_TARGET ||
             consumer == DB_ARM64_SELECT_STORE_U64)) ||
           (producer == DB_ARM64_SELECT_LOAD_U64_TARGET &&
            (consumer == DB_ARM64_SELECT_PUSH_GUEST_U64 ||
             consumer == DB_ARM64_SELECT_BRANCH_GUEST_TARGET)) ||
           (producer == DB_ARM64_SELECT_PUSH_GUEST_U64 &&
            (consumer == DB_ARM64_SELECT_BRANCH_GUEST_DIRECT ||
             consumer == DB_ARM64_SELECT_BRANCH_GUEST_TARGET)) ||
           (producer == DB_ARM64_SELECT_POP_GUEST_U64 &&
            consumer == DB_ARM64_SELECT_BRANCH_GUEST_TARGET) ||
           (producer == DB_ARM64_SELECT_TEST_GUEST_CONDITION &&
            consumer == DB_ARM64_SELECT_BRANCH_IF_TRUE);
}

bool db_x64_arm64_plan_requires_consumer_internal(
    db_arm64_select_opcode opcode)
{
    return opcode == DB_ARM64_SELECT_EOR_GUEST_GPR ||
           opcode == DB_ARM64_SELECT_FORM_GUEST_ADDRESS ||
           opcode == DB_ARM64_SELECT_LOAD_U64_TARGET ||
           opcode == DB_ARM64_SELECT_PUSH_GUEST_U64 ||
           opcode == DB_ARM64_SELECT_POP_GUEST_U64 ||
           opcode == DB_ARM64_SELECT_TEST_GUEST_CONDITION;
}

bool db_x64_arm64_plan_is_consumer_internal(db_arm64_select_opcode opcode)
{
    return opcode == DB_ARM64_SELECT_UPDATE_LOGIC_FLAGS ||
           opcode == DB_ARM64_SELECT_LOAD_U64 ||
           opcode == DB_ARM64_SELECT_LOAD_U64_TARGET ||
           opcode == DB_ARM64_SELECT_STORE_U64 ||
           opcode == DB_ARM64_SELECT_BRANCH_GUEST_TARGET ||
           opcode == DB_ARM64_SELECT_BRANCH_IF_TRUE;
}

static uint32_t chunks(uint64_t value)
{
    uint32_t count = 0u;
    uint32_t index;
    for (index = 0u; index < 4u; ++index) {
        if (((value >> (index * 16u)) & UINT64_C(0xffff)) != 0u) ++count;
    }
    return count == 0u ? 1u : count;
}

uint32_t db_x64_arm64_plan_expansion_internal(
    const db_arm64_select_record *record)
{
    switch (record->opcode) {
    case DB_ARM64_SELECT_NOP: return 1u;
    case DB_ARM64_SELECT_MATERIALIZE_U64: return chunks(record->value) + 1u;
    case DB_ARM64_SELECT_MOVE_GUEST_GPR: return 2u;
    case DB_ARM64_SELECT_EOR_GUEST_GPR: return 4u;
    case DB_ARM64_SELECT_UPDATE_LOGIC_FLAGS: return 1u;
    case DB_ARM64_SELECT_FORM_GUEST_ADDRESS: return 1u;
    case DB_ARM64_SELECT_LOAD_U64: return 2u;
    case DB_ARM64_SELECT_LOAD_U64_TARGET: return 1u;
    case DB_ARM64_SELECT_STORE_U64: return 2u;
    case DB_ARM64_SELECT_PUSH_GUEST_U64: return chunks(record->value) + 1u;
    case DB_ARM64_SELECT_POP_GUEST_U64:
    case DB_ARM64_SELECT_BRANCH_GUEST_DIRECT:
    case DB_ARM64_SELECT_BRANCH_GUEST_TARGET:
    case DB_ARM64_SELECT_TEST_GUEST_CONDITION:
    case DB_ARM64_SELECT_BRANCH_IF_TRUE:
    case DB_ARM64_SELECT_RAISE_TRAP:
        return 1u;
    }
    return 0u;
}

bool db_x64_arm64_plan_state_offset_internal(
    uint8_t guest_register,
    uint32_t *offset)
{
    size_t value;
    if (!guest_register_valid(guest_register) || offset == NULL) return false;
    value = offsetof(db_x64_scalar_state, registers) +
            (size_t)guest_register * sizeof(uint64_t);
    if (value > UINT32_MAX) return false;
    *offset = (uint32_t)value;
    return true;
}
