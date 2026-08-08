#include "x64_ir_lower_internal.h"

#include <stddef.h>
#include <stdint.h>

static db_ir_condition db_x64_ir_lower_condition(uint8_t condition)
{
    static const db_ir_condition conditions[16] = {
        DB_IR_CONDITION_OVERFLOW,
        DB_IR_CONDITION_NOT_OVERFLOW,
        DB_IR_CONDITION_BELOW,
        DB_IR_CONDITION_ABOVE_OR_EQUAL,
        DB_IR_CONDITION_EQUAL,
        DB_IR_CONDITION_NOT_EQUAL,
        DB_IR_CONDITION_BELOW_OR_EQUAL,
        DB_IR_CONDITION_ABOVE,
        DB_IR_CONDITION_SIGN,
        DB_IR_CONDITION_NOT_SIGN,
        DB_IR_CONDITION_PARITY,
        DB_IR_CONDITION_NOT_PARITY,
        DB_IR_CONDITION_LESS,
        DB_IR_CONDITION_GREATER_OR_EQUAL,
        DB_IR_CONDITION_LESS_OR_EQUAL,
        DB_IR_CONDITION_GREATER
    };

    return conditions[condition];
}

static void db_x64_ir_lower_initial_instruction(
    db_ir_instruction *instruction,
    const db_x64_block_record *record)
{
    size_t index;

    instruction->source_rip = record->rip;
    instruction->fallthrough_rip = record->fallthrough_rip;
    instruction->direct_target = record->direct_target;
    instruction->immediate = 0u;
    instruction->address.displacement = 0;
    instruction->address.mode = DB_IR_ADDRESS_NONE;
    instruction->address.base_register = DB_IR_REGISTER_NONE;
    instruction->address.index_register = DB_IR_REGISTER_NONE;
    instruction->address.scale = UINT8_C(1);
    for (index = 0u; index < sizeof(instruction->address.reserved); ++index) {
        instruction->address.reserved[index] = 0u;
    }
    instruction->opcode = DB_IR_OPCODE_NOP;
    instruction->width = DB_IR_WIDTH_NONE;
    instruction->condition = (db_ir_condition)DB_IR_CONDITION_NONE;
    instruction->trap = DB_IR_TRAP_NONE;
    instruction->flag_effect = DB_IR_FLAG_EFFECT_NONE;
    instruction->destination_register = DB_IR_REGISTER_NONE;
    instruction->source_register = DB_IR_REGISTER_NONE;
    instruction->has_direct_target = record->has_direct_target;
    for (index = 0u; index < sizeof(instruction->reserved); ++index) {
        instruction->reserved[index] = 0u;
    }
}

static db_ir_address_mode db_x64_ir_lower_address_mode(
    db_x64_address_mode mode)
{
    switch (mode) {
    case DB_X64_ADDRESS_BASE_INDEX:
        return DB_IR_ADDRESS_BASE_INDEX;
    case DB_X64_ADDRESS_RIP_RELATIVE:
        return DB_IR_ADDRESS_IP_RELATIVE;
    case DB_X64_ADDRESS_ABSOLUTE:
        return DB_IR_ADDRESS_ABSOLUTE;
    }

    return DB_IR_ADDRESS_NONE;
}

static void db_x64_ir_lower_copy_memory_address(
    const db_x64_memory_instruction *memory,
    db_ir_instruction *instruction)
{
    instruction->address.displacement = memory->memory.address.displacement;
    instruction->address.mode = db_x64_ir_lower_address_mode(
        memory->memory.address.mode);
    instruction->address.base_register = memory->memory.address.base_register;
    instruction->address.index_register = memory->memory.address.index_register;
    instruction->address.scale = memory->memory.address.scale;
}

db_x64_ir_lower_status db_x64_ir_lower_emit_record_internal(
    const db_x64_block_record *record,
    db_ir_instruction *instruction)
{
    db_x64_ir_lower_initial_instruction(instruction, record);

    if (record->path == DB_X64_BLOCK_PATH_MEMORY) {
        db_x64_ir_lower_copy_memory_address(&record->memory, instruction);

        switch (record->memory.kind) {
        case DB_X64_MEMORY_INSTRUCTION_LOAD_R64:
            instruction->opcode = DB_IR_OPCODE_LOAD_U64;
            instruction->width = DB_IR_WIDTH_64;
            instruction->destination_register = record->memory.register_operand;
            return DB_X64_IR_LOWER_OK;

        case DB_X64_MEMORY_INSTRUCTION_STORE_R64:
            instruction->opcode = DB_IR_OPCODE_STORE_U64;
            instruction->width = DB_IR_WIDTH_64;
            instruction->source_register = record->memory.register_operand;
            return DB_X64_IR_LOWER_OK;

        case DB_X64_MEMORY_INSTRUCTION_CALL_RM64:
            instruction->opcode = DB_IR_OPCODE_CALL_INDIRECT;
            return DB_X64_IR_LOWER_OK;

        case DB_X64_MEMORY_INSTRUCTION_JMP_RM64:
            instruction->opcode = DB_IR_OPCODE_JUMP_INDIRECT;
            return DB_X64_IR_LOWER_OK;
        }

        return DB_X64_IR_LOWER_UNSUPPORTED_RECORD;
    }

    switch (record->scalar.kind) {
    case DB_X64_INSTRUCTION_NOP:
        instruction->opcode = DB_IR_OPCODE_NOP;
        return DB_X64_IR_LOWER_OK;

    case DB_X64_INSTRUCTION_MOV_R64_IMM64:
        instruction->opcode = DB_IR_OPCODE_MOVE_IMMEDIATE;
        instruction->width = DB_IR_WIDTH_64;
        instruction->destination_register =
            record->scalar.destination_register;
        instruction->immediate = record->scalar.immediate;
        return DB_X64_IR_LOWER_OK;

    case DB_X64_INSTRUCTION_MOV_R64_R64:
        instruction->opcode = DB_IR_OPCODE_MOVE_REGISTER;
        instruction->width = DB_IR_WIDTH_64;
        instruction->destination_register =
            record->scalar.destination_register;
        instruction->source_register = record->scalar.source_register;
        return DB_X64_IR_LOWER_OK;

    case DB_X64_INSTRUCTION_XOR_R32_R32:
    case DB_X64_INSTRUCTION_XOR_R64_R64:
        instruction->opcode = DB_IR_OPCODE_XOR;
        instruction->width =
            record->scalar.kind == DB_X64_INSTRUCTION_XOR_R32_R32
                ? DB_IR_WIDTH_32
                : DB_IR_WIDTH_64;
        instruction->destination_register =
            record->scalar.destination_register;
        instruction->source_register = record->scalar.source_register;
        instruction->flag_effect = DB_IR_FLAG_EFFECT_LOGIC_RESULT;
        return DB_X64_IR_LOWER_OK;

    case DB_X64_INSTRUCTION_CALL_REL32:
        instruction->opcode = DB_IR_OPCODE_CALL_DIRECT;
        return DB_X64_IR_LOWER_OK;

    case DB_X64_INSTRUCTION_RET:
        instruction->opcode = DB_IR_OPCODE_RETURN;
        return DB_X64_IR_LOWER_OK;

    case DB_X64_INSTRUCTION_JMP_REL8:
    case DB_X64_INSTRUCTION_JMP_REL32:
        instruction->opcode = DB_IR_OPCODE_JUMP_DIRECT;
        return DB_X64_IR_LOWER_OK;

    case DB_X64_INSTRUCTION_JCC_REL8:
    case DB_X64_INSTRUCTION_JCC_REL32:
        instruction->opcode = DB_IR_OPCODE_JUMP_CONDITIONAL;
        instruction->condition =
            db_x64_ir_lower_condition(record->scalar.condition);
        return DB_X64_IR_LOWER_OK;

    case DB_X64_INSTRUCTION_INT3:
        instruction->opcode = DB_IR_OPCODE_TRAP;
        instruction->trap = DB_IR_TRAP_BREAKPOINT;
        return DB_X64_IR_LOWER_OK;

    case DB_X64_INSTRUCTION_UD2:
        instruction->opcode = DB_IR_OPCODE_TRAP;
        instruction->trap = DB_IR_TRAP_INVALID_OPCODE;
        return DB_X64_IR_LOWER_OK;

    case DB_X64_INSTRUCTION_SYSCALL:
        instruction->opcode = DB_IR_OPCODE_TRAP;
        instruction->trap = DB_IR_TRAP_SYSCALL;
        return DB_X64_IR_LOWER_OK;
    }

    return DB_X64_IR_LOWER_UNSUPPORTED_RECORD;
}
