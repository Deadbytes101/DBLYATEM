#include "dblyat/x64_ir_lower.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define CHECK(value) do { if (!(value)) return 0; } while (0)

static void fill(void *data, size_t size, uint8_t value)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t index;
    for (index = 0u; index < size; ++index) {
        bytes[index] = value;
    }
}

static int same(const void *left, const void *right, size_t size)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    size_t index;
    for (index = 0u; index < size; ++index) {
        if (a[index] != b[index]) return 0;
    }
    return 1;
}

static db_x64_block_record scalar_record(uint64_t rip,
                                         db_x64_instruction_kind kind,
                                         uint8_t length,
                                         db_x64_block_control control)
{
    db_x64_block_record record;
    fill(&record, sizeof(record), 0u);
    record.rip = rip;
    record.fallthrough_rip = rip + length;
    record.path = DB_X64_BLOCK_PATH_SCALAR;
    record.control = control;
    record.scalar.kind = kind;
    record.scalar.length = length;
    record.scalar.destination_register = DB_X64_REGISTER_NONE;
    record.scalar.source_register = DB_X64_REGISTER_NONE;
    record.scalar.condition = DB_X64_CONDITION_NONE;
    return record;
}

static db_x64_block_record memory_record(uint64_t rip,
                                         db_x64_memory_instruction_kind kind,
                                         uint8_t reg)
{
    db_x64_block_record record;
    fill(&record, sizeof(record), 0u);
    record.rip = rip;
    record.fallthrough_rip = rip + UINT64_C(3);
    record.path = DB_X64_BLOCK_PATH_MEMORY;
    record.control = DB_X64_BLOCK_CONTROL_LINEAR;
    record.memory.kind = kind;
    record.memory.rex = UINT8_C(0x48);
    record.memory.opcode =
        kind == DB_X64_MEMORY_INSTRUCTION_LOAD_R64
            ? UINT8_C(0x8b) : UINT8_C(0x89);
    record.memory.register_operand = reg;
    record.memory.instruction_length = UINT8_C(3);
    record.memory.memory.address.mode = DB_X64_ADDRESS_BASE_INDEX;
    record.memory.memory.address.base_register = UINT8_C(0);
    record.memory.memory.address.index_register = DB_X64_REGISTER_NONE;
    record.memory.memory.address.scale = UINT8_C(1);
    record.memory.memory.address.instruction_length = 0u;
    record.memory.memory.register_operand = reg;
    record.memory.memory.encoded_length = UINT8_C(1);
    record.memory.memory.instruction_length = UINT8_C(3);
    record.memory.memory.modrm = (uint8_t)(reg << 3u);
    record.memory.memory.sib = DB_X64_MEMORY_NO_SIB;
    return record;
}

static void set_direct(db_x64_block_record *record, int64_t relative)
{
    record->scalar.relative = relative;
    record->direct_target =
        relative >= 0
            ? record->fallthrough_rip + (uint64_t)relative
            : record->fallthrough_rip -
                  ((uint64_t)(-(relative + 1)) + UINT64_C(1));
    record->has_direct_target = UINT8_C(1);
}

static int test_mixed_block(void)
{
    db_x64_block_record records[4];
    db_ir_instruction output[6];
    db_ir_instruction untouched;
    db_x64_ir_lower_summary summary;

    records[0] = scalar_record(UINT64_C(0x1000),
                               DB_X64_INSTRUCTION_MOV_R64_IMM64,
                               UINT8_C(10),
                               DB_X64_BLOCK_CONTROL_LINEAR);
    records[0].scalar.rex = UINT8_C(0x48);
    records[0].scalar.destination_register = UINT8_C(2);
    records[0].scalar.immediate = UINT64_C(0x1122334455667788);

    records[1] = memory_record(UINT64_C(0x100a),
                               DB_X64_MEMORY_INSTRUCTION_LOAD_R64,
                               UINT8_C(3));

    records[2] = scalar_record(UINT64_C(0x100d),
                               DB_X64_INSTRUCTION_XOR_R64_R64,
                               UINT8_C(3),
                               DB_X64_BLOCK_CONTROL_LINEAR);
    records[2].scalar.rex = UINT8_C(0x48);
    records[2].scalar.destination_register = UINT8_C(1);
    records[2].scalar.source_register = UINT8_C(2);

    records[3] = scalar_record(UINT64_C(0x1010),
                               DB_X64_INSTRUCTION_JCC_REL8,
                               UINT8_C(2),
                               DB_X64_BLOCK_CONTROL_CONDITIONAL_JUMP);
    records[3].scalar.condition = UINT8_C(4);
    set_direct(&records[3], INT64_C(16));

    fill(output, sizeof(output), UINT8_C(0xa5));
    untouched = output[4];
    fill(&summary, sizeof(summary), UINT8_C(0x5a));

    CHECK(db_x64_ir_lower_block(records, 4u, output, 6u, &summary) ==
          DB_X64_IR_LOWER_OK);
    CHECK(output[0].opcode == DB_IR_OPCODE_MOVE_IMMEDIATE);
    CHECK(output[0].width == DB_IR_WIDTH_64);
    CHECK(output[0].destination_register == UINT8_C(2));
    CHECK(output[0].immediate == UINT64_C(0x1122334455667788));
    CHECK(output[1].opcode == DB_IR_OPCODE_LOAD_U64);
    CHECK(output[1].destination_register == UINT8_C(3));
    CHECK(output[1].address.mode == DB_IR_ADDRESS_BASE_INDEX);
    CHECK(output[1].address.base_register == UINT8_C(0));
    CHECK(output[1].address.index_register == DB_IR_REGISTER_NONE);
    CHECK(output[2].opcode == DB_IR_OPCODE_XOR);
    CHECK(output[2].width == DB_IR_WIDTH_64);
    CHECK(output[2].flag_effect == DB_IR_FLAG_EFFECT_LOGIC_RESULT);
    CHECK(output[3].opcode == DB_IR_OPCODE_JUMP_CONDITIONAL);
    CHECK(output[3].condition == DB_IR_CONDITION_EQUAL);
    CHECK(output[3].has_direct_target == UINT8_C(1));
    CHECK(output[3].direct_target == UINT64_C(0x1022));
    CHECK(summary.source_rip == UINT64_C(0x1000));
    CHECK(summary.final_fallthrough_rip == UINT64_C(0x1012));
    CHECK(summary.record_count == 4u);
    CHECK(summary.instruction_count == 4u);
    CHECK(summary.has_terminator == UINT8_C(1));
    CHECK(summary.terminator_opcode == DB_IR_OPCODE_JUMP_CONDITIONAL);
    CHECK(same(&output[4], &untouched, sizeof(untouched)) != 0);
    return 1;
}

static int test_scalar_semantics(void)
{
    db_x64_block_record records[5];
    db_ir_instruction output[5];
    db_x64_ir_lower_summary summary;

    records[0] = scalar_record(UINT64_C(0x2000),
                               DB_X64_INSTRUCTION_NOP, 1u,
                               DB_X64_BLOCK_CONTROL_LINEAR);
    records[1] = scalar_record(UINT64_C(0x2001),
                               DB_X64_INSTRUCTION_MOV_R64_R64, 3u,
                               DB_X64_BLOCK_CONTROL_LINEAR);
    records[1].scalar.rex = UINT8_C(0x48);
    records[1].scalar.destination_register = UINT8_C(8);
    records[1].scalar.source_register = UINT8_C(9);
    records[2] = scalar_record(UINT64_C(0x2004),
                               DB_X64_INSTRUCTION_XOR_R32_R32, 2u,
                               DB_X64_BLOCK_CONTROL_LINEAR);
    records[2].scalar.destination_register = UINT8_C(0);
    records[2].scalar.source_register = UINT8_C(0);
    records[3] = memory_record(UINT64_C(0x2006),
                               DB_X64_MEMORY_INSTRUCTION_STORE_R64,
                               UINT8_C(7));
    records[4] = scalar_record(UINT64_C(0x2009),
                               DB_X64_INSTRUCTION_CALL_REL32, 5u,
                               DB_X64_BLOCK_CONTROL_DIRECT_CALL);
    set_direct(&records[4], INT64_C(-16));

    CHECK(db_x64_ir_lower_block(records, 5u, output, 5u, &summary) ==
          DB_X64_IR_LOWER_OK);
    CHECK(output[0].opcode == DB_IR_OPCODE_NOP);
    CHECK(output[1].opcode == DB_IR_OPCODE_MOVE_REGISTER);
    CHECK(output[1].destination_register == UINT8_C(8));
    CHECK(output[1].source_register == UINT8_C(9));
    CHECK(output[2].opcode == DB_IR_OPCODE_XOR);
    CHECK(output[2].width == DB_IR_WIDTH_32);
    CHECK(output[3].opcode == DB_IR_OPCODE_STORE_U64);
    CHECK(output[3].source_register == UINT8_C(7));
    CHECK(output[4].opcode == DB_IR_OPCODE_CALL_DIRECT);
    CHECK(output[4].direct_target == UINT64_C(0x1ffe));
    return 1;
}

static int test_traps_return_and_jump(void)
{
    db_x64_block_record record;
    db_ir_instruction output;
    db_x64_ir_lower_summary summary;

    record = scalar_record(UINT64_C(0x3000), DB_X64_INSTRUCTION_RET, 1u,
                           DB_X64_BLOCK_CONTROL_RETURN);
    CHECK(db_x64_ir_lower_block(&record, 1u, &output, 1u, &summary) ==
          DB_X64_IR_LOWER_OK);
    CHECK(output.opcode == DB_IR_OPCODE_RETURN);

    record = scalar_record(UINT64_C(0x3010), DB_X64_INSTRUCTION_JMP_REL32, 5u,
                           DB_X64_BLOCK_CONTROL_DIRECT_JUMP);
    set_direct(&record, INT64_C(32));
    CHECK(db_x64_ir_lower_block(&record, 1u, &output, 1u, &summary) ==
          DB_X64_IR_LOWER_OK);
    CHECK(output.opcode == DB_IR_OPCODE_JUMP_DIRECT);

    record = scalar_record(UINT64_C(0x3020), DB_X64_INSTRUCTION_INT3, 1u,
                           DB_X64_BLOCK_CONTROL_TRAP);
    CHECK(db_x64_ir_lower_block(&record, 1u, &output, 1u, &summary) ==
          DB_X64_IR_LOWER_OK);
    CHECK(output.trap == DB_IR_TRAP_BREAKPOINT);

    record = scalar_record(UINT64_C(0x3030), DB_X64_INSTRUCTION_UD2, 2u,
                           DB_X64_BLOCK_CONTROL_TRAP);
    CHECK(db_x64_ir_lower_block(&record, 1u, &output, 1u, &summary) ==
          DB_X64_IR_LOWER_OK);
    CHECK(output.trap == DB_IR_TRAP_INVALID_OPCODE);

    record = scalar_record(UINT64_C(0x3040), DB_X64_INSTRUCTION_SYSCALL, 2u,
                           DB_X64_BLOCK_CONTROL_TRAP);
    CHECK(db_x64_ir_lower_block(&record, 1u, &output, 1u, &summary) ==
          DB_X64_IR_LOWER_OK);
    CHECK(output.trap == DB_IR_TRAP_SYSCALL);
    return 1;
}

static int test_address_modes(void)
{
    db_x64_block_record records[2];
    db_ir_instruction output[2];
    db_x64_ir_lower_summary summary;

    records[0] = memory_record(UINT64_C(0x4000),
                               DB_X64_MEMORY_INSTRUCTION_LOAD_R64, 1u);
    records[0].memory.memory.address.mode = DB_X64_ADDRESS_RIP_RELATIVE;
    records[0].memory.memory.address.base_register = DB_X64_REGISTER_NONE;
    records[0].memory.memory.address.index_register = DB_X64_REGISTER_NONE;
    records[0].memory.memory.address.instruction_length = 3u;
    records[0].memory.memory.address.displacement = INT64_C(64);
    records[0].memory.memory.encoded_length = 1u;
    records[0].memory.memory.modrm = UINT8_C(0x0d);

    records[1] = memory_record(UINT64_C(0x4003),
                               DB_X64_MEMORY_INSTRUCTION_STORE_R64, 2u);
    records[1].memory.memory.address.mode = DB_X64_ADDRESS_ABSOLUTE;
    records[1].memory.memory.address.base_register = DB_X64_REGISTER_NONE;
    records[1].memory.memory.address.index_register = DB_X64_REGISTER_NONE;
    records[1].memory.memory.address.displacement = INT64_C(0x1234);
    records[1].memory.memory.modrm = UINT8_C(0x14);
    records[1].memory.memory.sib = UINT8_C(0x25);

    CHECK(db_x64_ir_lower_block(records, 2u, output, 2u, &summary) ==
          DB_X64_IR_LOWER_OK);
    CHECK(output[0].address.mode == DB_IR_ADDRESS_IP_RELATIVE);
    CHECK(output[0].address.displacement == INT64_C(64));
    CHECK(output[1].address.mode == DB_IR_ADDRESS_ABSOLUTE);
    CHECK(output[1].address.displacement == INT64_C(0x1234));
    return 1;
}

static int test_all_or_nothing_failures(void)
{
    db_x64_block_record records[2];
    db_ir_instruction output[2];
    db_ir_instruction output_before[2];
    db_x64_ir_lower_summary summary;
    db_x64_ir_lower_summary summary_before;

    records[0] = scalar_record(UINT64_C(0x5000), DB_X64_INSTRUCTION_NOP, 1u,
                               DB_X64_BLOCK_CONTROL_LINEAR);
    records[1] = scalar_record(UINT64_C(0x5001), DB_X64_INSTRUCTION_RET, 1u,
                               DB_X64_BLOCK_CONTROL_RETURN);
    records[1].reserved[2] = UINT8_C(1);
    fill(output, sizeof(output), UINT8_C(0xa5));
    fill(&summary, sizeof(summary), UINT8_C(0x5a));
    output_before[0] = output[0];
    output_before[1] = output[1];
    summary_before = summary;
    CHECK(db_x64_ir_lower_block(records, 2u, output, 2u, &summary) ==
          DB_X64_IR_LOWER_BAD_RECORD);
    CHECK(same(output, output_before, sizeof(output)) != 0);
    CHECK(same(&summary, &summary_before, sizeof(summary)) != 0);

    records[1] = scalar_record(UINT64_C(0x5002), DB_X64_INSTRUCTION_RET, 1u,
                               DB_X64_BLOCK_CONTROL_RETURN);
    CHECK(db_x64_ir_lower_block(records, 2u, output, 2u, &summary) ==
          DB_X64_IR_LOWER_DISCONTINUOUS_BLOCK);
    CHECK(same(output, output_before, sizeof(output)) != 0);

    records[0] = scalar_record(UINT64_C(0x5000), DB_X64_INSTRUCTION_RET, 1u,
                               DB_X64_BLOCK_CONTROL_RETURN);
    records[1] = scalar_record(UINT64_C(0x5001), DB_X64_INSTRUCTION_NOP, 1u,
                               DB_X64_BLOCK_CONTROL_LINEAR);
    CHECK(db_x64_ir_lower_block(records, 2u, output, 2u, &summary) ==
          DB_X64_IR_LOWER_TERMINATOR_ORDER);
    CHECK(same(output, output_before, sizeof(output)) != 0);
    return 1;
}

static int test_contract_failures(void)
{
    db_x64_block_record record = scalar_record(
        UINT64_C(0x6000), DB_X64_INSTRUCTION_NOP, 1u,
        DB_X64_BLOCK_CONTROL_LINEAR);
    db_ir_instruction output[2];
    db_x64_ir_lower_summary summary;

    CHECK(db_x64_ir_lower_block(NULL, 1u, output, 2u, &summary) ==
          DB_X64_IR_LOWER_NULL_ARGUMENT);
    CHECK(db_x64_ir_lower_block(&record, 0u, output, 2u, &summary) ==
          DB_X64_IR_LOWER_BAD_COUNT);
    CHECK(db_x64_ir_lower_block(&record, 1u, output, 0u, &summary) ==
          DB_X64_IR_LOWER_OUTPUT_CAPACITY);
    CHECK(db_x64_ir_lower_block(&record, 1u,
              (db_ir_instruction *)(void *)&record, 1u, &summary) ==
          DB_X64_IR_LOWER_BUFFER_OVERLAP);
    CHECK(db_x64_ir_lower_block(&record, 1u, output, 2u,
              (db_x64_ir_lower_summary *)(void *)output) ==
          DB_X64_IR_LOWER_BUFFER_OVERLAP);
    return 1;
}

static int test_names(void)
{
    CHECK(db_ir_opcode_name(DB_IR_OPCODE_XOR)[0] == 'x');
    CHECK(db_ir_condition_name(DB_IR_CONDITION_EQUAL)[0] == 'e');
    CHECK(db_ir_trap_name(DB_IR_TRAP_SYSCALL)[0] == 's');
    CHECK(db_x64_ir_lower_status_name(DB_X64_IR_LOWER_OK)[0] == 'o');
    return 1;
}

int main(void)
{
    CHECK(test_mixed_block());
    CHECK(test_scalar_semantics());
    CHECK(test_traps_return_and_jump());
    CHECK(test_address_modes());
    CHECK(test_all_or_nothing_failures());
    CHECK(test_contract_failures());
    CHECK(test_names());

    (void)write(STDOUT_FILENO,
                "X64 IR LOWER TEST: PASS\n",
                sizeof("X64 IR LOWER TEST: PASS\n") - 1u);
    return 0;
}
