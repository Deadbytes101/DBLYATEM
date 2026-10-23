#include "x64_arm64_plan_internal.h"

#include <stddef.h>
#include <stdint.h>

static db_x64_arm64_plan_record initial(
    const db_arm64_select_record *selection,
    uint32_t source_select_index,
    db_x64_arm64_plan_opcode opcode)
{
    db_x64_arm64_plan_record record = {0};
    record.source_rip = selection->source_rip;
    record.source_select_index = source_select_index;
    record.opcode = opcode;
    record.helper = DB_X64_ARM64_HELPER_NONE;
    record.width = DB_IR_WIDTH_NONE;
    record.condition = (db_ir_condition)DB_IR_CONDITION_NONE;
    record.trap = DB_IR_TRAP_NONE;
    record.address_mode = DB_IR_ADDRESS_NONE;
    record.host_destination = DB_X64_ARM64_HOST_NONE;
    record.host_source = DB_X64_ARM64_HOST_NONE;
    record.host_auxiliary = DB_X64_ARM64_HOST_NONE;
    record.guest_register = DB_X64_ARM64_GUEST_NONE;
    record.base_guest_register = DB_X64_ARM64_GUEST_NONE;
    record.index_guest_register = DB_X64_ARM64_GUEST_NONE;
    record.scale = UINT8_C(1);
    return record;
}

static uint32_t materialize(const db_arm64_select_record *selection,
                            uint32_t source_select_index,
                            uint64_t value,
                            uint8_t host_destination,
                            db_x64_arm64_plan_record *records)
{
    uint32_t cursor = 0u;
    uint32_t chunk_index;
    bool started = false;

    for (chunk_index = 0u; chunk_index < 4u; ++chunk_index) {
        const uint16_t chunk =
            (uint16_t)((value >> (chunk_index * 16u)) & UINT64_C(0xffff));
        db_x64_arm64_plan_record record;

        if (chunk == 0u && (value != 0u || started)) continue;
        record = initial(selection, source_select_index,
                         started ? DB_X64_ARM64_PLAN_MOVK_U16
                                 : DB_X64_ARM64_PLAN_MOVZ_U16);
        record.immediate16 = chunk;
        record.shift = (uint8_t)(chunk_index * 16u);
        record.host_destination = host_destination;
        records[cursor++] = record;
        started = true;
    }

    if (!started) {
        db_x64_arm64_plan_record record = initial(
            selection, source_select_index, DB_X64_ARM64_PLAN_MOVZ_U16);
        record.host_destination = host_destination;
        records[cursor++] = record;
    }
    return cursor;
}

static db_x64_arm64_plan_record state_record(
    const db_arm64_select_record *selection,
    uint32_t source_select_index,
    db_x64_arm64_plan_opcode opcode,
    uint8_t guest_register,
    uint8_t host_register)
{
    db_x64_arm64_plan_record record =
        initial(selection, source_select_index, opcode);
    uint32_t offset = 0u;
    (void)db_x64_arm64_plan_state_offset_internal(guest_register, &offset);
    record.guest_state_offset = offset;
    record.guest_register = guest_register;
    record.width = DB_IR_WIDTH_64;
    if (opcode == DB_X64_ARM64_PLAN_LOAD_STATE_U64) {
        record.host_destination = host_register;
    } else {
        record.host_source = host_register;
    }
    return record;
}

static db_x64_arm64_plan_record helper_record(
    const db_arm64_select_record *selection,
    uint32_t source_select_index,
    db_x64_arm64_helper helper,
    bool terminal)
{
    db_x64_arm64_plan_record record = initial(
        selection, source_select_index,
        terminal ? DB_X64_ARM64_PLAN_TAIL_HELPER
                 : DB_X64_ARM64_PLAN_CALL_HELPER);
    record.helper = helper;
    record.terminal = terminal ? UINT8_C(1) : UINT8_C(0);
    return record;
}

void db_x64_arm64_plan_emit_internal(
    const db_arm64_select_record *selection,
    uint32_t source_select_index,
    db_x64_arm64_plan_record *records)
{
    db_x64_arm64_plan_record record;
    uint32_t cursor;

    switch (selection->opcode) {
    case DB_ARM64_SELECT_NOP:
        records[0] = initial(selection, source_select_index,
                             DB_X64_ARM64_PLAN_NOP);
        return;

    case DB_ARM64_SELECT_MATERIALIZE_U64:
        cursor = materialize(selection, source_select_index, selection->value,
                             DB_X64_ARM64_HOST_PRIMARY, records);
        records[cursor] = state_record(
            selection, source_select_index, DB_X64_ARM64_PLAN_STORE_STATE_U64,
            selection->destination_register, DB_X64_ARM64_HOST_PRIMARY);
        return;

    case DB_ARM64_SELECT_MOVE_GUEST_GPR:
        records[0] = state_record(
            selection, source_select_index, DB_X64_ARM64_PLAN_LOAD_STATE_U64,
            selection->source_register, DB_X64_ARM64_HOST_PRIMARY);
        records[1] = state_record(
            selection, source_select_index, DB_X64_ARM64_PLAN_STORE_STATE_U64,
            selection->destination_register, DB_X64_ARM64_HOST_PRIMARY);
        return;

    case DB_ARM64_SELECT_EOR_GUEST_GPR:
        records[0] = state_record(
            selection, source_select_index, DB_X64_ARM64_PLAN_LOAD_STATE_U64,
            selection->destination_register, DB_X64_ARM64_HOST_PRIMARY);
        records[1] = state_record(
            selection, source_select_index, DB_X64_ARM64_PLAN_LOAD_STATE_U64,
            selection->source_register, DB_X64_ARM64_HOST_SECONDARY);
        record = initial(selection, source_select_index,
                         DB_X64_ARM64_PLAN_EOR_REGISTER);
        record.width = selection->width;
        record.host_destination = DB_X64_ARM64_HOST_PRIMARY;
        record.host_source = DB_X64_ARM64_HOST_PRIMARY;
        record.host_auxiliary = DB_X64_ARM64_HOST_SECONDARY;
        records[2] = record;
        records[3] = state_record(
            selection, source_select_index, DB_X64_ARM64_PLAN_STORE_STATE_U64,
            selection->destination_register, DB_X64_ARM64_HOST_PRIMARY);
        return;

    case DB_ARM64_SELECT_SUB_GUEST_IMM8:
        cursor = materialize(selection, source_select_index,
                             selection->value, DB_X64_ARM64_HELPER_ARG0,
                             records);
        cursor += materialize(selection, source_select_index,
                              (uint64_t)selection->destination_register,
                              DB_X64_ARM64_HELPER_ARG1, &records[cursor]);
        records[cursor] = helper_record(
            selection, source_select_index,
            DB_X64_ARM64_HELPER_SUB_GUEST_IMM8, false);
        return;

    case DB_ARM64_SELECT_UPDATE_LOGIC_FLAGS:
        cursor = materialize(selection,
                             source_select_index,
                             (uint64_t)(uint32_t)selection->width,
                             DB_X64_ARM64_HELPER_ARG0,
                             records);
        cursor += materialize(selection,
                              source_select_index,
                              (uint64_t)selection->source_register,
                              DB_X64_ARM64_HELPER_ARG1,
                              &records[cursor]);
        record = helper_record(selection, source_select_index,
                               DB_X64_ARM64_HELPER_UPDATE_LOGIC_FLAGS, false);
        record.width = selection->width;
        record.guest_register = selection->source_register;
        records[cursor] = record;
        return;

    case DB_ARM64_SELECT_FORM_GUEST_ADDRESS:
        cursor = materialize(selection,
                             source_select_index,
                             selection->value,
                             DB_X64_ARM64_HELPER_ARG0,
                             records);
        cursor += materialize(selection,
                              source_select_index,
                              (uint64_t)selection->displacement,
                              DB_X64_ARM64_HELPER_ARG1,
                              &records[cursor]);
        cursor += materialize(selection,
                              source_select_index,
                              (uint64_t)(uint32_t)selection->address_mode,
                              DB_X64_ARM64_HELPER_ARG2,
                              &records[cursor]);
        cursor += materialize(selection,
                              source_select_index,
                              (uint64_t)selection->base_register,
                              DB_X64_ARM64_HELPER_ARG3,
                              &records[cursor]);
        cursor += materialize(selection,
                              source_select_index,
                              (uint64_t)selection->index_register,
                              DB_X64_ARM64_HELPER_ARG4,
                              &records[cursor]);
        cursor += materialize(selection,
                              source_select_index,
                              (uint64_t)selection->scale,
                              DB_X64_ARM64_HELPER_ARG5,
                              &records[cursor]);
        record = helper_record(selection, source_select_index,
                               DB_X64_ARM64_HELPER_FORM_GUEST_ADDRESS, false);
        record.value = selection->value;
        record.displacement = selection->displacement;
        record.address_mode = selection->address_mode;
        record.base_guest_register = selection->base_register;
        record.index_guest_register = selection->index_register;
        record.scale = selection->scale;
        record.host_destination = DB_X64_ARM64_HOST_PRIMARY;
        records[cursor] = record;
        return;

    case DB_ARM64_SELECT_LOAD_U64:
        record = helper_record(selection, source_select_index,
                               DB_X64_ARM64_HELPER_READ_GUEST_U64, false);
        record.host_source = DB_X64_ARM64_HOST_PRIMARY;
        record.host_destination = DB_X64_ARM64_HOST_SECONDARY;
        records[0] = record;
        records[1] = state_record(
            selection, source_select_index, DB_X64_ARM64_PLAN_STORE_STATE_U64,
            selection->destination_register, DB_X64_ARM64_HOST_SECONDARY);
        return;

    case DB_ARM64_SELECT_LOAD_U64_TARGET:
        record = helper_record(selection, source_select_index,
                               DB_X64_ARM64_HELPER_READ_GUEST_U64, false);
        record.host_source = DB_X64_ARM64_HOST_PRIMARY;
        record.host_destination = DB_X64_ARM64_HOST_TARGET;
        records[0] = record;
        return;

    case DB_ARM64_SELECT_STORE_U64:
        records[0] = state_record(
            selection, source_select_index, DB_X64_ARM64_PLAN_LOAD_STATE_U64,
            selection->source_register, DB_X64_ARM64_HOST_SECONDARY);
        record = helper_record(selection, source_select_index,
                               DB_X64_ARM64_HELPER_WRITE_GUEST_U64, false);
        record.host_source = DB_X64_ARM64_HOST_PRIMARY;
        record.host_auxiliary = DB_X64_ARM64_HOST_SECONDARY;
        records[1] = record;
        return;

    case DB_ARM64_SELECT_PUSH_GUEST_U64:
        cursor = materialize(selection, source_select_index, selection->value,
                             DB_X64_ARM64_HOST_SECONDARY, records);
        record = helper_record(selection, source_select_index,
                               DB_X64_ARM64_HELPER_PUSH_GUEST_U64, false);
        record.host_source = DB_X64_ARM64_HOST_SECONDARY;
        records[cursor] = record;
        return;

    case DB_ARM64_SELECT_POP_GUEST_U64:
        record = helper_record(selection, source_select_index,
                               DB_X64_ARM64_HELPER_POP_GUEST_U64, false);
        record.host_destination = DB_X64_ARM64_HOST_TARGET;
        records[0] = record;
        return;

    case DB_ARM64_SELECT_BRANCH_GUEST_DIRECT:
        record = helper_record(selection, source_select_index,
                               DB_X64_ARM64_HELPER_DISPATCH_DIRECT, true);
        record.value = selection->value;
        records[0] = record;
        return;

    case DB_ARM64_SELECT_BRANCH_GUEST_TARGET:
        record = helper_record(selection, source_select_index,
                               DB_X64_ARM64_HELPER_DISPATCH_TARGET, true);
        record.host_source = DB_X64_ARM64_HOST_TARGET;
        records[0] = record;
        return;

    case DB_ARM64_SELECT_TEST_GUEST_CONDITION:
        cursor = materialize(selection,
                             source_select_index,
                             (uint64_t)(uint32_t)selection->condition,
                             DB_X64_ARM64_HELPER_ARG0,
                             records);
        record = helper_record(selection, source_select_index,
                               DB_X64_ARM64_HELPER_TEST_GUEST_CONDITION, false);
        record.condition = selection->condition;
        record.host_destination = DB_X64_ARM64_HOST_TARGET;
        records[cursor] = record;
        return;

    case DB_ARM64_SELECT_BRANCH_IF_TRUE:
        record = helper_record(
            selection, source_select_index,
            DB_X64_ARM64_HELPER_DISPATCH_CONDITIONAL, true);
        record.value = selection->value;
        record.condition = selection->condition;
        record.host_source = DB_X64_ARM64_HOST_TARGET;
        records[0] = record;
        return;

    case DB_ARM64_SELECT_RAISE_TRAP:
        record = helper_record(selection, source_select_index,
                               DB_X64_ARM64_HELPER_RAISE_TRAP, true);
        record.trap = selection->trap;
        records[0] = record;
        return;
    }
}
