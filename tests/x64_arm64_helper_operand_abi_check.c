#include "dblyat/x64_arm64_encode.h"
#include "dblyat/x64_arm64_plan.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("ARM64 HELPER OPERAND ABI CHECK: FAIL line=%d\n",       \
                   __LINE__);                                                \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static db_arm64_select_record selection(
    uint64_t rip,
    uint32_t ir_index,
    db_arm64_select_opcode opcode)
{
    db_arm64_select_record record;
    memset(&record, 0, sizeof(record));
    record.source_rip = rip;
    record.source_ir_index = ir_index;
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

static int same(const void *left, const void *right, size_t size)
{
    return memcmp(left, right, size) == 0;
}

static int form_address_abi(void)
{
    db_arm64_select_record input[2];
    db_x64_arm64_plan_record plans[16];
    db_x64_arm64_plan_summary plan;
    uint32_t words[16];
    db_x64_arm64_relocation relocations[4];
    db_x64_arm64_encode_summary encode;

    input[0] = selection(UINT64_C(0x140001000), 0u,
                         DB_ARM64_SELECT_FORM_GUEST_ADDRESS);
    input[0].address_mode = DB_IR_ADDRESS_BASE_INDEX;
    input[0].base_register = 0u;
    input[0].index_register = 1u;
    input[0].scale = 4u;
    input[0].displacement = -32;
    input[0].temp = DB_ARM64_SELECT_TEMP_ADDRESS;
    input[1] = selection(UINT64_C(0x140001000), 0u,
                         DB_ARM64_SELECT_LOAD_U64);
    input[1].width = DB_IR_WIDTH_64;
    input[1].destination_register = 5u;
    input[1].temp = DB_ARM64_SELECT_TEMP_ADDRESS;

    memset(plans, 0xa5, sizeof(plans));
    memset(&plan, 0xa5, sizeof(plan));
    CHECK(db_x64_arm64_plan_block(input, 2u, plans, 16u, &plan) ==
          DB_X64_ARM64_PLAN_OK);
    CHECK(plan.plan_count == 12u);
    CHECK(plans[0].host_destination == DB_X64_ARM64_HELPER_ARG0 &&
          plans[0].immediate16 == 0u);
    CHECK(plans[1].host_destination == DB_X64_ARM64_HELPER_ARG1 &&
          plans[1].immediate16 == UINT16_C(0xffe0));
    CHECK(plans[2].host_destination == DB_X64_ARM64_HELPER_ARG1 &&
          plans[2].shift == 16u && plans[2].immediate16 == UINT16_C(0xffff));
    CHECK(plans[3].host_destination == DB_X64_ARM64_HELPER_ARG1 &&
          plans[3].shift == 32u);
    CHECK(plans[4].host_destination == DB_X64_ARM64_HELPER_ARG1 &&
          plans[4].shift == 48u);
    CHECK(plans[5].host_destination == DB_X64_ARM64_HELPER_ARG2 &&
          plans[5].immediate16 == UINT16_C(DB_IR_ADDRESS_BASE_INDEX));
    CHECK(plans[6].host_destination == DB_X64_ARM64_HELPER_ARG3 &&
          plans[6].immediate16 == 0u);
    CHECK(plans[7].host_destination == DB_X64_ARM64_HELPER_ARG4 &&
          plans[7].immediate16 == 1u);
    CHECK(plans[8].host_destination == DB_X64_ARM64_HELPER_ARG5 &&
          plans[8].immediate16 == 4u);
    CHECK(plans[9].helper == DB_X64_ARM64_HELPER_FORM_GUEST_ADDRESS);
    CHECK(plans[10].helper == DB_X64_ARM64_HELPER_READ_GUEST_U64);

    CHECK(db_x64_arm64_encode_block(plans, plan.plan_count,
                                    words, 16u,
                                    relocations, 4u,
                                    &encode) == DB_X64_ARM64_ENCODE_OK);
    CHECK(words[0] == UINT32_C(0xd2800000));
    CHECK(words[1] == UINT32_C(0xd29ffc01));
    CHECK(words[2] == UINT32_C(0xf2bfffe1));
    CHECK(words[3] == UINT32_C(0xf2dfffe1));
    CHECK(words[4] == UINT32_C(0xf2ffffe1));
    CHECK(words[5] == UINT32_C(0xd2800022));
    CHECK(words[6] == UINT32_C(0xd2800003));
    CHECK(words[7] == UINT32_C(0xd2800024));
    CHECK(words[8] == UINT32_C(0xd2800085));
    CHECK(words[9] == UINT32_C(0x94000000));
    CHECK(encode.relocation_count == 2u);
    CHECK(relocations[0].word_index == 9u &&
          relocations[0].helper == DB_X64_ARM64_HELPER_FORM_GUEST_ADDRESS);
    return 1;
}

static int logic_flags_abi(void)
{
    db_arm64_select_record input[2];
    db_x64_arm64_plan_record plans[8];
    db_x64_arm64_plan_summary plan;
    uint32_t words[8];
    db_x64_arm64_relocation relocations[2];
    db_x64_arm64_encode_summary encode;

    input[0] = selection(UINT64_C(0x2000), 0u,
                         DB_ARM64_SELECT_EOR_GUEST_GPR);
    input[0].width = DB_IR_WIDTH_64;
    input[0].destination_register = 1u;
    input[0].source_register = 2u;
    input[1] = selection(UINT64_C(0x2000), 0u,
                         DB_ARM64_SELECT_UPDATE_LOGIC_FLAGS);
    input[1].width = DB_IR_WIDTH_64;
    input[1].source_register = 1u;

    CHECK(db_x64_arm64_plan_block(input, 2u, plans, 8u, &plan) ==
          DB_X64_ARM64_PLAN_OK);
    CHECK(plan.plan_count == 7u);
    CHECK(plans[4].host_destination == DB_X64_ARM64_HELPER_ARG0 &&
          plans[4].immediate16 == UINT16_C(64));
    CHECK(plans[5].host_destination == DB_X64_ARM64_HELPER_ARG1 &&
          plans[5].immediate16 == UINT16_C(1));
    CHECK(plans[6].helper == DB_X64_ARM64_HELPER_UPDATE_LOGIC_FLAGS);
    CHECK(db_x64_arm64_encode_block(plans, plan.plan_count,
                                    words, 8u,
                                    relocations, 2u,
                                    &encode) == DB_X64_ARM64_ENCODE_OK);
    CHECK(words[4] == UINT32_C(0xd2800800));
    CHECK(words[5] == UINT32_C(0xd2800021));
    CHECK(words[6] == UINT32_C(0x94000000));
    return 1;
}

static int condition_abi(void)
{
    db_arm64_select_record input[2];
    db_x64_arm64_plan_record plans[4];
    db_x64_arm64_plan_summary plan;
    uint32_t words[4];
    db_x64_arm64_relocation relocations[2];
    db_x64_arm64_encode_summary encode;

    input[0] = selection(UINT64_C(0x3000), 0u,
                         DB_ARM64_SELECT_TEST_GUEST_CONDITION);
    input[0].condition = DB_IR_CONDITION_LESS;
    input[0].temp = DB_ARM64_SELECT_TEMP_PREDICATE;
    input[1] = selection(UINT64_C(0x3000), 0u,
                         DB_ARM64_SELECT_BRANCH_IF_TRUE);
    input[1].condition = DB_IR_CONDITION_LESS;
    input[1].temp = DB_ARM64_SELECT_TEMP_PREDICATE;
    input[1].value = UINT64_C(0x4000);

    CHECK(db_x64_arm64_plan_block(input, 2u, plans, 4u, &plan) ==
          DB_X64_ARM64_PLAN_OK);
    CHECK(plan.plan_count == 3u);
    CHECK(plans[0].host_destination == DB_X64_ARM64_HELPER_ARG0 &&
          plans[0].immediate16 == UINT16_C(DB_IR_CONDITION_LESS));
    CHECK(plans[1].helper == DB_X64_ARM64_HELPER_TEST_GUEST_CONDITION);
    CHECK(plans[2].helper == DB_X64_ARM64_HELPER_DISPATCH_CONDITIONAL);
    CHECK(db_x64_arm64_encode_block(plans, plan.plan_count,
                                    words, 4u,
                                    relocations, 2u,
                                    &encode) == DB_X64_ARM64_ENCODE_OK);
    CHECK(words[0] == UINT32_C(0xd2800180));
    CHECK(words[1] == UINT32_C(0x94000000));
    CHECK(words[2] == UINT32_C(0x14000000));
    return 1;
}

static int stale_setup_rejected_transactionally(void)
{
    db_arm64_select_record input[3];
    db_x64_arm64_plan_record plans[16];
    db_x64_arm64_plan_summary plan;
    uint32_t words[16];
    uint32_t words_before[16];
    db_x64_arm64_relocation relocations[4];
    db_x64_arm64_relocation relocations_before[4];
    db_x64_arm64_encode_summary encode;
    db_x64_arm64_encode_summary encode_before;

    input[0] = selection(UINT64_C(0x5000), 0u,
                         DB_ARM64_SELECT_FORM_GUEST_ADDRESS);
    input[0].address_mode = DB_IR_ADDRESS_BASE_INDEX;
    input[0].base_register = 0u;
    input[0].index_register = 1u;
    input[0].scale = 4u;
    input[0].displacement = -32;
    input[0].temp = DB_ARM64_SELECT_TEMP_ADDRESS;
    input[1] = selection(UINT64_C(0x5000), 0u,
                         DB_ARM64_SELECT_LOAD_U64_TARGET);
    input[1].width = DB_IR_WIDTH_64;
    input[1].temp = DB_ARM64_SELECT_TEMP_ADDRESS;
    input[2] = selection(UINT64_C(0x5000), 0u,
                         DB_ARM64_SELECT_BRANCH_GUEST_TARGET);
    input[2].temp = DB_ARM64_SELECT_TEMP_TARGET;

    CHECK(db_x64_arm64_plan_block(input, 3u, plans, 16u, &plan) ==
          DB_X64_ARM64_PLAN_OK);
    plans[5].immediate16 = UINT16_C(DB_IR_ADDRESS_IP_RELATIVE);
    memset(words, 0xa5, sizeof(words));
    memset(relocations, 0x5a, sizeof(relocations));
    memset(&encode, 0x3c, sizeof(encode));
    memcpy(words_before, words, sizeof(words));
    memcpy(relocations_before, relocations, sizeof(relocations));
    encode_before = encode;

    CHECK(db_x64_arm64_encode_block(plans, plan.plan_count,
                                    words, 16u,
                                    relocations, 4u,
                                    &encode) ==
          DB_X64_ARM64_ENCODE_BAD_HELPER_ABI);
    CHECK(same(words, words_before, sizeof(words)));
    CHECK(same(relocations, relocations_before, sizeof(relocations)));
    CHECK(same(&encode, &encode_before, sizeof(encode)));
    return 1;
}

int main(void)
{
    CHECK(DB_X64_ARM64_HELPER_OPERAND_ABI_VERSION == UINT32_C(1));
    CHECK(form_address_abi());
    CHECK(logic_flags_abi());
    CHECK(condition_abi());
    CHECK(stale_setup_rejected_transactionally());

    printf("ARM64 HELPER OPERAND ABI CHECK: PASS\n");
    printf("FORM ADDRESS ARGUMENT ABI: PASS\n");
    printf("LOGIC FLAGS ARGUMENT ABI: PASS\n");
    printf("CONDITION ARGUMENT ABI: PASS\n");
    printf("STALE HELPER ABI TRANSACTIONAL REJECTION: PASS\n");
    printf("MEMORY HELPER BACKEND: NOT IMPLEMENTED\n");
    printf("NATIVE ARM64 HELPER EXECUTION: NOT RUN\n");
    return 0;
}
