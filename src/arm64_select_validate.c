#include "arm64_select_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_arm64_select_zero(const void *data, size_t size)
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

static bool db_arm64_select_canonical_nonzero(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return address != 0u &&
           upper == (sign != 0u ? UINT64_C(0xffff) : UINT64_C(0));
}

static bool db_arm64_select_register(uint8_t value)
{
    return value < UINT8_C(16);
}

static bool db_arm64_select_register_or_none(uint8_t value)
{
    return db_arm64_select_register(value) || value == DB_IR_REGISTER_NONE;
}

static bool db_arm64_select_condition_valid(db_ir_condition condition)
{
    return (unsigned int)condition < UINT8_C(16) ||
           condition == (db_ir_condition)DB_IR_CONDITION_NONE;
}

static bool db_arm64_select_trap_valid(db_ir_trap trap)
{
    return trap >= DB_IR_TRAP_NONE && trap <= DB_IR_TRAP_SYSCALL;
}

static bool db_arm64_select_width_valid(db_ir_width width)
{
    return width == DB_IR_WIDTH_NONE ||
           width == DB_IR_WIDTH_32 ||
           width == DB_IR_WIDTH_64;
}

static bool db_arm64_select_address_valid(const db_ir_address *address)
{
    if (!db_arm64_select_zero(address->reserved,
                              sizeof(address->reserved)) ||
        !db_arm64_select_register_or_none(address->base_register) ||
        !db_arm64_select_register_or_none(address->index_register) ||
        (address->scale != UINT8_C(1) &&
         address->scale != UINT8_C(2) &&
         address->scale != UINT8_C(4) &&
         address->scale != UINT8_C(8))) {
        return false;
    }

    switch (address->mode) {
    case DB_IR_ADDRESS_NONE:
        return address->displacement == 0 &&
               address->base_register == DB_IR_REGISTER_NONE &&
               address->index_register == DB_IR_REGISTER_NONE &&
               address->scale == UINT8_C(1);

    case DB_IR_ADDRESS_BASE_INDEX:
        return address->base_register != DB_IR_REGISTER_NONE ||
               address->index_register != DB_IR_REGISTER_NONE;

    case DB_IR_ADDRESS_IP_RELATIVE:
    case DB_IR_ADDRESS_ABSOLUTE:
        return address->base_register == DB_IR_REGISTER_NONE &&
               address->index_register == DB_IR_REGISTER_NONE &&
               address->scale == UINT8_C(1);
    }

    return false;
}

static bool db_arm64_select_common_valid(const db_ir_instruction *instruction)
{
    return db_arm64_select_canonical_nonzero(instruction->source_rip) &&
           db_arm64_select_canonical_nonzero(instruction->fallthrough_rip) &&
           instruction->has_direct_target <= UINT8_C(1) &&
           db_arm64_select_width_valid(instruction->width) &&
           db_arm64_select_condition_valid(instruction->condition) &&
           db_arm64_select_trap_valid(instruction->trap) &&
           (instruction->flag_effect == DB_IR_FLAG_EFFECT_NONE ||
            instruction->flag_effect == DB_IR_FLAG_EFFECT_LOGIC_RESULT ||
            instruction->flag_effect == DB_IR_FLAG_EFFECT_SUB_RESULT) &&
           db_arm64_select_register_or_none(
               instruction->destination_register) &&
           db_arm64_select_register_or_none(instruction->source_register) &&
           db_arm64_select_address_valid(&instruction->address) &&
           db_arm64_select_zero(instruction->reserved,
                                sizeof(instruction->reserved));
}

static bool db_arm64_select_no_target(const db_ir_instruction *instruction)
{
    return instruction->has_direct_target == 0u &&
           instruction->direct_target == 0u;
}

static bool db_arm64_select_direct_target(
    const db_ir_instruction *instruction)
{
    return instruction->has_direct_target == UINT8_C(1) &&
           db_arm64_select_canonical_nonzero(instruction->direct_target);
}

static bool db_arm64_select_no_address(const db_ir_instruction *instruction)
{
    return instruction->address.mode == DB_IR_ADDRESS_NONE;
}

static bool db_arm64_select_no_registers(const db_ir_instruction *instruction)
{
    return instruction->destination_register == DB_IR_REGISTER_NONE &&
           instruction->source_register == DB_IR_REGISTER_NONE;
}

static bool db_arm64_select_no_condition_trap_flags(
    const db_ir_instruction *instruction)
{
    return instruction->condition ==
               (db_ir_condition)DB_IR_CONDITION_NONE &&
           instruction->trap == DB_IR_TRAP_NONE &&
           instruction->flag_effect == DB_IR_FLAG_EFFECT_NONE;
}

static bool db_arm64_select_indirect_control(
    const db_ir_instruction *instruction)
{
    return instruction->width == DB_IR_WIDTH_NONE &&
           instruction->immediate == 0u &&
           db_arm64_select_no_registers(instruction) &&
           instruction->address.mode != DB_IR_ADDRESS_NONE &&
           db_arm64_select_no_target(instruction) &&
           db_arm64_select_no_condition_trap_flags(instruction);
}

bool db_arm64_select_ir_valid_internal(const db_ir_instruction *instruction)
{
    if (!db_arm64_select_common_valid(instruction)) {
        return false;
    }

    switch (instruction->opcode) {
    case DB_IR_OPCODE_NOP:
        return instruction->width == DB_IR_WIDTH_NONE &&
               instruction->immediate == 0u &&
               db_arm64_select_no_registers(instruction) &&
               db_arm64_select_no_address(instruction) &&
               db_arm64_select_no_target(instruction) &&
               db_arm64_select_no_condition_trap_flags(instruction);

    case DB_IR_OPCODE_MOVE_IMMEDIATE:
        return instruction->width == DB_IR_WIDTH_64 &&
               db_arm64_select_register(
                   instruction->destination_register) &&
               instruction->source_register == DB_IR_REGISTER_NONE &&
               db_arm64_select_no_address(instruction) &&
               db_arm64_select_no_target(instruction) &&
               db_arm64_select_no_condition_trap_flags(instruction);

    case DB_IR_OPCODE_MOVE_REGISTER:
        return instruction->width == DB_IR_WIDTH_64 &&
               instruction->immediate == 0u &&
               db_arm64_select_register(
                   instruction->destination_register) &&
               db_arm64_select_register(instruction->source_register) &&
               db_arm64_select_no_address(instruction) &&
               db_arm64_select_no_target(instruction) &&
               db_arm64_select_no_condition_trap_flags(instruction);

    case DB_IR_OPCODE_SUB_IMMEDIATE:
        return instruction->width == DB_IR_WIDTH_64 &&
               instruction->destination_register == UINT8_C(4) &&
               instruction->source_register == DB_IR_REGISTER_NONE &&
               (uint64_t)(int64_t)(int8_t)instruction->immediate ==
                   instruction->immediate &&
               db_arm64_select_no_address(instruction) &&
               db_arm64_select_no_target(instruction) &&
               instruction->condition ==
                   (db_ir_condition)DB_IR_CONDITION_NONE &&
               instruction->trap == DB_IR_TRAP_NONE &&
               instruction->flag_effect == DB_IR_FLAG_EFFECT_SUB_RESULT;

    case DB_IR_OPCODE_XOR:
        return (instruction->width == DB_IR_WIDTH_32 ||
                instruction->width == DB_IR_WIDTH_64) &&
               instruction->immediate == 0u &&
               db_arm64_select_register(
                   instruction->destination_register) &&
               db_arm64_select_register(instruction->source_register) &&
               db_arm64_select_no_address(instruction) &&
               db_arm64_select_no_target(instruction) &&
               instruction->condition ==
                   (db_ir_condition)DB_IR_CONDITION_NONE &&
               instruction->trap == DB_IR_TRAP_NONE &&
               instruction->flag_effect == DB_IR_FLAG_EFFECT_LOGIC_RESULT;

    case DB_IR_OPCODE_LOAD_U64:
        return instruction->width == DB_IR_WIDTH_64 &&
               instruction->immediate == 0u &&
               db_arm64_select_register(
                   instruction->destination_register) &&
               instruction->source_register == DB_IR_REGISTER_NONE &&
               instruction->address.mode != DB_IR_ADDRESS_NONE &&
               db_arm64_select_no_target(instruction) &&
               db_arm64_select_no_condition_trap_flags(instruction);

    case DB_IR_OPCODE_STORE_U64:
        return instruction->width == DB_IR_WIDTH_64 &&
               instruction->immediate == 0u &&
               instruction->destination_register == DB_IR_REGISTER_NONE &&
               db_arm64_select_register(instruction->source_register) &&
               instruction->address.mode != DB_IR_ADDRESS_NONE &&
               db_arm64_select_no_target(instruction) &&
               db_arm64_select_no_condition_trap_flags(instruction);

    case DB_IR_OPCODE_CALL_DIRECT:
    case DB_IR_OPCODE_JUMP_DIRECT:
        return instruction->width == DB_IR_WIDTH_NONE &&
               instruction->immediate == 0u &&
               db_arm64_select_no_registers(instruction) &&
               db_arm64_select_no_address(instruction) &&
               db_arm64_select_direct_target(instruction) &&
               db_arm64_select_no_condition_trap_flags(instruction);

    case DB_IR_OPCODE_CALL_INDIRECT:
    case DB_IR_OPCODE_JUMP_INDIRECT:
        return db_arm64_select_indirect_control(instruction);

    case DB_IR_OPCODE_RETURN:
        return instruction->width == DB_IR_WIDTH_NONE &&
               instruction->immediate == 0u &&
               db_arm64_select_no_registers(instruction) &&
               db_arm64_select_no_address(instruction) &&
               db_arm64_select_no_target(instruction) &&
               db_arm64_select_no_condition_trap_flags(instruction);

    case DB_IR_OPCODE_JUMP_CONDITIONAL:
        return instruction->width == DB_IR_WIDTH_NONE &&
               instruction->immediate == 0u &&
               db_arm64_select_no_registers(instruction) &&
               db_arm64_select_no_address(instruction) &&
               db_arm64_select_direct_target(instruction) &&
               (unsigned int)instruction->condition < UINT8_C(16) &&
               instruction->trap == DB_IR_TRAP_NONE &&
               instruction->flag_effect == DB_IR_FLAG_EFFECT_NONE;

    case DB_IR_OPCODE_TRAP:
        return instruction->width == DB_IR_WIDTH_NONE &&
               instruction->immediate == 0u &&
               db_arm64_select_no_registers(instruction) &&
               db_arm64_select_no_address(instruction) &&
               db_arm64_select_no_target(instruction) &&
               instruction->condition ==
                   (db_ir_condition)DB_IR_CONDITION_NONE &&
               instruction->trap != DB_IR_TRAP_NONE &&
               instruction->flag_effect == DB_IR_FLAG_EFFECT_NONE;
    }

    return false;
}

bool db_arm64_select_terminator_internal(db_ir_opcode opcode)
{
    return opcode == DB_IR_OPCODE_CALL_DIRECT ||
           opcode == DB_IR_OPCODE_CALL_INDIRECT ||
           opcode == DB_IR_OPCODE_RETURN ||
           opcode == DB_IR_OPCODE_JUMP_DIRECT ||
           opcode == DB_IR_OPCODE_JUMP_INDIRECT ||
           opcode == DB_IR_OPCODE_JUMP_CONDITIONAL ||
           opcode == DB_IR_OPCODE_TRAP;
}

uint32_t db_arm64_select_expansion_internal(db_ir_opcode opcode)
{
    switch (opcode) {
    case DB_IR_OPCODE_CALL_INDIRECT:
        return UINT32_C(4);

    case DB_IR_OPCODE_JUMP_INDIRECT:
        return UINT32_C(3);

    case DB_IR_OPCODE_XOR:
    case DB_IR_OPCODE_LOAD_U64:
    case DB_IR_OPCODE_STORE_U64:
    case DB_IR_OPCODE_CALL_DIRECT:
    case DB_IR_OPCODE_RETURN:
    case DB_IR_OPCODE_JUMP_CONDITIONAL:
        return UINT32_C(2);

    case DB_IR_OPCODE_NOP:
    case DB_IR_OPCODE_SUB_IMMEDIATE:
    case DB_IR_OPCODE_MOVE_IMMEDIATE:
    case DB_IR_OPCODE_MOVE_REGISTER:
    case DB_IR_OPCODE_JUMP_DIRECT:
    case DB_IR_OPCODE_TRAP:
        return UINT32_C(1);
    }

    return UINT32_C(0);
}
