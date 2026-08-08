#include "x64_ir_lower_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DB_X64_IR_LOWER_MAX_INSTRUCTION_LENGTH UINT8_C(15)

static bool db_x64_ir_lower_zero(const void *data, size_t size)
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

static bool db_x64_ir_lower_canonical_nonzero(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return address != 0u &&
           upper == (sign != 0u ? UINT64_C(0xffff) : UINT64_C(0));
}

static bool db_x64_ir_lower_add_unsigned(uint64_t base,
                                         uint64_t value,
                                         uint64_t *result)
{
    uint64_t planned;

    if (UINT64_MAX - base < value) {
        return false;
    }

    planned = base + value;
    if (!db_x64_ir_lower_canonical_nonzero(planned)) {
        return false;
    }

    *result = planned;
    return true;
}

static bool db_x64_ir_lower_add_relative(uint64_t base,
                                         int64_t relative,
                                         uint64_t *result)
{
    uint64_t magnitude;
    uint64_t planned;

    if (relative >= 0) {
        return db_x64_ir_lower_add_unsigned(base,
                                            (uint64_t)relative,
                                            result);
    }

    magnitude = (uint64_t)(-(relative + 1)) + UINT64_C(1);
    if (base < magnitude) {
        return false;
    }

    planned = base - magnitude;
    if (!db_x64_ir_lower_canonical_nonzero(planned)) {
        return false;
    }

    *result = planned;
    return true;
}

static bool db_x64_ir_lower_register(uint8_t value)
{
    return value < UINT8_C(16);
}

static bool db_x64_ir_lower_register_or_none(uint8_t value)
{
    return db_x64_ir_lower_register(value) ||
           value == DB_X64_REGISTER_NONE;
}

static bool db_x64_ir_lower_scalar_reserved_zero(
    const db_x64_instruction *instruction)
{
    return instruction->reserved[0] == 0u &&
           instruction->reserved[1] == 0u &&
           instruction->reserved[2] == 0u;
}

static bool db_x64_ir_lower_memory_reserved_zero(
    const db_x64_memory_instruction *instruction)
{
    return instruction->reserved[0] == 0u &&
           instruction->reserved[1] == 0u &&
           instruction->reserved[2] == 0u &&
           instruction->reserved[3] == 0u &&
           instruction->memory.reserved[0] == 0u &&
           instruction->memory.reserved[1] == 0u &&
           instruction->memory.reserved[2] == 0u &&
           instruction->memory.address.reserved[0] == 0u &&
           instruction->memory.address.reserved[1] == 0u &&
           instruction->memory.address.reserved[2] == 0u &&
           instruction->memory.address.reserved[3] == 0u;
}

static bool db_x64_ir_lower_scalar_empty_fields(
    const db_x64_instruction *instruction)
{
    return instruction->immediate == 0u &&
           instruction->destination_register == DB_X64_REGISTER_NONE &&
           instruction->source_register == DB_X64_REGISTER_NONE;
}

static bool db_x64_ir_lower_relative_fits(int64_t value, bool short_form)
{
    return short_form
               ? value >= INT8_MIN && value <= INT8_MAX
               : value >= INT32_MIN && value <= INT32_MAX;
}

static bool db_x64_ir_lower_scalar_valid(
    const db_x64_instruction *instruction)
{
    if (instruction->length == 0u ||
        instruction->length > DB_X64_IR_LOWER_MAX_INSTRUCTION_LENGTH ||
        !db_x64_ir_lower_scalar_reserved_zero(instruction) ||
        (instruction->rex != 0u &&
         (instruction->rex < UINT8_C(0x40) ||
          instruction->rex > UINT8_C(0x4f)))) {
        return false;
    }

    switch (instruction->kind) {
    case DB_X64_INSTRUCTION_NOP:
    case DB_X64_INSTRUCTION_RET:
    case DB_X64_INSTRUCTION_INT3:
        return instruction->length == 1u &&
               instruction->rex == 0u &&
               instruction->relative == 0 &&
               instruction->condition == DB_X64_CONDITION_NONE &&
               db_x64_ir_lower_scalar_empty_fields(instruction);

    case DB_X64_INSTRUCTION_UD2:
    case DB_X64_INSTRUCTION_SYSCALL:
        return instruction->length == 2u &&
               instruction->rex == 0u &&
               instruction->relative == 0 &&
               instruction->condition == DB_X64_CONDITION_NONE &&
               db_x64_ir_lower_scalar_empty_fields(instruction);

    case DB_X64_INSTRUCTION_CALL_REL32:
    case DB_X64_INSTRUCTION_JMP_REL32:
        return instruction->length == 5u &&
               instruction->rex == 0u &&
               db_x64_ir_lower_relative_fits(instruction->relative, false) &&
               instruction->condition == DB_X64_CONDITION_NONE &&
               db_x64_ir_lower_scalar_empty_fields(instruction);

    case DB_X64_INSTRUCTION_JMP_REL8:
        return instruction->length == 2u &&
               instruction->rex == 0u &&
               db_x64_ir_lower_relative_fits(instruction->relative, true) &&
               instruction->condition == DB_X64_CONDITION_NONE &&
               db_x64_ir_lower_scalar_empty_fields(instruction);

    case DB_X64_INSTRUCTION_JCC_REL8:
    case DB_X64_INSTRUCTION_JCC_REL32:
        return instruction->length ==
                   (uint8_t)(instruction->kind == DB_X64_INSTRUCTION_JCC_REL8
                                 ? 2u
                                 : 6u) &&
               instruction->rex == 0u &&
               db_x64_ir_lower_relative_fits(
                   instruction->relative,
                   instruction->kind == DB_X64_INSTRUCTION_JCC_REL8) &&
               instruction->condition < UINT8_C(16) &&
               db_x64_ir_lower_scalar_empty_fields(instruction);

    case DB_X64_INSTRUCTION_MOV_R64_IMM64:
        return instruction->length == 10u &&
               (instruction->rex & UINT8_C(0x08)) != 0u &&
               instruction->relative == 0 &&
               db_x64_ir_lower_register(instruction->destination_register) &&
               instruction->source_register == DB_X64_REGISTER_NONE &&
               instruction->condition == DB_X64_CONDITION_NONE;

    case DB_X64_INSTRUCTION_MOV_R64_R64:
    case DB_X64_INSTRUCTION_XOR_R64_R64:
        return instruction->length == 3u &&
               (instruction->rex & UINT8_C(0x08)) != 0u &&
               instruction->immediate == 0u &&
               instruction->relative == 0 &&
               db_x64_ir_lower_register(instruction->destination_register) &&
               db_x64_ir_lower_register(instruction->source_register) &&
               instruction->condition == DB_X64_CONDITION_NONE;

    case DB_X64_INSTRUCTION_XOR_R32_R32:
        return instruction->length ==
                   (uint8_t)(instruction->rex == 0u ? 2u : 3u) &&
               (instruction->rex & UINT8_C(0x08)) == 0u &&
               instruction->immediate == 0u &&
               instruction->relative == 0 &&
               db_x64_ir_lower_register(instruction->destination_register) &&
               db_x64_ir_lower_register(instruction->source_register) &&
               instruction->condition == DB_X64_CONDITION_NONE;
    }

    return false;
}

static bool db_x64_ir_lower_address_valid(
    const db_x64_address_expression *address,
    uint8_t instruction_length)
{
    if (!db_x64_ir_lower_register_or_none(address->base_register) ||
        !db_x64_ir_lower_register_or_none(address->index_register) ||
        (address->scale != UINT8_C(1) &&
         address->scale != UINT8_C(2) &&
         address->scale != UINT8_C(4) &&
         address->scale != UINT8_C(8))) {
        return false;
    }

    switch (address->mode) {
    case DB_X64_ADDRESS_BASE_INDEX:
        return address->instruction_length == 0u &&
               (address->base_register != DB_X64_REGISTER_NONE ||
                address->index_register != DB_X64_REGISTER_NONE);

    case DB_X64_ADDRESS_RIP_RELATIVE:
        return address->base_register == DB_X64_REGISTER_NONE &&
               address->index_register == DB_X64_REGISTER_NONE &&
               address->scale == UINT8_C(1) &&
               address->instruction_length == instruction_length;

    case DB_X64_ADDRESS_ABSOLUTE:
        return address->base_register == DB_X64_REGISTER_NONE &&
               address->index_register == DB_X64_REGISTER_NONE &&
               address->scale == UINT8_C(1) &&
               address->instruction_length == 0u;
    }

    return false;
}

static bool db_x64_ir_lower_memory_common_valid(
    const db_x64_memory_instruction *instruction)
{
    const db_x64_memory_operand *memory = &instruction->memory;
    uint8_t mod;

    if (!db_x64_ir_lower_memory_reserved_zero(instruction) ||
        instruction->instruction_length < UINT8_C(2) ||
        instruction->instruction_length >
            DB_X64_IR_LOWER_MAX_INSTRUCTION_LENGTH ||
        (instruction->rex != 0u &&
         (instruction->rex < UINT8_C(0x40) ||
          instruction->rex > UINT8_C(0x4f))) ||
        memory->instruction_length != instruction->instruction_length ||
        memory->encoded_length == 0u ||
        memory->encoded_length >= instruction->instruction_length ||
        (memory->sib != DB_X64_MEMORY_NO_SIB &&
         (memory->modrm & UINT8_C(0x07)) != UINT8_C(4)) ||
        !db_x64_ir_lower_address_valid(&memory->address,
                                       instruction->instruction_length)) {
        return false;
    }

    mod = (uint8_t)(memory->modrm >> 6u);
    return mod != UINT8_C(3);
}

static bool db_x64_ir_lower_memory_valid(
    const db_x64_memory_instruction *instruction)
{
    const db_x64_memory_operand *memory = &instruction->memory;

    if (!db_x64_ir_lower_memory_common_valid(instruction)) {
        return false;
    }

    switch (instruction->kind) {
    case DB_X64_MEMORY_INSTRUCTION_LOAD_R64:
        return instruction->instruction_length >= UINT8_C(3) &&
               instruction->rex >= UINT8_C(0x48) &&
               instruction->rex <= UINT8_C(0x4f) &&
               instruction->opcode == UINT8_C(0x8b) &&
               db_x64_ir_lower_register(instruction->register_operand) &&
               memory->register_operand == instruction->register_operand;

    case DB_X64_MEMORY_INSTRUCTION_STORE_R64:
        return instruction->instruction_length >= UINT8_C(3) &&
               instruction->rex >= UINT8_C(0x48) &&
               instruction->rex <= UINT8_C(0x4f) &&
               instruction->opcode == UINT8_C(0x89) &&
               db_x64_ir_lower_register(instruction->register_operand) &&
               memory->register_operand == instruction->register_operand;

    case DB_X64_MEMORY_INSTRUCTION_CALL_RM64:
        return instruction->opcode == UINT8_C(0xff) &&
               instruction->register_operand == DB_X64_REGISTER_NONE &&
               memory->register_operand == UINT8_C(2) &&
               (instruction->rex & UINT8_C(0x04)) == 0u;

    case DB_X64_MEMORY_INSTRUCTION_JMP_RM64:
        return instruction->opcode == UINT8_C(0xff) &&
               instruction->register_operand == DB_X64_REGISTER_NONE &&
               memory->register_operand == UINT8_C(4) &&
               (instruction->rex & UINT8_C(0x04)) == 0u;
    }

    return false;
}

static db_x64_block_control db_x64_ir_lower_expected_control_scalar(
    const db_x64_instruction *instruction)
{
    switch (instruction->kind) {
    case DB_X64_INSTRUCTION_RET:
        return DB_X64_BLOCK_CONTROL_RETURN;
    case DB_X64_INSTRUCTION_CALL_REL32:
        return DB_X64_BLOCK_CONTROL_DIRECT_CALL;
    case DB_X64_INSTRUCTION_JMP_REL32:
    case DB_X64_INSTRUCTION_JMP_REL8:
        return DB_X64_BLOCK_CONTROL_DIRECT_JUMP;
    case DB_X64_INSTRUCTION_JCC_REL8:
    case DB_X64_INSTRUCTION_JCC_REL32:
        return DB_X64_BLOCK_CONTROL_CONDITIONAL_JUMP;
    case DB_X64_INSTRUCTION_INT3:
    case DB_X64_INSTRUCTION_UD2:
    case DB_X64_INSTRUCTION_SYSCALL:
        return DB_X64_BLOCK_CONTROL_TRAP;
    case DB_X64_INSTRUCTION_NOP:
    case DB_X64_INSTRUCTION_MOV_R64_IMM64:
    case DB_X64_INSTRUCTION_MOV_R64_R64:
    case DB_X64_INSTRUCTION_XOR_R32_R32:
    case DB_X64_INSTRUCTION_XOR_R64_R64:
        return DB_X64_BLOCK_CONTROL_LINEAR;
    }

    return DB_X64_BLOCK_CONTROL_TRAP;
}

static db_x64_block_control db_x64_ir_lower_expected_control_memory(
    const db_x64_memory_instruction *instruction)
{
    switch (instruction->kind) {
    case DB_X64_MEMORY_INSTRUCTION_LOAD_R64:
    case DB_X64_MEMORY_INSTRUCTION_STORE_R64:
        return DB_X64_BLOCK_CONTROL_LINEAR;
    case DB_X64_MEMORY_INSTRUCTION_CALL_RM64:
        return DB_X64_BLOCK_CONTROL_INDIRECT_CALL;
    case DB_X64_MEMORY_INSTRUCTION_JMP_RM64:
        return DB_X64_BLOCK_CONTROL_INDIRECT_JUMP;
    }

    return DB_X64_BLOCK_CONTROL_TRAP;
}

bool db_x64_ir_lower_control_is_terminator_internal(
    db_x64_block_control control)
{
    return control != DB_X64_BLOCK_CONTROL_LINEAR;
}

static bool db_x64_ir_lower_record_reserved_zero(
    const db_x64_block_record *record)
{
    size_t index;

    for (index = 0u; index < sizeof(record->reserved); ++index) {
        if (record->reserved[index] != 0u) {
            return false;
        }
    }

    return true;
}

db_x64_ir_lower_status db_x64_ir_lower_validate_record_internal(
    const db_x64_block_record *record)
{
    uint64_t expected_fallthrough;
    uint64_t expected_target;
    uint8_t length;
    bool target_required;

    if (!db_x64_ir_lower_record_reserved_zero(record) ||
        !db_x64_ir_lower_canonical_nonzero(record->rip) ||
        !db_x64_ir_lower_canonical_nonzero(record->fallthrough_rip) ||
        record->has_direct_target > UINT8_C(1)) {
        return DB_X64_IR_LOWER_BAD_RECORD;
    }

    if (record->path == DB_X64_BLOCK_PATH_SCALAR) {
        if (!db_x64_ir_lower_scalar_valid(&record->scalar) ||
            !db_x64_ir_lower_zero(&record->memory,
                                  sizeof(record->memory)) ||
            record->control !=
                db_x64_ir_lower_expected_control_scalar(&record->scalar)) {
            return DB_X64_IR_LOWER_BAD_RECORD;
        }
        length = record->scalar.length;
    } else if (record->path == DB_X64_BLOCK_PATH_MEMORY) {
        if (!db_x64_ir_lower_memory_valid(&record->memory) ||
            !db_x64_ir_lower_zero(&record->scalar,
                                  sizeof(record->scalar)) ||
            record->control !=
                db_x64_ir_lower_expected_control_memory(&record->memory)) {
            return DB_X64_IR_LOWER_BAD_RECORD;
        }
        length = record->memory.instruction_length;
    } else {
        return DB_X64_IR_LOWER_BAD_RECORD;
    }

    if (!db_x64_ir_lower_add_unsigned(record->rip,
                                      length,
                                      &expected_fallthrough) ||
        expected_fallthrough != record->fallthrough_rip) {
        return DB_X64_IR_LOWER_BAD_RECORD;
    }

    target_required =
        record->control == DB_X64_BLOCK_CONTROL_DIRECT_CALL ||
        record->control == DB_X64_BLOCK_CONTROL_DIRECT_JUMP ||
        record->control == DB_X64_BLOCK_CONTROL_CONDITIONAL_JUMP;

    if (target_required) {
        if (record->has_direct_target != UINT8_C(1) ||
            !db_x64_ir_lower_canonical_nonzero(record->direct_target) ||
            !db_x64_ir_lower_add_relative(record->fallthrough_rip,
                                          record->scalar.relative,
                                          &expected_target) ||
            expected_target != record->direct_target) {
            return DB_X64_IR_LOWER_BAD_RECORD;
        }
    } else if (record->has_direct_target != 0u ||
               record->direct_target != 0u) {
        return DB_X64_IR_LOWER_BAD_RECORD;
    }

    return DB_X64_IR_LOWER_OK;
}
