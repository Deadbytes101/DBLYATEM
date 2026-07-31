#include "dblyat/x64_memory_execute.h"

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

static void put_u64(uint8_t *bytes, size_t offset, uint64_t value)
{
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        bytes[offset + index] =
            (uint8_t)(value >> (unsigned int)(index * 8u));
    }
}

static uint64_t get_u64(const uint8_t *bytes, size_t offset)
{
    uint64_t value = 0u;
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[offset + index] <<
                 (unsigned int)(index * 8u);
    }

    return value;
}

static db_x64_scalar_state make_state(void)
{
    db_x64_scalar_state state;
    size_t index;

    for (index = 0u; index < 16u; ++index) {
        state.registers[index] = UINT64_C(0x1000) + (uint64_t)index;
    }

    state.rip = UINT64_C(0x400000);
    state.rflags = DB_X64_RFLAGS_FIXED_ONE | UINT64_C(0x200);
    return state;
}

static db_x64_memory_view make_view(uint8_t *bytes,
                                    size_t size,
                                    uint64_t guest_base,
                                    uint8_t permissions)
{
    db_x64_memory_view view;
    size_t index;

    view.bytes = bytes;
    view.size = size;
    view.guest_base = guest_base;
    view.permissions = permissions;
    for (index = 0u; index < sizeof(view.reserved); ++index) {
        view.reserved[index] = 0u;
    }
    return view;
}

static int preserved(const uint8_t *instruction,
                     size_t instruction_size,
                     db_x64_scalar_state *state,
                     db_x64_memory_view *view,
                     db_x64_memory_execute_status expected)
{
    uint8_t memory_before[128];
    db_x64_scalar_state state_before = *state;
    db_x64_memory_execute_summary summary;
    db_x64_memory_execute_summary summary_before;
    size_t index;

    CHECK(view->size <= sizeof(memory_before));
    for (index = 0u; index < view->size; ++index) {
        memory_before[index] = view->bytes[index];
    }

    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    summary_before = summary;

    CHECK(db_x64_memory_execute_one(instruction,
                                    instruction_size,
                                    state,
                                    view,
                                    &summary) == expected);
    CHECK(same(state, &state_before, sizeof(*state)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    CHECK(same(view->bytes, memory_before, view->size));
    return 1;
}

static int test_load_base(void)
{
    static const uint8_t instruction[] = {
        UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x08)
    };
    static const uint64_t loaded = UINT64_C(0x1122334455667788);
    uint8_t memory[64];
    db_x64_scalar_state state = make_state();
    db_x64_memory_view view;
    db_x64_memory_execute_summary summary;

    fill(memory, sizeof(memory), 0u);
    put_u64(memory, 16u, loaded);
    view = make_view(memory,
                     sizeof(memory),
                     UINT64_C(0x1000),
                     DB_X64_MEMORY_PERMISSION_READ |
                         DB_X64_MEMORY_PERMISSION_WRITE);
    state.registers[0] = UINT64_C(0x1010);

    CHECK(db_x64_memory_execute_one(instruction,
                                    sizeof(instruction),
                                    &state,
                                    &view,
                                    &summary) == DB_X64_MEMORY_EXECUTE_OK);
    CHECK(state.registers[1] == loaded);
    CHECK(state.rip == UINT64_C(0x400003));
    CHECK(state.rflags ==
          (DB_X64_RFLAGS_FIXED_ONE | UINT64_C(0x200)));
    CHECK(summary.instruction.kind ==
          DB_X64_MEMORY_INSTRUCTION_LOAD_R64);
    CHECK(summary.instruction.instruction_length == 3u);
    CHECK(summary.address.base_value == UINT64_C(0x1010));
    CHECK(summary.address.linear_address == UINT64_C(0x1010));
    CHECK(summary.access.kind == DB_X64_MEMORY_ACCESS_READ_U64);
    CHECK(summary.access.offset == 16u);
    CHECK(summary.access.value == loaded);
    CHECK(summary.source_rip == UINT64_C(0x400000));
    CHECK(summary.next_rip == UINT64_C(0x400003));
    CHECK(summary.previous_register == UINT64_C(0x1001));
    CHECK(summary.next_register == loaded);
    CHECK(summary.previous_rflags == state.rflags);
    CHECK(summary.next_rflags == state.rflags);
    CHECK(summary.register_operand == 1u);
    CHECK(summary.memory_written == 0u);
    return 1;
}

static int test_store_extended_register(void)
{
    static const uint8_t instruction[] = {
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0x08)
    };
    static const uint64_t stored = UINT64_C(0xfedcba9876543210);
    uint8_t memory[64];
    db_x64_scalar_state state = make_state();
    db_x64_memory_view view;
    db_x64_memory_execute_summary summary;

    fill(memory, sizeof(memory), UINT8_C(0xcc));
    view = make_view(memory,
                     sizeof(memory),
                     UINT64_C(0x2000),
                     DB_X64_MEMORY_PERMISSION_READ |
                         DB_X64_MEMORY_PERMISSION_WRITE);
    state.registers[0] = UINT64_C(0x2020);
    state.registers[9] = stored;

    CHECK(db_x64_memory_execute_one(instruction,
                                    sizeof(instruction),
                                    &state,
                                    &view,
                                    &summary) == DB_X64_MEMORY_EXECUTE_OK);
    CHECK(get_u64(memory, 32u) == stored);
    CHECK(state.registers[9] == stored);
    CHECK(state.rip == UINT64_C(0x400003));
    CHECK(summary.instruction.kind ==
          DB_X64_MEMORY_INSTRUCTION_STORE_R64);
    CHECK(summary.address.linear_address == UINT64_C(0x2020));
    CHECK(summary.access.kind == DB_X64_MEMORY_ACCESS_WRITE_U64);
    CHECK(summary.access.offset == 32u);
    CHECK(summary.access.value == stored);
    CHECK(summary.previous_register == stored);
    CHECK(summary.next_register == stored);
    CHECK(summary.register_operand == 9u);
    CHECK(summary.memory_written == 1u);
    return 1;
}

static int test_rip_relative_load(void)
{
    static const uint8_t instruction[] = {
        UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x05),
        UINT8_C(0x19), 0u, 0u, 0u
    };
    static const uint64_t loaded = UINT64_C(0x8877665544332211);
    uint8_t memory[64];
    db_x64_scalar_state state = make_state();
    db_x64_memory_view view;
    db_x64_memory_execute_summary summary;

    fill(memory, sizeof(memory), 0u);
    put_u64(memory, 32u, loaded);
    view = make_view(memory,
                     sizeof(memory),
                     UINT64_C(0x400000),
                     DB_X64_MEMORY_PERMISSION_READ);

    CHECK(db_x64_memory_execute_one(instruction,
                                    sizeof(instruction),
                                    &state,
                                    &view,
                                    &summary) == DB_X64_MEMORY_EXECUTE_OK);
    CHECK(state.registers[0] == loaded);
    CHECK(state.rip == UINT64_C(0x400007));
    CHECK(summary.address.mode == DB_X64_ADDRESS_RIP_RELATIVE);
    CHECK(summary.address.base_value == UINT64_C(0x400007));
    CHECK(summary.address.linear_address == UINT64_C(0x400020));
    CHECK(summary.access.offset == 32u);
    return 1;
}

static int test_self_modifying_store(void)
{
    static const uint64_t stored = UINT64_C(0x0102030405060708);
    uint8_t memory[32];
    db_x64_scalar_state state = make_state();
    db_x64_memory_view view;
    db_x64_memory_execute_summary summary;

    fill(memory, sizeof(memory), UINT8_C(0xcc));
    memory[0] = UINT8_C(0x48);
    memory[1] = UINT8_C(0x89);
    memory[2] = UINT8_C(0x08);
    view = make_view(memory,
                     sizeof(memory),
                     UINT64_C(0x3000),
                     DB_X64_MEMORY_PERMISSION_WRITE);
    state.registers[0] = UINT64_C(0x3000);
    state.registers[1] = stored;

    CHECK(db_x64_memory_execute_one(memory,
                                    3u,
                                    &state,
                                    &view,
                                    &summary) == DB_X64_MEMORY_EXECUTE_OK);
    CHECK(get_u64(memory, 0u) == stored);
    CHECK(summary.instruction.opcode == UINT8_C(0x89));
    CHECK(summary.access.guest_address == UINT64_C(0x3000));
    CHECK(summary.memory_written == 1u);
    return 1;
}

static int test_failures_preserve_everything(void)
{
    static const uint8_t load[] = {
        UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x08)
    };
    static const uint8_t store[] = {
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0x08)
    };
    static const uint8_t unsupported[] = {
        UINT8_C(0x48), UINT8_C(0x90)
    };
    uint8_t memory[64];
    db_x64_scalar_state state;
    db_x64_memory_view view;

    fill(memory, sizeof(memory), UINT8_C(0x5a));
    state = make_state();
    view = make_view(memory,
                     sizeof(memory),
                     UINT64_C(0x1000),
                     DB_X64_MEMORY_PERMISSION_READ |
                         DB_X64_MEMORY_PERMISSION_WRITE);
    state.registers[0] = UINT64_C(0x1010);
    CHECK(preserved(unsupported,
                    sizeof(unsupported),
                    &state,
                    &view,
                    DB_X64_MEMORY_EXECUTE_DECODE_FAILED));

    state.rip = 0u;
    CHECK(preserved(load,
                    sizeof(load),
                    &state,
                    &view,
                    DB_X64_MEMORY_EXECUTE_BAD_STATE));

    state = make_state();
    state.rflags = 0u;
    state.registers[0] = UINT64_C(0x1010);
    CHECK(preserved(load,
                    sizeof(load),
                    &state,
                    &view,
                    DB_X64_MEMORY_EXECUTE_BAD_STATE));

    state = make_state();
    state.rip = UINT64_MAX;
    state.registers[0] = UINT64_C(0x1010);
    CHECK(preserved(load,
                    sizeof(load),
                    &state,
                    &view,
                    DB_X64_MEMORY_EXECUTE_ADDRESS_OVERFLOW));

    state = make_state();
    state.rip = UINT64_C(0x00007ffffffffffe);
    state.registers[0] = UINT64_C(0x1010);
    CHECK(preserved(load,
                    sizeof(load),
                    &state,
                    &view,
                    DB_X64_MEMORY_EXECUTE_NONCANONICAL_ADDRESS));

    state = make_state();
    state.registers[0] = UINT64_C(0x0000800000000000);
    CHECK(preserved(load,
                    sizeof(load),
                    &state,
                    &view,
                    DB_X64_MEMORY_EXECUTE_ADDRESS_FAILED));

    state = make_state();
    state.registers[0] = UINT64_C(0x1010);
    view.permissions = DB_X64_MEMORY_PERMISSION_WRITE;
    CHECK(preserved(load,
                    sizeof(load),
                    &state,
                    &view,
                    DB_X64_MEMORY_EXECUTE_MEMORY_FAILED));

    state = make_state();
    state.registers[0] = UINT64_C(0x1010);
    view.permissions = DB_X64_MEMORY_PERMISSION_READ;
    CHECK(preserved(store,
                    sizeof(store),
                    &state,
                    &view,
                    DB_X64_MEMORY_EXECUTE_MEMORY_FAILED));

    state = make_state();
    state.registers[0] = UINT64_C(0x103d);
    view.permissions = DB_X64_MEMORY_PERMISSION_READ |
                       DB_X64_MEMORY_PERMISSION_WRITE;
    CHECK(preserved(load,
                    sizeof(load),
                    &state,
                    &view,
                    DB_X64_MEMORY_EXECUTE_MEMORY_FAILED));
    return 1;
}

static int test_alias_guards_and_names(void)
{
    static const uint8_t load[] = {
        UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x08)
    };
    uint8_t memory[64];
    db_x64_scalar_state state = make_state();
    db_x64_memory_view view;
    db_x64_memory_execute_summary summary;
    union state_summary_alias {
        db_x64_scalar_state state;
        db_x64_memory_execute_summary summary;
    } state_summary;
    union bytes_state_alias {
        uint8_t bytes[sizeof(db_x64_scalar_state)];
        db_x64_scalar_state state;
    } bytes_state;
    union backing_summary_alias {
        uint8_t bytes[sizeof(db_x64_memory_execute_summary)];
        db_x64_memory_execute_summary summary;
    } backing_summary;

    fill(memory, sizeof(memory), 0u);
    view = make_view(memory,
                     sizeof(memory),
                     UINT64_C(0x1000),
                     DB_X64_MEMORY_PERMISSION_READ);
    state.registers[0] = UINT64_C(0x1000);

    CHECK(db_x64_memory_execute_one(NULL,
                                    sizeof(load),
                                    &state,
                                    &view,
                                    &summary) ==
          DB_X64_MEMORY_EXECUTE_NULL_ARGUMENT);
    CHECK(db_x64_memory_execute_one(load,
                                    sizeof(load),
                                    NULL,
                                    &view,
                                    &summary) ==
          DB_X64_MEMORY_EXECUTE_NULL_ARGUMENT);
    CHECK(db_x64_memory_execute_one(load,
                                    sizeof(load),
                                    &state,
                                    NULL,
                                    &summary) ==
          DB_X64_MEMORY_EXECUTE_NULL_ARGUMENT);
    CHECK(db_x64_memory_execute_one(load,
                                    sizeof(load),
                                    &state,
                                    &view,
                                    NULL) ==
          DB_X64_MEMORY_EXECUTE_NULL_ARGUMENT);

    state_summary.state = state;
    CHECK(db_x64_memory_execute_one(load,
                                    sizeof(load),
                                    &state_summary.state,
                                    &view,
                                    &state_summary.summary) ==
          DB_X64_MEMORY_EXECUTE_BUFFER_OVERLAP);

    fill(bytes_state.bytes, sizeof(bytes_state.bytes), 0u);
    bytes_state.bytes[0] = UINT8_C(0x48);
    bytes_state.bytes[1] = UINT8_C(0x8b);
    bytes_state.bytes[2] = UINT8_C(0x08);
    CHECK(db_x64_memory_execute_one(bytes_state.bytes,
                                    3u,
                                    &bytes_state.state,
                                    &view,
                                    &summary) ==
          DB_X64_MEMORY_EXECUTE_BUFFER_OVERLAP);

    fill(backing_summary.bytes, sizeof(backing_summary.bytes), 0u);
    view = make_view(backing_summary.bytes,
                     sizeof(backing_summary.bytes),
                     UINT64_C(0x1000),
                     DB_X64_MEMORY_PERMISSION_READ);
    state = make_state();
    state.registers[0] = UINT64_C(0x1000);
    CHECK(db_x64_memory_execute_one(load,
                                    sizeof(load),
                                    &state,
                                    &view,
                                    &backing_summary.summary) ==
          DB_X64_MEMORY_EXECUTE_BUFFER_OVERLAP);

    CHECK(db_x64_memory_execute_status_name(
              DB_X64_MEMORY_EXECUTE_OK)[0] == 'o');
    CHECK(db_x64_memory_execute_status_name(
              DB_X64_MEMORY_EXECUTE_MEMORY_FAILED)[0] == 'g');
    CHECK(db_x64_memory_execute_status_name(
              (db_x64_memory_execute_status)99)[0] == 'u');
    return 1;
}

int main(void)
{
    static const char pass[] = "X64 MEMORY EXECUTE TEST: PASS\n";
    static const char fail[] = "X64 MEMORY EXECUTE TEST: FAIL\n";

    if (test_load_base() &&
        test_store_extended_register() &&
        test_rip_relative_load() &&
        test_self_modifying_store() &&
        test_failures_preserve_everything() &&
        test_alias_guards_and_names()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
