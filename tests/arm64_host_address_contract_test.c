#include "dblyat/arm64_code_image.h"
#include "dblyat/x64_arm64_relocate.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) return 0; } while (0)

static db_x64_arm64_relocation make_jump_relocation(void)
{
    db_x64_arm64_relocation relocation;

    memset(&relocation, 0, sizeof(relocation));
    relocation.source_rip = UINT64_C(0x4000);
    relocation.word_index = 0u;
    relocation.source_plan_index = 0u;
    relocation.kind = DB_X64_ARM64_RELOCATION_JUMP26;
    relocation.helper = DB_X64_ARM64_HELPER_DISPATCH_DIRECT;
    relocation.width = DB_IR_WIDTH_NONE;
    relocation.condition = (db_ir_condition)UINT8_C(0xff);
    relocation.trap = DB_IR_TRAP_NONE;
    relocation.address_mode = DB_IR_ADDRESS_NONE;
    relocation.host_destination = UINT8_C(0xff);
    relocation.host_source = UINT8_C(0xff);
    relocation.host_auxiliary = UINT8_C(0xff);
    relocation.guest_register = UINT8_C(0xff);
    relocation.base_guest_register = UINT8_C(0xff);
    relocation.index_guest_register = UINT8_C(0xff);
    relocation.scale = UINT8_C(1);
    return relocation;
}

static int test_unsigned_arm64_relocation_addresses(void)
{
    const uint32_t source[1] = {UINT32_C(0x14000000)};
    const uint64_t code_base = UINT64_C(0x0000ffff00000000);
    const uint64_t helper_target = UINT64_C(0x0000aaaa12345678);
    db_x64_arm64_relocation relocation = make_jump_relocation();
    db_x64_arm64_helper_targets targets;
    uint32_t output[6];
    db_x64_arm64_resolution decision;
    db_x64_arm64_relocate_summary summary;

    memset(&targets, 0, sizeof(targets));
    memset(output, 0xa5, sizeof(output));
    memset(&decision, 0x5a, sizeof(decision));
    memset(&summary, 0x3c, sizeof(summary));
    targets.addresses[DB_X64_ARM64_HELPER_DISPATCH_DIRECT] =
        helper_target;

    CHECK(db_x64_arm64_resolve_helpers(
              source,
              1u,
              &relocation,
              1u,
              &targets,
              code_base,
              output,
              6u,
              &decision,
              1u,
              &summary) == DB_X64_ARM64_RELOCATE_OK);
    CHECK(decision.source_address == code_base);
    CHECK(decision.helper_target == helper_target);
    CHECK(decision.resolution == DB_X64_ARM64_RESOLUTION_FAR_THUNK);
    CHECK(decision.thunk_address == code_base + UINT64_C(4));
    CHECK(summary.relocation_count == 1u);
    CHECK(summary.thunk_count == 1u);
    CHECK(summary.output_word_count <= 6u);

    CHECK(db_x64_arm64_resolve_helpers(
              source,
              1u,
              &relocation,
              1u,
              &targets,
              UINT64_C(0x0001000000000000),
              output,
              6u,
              &decision,
              1u,
              &summary) == DB_X64_ARM64_RELOCATE_BAD_CODE_BASE);

    targets.addresses[DB_X64_ARM64_HELPER_DISPATCH_DIRECT] =
        UINT64_C(0x0001000000000000);
    CHECK(db_x64_arm64_resolve_helpers(
              source,
              1u,
              &relocation,
              1u,
              &targets,
              code_base,
              output,
              6u,
              &decision,
              1u,
              &summary) == DB_X64_ARM64_RELOCATE_BAD_TARGET_TABLE);

    targets.addresses[DB_X64_ARM64_HELPER_DISPATCH_DIRECT] =
        helper_target;
    relocation.source_rip = UINT64_C(0x0000800000000000);
    CHECK(db_x64_arm64_resolve_helpers(
              source,
              1u,
              &relocation,
              1u,
              &targets,
              code_base,
              output,
              6u,
              &decision,
              1u,
              &summary) == DB_X64_ARM64_RELOCATE_BAD_RELOCATION);
    return 1;
}

static int test_unsigned_arm64_code_image_addresses(void)
{
    const uint32_t word[1] = {UINT32_C(0xd503201f)};
    const uint64_t code_base = UINT64_C(0x0000ffff00000000);
    uint8_t output[sizeof(word)];
    db_arm64_code_image_plan plan;
    static uint32_t overflow_words[1025];
    static uint8_t overflow_output[4100];

    memset(output, 0xa5, sizeof(output));
    memset(&plan, 0x5a, sizeof(plan));

    CHECK(db_arm64_code_image_build(
              word,
              1u,
              code_base,
              0u,
              UINT64_C(4096),
              output,
              sizeof(output),
              &plan) == DB_ARM64_CODE_IMAGE_OK);
    CHECK(plan.code_base == code_base);
    CHECK(plan.entry_address == code_base);
    CHECK(plan.image_end_address == code_base + UINT64_C(3));
    CHECK(plan.allocation_end_address ==
          code_base + UINT64_C(4095));

    CHECK(db_arm64_code_image_build(
              word,
              1u,
              UINT64_C(0x0001000000000000),
              0u,
              UINT64_C(4096),
              output,
              sizeof(output),
              &plan) == DB_ARM64_CODE_IMAGE_BAD_CODE_BASE);

    memset(overflow_words, 0, sizeof(overflow_words));
    memset(overflow_output, 0, sizeof(overflow_output));
    CHECK(db_arm64_code_image_build(
              overflow_words,
              1025u,
              UINT64_C(0x0000fffffffff000),
              0u,
              UINT64_C(4096),
              overflow_output,
              sizeof(overflow_output),
              &plan) == DB_ARM64_CODE_IMAGE_SIZE_OVERFLOW);
    return 1;
}

int main(void)
{
    if (!test_unsigned_arm64_relocation_addresses()) {
        return 11;
    }
    if (!test_unsigned_arm64_code_image_addresses()) {
        return 12;
    }

    (void)write(
        STDOUT_FILENO,
        "ARM64 HOST ADDRESS CONTRACT TEST: PASS\n",
        sizeof("ARM64 HOST ADDRESS CONTRACT TEST: PASS\n") - 1u);
    return 0;
}
