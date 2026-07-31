#include "dblyat/arm64_select.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define CHECK(v) do { if (!(v)) return 0; } while (0)

static void fill(void *p, size_t n, uint8_t v)
{
    uint8_t *b = (uint8_t *)p;
    size_t i;
    for (i = 0u; i < n; ++i) b[i] = v;
}

static int same(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    size_t i;
    for (i = 0u; i < n; ++i) if (x[i] != y[i]) return 0;
    return 1;
}

static db_ir_instruction ir(uint64_t rip, uint64_t next, db_ir_opcode op)
{
    db_ir_instruction x;
    fill(&x, sizeof(x), 0u);
    x.source_rip = rip;
    x.fallthrough_rip = next;
    x.opcode = op;
    x.width = DB_IR_WIDTH_NONE;
    x.condition = (db_ir_condition)DB_IR_CONDITION_NONE;
    x.trap = DB_IR_TRAP_NONE;
    x.flag_effect = DB_IR_FLAG_EFFECT_NONE;
    x.destination_register = DB_IR_REGISTER_NONE;
    x.source_register = DB_IR_REGISTER_NONE;
    x.address.mode = DB_IR_ADDRESS_NONE;
    x.address.base_register = DB_IR_REGISTER_NONE;
    x.address.index_register = DB_IR_REGISTER_NONE;
    x.address.scale = UINT8_C(1);
    return x;
}

static void target(db_ir_instruction *x, uint64_t value)
{
    x->direct_target = value;
    x->has_direct_target = UINT8_C(1);
}

static void base_index(db_ir_instruction *x, uint8_t base, uint8_t index,
                       uint8_t scale, int64_t displacement)
{
    x->address.mode = DB_IR_ADDRESS_BASE_INDEX;
    x->address.base_register = base;
    x->address.index_register = index;
    x->address.scale = scale;
    x->address.displacement = displacement;
}

static int mixed(void)
{
    db_ir_instruction in[6];
    db_arm64_select_record out[12];
    db_arm64_select_record untouched;
    db_arm64_select_summary s;

    in[0] = ir(UINT64_C(0x1000), UINT64_C(0x100a),
               DB_IR_OPCODE_MOVE_IMMEDIATE);
    in[0].width = DB_IR_WIDTH_64;
    in[0].destination_register = UINT8_C(2);
    in[0].immediate = UINT64_C(0x1122334455667788);
    in[1] = ir(UINT64_C(0x100a), UINT64_C(0x100d),
               DB_IR_OPCODE_MOVE_REGISTER);
    in[1].width = DB_IR_WIDTH_64;
    in[1].destination_register = UINT8_C(3);
    in[1].source_register = UINT8_C(2);
    in[2] = ir(UINT64_C(0x100d), UINT64_C(0x1010), DB_IR_OPCODE_XOR);
    in[2].width = DB_IR_WIDTH_64;
    in[2].destination_register = UINT8_C(3);
    in[2].source_register = UINT8_C(4);
    in[2].flag_effect = DB_IR_FLAG_EFFECT_LOGIC_RESULT;
    in[3] = ir(UINT64_C(0x1010), UINT64_C(0x1013), DB_IR_OPCODE_LOAD_U64);
    in[3].width = DB_IR_WIDTH_64;
    in[3].destination_register = UINT8_C(5);
    base_index(&in[3], UINT8_C(0), UINT8_C(1), UINT8_C(4), INT64_C(-32));
    in[4] = ir(UINT64_C(0x1013), UINT64_C(0x1016), DB_IR_OPCODE_STORE_U64);
    in[4].width = DB_IR_WIDTH_64;
    in[4].source_register = UINT8_C(6);
    base_index(&in[4], UINT8_C(7), DB_IR_REGISTER_NONE, UINT8_C(1), 16);
    in[5] = ir(UINT64_C(0x1016), UINT64_C(0x1018),
               DB_IR_OPCODE_JUMP_CONDITIONAL);
    in[5].condition = DB_IR_CONDITION_EQUAL;
    target(&in[5], UINT64_C(0x1050));

    fill(out, sizeof(out), UINT8_C(0xa5));
    untouched = out[10];
    CHECK(db_arm64_select_block(in, 6u, out, 12u, &s) == DB_ARM64_SELECT_OK);
    CHECK(s.operation_count == 10u && s.required_capacity == 10u);
    CHECK(s.has_terminator == 1u &&
          s.terminator_opcode == DB_ARM64_SELECT_BRANCH_IF_TRUE);
    CHECK(out[0].opcode == DB_ARM64_SELECT_MATERIALIZE_U64 &&
          out[0].value == UINT64_C(0x1122334455667788));
    CHECK(out[1].opcode == DB_ARM64_SELECT_MOVE_GUEST_GPR);
    CHECK(out[2].opcode == DB_ARM64_SELECT_EOR_GUEST_GPR &&
          out[3].opcode == DB_ARM64_SELECT_UPDATE_LOGIC_FLAGS);
    CHECK(out[4].opcode == DB_ARM64_SELECT_FORM_GUEST_ADDRESS &&
          out[4].base_register == 0u && out[4].index_register == 1u &&
          out[4].scale == 4u && out[4].displacement == -32);
    CHECK(out[5].opcode == DB_ARM64_SELECT_LOAD_U64 &&
          out[5].destination_register == 5u);
    CHECK(out[6].opcode == DB_ARM64_SELECT_FORM_GUEST_ADDRESS &&
          out[7].opcode == DB_ARM64_SELECT_STORE_U64);
    CHECK(out[8].opcode == DB_ARM64_SELECT_TEST_GUEST_CONDITION &&
          out[9].opcode == DB_ARM64_SELECT_BRANCH_IF_TRUE &&
          out[9].value == UINT64_C(0x1050));
    CHECK(same(&out[10], &untouched, sizeof(untouched)) != 0);
    return 1;
}

static int addresses(void)
{
    db_ir_instruction in[2];
    db_arm64_select_record out[4];
    db_arm64_select_summary s;
    in[0] = ir(UINT64_C(0x2000), UINT64_C(0x2007), DB_IR_OPCODE_LOAD_U64);
    in[0].width = DB_IR_WIDTH_64;
    in[0].destination_register = 1u;
    in[0].address.mode = DB_IR_ADDRESS_IP_RELATIVE;
    in[0].address.base_register = DB_IR_REGISTER_NONE;
    in[0].address.index_register = DB_IR_REGISTER_NONE;
    in[0].address.scale = 1u;
    in[0].address.displacement = 64;
    in[1] = ir(UINT64_C(0x2007), UINT64_C(0x200e), DB_IR_OPCODE_STORE_U64);
    in[1].width = DB_IR_WIDTH_64;
    in[1].source_register = 2u;
    in[1].address.mode = DB_IR_ADDRESS_ABSOLUTE;
    in[1].address.base_register = DB_IR_REGISTER_NONE;
    in[1].address.index_register = DB_IR_REGISTER_NONE;
    in[1].address.scale = 1u;
    in[1].address.displacement = INT64_C(0x1234);
    CHECK(db_arm64_select_block(in, 2u, out, 4u, &s) == DB_ARM64_SELECT_OK);
    CHECK(out[0].address_mode == DB_IR_ADDRESS_IP_RELATIVE &&
          out[0].value == UINT64_C(0x2007));
    CHECK(out[2].address_mode == DB_IR_ADDRESS_ABSOLUTE && out[2].value == 0u);
    return 1;
}

static int controls(void)
{
    db_ir_instruction x;
    db_arm64_select_record out[2];
    db_arm64_select_summary s;
    uint32_t c;
    x = ir(UINT64_C(0x3000), UINT64_C(0x3005), DB_IR_OPCODE_CALL_DIRECT);
    target(&x, UINT64_C(0x4000));
    CHECK(db_arm64_select_block(&x, 1u, out, 2u, &s) == DB_ARM64_SELECT_OK);
    CHECK(out[0].opcode == DB_ARM64_SELECT_PUSH_GUEST_U64 &&
          out[0].value == UINT64_C(0x3005) &&
          out[1].opcode == DB_ARM64_SELECT_BRANCH_GUEST_DIRECT);
    x = ir(UINT64_C(0x3010), UINT64_C(0x3011), DB_IR_OPCODE_RETURN);
    CHECK(db_arm64_select_block(&x, 1u, out, 2u, &s) == DB_ARM64_SELECT_OK);
    CHECK(out[0].temp == DB_ARM64_SELECT_TEMP_TARGET &&
          out[1].opcode == DB_ARM64_SELECT_BRANCH_GUEST_TARGET);
    x = ir(UINT64_C(0x3020), UINT64_C(0x3025), DB_IR_OPCODE_JUMP_DIRECT);
    target(&x, UINT64_C(0x5000));
    CHECK(db_arm64_select_block(&x, 1u, out, 2u, &s) == DB_ARM64_SELECT_OK);
    CHECK(out[0].value == UINT64_C(0x5000));
    x = ir(UINT64_C(0x3030), UINT64_C(0x3031), DB_IR_OPCODE_TRAP);
    x.trap = DB_IR_TRAP_BREAKPOINT;
    CHECK(db_arm64_select_block(&x, 1u, out, 2u, &s) == DB_ARM64_SELECT_OK);
    CHECK(out[0].opcode == DB_ARM64_SELECT_RAISE_TRAP);
    for (c = 0u; c < 16u; ++c) {
        x = ir(UINT64_C(0x3040), UINT64_C(0x3042),
               DB_IR_OPCODE_JUMP_CONDITIONAL);
        x.condition = (db_ir_condition)c;
        target(&x, UINT64_C(0x3100));
        CHECK(db_arm64_select_block(&x, 1u, out, 2u, &s) ==
              DB_ARM64_SELECT_OK);
        CHECK(out[0].condition == (db_ir_condition)c &&
              out[1].condition == (db_ir_condition)c);
    }
    return 1;
}

static int failures(void)
{
    db_ir_instruction in[2];
    db_arm64_select_record out[4];
    db_arm64_select_record before[4];
    db_arm64_select_summary s;
    db_arm64_select_summary sb;
    in[0] = ir(UINT64_C(0x5000), UINT64_C(0x5001), DB_IR_OPCODE_NOP);
    in[1] = ir(UINT64_C(0x5001), UINT64_C(0x5004), DB_IR_OPCODE_XOR);
    in[1].width = DB_IR_WIDTH_64;
    in[1].destination_register = 1u;
    in[1].source_register = 2u;
    in[1].flag_effect = DB_IR_FLAG_EFFECT_LOGIC_RESULT;
    in[1].reserved[2] = 1u;
    fill(out, sizeof(out), UINT8_C(0xa5));
    fill(&s, sizeof(s), UINT8_C(0x5a));
    before[0] = out[0]; before[1] = out[1];
    before[2] = out[2]; before[3] = out[3]; sb = s;
    CHECK(db_arm64_select_block(in, 2u, out, 4u, &s) ==
          DB_ARM64_SELECT_BAD_IR);
    CHECK(same(out, before, sizeof(out)) && same(&s, &sb, sizeof(s)));
    in[1].reserved[2] = 0u;
    in[1].source_rip = UINT64_C(0x5002);
    CHECK(db_arm64_select_block(in, 2u, out, 4u, &s) ==
          DB_ARM64_SELECT_DISCONTINUOUS_IR);
    in[0] = ir(UINT64_C(0x5000), UINT64_C(0x5001), DB_IR_OPCODE_RETURN);
    in[1] = ir(UINT64_C(0x5001), UINT64_C(0x5002), DB_IR_OPCODE_NOP);
    CHECK(db_arm64_select_block(in, 2u, out, 4u, &s) ==
          DB_ARM64_SELECT_TERMINATOR_ORDER);
    in[0] = ir(UINT64_C(0x6000), UINT64_C(0x6003), DB_IR_OPCODE_XOR);
    in[0].width = DB_IR_WIDTH_32;
    in[0].destination_register = 0u;
    in[0].source_register = 0u;
    in[0].flag_effect = DB_IR_FLAG_EFFECT_LOGIC_RESULT;
    CHECK(db_arm64_select_block(in, 1u, out, 1u, &s) ==
          DB_ARM64_SELECT_OUTPUT_CAPACITY);
    CHECK(db_arm64_select_block(in, 1u,
              (db_arm64_select_record *)(void *)in, 1u, &s) ==
          DB_ARM64_SELECT_BUFFER_OVERLAP);
    return 1;
}

static int bad_fields(void)
{
    db_ir_instruction x;
    db_arm64_select_record out[2];
    db_arm64_select_summary s;
    x = ir(UINT64_C(0x7000), UINT64_C(0x7001), DB_IR_OPCODE_NOP);
    x.destination_register = 0u;
    CHECK(db_arm64_select_block(&x, 1u, out, 2u, &s) == DB_ARM64_SELECT_BAD_IR);
    x = ir(UINT64_C(0x7000), UINT64_C(0x7007), DB_IR_OPCODE_LOAD_U64);
    x.width = DB_IR_WIDTH_64;
    x.destination_register = 1u;
    x.address.mode = DB_IR_ADDRESS_BASE_INDEX;
    x.address.base_register = DB_IR_REGISTER_NONE;
    x.address.index_register = DB_IR_REGISTER_NONE;
    x.address.scale = 1u;
    CHECK(db_arm64_select_block(&x, 1u, out, 2u, &s) == DB_ARM64_SELECT_BAD_IR);
    x = ir(UINT64_C(0x7000), UINT64_C(0x7002),
           DB_IR_OPCODE_JUMP_CONDITIONAL);
    x.condition = (db_ir_condition)16;
    target(&x, UINT64_C(0x7100));
    CHECK(db_arm64_select_block(&x, 1u, out, 2u, &s) == DB_ARM64_SELECT_BAD_IR);
    x = ir(UINT64_C(0x0000800000000000), UINT64_C(0x7001), DB_IR_OPCODE_NOP);
    CHECK(db_arm64_select_block(&x, 1u, out, 2u, &s) == DB_ARM64_SELECT_BAD_IR);
    return 1;
}

int main(void)
{
    db_ir_instruction x = ir(UINT64_C(0x8000), UINT64_C(0x8001),
                             DB_IR_OPCODE_NOP);
    db_arm64_select_record out[2];
    db_arm64_select_summary s;
    CHECK(mixed());
    CHECK(addresses());
    CHECK(controls());
    CHECK(failures());
    CHECK(bad_fields());
    CHECK(db_arm64_select_block(NULL, 1u, out, 2u, &s) ==
          DB_ARM64_SELECT_NULL_ARGUMENT);
    CHECK(db_arm64_select_block(&x, 0u, out, 2u, &s) ==
          DB_ARM64_SELECT_BAD_COUNT);
    CHECK(db_arm64_select_status_name(DB_ARM64_SELECT_OK)[0] == 'o');
    CHECK(db_arm64_select_opcode_name(DB_ARM64_SELECT_EOR_GUEST_GPR)[0] == 'e');
    (void)write(STDOUT_FILENO, "ARM64 SELECT TEST: PASS\n",
                sizeof("ARM64 SELECT TEST: PASS\n") - 1u);
    return 0;
}
