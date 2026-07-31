#include "dblyat/x64_stack_execute.h"

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

static db_x64_memory_view make_view(uint8_t *bytes,
                                    size_t size,
                                    uint64_t guest_base,
                                    uint8_t permissions)
{
    db_x64_memory_view view;

    fill(&view, sizeof(view), 0u);
    view.bytes = bytes;
    view.size = size;
    view.guest_base = guest_base;
    view.permissions = permissions;
    return view;
}

static int test_call_success(void)
{
    const uint8_t instruction[] = {0xe8u, 0x20u, 0x00u, 0x00u, 0x00u};
    uint8_t memory[64];
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x2040));
    db_x64_memory_view view = make_view(
        memory, sizeof(memory), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ | DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_stack_execute_summary summary;

    fill(memory, sizeof(memory), UINT8_C(0xa5));
    fill(&summary, sizeof(summary), UINT8_C(0x5a));

    CHECK(db_x64_stack_execute_one(instruction,
                                   sizeof(instruction),
                                   &state,
                                   &view,
                                   &summary) == DB_X64_STACK_EXECUTE_OK);
    CHECK(state.rip == UINT64_C(0x1025));
    CHECK(state.registers[DB_X64_STACK_POINTER_REGISTER] ==
          UINT64_C(0x2038));
    CHECK(state.rflags == DB_X64_RFLAGS_FIXED_ONE);
    CHECK(get_u64(memory + 56u) == UINT64_C(0x1005));
    CHECK(summary.instruction.kind == DB_X64_INSTRUCTION_CALL_REL32);
    CHECK(summary.source_rip == UINT64_C(0x1000));
    CHECK(summary.fallthrough_rip == UINT64_C(0x1005));
    CHECK(summary.next_rip == UINT64_C(0x1025));
    CHECK(summary.previous_rsp == UINT64_C(0x2040));
    CHECK(summary.next_rsp == UINT64_C(0x2038));
    CHECK(summary.stack_value == UINT64_C(0x1005));
    CHECK(summary.access.kind == DB_X64_MEMORY_ACCESS_WRITE_U64);
    CHECK(summary.access.guest_address == UINT64_C(0x2038));
    CHECK(summary.memory_written == UINT8_C(1));
    return 1;
}

static int test_call_negative_relative(void)
{
    const uint8_t instruction[] = {0xe8u, 0xf0u, 0xffu, 0xffu, 0xffu};
    uint8_t memory[32];
    db_x64_scalar_state state = make_state(UINT64_C(0x1100),
                                           UINT64_C(0x3020));
    db_x64_memory_view view = make_view(
        memory, sizeof(memory), UINT64_C(0x3000),
        DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_stack_execute_summary summary;

    fill(memory, sizeof(memory), 0u);
    CHECK(db_x64_stack_execute_one(instruction,
                                   sizeof(instruction),
                                   &state,
                                   &view,
                                   &summary) == DB_X64_STACK_EXECUTE_OK);
    CHECK(state.rip == UINT64_C(0x10f5));
    CHECK(state.registers[DB_X64_STACK_POINTER_REGISTER] ==
          UINT64_C(0x3018));
    CHECK(get_u64(memory + 24u) == UINT64_C(0x1105));
    return 1;
}

static int test_ret_success(void)
{
    const uint8_t instruction[] = {0xc3u};
    uint8_t memory[32];
    uint8_t before[32];
    db_x64_scalar_state state = make_state(UINT64_C(0x4000),
                                           UINT64_C(0x5010));
    db_x64_memory_view view = make_view(
        memory, sizeof(memory), UINT64_C(0x5000),
        DB_X64_MEMORY_PERMISSION_READ);
    db_x64_stack_execute_summary summary;
    size_t index;

    fill(memory, sizeof(memory), UINT8_C(0x11));
    put_u64(memory + 16u, UINT64_C(0x6789));
    for (index = 0u; index < sizeof(memory); ++index) {
        before[index] = memory[index];
    }

    CHECK(db_x64_stack_execute_one(instruction,
                                   sizeof(instruction),
                                   &state,
                                   &view,
                                   &summary) == DB_X64_STACK_EXECUTE_OK);
    CHECK(state.rip == UINT64_C(0x6789));
    CHECK(state.registers[DB_X64_STACK_POINTER_REGISTER] ==
          UINT64_C(0x5018));
    CHECK(same(memory, before, sizeof(memory)) != 0);
    CHECK(summary.instruction.kind == DB_X64_INSTRUCTION_RET);
    CHECK(summary.fallthrough_rip == UINT64_C(0x4001));
    CHECK(summary.next_rip == UINT64_C(0x6789));
    CHECK(summary.stack_value == UINT64_C(0x6789));
    CHECK(summary.access.kind == DB_X64_MEMORY_ACCESS_READ_U64);
    CHECK(summary.memory_written == UINT8_C(0));
    return 1;
}

static int test_call_failure_preserves_all(void)
{
    const uint8_t instruction[] = {0xe8u, 0x00u, 0x00u, 0x00u, 0x00u};
    uint8_t memory[32];
    uint8_t memory_before[32];
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x2020));
    db_x64_scalar_state state_before = state;
    db_x64_memory_view view = make_view(
        memory, sizeof(memory), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ);
    db_x64_stack_execute_summary summary;
    db_x64_stack_execute_summary summary_before;
    size_t index;

    fill(memory, sizeof(memory), UINT8_C(0xa5));
    for (index = 0u; index < sizeof(memory); ++index) {
        memory_before[index] = memory[index];
    }
    fill(&summary, sizeof(summary), UINT8_C(0x5a));
    summary_before = summary;

    CHECK(db_x64_stack_execute_one(instruction,
                                   sizeof(instruction),
                                   &state,
                                   &view,
                                   &summary) ==
          DB_X64_STACK_EXECUTE_MEMORY_FAILED);
    CHECK(same(&state, &state_before, sizeof(state)) != 0);
    CHECK(same(&summary, &summary_before, sizeof(summary)) != 0);
    CHECK(same(memory, memory_before, sizeof(memory)) != 0);
    return 1;
}

static int test_ret_permission_failure(void)
{
    const uint8_t instruction[] = {0xc3u};
    uint8_t memory[32];
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x2000));
    db_x64_scalar_state state_before = state;
    db_x64_memory_view view = make_view(
        memory, sizeof(memory), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_stack_execute_summary summary;
    db_x64_stack_execute_summary summary_before;

    fill(memory, sizeof(memory), 0u);
    fill(&summary, sizeof(summary), UINT8_C(0x3c));
    summary_before = summary;

    CHECK(db_x64_stack_execute_one(instruction,
                                   sizeof(instruction),
                                   &state,
                                   &view,
                                   &summary) ==
          DB_X64_STACK_EXECUTE_MEMORY_FAILED);
    CHECK(same(&state, &state_before, sizeof(state)) != 0);
    CHECK(same(&summary, &summary_before, sizeof(summary)) != 0);
    return 1;
}

static int test_bounds_failure(void)
{
    const uint8_t call[] = {0xe8u, 0x00u, 0x00u, 0x00u, 0x00u};
    const uint8_t ret[] = {0xc3u};
    uint8_t memory[8];
    db_x64_scalar_state call_state = make_state(UINT64_C(0x1000),
                                                UINT64_C(0x1ff8));
    db_x64_scalar_state ret_state = make_state(UINT64_C(0x1000),
                                               UINT64_C(0x2008));
    db_x64_memory_view view = make_view(
        memory, sizeof(memory), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ | DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_stack_execute_summary summary;

    fill(memory, sizeof(memory), UINT8_C(0x44));
    CHECK(db_x64_stack_execute_one(call, sizeof(call), &call_state,
                                   &view, &summary) ==
          DB_X64_STACK_EXECUTE_MEMORY_FAILED);
    CHECK(db_x64_stack_execute_one(ret, sizeof(ret), &ret_state,
                                   &view, &summary) ==
          DB_X64_STACK_EXECUTE_MEMORY_FAILED);
    return 1;
}

static int test_noncanonical_targets(void)
{
    const uint8_t call[] = {0xe8u, 0x01u, 0x00u, 0x00u, 0x00u};
    const uint8_t ret[] = {0xc3u};
    uint8_t call_memory[16];
    uint8_t ret_memory[16];
    db_x64_scalar_state call_state = make_state(
        UINT64_C(0x00007ffffffffffa), UINT64_C(0x2010));
    db_x64_scalar_state ret_state = make_state(UINT64_C(0x1000),
                                               UINT64_C(0x3000));
    db_x64_memory_view call_view = make_view(
        call_memory, sizeof(call_memory), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_memory_view ret_view = make_view(
        ret_memory, sizeof(ret_memory), UINT64_C(0x3000),
        DB_X64_MEMORY_PERMISSION_READ);
    db_x64_stack_execute_summary summary;

    fill(call_memory, sizeof(call_memory), UINT8_C(0x7e));
    fill(ret_memory, sizeof(ret_memory), 0u);
    put_u64(ret_memory, UINT64_C(0x0000800000000000));

    CHECK(db_x64_stack_execute_one(call, sizeof(call), &call_state,
                                   &call_view, &summary) ==
          DB_X64_STACK_EXECUTE_NONCANONICAL_ADDRESS);
    CHECK(db_x64_stack_execute_one(ret, sizeof(ret), &ret_state,
                                   &ret_view, &summary) ==
          DB_X64_STACK_EXECUTE_NONCANONICAL_ADDRESS);
    return 1;
}

static int test_address_overflow(void)
{
    const uint8_t call[] = {0xe8u, 0x00u, 0x00u, 0x00u, 0x00u};
    const uint8_t ret[] = {0xc3u};
    uint8_t memory[16];
    db_x64_scalar_state rip_state = make_state(UINT64_MAX - UINT64_C(2),
                                               UINT64_C(0x2010));
    db_x64_scalar_state rsp_call = make_state(UINT64_C(0x1000),
                                              UINT64_C(4));
    db_x64_scalar_state rsp_ret = make_state(UINT64_C(0x1000),
                                             UINT64_MAX - UINT64_C(4));
    db_x64_memory_view view = make_view(
        memory, sizeof(memory), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ | DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_stack_execute_summary summary;

    fill(memory, sizeof(memory), 0u);
    CHECK(db_x64_stack_execute_one(call, sizeof(call), &rip_state,
                                   &view, &summary) ==
          DB_X64_STACK_EXECUTE_ADDRESS_OVERFLOW);
    CHECK(db_x64_stack_execute_one(call, sizeof(call), &rsp_call,
                                   &view, &summary) ==
          DB_X64_STACK_EXECUTE_ADDRESS_OVERFLOW);
    CHECK(db_x64_stack_execute_one(ret, sizeof(ret), &rsp_ret,
                                   &view, &summary) ==
          DB_X64_STACK_EXECUTE_ADDRESS_OVERFLOW);
    return 1;
}

static int test_bad_state(void)
{
    const uint8_t ret[] = {0xc3u};
    uint8_t memory[16];
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x2000));
    db_x64_memory_view view = make_view(
        memory, sizeof(memory), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ);
    db_x64_stack_execute_summary summary;

    state.rflags = 0u;
    CHECK(db_x64_stack_execute_one(ret, sizeof(ret), &state,
                                   &view, &summary) ==
          DB_X64_STACK_EXECUTE_BAD_STATE);
    state = make_state(UINT64_C(0x1000), 0u);
    CHECK(db_x64_stack_execute_one(ret, sizeof(ret), &state,
                                   &view, &summary) ==
          DB_X64_STACK_EXECUTE_BAD_STATE);
    return 1;
}

static int test_decode_and_unsupported(void)
{
    const uint8_t truncated[] = {0xe8u};
    const uint8_t nop[] = {0x90u};
    uint8_t memory[16];
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x2000));
    db_x64_memory_view view = make_view(
        memory, sizeof(memory), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ | DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_stack_execute_summary summary;

    CHECK(db_x64_stack_execute_one(truncated, sizeof(truncated), &state,
                                   &view, &summary) ==
          DB_X64_STACK_EXECUTE_DECODE_FAILED);
    CHECK(db_x64_stack_execute_one(nop, sizeof(nop), &state,
                                   &view, &summary) ==
          DB_X64_STACK_EXECUTE_UNSUPPORTED_INSTRUCTION);
    return 1;
}

static int test_alias_rejection(void)
{
    const uint8_t ret[] = {0xc3u};
    uint8_t memory[256];
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x2000));
    db_x64_memory_view view = make_view(
        memory, sizeof(memory), UINT64_C(0x2000),
        DB_X64_MEMORY_PERMISSION_READ);
    db_x64_stack_execute_summary summary;

    CHECK(db_x64_stack_execute_one(
              ret, sizeof(ret), &state, &view,
              (db_x64_stack_execute_summary *)(void *)&state) ==
          DB_X64_STACK_EXECUTE_BUFFER_OVERLAP);

    view.bytes = (uint8_t *)(void *)&state;
    view.size = sizeof(state);
    view.guest_base = UINT64_C(0x2000);
    CHECK(db_x64_stack_execute_one(ret, sizeof(ret), &state,
                                   &view, &summary) ==
          DB_X64_STACK_EXECUTE_BUFFER_OVERLAP);
    return 1;
}

static int test_self_modifying_call(void)
{
    uint8_t memory[32];
    db_x64_scalar_state state = make_state(UINT64_C(0x1000),
                                           UINT64_C(0x1008));
    db_x64_memory_view view = make_view(
        memory, sizeof(memory), UINT64_C(0x1000),
        DB_X64_MEMORY_PERMISSION_WRITE);
    db_x64_stack_execute_summary summary;

    fill(memory, sizeof(memory), UINT8_C(0xcc));
    memory[0] = UINT8_C(0xe8);
    memory[1] = 0u;
    memory[2] = 0u;
    memory[3] = 0u;
    memory[4] = 0u;

    CHECK(db_x64_stack_execute_one(memory, 5u, &state,
                                   &view, &summary) ==
          DB_X64_STACK_EXECUTE_OK);
    CHECK(state.rip == UINT64_C(0x1005));
    CHECK(state.registers[DB_X64_STACK_POINTER_REGISTER] ==
          UINT64_C(0x1000));
    CHECK(get_u64(memory) == UINT64_C(0x1005));
    return 1;
}

int main(void)
{
    CHECK(test_call_success());
    CHECK(test_call_negative_relative());
    CHECK(test_ret_success());
    CHECK(test_call_failure_preserves_all());
    CHECK(test_ret_permission_failure());
    CHECK(test_bounds_failure());
    CHECK(test_noncanonical_targets());
    CHECK(test_address_overflow());
    CHECK(test_bad_state());
    CHECK(test_decode_and_unsupported());
    CHECK(test_alias_rejection());
    CHECK(test_self_modifying_call());

    (void)write(STDOUT_FILENO,
                "X64 STACK EXECUTE TEST: PASS\n",
                sizeof("X64 STACK EXECUTE TEST: PASS\n") - 1u);
    return 0;
}
