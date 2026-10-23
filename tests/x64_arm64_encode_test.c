#include "dblyat/x64_arm64_encode.h"
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#define CHECK(v) do{if(!(v))return 0;}while(0)
static void fill(void *p, size_t n, uint8_t v) {
    uint8_t *b = p;
    size_t i;
    for (i = 0; i < n; ++i) b[i] = v;
}
static int same(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    size_t i;
    for (i = 0; i < n; ++i) if (x[i] != y[i]) return 0;
    return 1;
}
static db_x64_arm64_plan_record rec(uint64_t rip, uint32_t ix, db_x64_arm64_plan_opcode op) {
    db_x64_arm64_plan_record p;
    fill(&p, sizeof(p), 0);
    p.source_rip = rip;
    p.source_select_index = ix;
    p.opcode = op;
    p.helper = DB_X64_ARM64_HELPER_NONE;
    p.width = DB_IR_WIDTH_NONE;
    p.condition = (db_ir_condition) UINT8_C(0xff);
    p.trap = DB_IR_TRAP_NONE;
    p.address_mode = DB_IR_ADDRESS_NONE;
    p.host_destination = DB_X64_ARM64_HOST_NONE;
    p.host_source = DB_X64_ARM64_HOST_NONE;
    p.host_auxiliary = DB_X64_ARM64_HOST_NONE;
    p.guest_register = DB_X64_ARM64_GUEST_NONE;
    p.base_guest_register = DB_X64_ARM64_GUEST_NONE;
    p.index_guest_register = DB_X64_ARM64_GUEST_NONE;
    p.scale = 1;
    return p;
}
static int known_words(void) {
    db_x64_arm64_plan_record p[13];
    uint32_t w[13];
    db_x64_arm64_relocation r[2];
    db_x64_arm64_encode_summary s;
    p[0] = rec(0x1000, 0, DB_X64_ARM64_PLAN_NOP);
    p[1] = rec(0x1001, 1, DB_X64_ARM64_PLAN_LOAD_STATE_U64);
    p[1].width = DB_IR_WIDTH_64;
    p[1].guest_register = 2;
    p[1].guest_state_offset = (uint32_t) offsetof(db_x64_scalar_state, registers[2]);
    p[1].host_destination = 16;
    p[2] = rec(0x1002, 2, DB_X64_ARM64_PLAN_STORE_STATE_U64);
    p[2].width = DB_IR_WIDTH_64;
    p[2].guest_register = 3;
    p[2].guest_state_offset = (uint32_t) offsetof(db_x64_scalar_state, registers[3]);
    p[2].host_source = 17;
    p[3] = rec(0x1003, 3, DB_X64_ARM64_PLAN_MOVZ_U16);
    p[3].immediate16 = 0x7788;
    p[3].host_destination = 16;
    p[4] = rec(0x1004, 3, DB_X64_ARM64_PLAN_MOVK_U16);
    p[4].immediate16 = 0x5566;
    p[4].shift = 16;
    p[4].host_destination = 16;
    p[5] = rec(0x1005, 3, DB_X64_ARM64_PLAN_MOVK_U16);
    p[5].immediate16 = 0x3344;
    p[5].shift = 32;
    p[5].host_destination = 16;
    p[6] = rec(0x1006, 3, DB_X64_ARM64_PLAN_MOVK_U16);
    p[6].immediate16 = 0x1122;
    p[6].shift = 48;
    p[6].host_destination = 16;
    p[7] = rec(0x1007, 4, DB_X64_ARM64_PLAN_EOR_REGISTER);
    p[7].width = DB_IR_WIDTH_64;
    p[7].host_destination = 16;
    p[7].host_source = 16;
    p[7].host_auxiliary = 17;
    p[8] = rec(0x1008, 5, DB_X64_ARM64_PLAN_EOR_REGISTER);
    p[8].width = DB_IR_WIDTH_32;
    p[8].host_destination = 16;
    p[8].host_source = 16;
    p[8].host_auxiliary = 17;
    p[9] = rec(0x1009, 6, DB_X64_ARM64_PLAN_MOVZ_U16);
    p[9].immediate16 = UINT16_C(64);
    p[9].host_destination = DB_X64_ARM64_HELPER_ARG0;
    p[10] = rec(0x1009, 6, DB_X64_ARM64_PLAN_MOVZ_U16);
    p[10].immediate16 = UINT16_C(3);
    p[10].host_destination = DB_X64_ARM64_HELPER_ARG1;
    p[11] = rec(0x1009, 6, DB_X64_ARM64_PLAN_CALL_HELPER);
    p[11].helper = DB_X64_ARM64_HELPER_UPDATE_LOGIC_FLAGS;
    p[11].width = DB_IR_WIDTH_64;
    p[11].guest_register = 3;
    p[12] = rec(0x100a, 7, DB_X64_ARM64_PLAN_TAIL_HELPER);
    p[12].helper = DB_X64_ARM64_HELPER_DISPATCH_DIRECT;
    p[12].value = 0x2000;
    p[12].terminal = 1;
    CHECK(db_x64_arm64_encode_block(p, 13, w, 13, r, 2, &s) == DB_X64_ARM64_ENCODE_OK);
    CHECK(w[0] == UINT32_C(0xd503201f));
    CHECK(w[1] == UINT32_C(0xf9400a70));
    CHECK(w[2] == UINT32_C(0xf9000e71));
    CHECK(w[3] == UINT32_C(0xd28ef110));
    CHECK(w[4] == UINT32_C(0xf2aaacd0));
    CHECK(w[5] == UINT32_C(0xf2c66890));
    CHECK(w[6] == UINT32_C(0xf2e22450));
    CHECK(w[7] == UINT32_C(0xca110210));
    CHECK(w[8] == UINT32_C(0x4a110210));
    CHECK(w[9] == UINT32_C(0xd2800800));
    CHECK(w[10] == UINT32_C(0xd2800061));
    CHECK(w[11] == UINT32_C(0x94000000));
    CHECK(w[12] == UINT32_C(0x14000000));
    CHECK(s.word_count == 13 && s.relocation_count == 2 && s.has_terminal == 1);
    CHECK(r[0].word_index == 11 && r[0].kind == DB_X64_ARM64_RELOCATION_CALL26 && r[0].helper == DB_X64_ARM64_HELPER_UPDATE_LOGIC_FLAGS && r[0].guest_register == 3);
    CHECK(r[1].word_index == 12 && r[1].kind == DB_X64_ARM64_RELOCATION_JUMP26 && r[1].helper == DB_X64_ARM64_HELPER_DISPATCH_DIRECT && r[1].value == 0x2000);
    return 1;
}
static int stale_helper_abi_is_transactional(void) {
    db_x64_arm64_plan_record p[3];
    uint32_t w[4], wb[4];
    db_x64_arm64_relocation r[2], rb[2];
    db_x64_arm64_encode_summary s, sb;
    size_t i;
    p[0] = rec(0x2100, 0, DB_X64_ARM64_PLAN_MOVZ_U16);
    p[0].immediate16 = UINT16_C(64);
    p[0].host_destination = DB_X64_ARM64_HELPER_ARG0;
    p[1] = rec(0x2100, 0, DB_X64_ARM64_PLAN_MOVZ_U16);
    p[1].immediate16 = UINT16_C(4);
    p[1].host_destination = DB_X64_ARM64_HELPER_ARG1;
    p[2] = rec(0x2100, 0, DB_X64_ARM64_PLAN_CALL_HELPER);
    p[2].helper = DB_X64_ARM64_HELPER_UPDATE_LOGIC_FLAGS;
    p[2].width = DB_IR_WIDTH_64;
    p[2].guest_register = 3;
    fill(w, sizeof(w), 0xa5);
    fill(r, sizeof(r), 0x5a);
    fill(&s, sizeof(s), 0x3c);
    for (i = 0; i < 4; ++i) wb[i] = w[i];
    for (i = 0; i < 2; ++i) rb[i] = r[i];
    sb = s;
    CHECK(db_x64_arm64_encode_block(p, 3, w, 4, r, 2, &s) ==
          DB_X64_ARM64_ENCODE_BAD_HELPER_ABI);
    CHECK(same(w, wb, sizeof(w)) && same(r, rb, sizeof(r)) && same(&s, &sb, sizeof(s)));
    p[1].immediate16 = UINT16_C(3);
    CHECK(db_x64_arm64_encode_block(p, 3, w, 4, r, 2, &s) == DB_X64_ARM64_ENCODE_OK);
    return 1;
}
static int failures(void) {
    db_x64_arm64_plan_record p[2];
    uint32_t w[4], wb[4];
    db_x64_arm64_relocation r[2], rb[2];
    db_x64_arm64_encode_summary s, sb;
    size_t i;
    p[0] = rec(0x3000, 0, DB_X64_ARM64_PLAN_NOP);
    p[1] = rec(0x3001, 1, DB_X64_ARM64_PLAN_TAIL_HELPER);
    p[1].helper = DB_X64_ARM64_HELPER_RAISE_TRAP;
    p[1].trap = DB_IR_TRAP_SYSCALL;
    p[1].terminal = 1;
    fill(w, sizeof(w), 0xa5);
    fill(r, sizeof(r), 0x5a);
    fill(&s, sizeof(s), 0x3c);
    for (i = 0; i < 4; i++) wb[i] = w[i];
    for (i = 0; i < 2; i++) rb[i] = r[i];
    sb = s;
    CHECK(db_x64_arm64_encode_block(p, 2, w, 1, r, 2, &s) == DB_X64_ARM64_ENCODE_WORD_CAPACITY);
    CHECK(same(w, wb, sizeof(w)) && same(r, rb, sizeof(r)) && same(&s, &sb, sizeof(s)));
    CHECK(db_x64_arm64_encode_block(p, 2, w, 4, r, 0, &s) == DB_X64_ARM64_ENCODE_RELOCATION_CAPACITY);
    p[0].source_select_index = 1;
    CHECK(db_x64_arm64_encode_block(p, 2, w, 4, r, 2, &s) == DB_X64_ARM64_ENCODE_BAD_GROUPING);
    p[0] = rec(0x3000, 0, DB_X64_ARM64_PLAN_TAIL_HELPER);
    p[0].helper = DB_X64_ARM64_HELPER_DISPATCH_TARGET;
    p[0].terminal = 1;
    CHECK(db_x64_arm64_encode_block(p, 2, w, 4, r, 2, &s) == DB_X64_ARM64_ENCODE_TERMINATOR_ORDER);
    p[0] = rec(0x3000, 0, DB_X64_ARM64_PLAN_LOAD_STATE_U64);
    p[0].width = DB_IR_WIDTH_64;
    p[0].guest_register = 1;
    p[0].guest_state_offset = 9;
    p[0].host_destination = 16;
    CHECK(db_x64_arm64_encode_block(p, 1, w, 4, r, 2, &s) == DB_X64_ARM64_ENCODE_BAD_PLAN);
    return 1;
}
int main(void) {
    db_x64_arm64_plan_record p = rec(0x4000, 0, DB_X64_ARM64_PLAN_NOP);
    uint32_t w[1];
    db_x64_arm64_relocation r[1];
    db_x64_arm64_encode_summary s;
    CHECK(known_words());
    CHECK(stale_helper_abi_is_transactional());
    CHECK(failures());
    CHECK(db_x64_arm64_encode_block(NULL, 1, w, 1, r, 1, &s) == DB_X64_ARM64_ENCODE_NULL_ARGUMENT);
    CHECK(db_x64_arm64_encode_block(&p, 0, w, 1, r, 1, &s) == DB_X64_ARM64_ENCODE_BAD_COUNT);
    CHECK(db_x64_arm64_encode_status_name(DB_X64_ARM64_ENCODE_OK)[0] == 'o');
    CHECK(db_x64_arm64_encode_status_name(DB_X64_ARM64_ENCODE_BAD_HELPER_ABI)[0] == 'b');
    (void) write(STDOUT_FILENO, "X64 ARM64 ENCODE TEST: PASS\n", sizeof("X64 ARM64 ENCODE TEST: PASS\n") - 1u);
    return 0;
}
