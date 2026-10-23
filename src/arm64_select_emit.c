#include "arm64_select_internal.h"

#include <stddef.h>
#include <stdint.h>

static db_arm64_select_record db_arm64_select_initial(
    const db_ir_instruction *instruction,
    uint32_t source_ir_index,
    db_arm64_select_opcode opcode)
{
    db_arm64_select_record record = {0};

    record.source_rip = instruction->source_rip;
    record.source_ir_index = source_ir_index;
    record.opcode = opcode;
    record.width = DB_IR_WIDTH_NONE;
    record.condition = (db_ir_condition)DB_IR_CONDITION_NONE;
    record.trap = DB_IR_TRAP_NONE;
    record.address_mode = DB_IR_ADDRESS_NONE;
    record.destination_register = DB_IR_REGISTER_NONE;
    record.source_register = DB_IR_REGISTER_NONE;
    record.base_register = DB_IR_REGISTER_NONE;
    record.index_register = DB_IR_REGISTER_NONE;
    record.scale = UINT8_C(1);
    record.temp = DB_ARM64_SELECT_TEMP_NONE;
    return record;
}

static void db_arm64_select_copy_address(
    db_arm64_select_record *record,
    const db_ir_instruction *instruction)
{
    record->value =
        instruction->address.mode == DB_IR_ADDRESS_IP_RELATIVE
            ? instruction->fallthrough_rip
            : UINT64_C(0);
    record->displacement = instruction->address.displacement;
    record->address_mode = instruction->address.mode;
    record->base_register = instruction->address.base_register;
    record->index_register = instruction->address.index_register;
    record->scale = instruction->address.scale;
    record->temp = DB_ARM64_SELECT_TEMP_ADDRESS;
}

static db_arm64_select_record db_arm64_select_form_address(
    const db_ir_instruction *instruction,
    uint32_t source_ir_index)
{
    db_arm64_select_record record = db_arm64_select_initial(
        instruction, source_ir_index, DB_ARM64_SELECT_FORM_GUEST_ADDRESS);

    db_arm64_select_copy_address(&record, instruction);
    return record;
}

void db_arm64_select_emit_instruction_internal(
    const db_ir_instruction *instruction,
    uint32_t source_ir_index,
    db_arm64_select_record *records)
{
    db_arm64_select_record first;
    db_arm64_select_record second;
    db_arm64_select_record third;
    db_arm64_select_record fourth;

    switch (instruction->opcode) {
    case DB_IR_OPCODE_NOP:
        records[0] = db_arm64_select_initial(
            instruction, source_ir_index, DB_ARM64_SELECT_NOP);
        return;

    case DB_IR_OPCODE_MOVE_IMMEDIATE:
        first = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_MATERIALIZE_U64);
        first.value = instruction->immediate;
        first.width = instruction->width;
        first.destination_register = instruction->destination_register;
        records[0] = first;
        return;

    case DB_IR_OPCODE_MOVE_REGISTER:
        first = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_MOVE_GUEST_GPR);
        first.width = instruction->width;
        first.destination_register = instruction->destination_register;
        first.source_register = instruction->source_register;
        records[0] = first;
        return;

    case DB_IR_OPCODE_XOR:
        first = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_EOR_GUEST_GPR);
        first.width = instruction->width;
        first.destination_register = instruction->destination_register;
        first.source_register = instruction->source_register;
        second = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_UPDATE_LOGIC_FLAGS);
        second.width = instruction->width;
        second.source_register = instruction->destination_register;
        records[0] = first;
        records[1] = second;
        return;

    case DB_IR_OPCODE_LOAD_U64:
    case DB_IR_OPCODE_STORE_U64:
        first = db_arm64_select_form_address(instruction, source_ir_index);
        second = db_arm64_select_initial(
            instruction, source_ir_index,
            instruction->opcode == DB_IR_OPCODE_LOAD_U64
                ? DB_ARM64_SELECT_LOAD_U64
                : DB_ARM64_SELECT_STORE_U64);
        second.width = DB_IR_WIDTH_64;
        second.temp = DB_ARM64_SELECT_TEMP_ADDRESS;
        if (instruction->opcode == DB_IR_OPCODE_LOAD_U64) {
            second.destination_register = instruction->destination_register;
        } else {
            second.source_register = instruction->source_register;
        }
        records[0] = first;
        records[1] = second;
        return;

    case DB_IR_OPCODE_CALL_DIRECT:
        first = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_PUSH_GUEST_U64);
        first.value = instruction->fallthrough_rip;
        second = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_BRANCH_GUEST_DIRECT);
        second.value = instruction->direct_target;
        records[0] = first;
        records[1] = second;
        return;

    case DB_IR_OPCODE_CALL_INDIRECT:
        first = db_arm64_select_form_address(instruction, source_ir_index);
        second = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_LOAD_U64_TARGET);
        second.width = DB_IR_WIDTH_64;
        second.temp = DB_ARM64_SELECT_TEMP_ADDRESS;
        third = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_PUSH_GUEST_U64);
        third.value = instruction->fallthrough_rip;
        fourth = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_BRANCH_GUEST_TARGET);
        fourth.temp = DB_ARM64_SELECT_TEMP_TARGET;
        records[0] = first;
        records[1] = second;
        records[2] = third;
        records[3] = fourth;
        return;

    case DB_IR_OPCODE_RETURN:
        first = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_POP_GUEST_U64);
        first.temp = DB_ARM64_SELECT_TEMP_TARGET;
        second = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_BRANCH_GUEST_TARGET);
        second.temp = DB_ARM64_SELECT_TEMP_TARGET;
        records[0] = first;
        records[1] = second;
        return;

    case DB_IR_OPCODE_JUMP_DIRECT:
        first = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_BRANCH_GUEST_DIRECT);
        first.value = instruction->direct_target;
        records[0] = first;
        return;

    case DB_IR_OPCODE_JUMP_INDIRECT:
        first = db_arm64_select_form_address(instruction, source_ir_index);
        second = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_LOAD_U64_TARGET);
        second.width = DB_IR_WIDTH_64;
        second.temp = DB_ARM64_SELECT_TEMP_ADDRESS;
        third = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_BRANCH_GUEST_TARGET);
        third.temp = DB_ARM64_SELECT_TEMP_TARGET;
        records[0] = first;
        records[1] = second;
        records[2] = third;
        return;

    case DB_IR_OPCODE_JUMP_CONDITIONAL:
        first = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_TEST_GUEST_CONDITION);
        first.condition = instruction->condition;
        first.temp = DB_ARM64_SELECT_TEMP_PREDICATE;
        second = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_BRANCH_IF_TRUE);
        second.value = instruction->direct_target;
        second.condition = instruction->condition;
        second.temp = DB_ARM64_SELECT_TEMP_PREDICATE;
        records[0] = first;
        records[1] = second;
        return;

    case DB_IR_OPCODE_TRAP:
        first = db_arm64_select_initial(
            instruction, source_ir_index,
            DB_ARM64_SELECT_RAISE_TRAP);
        first.trap = instruction->trap;
        records[0] = first;
        return;
    }
}
