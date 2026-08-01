#include "dblyat/x64_arm64_relocate.h"

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
        if (a[index] != b[index]) {
            return 0;
        }
    }
    return 1;
}

static db_x64_arm64_relocation relocation(
    uint32_t word_index,
    db_x64_arm64_relocation_kind kind,
    db_x64_arm64_helper helper)
{
    db_x64_arm64_relocation value;
    fill(&value, sizeof(value), 0u);
    value.source_rip = UINT64_C(0x4000);
    value.word_index = word_index;
    value.source_plan_index = word_index;
    value.kind = kind;
    value.helper = helper;
    value.width = DB_IR_WIDTH_NONE;
    value.condition = (db_ir_condition)UINT8_C(0xff);
    value.trap = DB_IR_TRAP_NONE;
    value.address_mode = DB_IR_ADDRESS_NONE;
    value.host_destination = UINT8_C(0xff);
    value.host_source = UINT8_C(0xff);
    value.host_auxiliary = UINT8_C(0xff);
    value.guest_register = UINT8_C(0xff);
    value.base_guest_register = UINT8_C(0xff);
    value.index_guest_register = UINT8_C(0xff);
    value.scale = UINT8_C(1);
    return value;
}

static db_x64_arm64_helper_targets targets(void)
{
    db_x64_arm64_helper_targets value;
    fill(&value, sizeof(value), 0u);
    return value;
}

static int test_direct(void)
{
    const uint32_t source[] = {
        UINT32_C(0xd503201f),
        UINT32_C(0x94000000),
        UINT32_C(0x14000000)
    };
    db_x64_arm64_relocation relocs[2];
    db_x64_arm64_helper_targets table = targets();
    uint32_t output[3];
    db_x64_arm64_resolution decisions[2];
    db_x64_arm64_relocate_summary summary;

    relocs[0] = relocation(1u, DB_X64_ARM64_RELOCATION_CALL26,
                           DB_X64_ARM64_HELPER_READ_GUEST_U64);
    relocs[1] = relocation(2u, DB_X64_ARM64_RELOCATION_JUMP26,
                           DB_X64_ARM64_HELPER_DISPATCH_DIRECT);
    table.addresses[DB_X64_ARM64_HELPER_READ_GUEST_U64] =
        UINT64_C(0x100104);
    table.addresses[DB_X64_ARM64_HELPER_DISPATCH_DIRECT] =
        UINT64_C(0x0fff00);
    fill(output, sizeof(output), 0u);
    fill(decisions, sizeof(decisions), 0u);
    fill(&summary, sizeof(summary), 0u);

    CHECK(db_x64_arm64_resolve_helpers(
              source, 3u, relocs, 2u, &table, UINT64_C(0x100000),
              output, 3u, decisions, 2u, &summary) ==
          DB_X64_ARM64_RELOCATE_OK);
    CHECK(output[0] == source[0]);
    CHECK(output[1] == UINT32_C(0x94000040));
    CHECK(output[2] == UINT32_C(0x17ffffbe));
    CHECK(decisions[0].resolution == DB_X64_ARM64_RESOLUTION_DIRECT);
    CHECK(decisions[0].source_address == UINT64_C(0x100004));
    CHECK(decisions[0].helper_target == UINT64_C(0x100104));
    CHECK(decisions[1].resolution == DB_X64_ARM64_RESOLUTION_DIRECT);
    CHECK(summary.output_word_count == 3u);
    CHECK(summary.direct_count == 2u);
    CHECK(summary.thunk_count == 0u);
    return 1;
}

static int test_far_thunks(void)
{
    const uint32_t source[] = {
        UINT32_C(0xd503201f),
        UINT32_C(0x94000000),
        UINT32_C(0xd503201f),
        UINT32_C(0x14000000)
    };
    db_x64_arm64_relocation relocs[2];
    db_x64_arm64_helper_targets table = targets();
    uint32_t output[12];
    db_x64_arm64_resolution decisions[2];
    db_x64_arm64_relocate_summary summary;

    relocs[0] = relocation(1u, DB_X64_ARM64_RELOCATION_CALL26,
                           DB_X64_ARM64_HELPER_READ_GUEST_U64);
    relocs[1] = relocation(3u, DB_X64_ARM64_RELOCATION_JUMP26,
                           DB_X64_ARM64_HELPER_DISPATCH_TARGET);
    table.addresses[DB_X64_ARM64_HELPER_READ_GUEST_U64] =
        UINT64_C(0x00007fff12345678);
    table.addresses[DB_X64_ARM64_HELPER_DISPATCH_TARGET] =
        UINT64_C(0xffff800012340000);
    fill(output, sizeof(output), 0u);
    fill(decisions, sizeof(decisions), 0u);
    fill(&summary, sizeof(summary), 0u);

    CHECK(db_x64_arm64_resolve_helpers(
              source, 4u, relocs, 2u, &table, UINT64_C(0x100000),
              output, 12u, decisions, 2u, &summary) ==
          DB_X64_ARM64_RELOCATE_OK);
    CHECK(output[1] == UINT32_C(0x94000003));
    CHECK(output[3] == UINT32_C(0x14000005));
    CHECK(output[4] == UINT32_C(0xd28acf10));
    CHECK(output[5] == UINT32_C(0xf2a24690));
    CHECK(output[6] == UINT32_C(0xf2cffff0));
    CHECK(output[7] == UINT32_C(0xd61f0200));
    CHECK(output[8] == UINT32_C(0xd2a24690));
    CHECK(output[9] == UINT32_C(0xf2d00010));
    CHECK(output[10] == UINT32_C(0xf2fffff0));
    CHECK(output[11] == UINT32_C(0xd61f0200));
    CHECK(decisions[0].resolution == DB_X64_ARM64_RESOLUTION_FAR_THUNK);
    CHECK(decisions[0].thunk_word_index == 4u);
    CHECK(decisions[0].thunk_word_count == 4u);
    CHECK(decisions[0].thunk_address == UINT64_C(0x100010));
    CHECK(decisions[1].thunk_word_index == 8u);
    CHECK(decisions[1].thunk_word_count == 4u);
    CHECK(summary.output_word_count == 12u);
    CHECK(summary.thunk_count == 2u);
    CHECK(summary.thunk_word_count == 8u);
    return 1;
}

static int test_no_relocations(void)
{
    const uint32_t source[] = {
        UINT32_C(0xd503201f),
        UINT32_C(0xd503201f)
    };
    db_x64_arm64_helper_targets table = targets();
    uint32_t output[2];
    db_x64_arm64_relocate_summary summary;

    fill(output, sizeof(output), 0u);
    fill(&summary, sizeof(summary), 0u);
    CHECK(db_x64_arm64_resolve_helpers(
              source, 2u, NULL, 0u, &table, UINT64_C(0x200000),
              output, 2u, NULL, 0u, &summary) ==
          DB_X64_ARM64_RELOCATE_OK);
    CHECK(output[0] == source[0]);
    CHECK(output[1] == source[1]);
    CHECK(summary.relocation_count == 0u);
    CHECK(summary.output_word_count == 2u);
    return 1;
}

static int test_failures_preserve(void)
{
    uint32_t source[2] = {
        UINT32_C(0xd503201f),
        UINT32_C(0x94000000)
    };
    db_x64_arm64_relocation reloc =
        relocation(1u, DB_X64_ARM64_RELOCATION_CALL26,
                   DB_X64_ARM64_HELPER_READ_GUEST_U64);
    db_x64_arm64_helper_targets table = targets();
    uint32_t output[8];
    uint32_t output_before[8];
    db_x64_arm64_resolution decision;
    db_x64_arm64_resolution decision_before;
    db_x64_arm64_relocate_summary summary;
    db_x64_arm64_relocate_summary summary_before;
    size_t index;

    table.addresses[DB_X64_ARM64_HELPER_READ_GUEST_U64] =
        UINT64_C(0x00007fff12345678);
    fill(output, sizeof(output), UINT8_C(0xa5));
    fill(&decision, sizeof(decision), UINT8_C(0x5a));
    fill(&summary, sizeof(summary), UINT8_C(0x3c));
    for (index = 0u; index < 8u; ++index) {
        output_before[index] = output[index];
    }
    decision_before = decision;
    summary_before = summary;

    CHECK(db_x64_arm64_resolve_helpers(
              source, 2u, &reloc, 1u, &table, UINT64_C(0x300000),
              output, 5u, &decision, 1u, &summary) ==
          DB_X64_ARM64_RELOCATE_WORD_CAPACITY);
    CHECK(same(output, output_before, sizeof(output)) != 0);
    CHECK(same(&decision, &decision_before, sizeof(decision)) != 0);
    CHECK(same(&summary, &summary_before, sizeof(summary)) != 0);

    source[1] = UINT32_C(0x14000000);
    CHECK(db_x64_arm64_resolve_helpers(
              source, 2u, &reloc, 1u, &table, UINT64_C(0x300000),
              output, 8u, &decision, 1u, &summary) ==
          DB_X64_ARM64_RELOCATE_PLACEHOLDER_MISMATCH);

    source[1] = UINT32_C(0x94000000);
    table.addresses[DB_X64_ARM64_HELPER_READ_GUEST_U64] = UINT64_C(3);
    CHECK(db_x64_arm64_resolve_helpers(
              source, 2u, &reloc, 1u, &table, UINT64_C(0x300000),
              output, 8u, &decision, 1u, &summary) ==
          DB_X64_ARM64_RELOCATE_BAD_TARGET_TABLE);

    table.addresses[DB_X64_ARM64_HELPER_READ_GUEST_U64] =
        UINT64_C(0x00007fff12345678);
    reloc.reserved[0] = UINT8_C(1);
    CHECK(db_x64_arm64_resolve_helpers(
              source, 2u, &reloc, 1u, &table, UINT64_C(0x300000),
              output, 8u, &decision, 1u, &summary) ==
          DB_X64_ARM64_RELOCATE_BAD_RELOCATION);
    return 1;
}

static int test_order_alias_and_base(void)
{
    const uint32_t source[] = {
        UINT32_C(0x94000000),
        UINT32_C(0x94000000)
    };
    db_x64_arm64_relocation relocs[2];
    db_x64_arm64_helper_targets table = targets();
    uint32_t output[8];
    db_x64_arm64_resolution decisions[2];
    db_x64_arm64_relocate_summary summary;

    relocs[0] = relocation(1u, DB_X64_ARM64_RELOCATION_CALL26,
                           DB_X64_ARM64_HELPER_READ_GUEST_U64);
    relocs[1] = relocation(0u, DB_X64_ARM64_RELOCATION_CALL26,
                           DB_X64_ARM64_HELPER_WRITE_GUEST_U64);
    table.addresses[DB_X64_ARM64_HELPER_READ_GUEST_U64] =
        UINT64_C(0x400100);
    table.addresses[DB_X64_ARM64_HELPER_WRITE_GUEST_U64] =
        UINT64_C(0x400200);

    CHECK(db_x64_arm64_resolve_helpers(
              source, 2u, relocs, 2u, &table, UINT64_C(0x400000),
              output, 8u, decisions, 2u, &summary) ==
          DB_X64_ARM64_RELOCATE_RELOCATION_ORDER);
    CHECK(db_x64_arm64_resolve_helpers(
              source, 2u, relocs, 2u, &table, UINT64_C(0x400002),
              output, 8u, decisions, 2u, &summary) ==
          DB_X64_ARM64_RELOCATE_BAD_CODE_BASE);
    CHECK(db_x64_arm64_resolve_helpers(
              source, 2u, relocs, 2u, &table, UINT64_C(0x400000),
              (uint32_t *)(void *)source, 2u,
              decisions, 2u, &summary) ==
          DB_X64_ARM64_RELOCATE_BUFFER_OVERLAP);
    return 1;
}

int main(void)
{
    if (!test_direct()) return 11;
    if (!test_far_thunks()) return 12;
    if (!test_no_relocations()) return 13;
    if (!test_failures_preserve()) return 14;
    if (!test_order_alias_and_base()) return 15;
    if (db_x64_arm64_relocate_status_name(
            DB_X64_ARM64_RELOCATE_OK)[0] != 'o') return 16;

    (void)write(STDOUT_FILENO,
                "X64 ARM64 RELOCATE TEST: PASS\n",
                sizeof("X64 ARM64 RELOCATE TEST: PASS\n") - 1u);
    return 0;
}
