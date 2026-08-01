#include "dblyat/x64_arm64_plan.h"

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

static db_arm64_select_record select_record(
    uint64_t rip, uint32_t ir_index, db_arm64_select_opcode opcode)
{
    db_arm64_select_record r;
    fill(&r, sizeof(r), 0u);
    r.source_rip = rip;
    r.source_ir_index = ir_index;
    r.opcode = opcode;
    r.width = DB_IR_WIDTH_NONE;
    r.condition = (db_ir_condition)DB_IR_CONDITION_NONE;
    r.trap = DB_IR_TRAP_NONE;
    r.address_mode = DB_IR_ADDRESS_NONE;
    r.destination_register = DB_IR_REGISTER_NONE;
    r.source_register = DB_IR_REGISTER_NONE;
    r.base_register = DB_IR_REGISTER_NONE;
    r.index_register = DB_IR_REGISTER_NONE;
    r.scale = 1u;
    r.temp = DB_ARM64_SELECT_TEMP_NONE;
    return r;
}

static int mixed_plan(void)
{
    db_arm64_select_record in[8];
    db_x64_arm64_plan_record out[24];
    db_x64_arm64_plan_record tail;
    db_x64_arm64_plan_summary s;
    uint32_t gpr2 = (uint32_t)offsetof(db_x64_scalar_state, registers[2]);

    in[0] = select_record(UINT64_C(0x1000), 0u,
                          DB_ARM64_SELECT_MATERIALIZE_U64);
    in[0].value = UINT64_C(0x1122334455667788);
    in[0].width = DB_IR_WIDTH_64;
    in[0].destination_register = 2u;
    in[1] = select_record(UINT64_C(0x100a), 1u,
                          DB_ARM64_SELECT_MOVE_GUEST_GPR);
    in[1].width = DB_IR_WIDTH_64;
    in[1].destination_register = 3u;
    in[1].source_register = 2u;
    in[2] = select_record(UINT64_C(0x100d), 2u,
                          DB_ARM64_SELECT_EOR_GUEST_GPR);
    in[2].width = DB_IR_WIDTH_64;
    in[2].destination_register = 3u;
    in[2].source_register = 4u;
    in[3] = select_record(UINT64_C(0x100d), 2u,
                          DB_ARM64_SELECT_UPDATE_LOGIC_FLAGS);
    in[3].width = DB_IR_WIDTH_64;
    in[3].source_register = 3u;
    in[4] = select_record(UINT64_C(0x1010), 3u,
                          DB_ARM64_SELECT_FORM_GUEST_ADDRESS);
    in[4].address_mode = DB_IR_ADDRESS_BASE_INDEX;
    in[4].base_register = 0u;
    in[4].index_register = 1u;
    in[4].scale = 4u;
    in[4].displacement = -32;
    in[4].temp = DB_ARM64_SELECT_TEMP_ADDRESS;
    in[5] = select_record(UINT64_C(0x1010), 3u,
                          DB_ARM64_SELECT_LOAD_U64);
    in[5].width = DB_IR_WIDTH_64;
    in[5].destination_register = 5u;
    in[5].temp = DB_ARM64_SELECT_TEMP_ADDRESS;
    in[6] = select_record(UINT64_C(0x1013), 4u,
                          DB_ARM64_SELECT_PUSH_GUEST_U64);
    in[6].value = UINT64_C(0x1018);
    in[7] = select_record(UINT64_C(0x1013), 4u,
                          DB_ARM64_SELECT_BRANCH_GUEST_DIRECT);
    in[7].value = UINT64_C(0x2000);

    fill(out, sizeof(out), UINT8_C(0xa5));
    tail = out[18];
    CHECK(db_x64_arm64_plan_block(in, 8u, out, 24u, &s) ==
          DB_X64_ARM64_PLAN_OK);
    CHECK(s.plan_count == 18u && s.required_capacity == 18u);
    CHECK(s.has_terminal == 1u &&
          s.terminal_helper == DB_X64_ARM64_HELPER_DISPATCH_DIRECT);
    CHECK(out[0].opcode == DB_X64_ARM64_PLAN_MOVZ_U16 &&
          out[0].immediate16 == UINT16_C(0x7788) && out[0].shift == 0u);
    CHECK(out[1].opcode == DB_X64_ARM64_PLAN_MOVK_U16 &&
          out[1].immediate16 == UINT16_C(0x5566) && out[1].shift == 16u);
    CHECK(out[4].opcode == DB_X64_ARM64_PLAN_STORE_STATE_U64 &&
          out[4].guest_state_offset == gpr2 &&
          out[4].host_source == DB_X64_ARM64_HOST_PRIMARY);
    CHECK(out[5].opcode == DB_X64_ARM64_PLAN_LOAD_STATE_U64 &&
          out[6].opcode == DB_X64_ARM64_PLAN_STORE_STATE_U64);
    CHECK(out[7].opcode == DB_X64_ARM64_PLAN_LOAD_STATE_U64 &&
          out[8].opcode == DB_X64_ARM64_PLAN_LOAD_STATE_U64 &&
          out[9].opcode == DB_X64_ARM64_PLAN_EOR_REGISTER &&
          out[10].opcode == DB_X64_ARM64_PLAN_STORE_STATE_U64);
    CHECK(out[11].helper == DB_X64_ARM64_HELPER_UPDATE_LOGIC_FLAGS);
    CHECK(out[12].helper == DB_X64_ARM64_HELPER_FORM_GUEST_ADDRESS &&
          out[12].address_mode == DB_IR_ADDRESS_BASE_INDEX);
    CHECK(out[13].helper == DB_X64_ARM64_HELPER_READ_GUEST_U64 &&
          out[14].opcode == DB_X64_ARM64_PLAN_STORE_STATE_U64);
    CHECK(out[15].opcode == DB_X64_ARM64_PLAN_MOVZ_U16 &&
          out[16].helper == DB_X64_ARM64_HELPER_PUSH_GUEST_U64);
    CHECK(out[17].opcode == DB_X64_ARM64_PLAN_TAIL_HELPER &&
          out[17].terminal == 1u && out[17].value == UINT64_C(0x2000));
    CHECK(same(&out[18], &tail, sizeof(tail)) != 0);
    return 1;
}

static int materialize_sparse_and_zero(void)
{
    db_arm64_select_record in;
    db_x64_arm64_plan_record out[6];
    db_x64_arm64_plan_summary s;
    in = select_record(UINT64_C(0x3000), 0u,
                       DB_ARM64_SELECT_MATERIALIZE_U64);
    in.width = DB_IR_WIDTH_64;
    in.destination_register = 1u;
    in.value = UINT64_C(0x0001000000020000);
    CHECK(db_x64_arm64_plan_block(&in, 1u, out, 6u, &s) ==
          DB_X64_ARM64_PLAN_OK);
    CHECK(s.plan_count == 3u);
    CHECK(out[0].opcode == DB_X64_ARM64_PLAN_MOVZ_U16 &&
          out[0].immediate16 == 2u && out[0].shift == 16u);
    CHECK(out[1].opcode == DB_X64_ARM64_PLAN_MOVK_U16 &&
          out[1].immediate16 == 1u && out[1].shift == 48u);
    in.value = 0u;
    CHECK(db_x64_arm64_plan_block(&in, 1u, out, 6u, &s) ==
          DB_X64_ARM64_PLAN_OK);
    CHECK(s.plan_count == 2u && out[0].opcode == DB_X64_ARM64_PLAN_MOVZ_U16 &&
          out[0].immediate16 == 0u && out[0].shift == 0u);
    return 1;
}

static int helpers_and_pairs(void)
{
    db_arm64_select_record in[2];
    db_x64_arm64_plan_record out[4];
    db_x64_arm64_plan_summary s;
    in[0] = select_record(UINT64_C(0x4000), 0u,
                          DB_ARM64_SELECT_POP_GUEST_U64);
    in[0].temp = DB_ARM64_SELECT_TEMP_TARGET;
    in[1] = select_record(UINT64_C(0x4000), 0u,
                          DB_ARM64_SELECT_BRANCH_GUEST_TARGET);
    in[1].temp = DB_ARM64_SELECT_TEMP_TARGET;
    CHECK(db_x64_arm64_plan_block(in, 2u, out, 4u, &s) ==
          DB_X64_ARM64_PLAN_OK);
    CHECK(out[0].helper == DB_X64_ARM64_HELPER_POP_GUEST_U64 &&
          out[0].host_destination == DB_X64_ARM64_HOST_TARGET);
    CHECK(out[1].helper == DB_X64_ARM64_HELPER_DISPATCH_TARGET &&
          out[1].terminal == 1u);
    in[0] = select_record(UINT64_C(0x4010), 0u,
                          DB_ARM64_SELECT_TEST_GUEST_CONDITION);
    in[0].condition = DB_IR_CONDITION_LESS;
    in[0].temp = DB_ARM64_SELECT_TEMP_PREDICATE;
    in[1] = select_record(UINT64_C(0x4010), 0u,
                          DB_ARM64_SELECT_BRANCH_IF_TRUE);
    in[1].condition = DB_IR_CONDITION_LESS;
    in[1].temp = DB_ARM64_SELECT_TEMP_PREDICATE;
    in[1].value = UINT64_C(0x5000);
    CHECK(db_x64_arm64_plan_block(in, 2u, out, 4u, &s) ==
          DB_X64_ARM64_PLAN_OK);
    CHECK(out[0].helper == DB_X64_ARM64_HELPER_TEST_GUEST_CONDITION &&
          out[1].helper == DB_X64_ARM64_HELPER_DISPATCH_CONDITIONAL);
    in[0] = select_record(UINT64_C(0x4020), 0u,
                          DB_ARM64_SELECT_RAISE_TRAP);
    in[0].trap = DB_IR_TRAP_SYSCALL;
    CHECK(db_x64_arm64_plan_block(in, 1u, out, 4u, &s) ==
          DB_X64_ARM64_PLAN_OK);
    CHECK(out[0].helper == DB_X64_ARM64_HELPER_RAISE_TRAP &&
          out[0].trap == DB_IR_TRAP_SYSCALL);
    return 1;
}

static int transactional_failures(void)
{
    db_arm64_select_record in[2];
    db_x64_arm64_plan_record out[8];
    db_x64_arm64_plan_record before[8];
    db_x64_arm64_plan_summary s;
    db_x64_arm64_plan_summary sb;
    size_t i;
    in[0] = select_record(UINT64_C(0x6000), 0u,
                          DB_ARM64_SELECT_EOR_GUEST_GPR);
    in[0].width = DB_IR_WIDTH_64;
    in[0].destination_register = 1u;
    in[0].source_register = 2u;
    in[1] = select_record(UINT64_C(0x6000), 0u,
                          DB_ARM64_SELECT_BRANCH_GUEST_DIRECT);
    in[1].value = UINT64_C(0x7000);
    fill(out, sizeof(out), UINT8_C(0xa5));
    fill(&s, sizeof(s), UINT8_C(0x5a));
    for (i = 0u; i < 8u; ++i) before[i] = out[i];
    sb = s;
    CHECK(db_x64_arm64_plan_block(in, 2u, out, 8u, &s) ==
          DB_X64_ARM64_PLAN_BAD_GROUPING);
    CHECK(same(out, before, sizeof(out)) && same(&s, &sb, sizeof(s)));
    in[1] = select_record(UINT64_C(0x6000), 0u,
                          DB_ARM64_SELECT_UPDATE_LOGIC_FLAGS);
    in[1].width = DB_IR_WIDTH_64;
    in[1].source_register = 1u;
    CHECK(db_x64_arm64_plan_block(in, 2u, out, 5u, &s) ==
          DB_X64_ARM64_PLAN_OK);
    CHECK(db_x64_arm64_plan_block(in, 2u, out, 4u, &s) ==
          DB_X64_ARM64_PLAN_OUTPUT_CAPACITY);
    CHECK(db_x64_arm64_plan_block(in, 2u,
              (db_x64_arm64_plan_record *)(void *)in, 2u, &s) ==
          DB_X64_ARM64_PLAN_BUFFER_OVERLAP);
    in[0].reserved[0] = 1u;
    CHECK(db_x64_arm64_plan_block(in, 2u, out, 8u, &s) ==
          DB_X64_ARM64_PLAN_BAD_SELECTION);
    return 1;
}

int main(void)
{
    db_arm64_select_record in = select_record(
        UINT64_C(0x8000), 0u, DB_ARM64_SELECT_NOP);
    db_x64_arm64_plan_record out[2];
    db_x64_arm64_plan_summary s;
    CHECK(mixed_plan());
    CHECK(materialize_sparse_and_zero());
    CHECK(helpers_and_pairs());
    CHECK(transactional_failures());
    CHECK(db_x64_arm64_plan_block(NULL, 1u, out, 2u, &s) ==
          DB_X64_ARM64_PLAN_NULL_ARGUMENT);
    CHECK(db_x64_arm64_plan_block(&in, 0u, out, 2u, &s) ==
          DB_X64_ARM64_PLAN_BAD_COUNT);
    CHECK(db_x64_arm64_plan_status_name(DB_X64_ARM64_PLAN_OK)[0] == 'o');
    CHECK(db_x64_arm64_plan_opcode_name(DB_X64_ARM64_PLAN_MOVZ_U16)[0] == 'm');
    CHECK(db_x64_arm64_helper_name(DB_X64_ARM64_HELPER_DISPATCH_DIRECT)[0]
          == 'd');
    (void)write(STDOUT_FILENO, "X64 ARM64 PLAN TEST: PASS\n",
                sizeof("X64 ARM64 PLAN TEST: PASS\n") - 1u);
    return 0;
}
