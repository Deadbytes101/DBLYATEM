#include "dblyat/arm64_published_block.h"
#include "dblyat/arm64_select.h"
#include "dblyat/x64_arm64_plan.h"
#include "dblyat/x64_block.h"
#include "dblyat/x64_ir_lower.h"
#include "dblyat/x64_memory_instruction.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("X64 INDIRECT MEMORY CONTROL TEST: FAIL line=%d\n",      \
                   __LINE__);                                                \
            return 0;                                                        \
        }                                                                    \
    } while (0)

typedef struct pipeline_result {
    db_x64_block_record block_record;
    db_x64_block_summary block;
    db_ir_instruction ir;
    db_x64_ir_lower_summary lower;
    db_arm64_select_record selections[8];
    db_arm64_select_summary select;
    db_x64_arm64_plan_record plans[32];
    db_x64_arm64_plan_summary plan;
} pipeline_result;

static int run_pipeline(const uint8_t *bytes,
                        size_t size,
                        uint64_t guest_base,
                        pipeline_result *result)
{
    db_x64_code_view code;
    db_x64_block_record records[4];
    db_ir_instruction ir[4];
    db_x64_ir_lower_summary lower;
    db_arm64_select_record selections[8];
    db_arm64_select_summary select;
    db_x64_arm64_plan_record plans[32];
    db_x64_arm64_plan_summary plan;

    memset(&code, 0, sizeof(code));
    memset(records, 0, sizeof(records));
    memset(ir, 0, sizeof(ir));
    memset(&lower, 0, sizeof(lower));
    memset(selections, 0, sizeof(selections));
    memset(&select, 0, sizeof(select));
    memset(plans, 0, sizeof(plans));
    memset(&plan, 0, sizeof(plan));
    memset(result, 0, sizeof(*result));

    code.bytes = bytes;
    code.size = size;
    code.guest_base = guest_base;

    CHECK(db_x64_block_build(&code,
                             guest_base,
                             4u,
                             32u,
                             records,
                             4u,
                             &result->block) == DB_X64_BLOCK_OK);
    CHECK(result->block.stop_reason == DB_X64_BLOCK_STOP_TERMINATOR);
    CHECK(result->block.record_count == 1u);

    CHECK(db_x64_ir_lower_block(records,
                                result->block.record_count,
                                ir,
                                4u,
                                &lower) == DB_X64_IR_LOWER_OK);
    CHECK(lower.instruction_count == 1u);
    CHECK(lower.has_terminator == 1u);

    CHECK(db_arm64_select_block(ir,
                                lower.instruction_count,
                                selections,
                                8u,
                                &select) == DB_ARM64_SELECT_OK);
    CHECK(select.has_terminator == 1u);

    CHECK(db_x64_arm64_plan_block(selections,
                                  select.operation_count,
                                  plans,
                                  32u,
                                  &plan) == DB_X64_ARM64_PLAN_OK);
    CHECK(plan.has_terminal == 1u);
    CHECK(plan.terminal_helper == DB_X64_ARM64_HELPER_DISPATCH_TARGET);

    result->block_record = records[0];
    result->ir = ir[0];
    result->lower = lower;
    memcpy(result->selections, selections, sizeof(selections));
    result->select = select;
    memcpy(result->plans, plans, sizeof(plans));
    result->plan = plan;
    return 1;
}

static int bind_target_terminal(const pipeline_result *pipeline)
{
    db_arm64_published_block published;
    const db_x64_arm64_plan_record *terminal;

    CHECK(pipeline->plan.plan_count != 0u);
    terminal = &pipeline->plans[pipeline->plan.plan_count - 1u];

    memset(&published, 0, sizeof(published));
    published.guest_source_rip = pipeline->block.source_rip;
    published.guest_end_rip =
        pipeline->block.source_rip + (uint64_t)pipeline->block.byte_count;
    published.guest_fallthrough_rip =
        pipeline->block.final_fallthrough_rip;
    published.source_generation = UINT64_C(7);
    published.translation_key = UINT64_C(0x11223344);
    published.guest_byte_count = pipeline->block.byte_count;
    published.guest_instruction_count = pipeline->block.record_count;
    published.arm64_word_count = pipeline->plan.plan_count;
    published.block_stop_reason = (uint32_t)pipeline->block.stop_reason;
    published.format_version = DB_ARM64_PUBLISHED_BLOCK_FORMAT_VERSION;

    CHECK(db_arm64_published_block_bind_terminal(terminal, &published) ==
          DB_ARM64_PUBLISHED_BLOCK_OK);
    CHECK(published.terminal_exit.kind ==
          (uint32_t)DB_TRANSLATED_TERMINAL_EXIT_TARGET);
    CHECK(published.terminal_exit.source_rip == terminal->source_rip);
    CHECK(published.terminal_exit.runtime_target_valid == 0u);
    CHECK(published.terminal_exit.runtime_value_valid == 0u);
    return 1;
}

static int test_decode_contract(void)
{
    const uint8_t call_rip[] = {
        UINT8_C(0xff), UINT8_C(0x15),
        UINT8_C(0x34), UINT8_C(0x12), UINT8_C(0x00), UINT8_C(0x00)
    };
    const uint8_t jump_rip[] = {
        UINT8_C(0xff), UINT8_C(0x25),
        UINT8_C(0xfc), UINT8_C(0xff), UINT8_C(0xff), UINT8_C(0xff)
    };
    const uint8_t call_rex_w[] = {
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0x15),
        UINT8_C(0x78), UINT8_C(0x56), UINT8_C(0x34), UINT8_C(0x12)
    };
    const uint8_t call_register[] = {UINT8_C(0xff), UINT8_C(0xd0)};
    const uint8_t jump_register[] = {UINT8_C(0xff), UINT8_C(0xe0)};
    const uint8_t unsupported_group[] = {
        UINT8_C(0xff), UINT8_C(0x0d),
        UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0)
    };
    const uint8_t rex_r_extension[] = {
        UINT8_C(0x44), UINT8_C(0xff), UINT8_C(0x15),
        UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0)
    };
    db_x64_memory_instruction instruction;

    memset(&instruction, 0, sizeof(instruction));
    CHECK(db_x64_memory_instruction_decode_one(
              call_rip, sizeof(call_rip), &instruction) ==
          DB_X64_MEMORY_INSTRUCTION_OK);
    CHECK(instruction.kind == DB_X64_MEMORY_INSTRUCTION_CALL_RM64);
    CHECK(instruction.opcode == UINT8_C(0xff));
    CHECK(instruction.opcode_extension == UINT8_C(2));
    CHECK(instruction.register_operand == DB_X64_REGISTER_NONE);
    CHECK(instruction.instruction_length == UINT8_C(6));
    CHECK(instruction.memory.address.mode == DB_X64_ADDRESS_RIP_RELATIVE);
    CHECK(instruction.memory.address.displacement == INT64_C(0x1234));
    CHECK(instruction.memory.address.instruction_length == UINT8_C(6));

    memset(&instruction, 0, sizeof(instruction));
    CHECK(db_x64_memory_instruction_decode_one(
              jump_rip, sizeof(jump_rip), &instruction) ==
          DB_X64_MEMORY_INSTRUCTION_OK);
    CHECK(instruction.kind == DB_X64_MEMORY_INSTRUCTION_JMP_RM64);
    CHECK(instruction.opcode_extension == UINT8_C(4));
    CHECK(instruction.register_operand == DB_X64_REGISTER_NONE);
    CHECK(instruction.memory.address.displacement == INT64_C(-4));

    memset(&instruction, 0, sizeof(instruction));
    CHECK(db_x64_memory_instruction_decode_one(
              call_rex_w, sizeof(call_rex_w), &instruction) ==
          DB_X64_MEMORY_INSTRUCTION_OK);
    CHECK(instruction.kind == DB_X64_MEMORY_INSTRUCTION_CALL_RM64);
    CHECK(instruction.rex == UINT8_C(0x48));
    CHECK(instruction.instruction_length == UINT8_C(7));
    CHECK(instruction.memory.address.instruction_length == UINT8_C(7));

    CHECK(db_x64_memory_instruction_decode_one(
              call_register, sizeof(call_register), &instruction) ==
          DB_X64_MEMORY_INSTRUCTION_REGISTER_ENCODING);
    CHECK(db_x64_memory_instruction_decode_one(
              jump_register, sizeof(jump_register), &instruction) ==
          DB_X64_MEMORY_INSTRUCTION_REGISTER_ENCODING);
    CHECK(db_x64_memory_instruction_decode_one(
              unsupported_group, sizeof(unsupported_group), &instruction) ==
          DB_X64_MEMORY_INSTRUCTION_UNSUPPORTED_OPCODE);
    CHECK(db_x64_memory_instruction_decode_one(
              rex_r_extension, sizeof(rex_r_extension), &instruction) ==
          DB_X64_MEMORY_INSTRUCTION_BAD_REX);
    return 1;
}

static int test_indirect_call_pipeline(void)
{
    const uint8_t bytes[] = {
        UINT8_C(0xff), UINT8_C(0x15),
        UINT8_C(0x20), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00)
    };
    const uint64_t base = UINT64_C(0x0000000140001000);
    pipeline_result pipeline;
    uint32_t index;
    uint32_t form_count = 0u;
    uint32_t read_count = 0u;
    uint32_t push_count = 0u;
    uint32_t dispatch_count = 0u;

    CHECK(run_pipeline(bytes, sizeof(bytes), base, &pipeline));
    CHECK(pipeline.block_record.path == DB_X64_BLOCK_PATH_MEMORY);
    CHECK(pipeline.block_record.control == DB_X64_BLOCK_CONTROL_INDIRECT_CALL);
    CHECK(pipeline.block_record.has_direct_target == 0u);
    CHECK(pipeline.block_record.direct_target == 0u);
    CHECK(pipeline.block_record.fallthrough_rip == base + UINT64_C(6));
    CHECK(pipeline.ir.opcode == DB_IR_OPCODE_CALL_INDIRECT);
    CHECK(pipeline.ir.address.mode == DB_IR_ADDRESS_IP_RELATIVE);
    CHECK(pipeline.ir.address.displacement == INT64_C(0x20));
    CHECK(pipeline.ir.fallthrough_rip == base + UINT64_C(6));
    CHECK(pipeline.ir.has_direct_target == 0u);
    CHECK(pipeline.select.operation_count == 4u);
    CHECK(pipeline.selections[0].opcode ==
          DB_ARM64_SELECT_FORM_GUEST_ADDRESS);
    CHECK(pipeline.selections[1].opcode == DB_ARM64_SELECT_LOAD_U64_TARGET);
    CHECK(pipeline.selections[2].opcode == DB_ARM64_SELECT_PUSH_GUEST_U64);
    CHECK(pipeline.selections[2].value == base + UINT64_C(6));
    CHECK(pipeline.selections[3].opcode ==
          DB_ARM64_SELECT_BRANCH_GUEST_TARGET);

    for (index = 0u; index < pipeline.plan.plan_count; ++index) {
        const db_x64_arm64_plan_record *plan = &pipeline.plans[index];
        if (plan->helper == DB_X64_ARM64_HELPER_FORM_GUEST_ADDRESS) {
            ++form_count;
            CHECK(plan->address_mode == DB_IR_ADDRESS_IP_RELATIVE);
            CHECK(plan->value == base + UINT64_C(6));
            CHECK(plan->displacement == INT64_C(0x20));
        } else if (plan->helper == DB_X64_ARM64_HELPER_READ_GUEST_U64) {
            ++read_count;
            CHECK(plan->host_source == DB_X64_ARM64_HOST_PRIMARY);
            CHECK(plan->host_destination == DB_X64_ARM64_HOST_TARGET);
        } else if (plan->helper == DB_X64_ARM64_HELPER_PUSH_GUEST_U64) {
            ++push_count;
            CHECK(plan->host_source == DB_X64_ARM64_HOST_SECONDARY);
        } else if (plan->helper == DB_X64_ARM64_HELPER_DISPATCH_TARGET) {
            ++dispatch_count;
            CHECK(plan->opcode == DB_X64_ARM64_PLAN_TAIL_HELPER);
            CHECK(plan->terminal == 1u);
            CHECK(plan->host_source == DB_X64_ARM64_HOST_TARGET);
        }
    }

    CHECK(form_count == 1u);
    CHECK(read_count == 1u);
    CHECK(push_count == 1u);
    CHECK(dispatch_count == 1u);
    CHECK(bind_target_terminal(&pipeline));
    return 1;
}

static int test_indirect_jump_pipeline(void)
{
    const uint8_t bytes[] = {
        UINT8_C(0xff), UINT8_C(0x25),
        UINT8_C(0x10), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00)
    };
    const uint64_t base = UINT64_C(0x0000000140002000);
    pipeline_result pipeline;
    uint32_t index;
    uint32_t read_count = 0u;
    uint32_t push_count = 0u;
    uint32_t dispatch_count = 0u;

    CHECK(run_pipeline(bytes, sizeof(bytes), base, &pipeline));
    CHECK(pipeline.block_record.path == DB_X64_BLOCK_PATH_MEMORY);
    CHECK(pipeline.block_record.control == DB_X64_BLOCK_CONTROL_INDIRECT_JUMP);
    CHECK(pipeline.block_record.has_direct_target == 0u);
    CHECK(pipeline.ir.opcode == DB_IR_OPCODE_JUMP_INDIRECT);
    CHECK(pipeline.ir.address.mode == DB_IR_ADDRESS_IP_RELATIVE);
    CHECK(pipeline.select.operation_count == 3u);
    CHECK(pipeline.selections[0].opcode ==
          DB_ARM64_SELECT_FORM_GUEST_ADDRESS);
    CHECK(pipeline.selections[1].opcode == DB_ARM64_SELECT_LOAD_U64_TARGET);
    CHECK(pipeline.selections[2].opcode ==
          DB_ARM64_SELECT_BRANCH_GUEST_TARGET);

    for (index = 0u; index < pipeline.plan.plan_count; ++index) {
        const db_x64_arm64_plan_record *plan = &pipeline.plans[index];
        if (plan->helper == DB_X64_ARM64_HELPER_READ_GUEST_U64) {
            ++read_count;
            CHECK(plan->host_destination == DB_X64_ARM64_HOST_TARGET);
        } else if (plan->helper == DB_X64_ARM64_HELPER_PUSH_GUEST_U64) {
            ++push_count;
        } else if (plan->helper == DB_X64_ARM64_HELPER_DISPATCH_TARGET) {
            ++dispatch_count;
            CHECK(plan->terminal == 1u);
        }
    }

    CHECK(read_count == 1u);
    CHECK(push_count == 0u);
    CHECK(dispatch_count == 1u);
    CHECK(bind_target_terminal(&pipeline));
    return 1;
}

int main(void)
{
    CHECK(test_decode_contract());
    CHECK(test_indirect_call_pipeline());
    CHECK(test_indirect_jump_pipeline());

    printf("X64 INDIRECT MEMORY CONTROL TEST: PASS\n");
    printf("FF /2 MEMORY CALL DECODE: PASS\n");
    printf("FF /4 MEMORY JUMP DECODE: PASS\n");
    printf("RIP RELATIVE IAT ADDRESSING: PASS\n");
    printf("INDIRECT CALL TARGET DISPATCH: PASS\n");
    printf("INDIRECT JUMP TARGET DISPATCH: PASS\n");
    printf("INDIRECT CONTROL ADVERSARIAL REJECTION: PASS\n");
    printf("TRANSLATED TARGET TERMINAL BINDING: PASS\n");
    printf("REGISTER INDIRECT CONTROL: NOT IMPLEMENTED\n");
    printf("PRODUCTION PE STARTUP RUN: NOT IMPLEMENTED\n");
    printf("NATIVE ARM64 PE EXECUTION: NOT RUN\n");
    return 0;
}
