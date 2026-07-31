#include "dblyat/x64_run.h"

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

static void put_u64(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        bytes[index] = (uint8_t)(value >> (unsigned int)(index * 8u));
    }
}

static uint64_t get_u64(const uint8_t *bytes)
{
    uint64_t value = 0u;
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[index] << (unsigned int)(index * 8u);
    }

    return value;
}

static db_x64_scalar_state make_state(uint64_t rip, uint64_t rsp)
{
    db_x64_scalar_state state;

    fill(&state, sizeof(state), 0u);
    state.rip = rip;
    state.rflags = DB_X64_RFLAGS_FIXED_ONE;
    state.registers[DB_X64_STACK_POINTER_REGISTER] = rsp;
    return state;
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

static db_x64_memory_view make_memory(uint8_t *bytes,
                                      size_t size,
                                      uint64_t guest_base,
                                      uint8_t permissions)
{
    db_x64_memory_view memory;

    fill(&memory, sizeof(memory), 0u);
    memory.bytes = bytes;
    memory.size = size;
    memory.guest_base = guest_base;
    memory.permissions = permissions;
    return memory;
}

static int test_scalar_budget(void)
{
    const uint8_t instructions[] = {0x90u, 0x90u, 0x90u};
    uint8_t memory_bytes[16];
    db_x64_code_view code = make_code(
        instructions, sizeof(instructions), UINT64_C(0x1000));
    db_x64_memory_view memory = make_memory(
        memory_bytes, sizeof(memory_bytes), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ | DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x2008));
    db_x64_run_summary summary;

    fill(memory_bytes, sizeof(memory_bytes), 0u);
    CHECK(db_x64_run_steps(&code, 2u, &state, &memory, &summary) ==
          DB_X64_RUN_OK);
    CHECK(state.rip == UINT64_C(0x1002));
    CHECK(summary.source_rip == UINT64_C(0x1000));
    CHECK(summary.final_rip == UINT64_C(0x1002));
    CHECK(summary.step_budget == 2u);
    CHECK(summary.completed_steps == 2u);
    CHECK(summary.stop_reason == DB_X64_RUN_STOP_STEP_BUDGET);
    CHECK(summary.last_completed_kind == DB_X64_RUN_STEP_SCALAR);
    CHECK(summary.attempted_kind == DB_X64_RUN_STEP_NONE);
    return 1;
}

static int test_code_bounds_after_commit(void)
{
    const uint8_t instructions[] = {0x90u};
    uint8_t memory_bytes[16];
    db_x64_code_view code = make_code(
        instructions, sizeof(instructions), UINT64_C(0x1000));
    db_x64_memory_view memory = make_memory(
        memory_bytes, sizeof(memory_bytes), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ | DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x2008));
    db_x64_run_summary summary;

    fill(memory_bytes, sizeof(memory_bytes), 0u);
    CHECK(db_x64_run_steps(&code, 2u, &state, &memory, &summary) ==
          DB_X64_RUN_OK);
    CHECK(state.rip == UINT64_C(0x1001));
    CHECK(summary.completed_steps == 1u);
    CHECK(summary.stop_reason == DB_X64_RUN_STOP_CODE_OUT_OF_BOUNDS);
    CHECK(summary.last_completed_kind == DB_X64_RUN_STEP_SCALAR);
    CHECK(summary.attempted_kind == DB_X64_RUN_STEP_NONE);
    return 1;
}

static int test_decode_failure(void)
{
    const uint8_t instructions[] = {0xffu};
    uint8_t memory_bytes[16];
    db_x64_code_view code = make_code(
        instructions, sizeof(instructions), UINT64_C(0x1000));
    db_x64_memory_view memory = make_memory(
        memory_bytes, sizeof(memory_bytes), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ | DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x2008));
    db_x64_scalar_state before = state;
    db_x64_run_summary summary;

    fill(memory_bytes, sizeof(memory_bytes), 0u);
    CHECK(db_x64_run_steps(&code, 1u, &state, &memory, &summary) ==
          DB_X64_RUN_OK);
    CHECK(same(&state, &before, sizeof(state)) != 0);
    CHECK(summary.completed_steps == 0u);
    CHECK(summary.stop_reason == DB_X64_RUN_STOP_DECODE_FAILED);
    CHECK(summary.scalar_decode_status != DB_X64_DECODE_OK);
    CHECK(summary.memory_decode_status != DB_X64_MEMORY_INSTRUCTION_OK);
    return 1;
}

static int test_execute_failure_after_commit(void)
{
    const uint8_t instructions[] = {0x90u, 0xccu};
    uint8_t memory_bytes[16];
    db_x64_code_view code = make_code(
        instructions, sizeof(instructions), UINT64_C(0x1000));
    db_x64_memory_view memory = make_memory(
        memory_bytes, sizeof(memory_bytes), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ | DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x2008));
    db_x64_run_summary summary;

    fill(memory_bytes, sizeof(memory_bytes), 0u);
    CHECK(db_x64_run_steps(&code, 2u, &state, &memory, &summary) ==
          DB_X64_RUN_OK);
    CHECK(state.rip == UINT64_C(0x1001));
    CHECK(summary.completed_steps == 1u);
    CHECK(summary.stop_reason == DB_X64_RUN_STOP_EXECUTE_FAILED);
    CHECK(summary.attempted_kind == DB_X64_RUN_STEP_SCALAR);
    CHECK(summary.scalar_execute_status == DB_X64_EXECUTE_INTERPRET_FAILED);
    return 1;
}

static int test_memory_load_then_scalar(void)
{
    const uint8_t instructions[] = {0x48u, 0x8bu, 0x03u, 0x90u};
    uint8_t memory_bytes[16];
    const uint64_t expected = UINT64_C(0x8877665544332211);
    db_x64_code_view code = make_code(
        instructions, sizeof(instructions), UINT64_C(0x1000));
    db_x64_memory_view memory = make_memory(
        memory_bytes, sizeof(memory_bytes), UINT64_C(0x3000),
        DB_X64_MEMORY_PERMISSION_READ);
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x3008));
    db_x64_run_summary summary;

    fill(memory_bytes, sizeof(memory_bytes), 0u);
    put_u64(memory_bytes, expected);
    state.registers[3] = UINT64_C(0x3000);

    CHECK(db_x64_run_steps(&code, 2u, &state, &memory, &summary) ==
          DB_X64_RUN_OK);
    CHECK(state.registers[0] == expected);
    CHECK(state.rip == UINT64_C(0x1004));
    CHECK(summary.completed_steps == 2u);
    CHECK(summary.last_completed_kind == DB_X64_RUN_STEP_SCALAR);
    return 1;
}

static int test_memory_failure_after_scalar_commit(void)
{
    const uint8_t instructions[] = {0x90u, 0x48u, 0x8bu, 0x03u};
    uint8_t memory_bytes[8];
    db_x64_code_view code = make_code(
        instructions, sizeof(instructions), UINT64_C(0x1000));
    db_x64_memory_view memory = make_memory(
        memory_bytes, sizeof(memory_bytes), UINT64_C(0x3000),
        DB_X64_MEMORY_PERMISSION_READ);
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x3008));
    db_x64_run_summary summary;

    fill(memory_bytes, sizeof(memory_bytes), 0u);
    state.registers[3] = UINT64_C(0x4000);

    CHECK(db_x64_run_steps(&code, 2u, &state, &memory, &summary) ==
          DB_X64_RUN_OK);
    CHECK(state.rip == UINT64_C(0x1001));
    CHECK(summary.completed_steps == 1u);
    CHECK(summary.stop_reason == DB_X64_RUN_STOP_EXECUTE_FAILED);
    CHECK(summary.attempted_kind == DB_X64_RUN_STEP_MEMORY);
    CHECK(summary.memory_execute_status == DB_X64_MEMORY_EXECUTE_MEMORY_FAILED);
    return 1;
}

static int test_call_ret_roundtrip(void)
{
    const uint8_t instructions[] = {
        0xe8u, 0x05u, 0x00u, 0x00u, 0x00u,
        0x90u, 0x90u, 0x90u, 0x90u, 0x90u,
        0xc3u
    };
    uint8_t stack[32];
    db_x64_code_view code = make_code(
        instructions, sizeof(instructions), UINT64_C(0x1000));
    db_x64_memory_view memory = make_memory(
        stack, sizeof(stack), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ | DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x2020));
    db_x64_run_summary summary;

    fill(stack, sizeof(stack), 0u);
    CHECK(db_x64_run_steps(&code, 2u, &state, &memory, &summary) ==
          DB_X64_RUN_OK);
    CHECK(state.rip == UINT64_C(0x1005));
    CHECK(state.registers[DB_X64_STACK_POINTER_REGISTER] ==
          UINT64_C(0x2020));
    CHECK(get_u64(stack + 24u) == UINT64_C(0x1005));
    CHECK(summary.completed_steps == 2u);
    CHECK(summary.last_completed_kind == DB_X64_RUN_STEP_STACK);
    return 1;
}

static int test_self_modifying_fetch(void)
{
    uint8_t instructions[24];
    db_x64_code_view code;
    db_x64_memory_view memory;
    db_x64_scalar_state state;
    db_x64_run_summary summary;

    fill(instructions, sizeof(instructions), UINT8_C(0xcc));
    instructions[0] = UINT8_C(0x48);
    instructions[1] = UINT8_C(0x89);
    instructions[2] = UINT8_C(0x05);
    instructions[3] = 0u;
    instructions[4] = 0u;
    instructions[5] = 0u;
    instructions[6] = 0u;

    code = make_code(instructions, sizeof(instructions), UINT64_C(0x1000));
    memory = make_memory(
        instructions, sizeof(instructions), UINT64_C(0x1000),
        DB_X64_MEMORY_PERMISSION_READ | DB_X64_MEMORY_PERMISSION_WRITE);
    state = make_state(UINT64_C(0x1000), UINT64_C(0x1018));
    state.registers[0] = UINT64_C(0x9090909090909090);

    CHECK(db_x64_run_steps(&code, 2u, &state, &memory, &summary) ==
          DB_X64_RUN_OK);
    CHECK(state.rip == UINT64_C(0x1008));
    CHECK(instructions[7] == UINT8_C(0x90));
    CHECK(summary.completed_steps == 2u);
    CHECK(summary.last_completed_kind == DB_X64_RUN_STEP_SCALAR);
    return 1;
}

static int test_preflight_preservation(void)
{
    const uint8_t instructions[] = {0x90u};
    uint8_t memory_bytes[16];
    db_x64_code_view code = make_code(
        instructions, sizeof(instructions), UINT64_C(0x1000));
    db_x64_memory_view memory = make_memory(
        memory_bytes, sizeof(memory_bytes), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ | DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x2008));
    db_x64_scalar_state state_before = state;
    db_x64_run_summary summary;
    db_x64_run_summary summary_before;

    fill(memory_bytes, sizeof(memory_bytes), 0u);
    fill(&summary, sizeof(summary), UINT8_C(0x5a));
    summary_before = summary;

    CHECK(db_x64_run_steps(&code, 0u, &state, &memory, &summary) ==
          DB_X64_RUN_BAD_STEP_BUDGET);
    CHECK(same(&state, &state_before, sizeof(state)) != 0);
    CHECK(same(&summary, &summary_before, sizeof(summary)) != 0);

    code.reserved[0] = UINT8_C(1);
    CHECK(db_x64_run_steps(&code, 1u, &state, &memory, &summary) ==
          DB_X64_RUN_BAD_CODE_VIEW);
    CHECK(same(&state, &state_before, sizeof(state)) != 0);
    CHECK(same(&summary, &summary_before, sizeof(summary)) != 0);
    return 1;
}

static int test_alias_rejection(void)
{
    uint8_t memory_bytes[16];
    db_x64_memory_view memory = make_memory(
        memory_bytes, sizeof(memory_bytes), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ | DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x2008));
    db_x64_run_summary summary;
    db_x64_code_view code;

    fill(memory_bytes, sizeof(memory_bytes), 0u);
    fill(&summary, sizeof(summary), 0u);
    ((uint8_t *)(void *)&summary)[0] = UINT8_C(0x90);
    code = make_code((const uint8_t *)(const void *)&summary,
                     1u,
                     UINT64_C(0x1000));

    CHECK(db_x64_run_steps(&code, 1u, &state, &memory, &summary) ==
          DB_X64_RUN_BUFFER_OVERLAP);
    return 1;
}

int main(void)
{
    CHECK(test_scalar_budget());
    CHECK(test_code_bounds_after_commit());
    CHECK(test_decode_failure());
    CHECK(test_execute_failure_after_commit());
    CHECK(test_memory_load_then_scalar());
    CHECK(test_memory_failure_after_scalar_commit());
    CHECK(test_call_ret_roundtrip());
    CHECK(test_self_modifying_fetch());
    CHECK(test_preflight_preservation());
    CHECK(test_alias_rejection());

    (void)write(STDOUT_FILENO,
                "X64 RUN TEST: PASS\n",
                sizeof("X64 RUN TEST: PASS\n") - 1u);
    return 0;
}
