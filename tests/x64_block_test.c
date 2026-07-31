#include "dblyat/x64_block.h"

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

static db_x64_code_view make_code(const uint8_t *bytes,
                                  size_t size,
                                  uint64_t guest_base)
{
    db_x64_code_view code;

    fill(&code, sizeof(code), 0u);
    code.bytes = bytes;
    code.size = size;
    code.guest_base = guest_base;
    return code;
}

static int test_mixed_block_terminator(void)
{
    const uint8_t bytes[] = {
        0x90u,
        0x48u, 0x8bu, 0x00u,
        0x75u, 0x02u
    };
    db_x64_code_view code =
        make_code(bytes, sizeof(bytes), UINT64_C(0x1000));
    db_x64_block_record records[8];
    db_x64_block_summary summary;

    fill(records, sizeof(records), UINT8_C(0xa5));
    fill(&summary, sizeof(summary), UINT8_C(0x5a));

    CHECK(db_x64_block_build(&code,
                             UINT64_C(0x1000),
                             8u,
                             64u,
                             records,
                             8u,
                             &summary) == DB_X64_BLOCK_OK);
    CHECK(summary.stop_reason == DB_X64_BLOCK_STOP_TERMINATOR);
    CHECK(summary.record_count == 3u);
    CHECK(summary.byte_count == 6u);
    CHECK(summary.source_rip == UINT64_C(0x1000));
    CHECK(summary.final_fallthrough_rip == UINT64_C(0x1006));

    CHECK(records[0].rip == UINT64_C(0x1000));
    CHECK(records[0].fallthrough_rip == UINT64_C(0x1001));
    CHECK(records[0].path == DB_X64_BLOCK_PATH_SCALAR);
    CHECK(records[0].control == DB_X64_BLOCK_CONTROL_LINEAR);
    CHECK(records[0].scalar.kind == DB_X64_INSTRUCTION_NOP);
    CHECK(records[0].has_direct_target == UINT8_C(0));

    CHECK(records[1].rip == UINT64_C(0x1001));
    CHECK(records[1].fallthrough_rip == UINT64_C(0x1004));
    CHECK(records[1].path == DB_X64_BLOCK_PATH_MEMORY);
    CHECK(records[1].control == DB_X64_BLOCK_CONTROL_LINEAR);
    CHECK(records[1].memory.kind ==
          DB_X64_MEMORY_INSTRUCTION_LOAD_R64);
    CHECK(records[1].memory.instruction_length == UINT8_C(3));

    CHECK(records[2].rip == UINT64_C(0x1004));
    CHECK(records[2].fallthrough_rip == UINT64_C(0x1006));
    CHECK(records[2].direct_target == UINT64_C(0x1008));
    CHECK(records[2].path == DB_X64_BLOCK_PATH_SCALAR);
    CHECK(records[2].control ==
          DB_X64_BLOCK_CONTROL_CONDITIONAL_JUMP);
    CHECK(records[2].has_direct_target == UINT8_C(1));
    CHECK(records[2].scalar.kind == DB_X64_INSTRUCTION_JCC_REL8);
    return 1;
}

static int test_direct_call_and_return(void)
{
    const uint8_t call_bytes[] = {
        0xe8u, 0xfbu, 0xffu, 0xffu, 0xffu
    };
    const uint8_t ret_bytes[] = {0xc3u};
    db_x64_code_view call_code =
        make_code(call_bytes, sizeof(call_bytes), UINT64_C(0x2000));
    db_x64_code_view ret_code =
        make_code(ret_bytes, sizeof(ret_bytes), UINT64_C(0x3000));
    db_x64_block_record records[2];
    db_x64_block_summary summary;

    CHECK(db_x64_block_build(&call_code,
                             UINT64_C(0x2000),
                             2u,
                             16u,
                             records,
                             2u,
                             &summary) == DB_X64_BLOCK_OK);
    CHECK(summary.stop_reason == DB_X64_BLOCK_STOP_TERMINATOR);
    CHECK(summary.record_count == 1u);
    CHECK(records[0].control == DB_X64_BLOCK_CONTROL_DIRECT_CALL);
    CHECK(records[0].fallthrough_rip == UINT64_C(0x2005));
    CHECK(records[0].direct_target == UINT64_C(0x2000));
    CHECK(records[0].has_direct_target == UINT8_C(1));

    CHECK(db_x64_block_build(&ret_code,
                             UINT64_C(0x3000),
                             2u,
                             16u,
                             records,
                             2u,
                             &summary) == DB_X64_BLOCK_OK);
    CHECK(summary.stop_reason == DB_X64_BLOCK_STOP_TERMINATOR);
    CHECK(summary.record_count == 1u);
    CHECK(records[0].control == DB_X64_BLOCK_CONTROL_RETURN);
    CHECK(records[0].has_direct_target == UINT8_C(0));
    CHECK(records[0].direct_target == 0u);
    return 1;
}

static int test_trap_terminators(void)
{
    const uint8_t int3[] = {0xccu};
    const uint8_t ud2[] = {0x0fu, 0x0bu};
    const uint8_t syscall[] = {0x0fu, 0x05u};
    const uint8_t *sequences[] = {int3, ud2, syscall};
    const size_t sizes[] = {sizeof(int3), sizeof(ud2), sizeof(syscall)};
    db_x64_block_record record;
    db_x64_block_summary summary;
    db_x64_code_view code;
    size_t index;

    for (index = 0u; index < 3u; ++index) {
        code = make_code(sequences[index],
                         sizes[index],
                         UINT64_C(0x4000));
        CHECK(db_x64_block_build(&code,
                                 UINT64_C(0x4000),
                                 1u,
                                 16u,
                                 &record,
                                 1u,
                                 &summary) == DB_X64_BLOCK_OK);
        CHECK(summary.stop_reason == DB_X64_BLOCK_STOP_TERMINATOR);
        CHECK(summary.record_count == 1u);
        CHECK(record.control == DB_X64_BLOCK_CONTROL_TRAP);
        CHECK(record.has_direct_target == UINT8_C(0));
    }

    return 1;
}

static int test_limits(void)
{
    const uint8_t bytes[] = {0x90u, 0x90u, 0x90u, 0x90u};
    db_x64_code_view code =
        make_code(bytes, sizeof(bytes), UINT64_C(0x5000));
    db_x64_block_record records[4];
    db_x64_block_summary summary;

    CHECK(db_x64_block_build(&code,
                             UINT64_C(0x5000),
                             2u,
                             4u,
                             records,
                             4u,
                             &summary) == DB_X64_BLOCK_OK);
    CHECK(summary.stop_reason ==
          DB_X64_BLOCK_STOP_INSTRUCTION_BUDGET);
    CHECK(summary.record_count == 2u);
    CHECK(summary.byte_count == 2u);

    CHECK(db_x64_block_build(&code,
                             UINT64_C(0x5000),
                             4u,
                             2u,
                             records,
                             4u,
                             &summary) == DB_X64_BLOCK_OK);
    CHECK(summary.stop_reason == DB_X64_BLOCK_STOP_BYTE_BUDGET);
    CHECK(summary.record_count == 2u);
    CHECK(summary.byte_count == 2u);

    CHECK(db_x64_block_build(&code,
                             UINT64_C(0x5000),
                             4u,
                             4u,
                             records,
                             1u,
                             &summary) == DB_X64_BLOCK_OK);
    CHECK(summary.stop_reason ==
          DB_X64_BLOCK_STOP_OUTPUT_CAPACITY);
    CHECK(summary.record_count == 1u);
    CHECK(summary.byte_count == 1u);
    return 1;
}

static int test_byte_budget_stops_before_record(void)
{
    const uint8_t bytes[] = {
        0xe8u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    db_x64_code_view code =
        make_code(bytes, sizeof(bytes), UINT64_C(0x6000));
    db_x64_block_record record;
    db_x64_block_record before;
    db_x64_block_summary summary;

    fill(&record, sizeof(record), UINT8_C(0x6a));
    before = record;

    CHECK(db_x64_block_build(&code,
                             UINT64_C(0x6000),
                             1u,
                             4u,
                             &record,
                             1u,
                             &summary) == DB_X64_BLOCK_OK);
    CHECK(summary.stop_reason == DB_X64_BLOCK_STOP_BYTE_BUDGET);
    CHECK(summary.record_count == 0u);
    CHECK(summary.byte_count == 0u);
    CHECK(same(&record, &before, sizeof(record)) != 0);
    return 1;
}

static int test_decode_failure_retains_prefix(void)
{
    const uint8_t bytes[] = {0x90u, 0xf1u};
    db_x64_code_view code =
        make_code(bytes, sizeof(bytes), UINT64_C(0x7000));
    db_x64_block_record records[2];
    db_x64_block_record second_before;
    db_x64_block_summary summary;

    fill(records, sizeof(records), UINT8_C(0x3c));
    second_before = records[1];

    CHECK(db_x64_block_build(&code,
                             UINT64_C(0x7000),
                             2u,
                             2u,
                             records,
                             2u,
                             &summary) == DB_X64_BLOCK_OK);
    CHECK(summary.stop_reason == DB_X64_BLOCK_STOP_DECODE_FAILED);
    CHECK(summary.record_count == 1u);
    CHECK(summary.byte_count == 1u);
    CHECK(summary.final_fallthrough_rip == UINT64_C(0x7001));
    CHECK(summary.scalar_decode_status != DB_X64_DECODE_OK);
    CHECK(summary.memory_decode_status !=
          DB_X64_MEMORY_INSTRUCTION_OK);
    CHECK(records[0].scalar.kind == DB_X64_INSTRUCTION_NOP);
    CHECK(same(&records[1],
               &second_before,
               sizeof(second_before)) != 0);
    return 1;
}

static int test_code_bounds_after_prefix(void)
{
    const uint8_t bytes[] = {0x90u};
    db_x64_code_view code =
        make_code(bytes, sizeof(bytes), UINT64_C(0x8000));
    db_x64_block_record records[2];
    db_x64_block_summary summary;

    CHECK(db_x64_block_build(&code,
                             UINT64_C(0x8000),
                             2u,
                             2u,
                             records,
                             2u,
                             &summary) == DB_X64_BLOCK_OK);
    CHECK(summary.stop_reason ==
          DB_X64_BLOCK_STOP_CODE_OUT_OF_BOUNDS);
    CHECK(summary.record_count == 1u);
    CHECK(summary.final_fallthrough_rip == UINT64_C(0x8001));
    return 1;
}

static int test_address_invalid(void)
{
    const uint8_t nop[] = {0x90u};
    const uint8_t jump[] = {0xebu, 0x01u};
    db_x64_code_view nop_code =
        make_code(nop,
                  sizeof(nop),
                  UINT64_C(0x00007fffffffffff));
    db_x64_code_view jump_code =
        make_code(jump,
                  sizeof(jump),
                  UINT64_C(0x00007ffffffffffd));
    db_x64_block_record record;
    db_x64_block_record before;
    db_x64_block_summary summary;

    fill(&record, sizeof(record), UINT8_C(0x71));
    before = record;

    CHECK(db_x64_block_build(&nop_code,
                             UINT64_C(0x00007fffffffffff),
                             1u,
                             1u,
                             &record,
                             1u,
                             &summary) == DB_X64_BLOCK_OK);
    CHECK(summary.stop_reason == DB_X64_BLOCK_STOP_ADDRESS_INVALID);
    CHECK(summary.record_count == 0u);
    CHECK(same(&record, &before, sizeof(record)) != 0);

    CHECK(db_x64_block_build(&jump_code,
                             UINT64_C(0x00007ffffffffffd),
                             1u,
                             2u,
                             &record,
                             1u,
                             &summary) == DB_X64_BLOCK_OK);
    CHECK(summary.stop_reason == DB_X64_BLOCK_STOP_ADDRESS_INVALID);
    CHECK(summary.record_count == 0u);
    CHECK(same(&record, &before, sizeof(record)) != 0);
    return 1;
}

static int test_api_failure_preserves_outputs(void)
{
    const uint8_t bytes[] = {0x90u};
    db_x64_code_view code =
        make_code(bytes, sizeof(bytes), UINT64_C(0x9000));
    db_x64_block_record record;
    db_x64_block_record record_before;
    db_x64_block_summary summary;
    db_x64_block_summary summary_before;

    fill(&record, sizeof(record), UINT8_C(0x45));
    fill(&summary, sizeof(summary), UINT8_C(0x54));
    record_before = record;
    summary_before = summary;

    CHECK(db_x64_block_build(&code,
                             UINT64_C(0x9000),
                             0u,
                             1u,
                             &record,
                             1u,
                             &summary) == DB_X64_BLOCK_BAD_LIMIT);
    CHECK(same(&record, &record_before, sizeof(record)) != 0);
    CHECK(same(&summary, &summary_before, sizeof(summary)) != 0);

    CHECK(db_x64_block_build(&code,
                             0u,
                             1u,
                             1u,
                             &record,
                             1u,
                             &summary) ==
          DB_X64_BLOCK_BAD_START_RIP);
    CHECK(same(&record, &record_before, sizeof(record)) != 0);
    CHECK(same(&summary, &summary_before, sizeof(summary)) != 0);

    code.reserved[0] = UINT8_C(1);
    CHECK(db_x64_block_build(&code,
                             UINT64_C(0x9000),
                             1u,
                             1u,
                             &record,
                             1u,
                             &summary) ==
          DB_X64_BLOCK_BAD_CODE_VIEW);
    CHECK(same(&record, &record_before, sizeof(record)) != 0);
    CHECK(same(&summary, &summary_before, sizeof(summary)) != 0);
    return 1;
}

static int test_alias_rejection(void)
{
    union aligned_storage {
        db_x64_block_record records[2];
        uint8_t bytes[sizeof(db_x64_block_record) * 2u];
    } storage;
    db_x64_code_view code;
    db_x64_block_summary summary;
    const uint8_t separate[] = {0x90u};
    db_x64_code_view separate_code =
        make_code(separate,
                  sizeof(separate),
                  UINT64_C(0xb000));
    db_x64_block_record records[2];

    fill(&storage, sizeof(storage), UINT8_C(0x90));
    code = make_code(storage.bytes,
                     sizeof(storage.bytes),
                     UINT64_C(0xa000));

    CHECK(db_x64_block_build(&code,
                             UINT64_C(0xa000),
                             1u,
                             1u,
                             storage.records,
                             2u,
                             &summary) ==
          DB_X64_BLOCK_BUFFER_OVERLAP);

    CHECK(db_x64_block_build(
              &separate_code,
              UINT64_C(0xb000),
              1u,
              1u,
              records,
              2u,
              (db_x64_block_summary *)(void *)records) ==
          DB_X64_BLOCK_BUFFER_OVERLAP);
    return 1;
}

static int test_null_arguments(void)
{
    const uint8_t bytes[] = {0x90u};
    db_x64_code_view code =
        make_code(bytes, sizeof(bytes), UINT64_C(0xc000));
    db_x64_block_record record;
    db_x64_block_summary summary;

    CHECK(db_x64_block_build(NULL,
                             UINT64_C(0xc000),
                             1u,
                             1u,
                             &record,
                             1u,
                             &summary) ==
          DB_X64_BLOCK_NULL_ARGUMENT);
    CHECK(db_x64_block_build(&code,
                             UINT64_C(0xc000),
                             1u,
                             1u,
                             NULL,
                             1u,
                             &summary) ==
          DB_X64_BLOCK_NULL_ARGUMENT);
    CHECK(db_x64_block_build(&code,
                             UINT64_C(0xc000),
                             1u,
                             1u,
                             &record,
                             1u,
                             NULL) ==
          DB_X64_BLOCK_NULL_ARGUMENT);
    return 1;
}

int main(void)
{
    CHECK(test_mixed_block_terminator());
    CHECK(test_direct_call_and_return());
    CHECK(test_trap_terminators());
    CHECK(test_limits());
    CHECK(test_byte_budget_stops_before_record());
    CHECK(test_decode_failure_retains_prefix());
    CHECK(test_code_bounds_after_prefix());
    CHECK(test_address_invalid());
    CHECK(test_api_failure_preserves_outputs());
    CHECK(test_alias_rejection());
    CHECK(test_null_arguments());

    (void)write(STDOUT_FILENO,
                "X64 BLOCK TEST: PASS\n",
                sizeof("X64 BLOCK TEST: PASS\n") - 1u);
    return 0;
}
