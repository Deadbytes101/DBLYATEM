#include "dblyat/linux_arm64_guest_runtime.h"
#include "dblyat/linux_arm64_translation_entry.h"
#include "linux_arm64_guest_runtime_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("LINUX ARM64 GUEST RUNTIME TEST: FAIL line=%d\n",       \
                   __LINE__);                                                \
            return 0;                                                        \
        }                                                                    \
    } while (0)

#define IMAGE_SIZE 8192u
#define STACK_SIZE 256u
#define GATEWAY_SIZE 32u
#define PAGE_SIZE 4096u
#define IMAGE_BASE UINT64_C(0x0000000140000000)
#define STACK_BASE UINT64_C(0x0000700000000000)
#define GATEWAY_BASE UINT64_C(0x0000000180000000)

typedef struct fixture {
    uint8_t image[IMAGE_SIZE];
    uint8_t stack[STACK_SIZE];
    uint8_t gateway[GATEWAY_SIZE];
    db_map_page_protection pages[2];
    db_linux_arm64_guest_memory memory;
} fixture;

static int same(const void *left, const void *right, size_t size)
{
    return memcmp(left, right, size) == 0;
}

static int init_fixture(fixture *f)
{
    memset(f, 0, sizeof(*f));
    f->pages[0] = DB_MAP_PAGE_RX;
    f->pages[1] = DB_MAP_PAGE_RW;
    f->image[16] = UINT8_C(0x88);
    f->image[17] = UINT8_C(0x77);
    f->image[18] = UINT8_C(0x66);
    f->image[19] = UINT8_C(0x55);
    f->image[20] = UINT8_C(0x44);
    f->image[21] = UINT8_C(0x33);
    f->image[22] = UINT8_C(0x22);
    f->image[23] = UINT8_C(0x11);
    f->gateway[0] = UINT8_C(0xde);
    f->gateway[1] = UINT8_C(0xad);
    f->gateway[2] = UINT8_C(0xbe);
    f->gateway[3] = UINT8_C(0xef);
    f->gateway[4] = UINT8_C(0x44);
    f->gateway[5] = UINT8_C(0x33);
    f->gateway[6] = UINT8_C(0x22);
    f->gateway[7] = UINT8_C(0x11);
    CHECK(db_linux_arm64_guest_memory_init(
              &f->memory,
              f->image,
              sizeof(f->image),
              f->pages,
              2u,
              PAGE_SIZE,
              IMAGE_BASE,
              f->stack,
              sizeof(f->stack),
              STACK_BASE,
              f->gateway,
              sizeof(f->gateway),
              GATEWAY_BASE) == DB_LINUX_ARM64_GUEST_RUNTIME_OK);
    CHECK(db_linux_arm64_guest_memory_validate(&f->memory) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_OK);
    return 1;
}

static int test_memory_permissions(void)
{
    fixture f;
    uint64_t value = 0u;
    uint8_t image_before[8];
    uint8_t gateway_before[8];

    CHECK(init_fixture(&f));
    CHECK(db_linux_arm64_guest_memory_read_u64(
              &f.memory, IMAGE_BASE + UINT64_C(16), &value) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_OK);
    CHECK(value == UINT64_C(0x1122334455667788));

    memcpy(image_before, &f.image[16], sizeof(image_before));
    CHECK(db_linux_arm64_guest_memory_write_u64(
              &f.memory,
              IMAGE_BASE + UINT64_C(16),
              UINT64_C(0xaabbccddeeff0011)) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_ACCESS_DENIED);
    CHECK(same(image_before, &f.image[16], sizeof(image_before)));

    CHECK(db_linux_arm64_guest_memory_write_u64(
              &f.memory,
              IMAGE_BASE + PAGE_SIZE + UINT64_C(24),
              UINT64_C(0x8877665544332211)) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_OK);
    CHECK(db_linux_arm64_guest_memory_read_u64(
              &f.memory,
              IMAGE_BASE + PAGE_SIZE + UINT64_C(24),
              &value) == DB_LINUX_ARM64_GUEST_RUNTIME_OK);
    CHECK(value == UINT64_C(0x8877665544332211));

    CHECK(db_linux_arm64_guest_memory_write_u64(
              &f.memory,
              STACK_BASE + UINT64_C(32),
              UINT64_C(0x0102030405060708)) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_OK);
    CHECK(db_linux_arm64_guest_memory_read_u64(
              &f.memory, STACK_BASE + UINT64_C(32), &value) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_OK);
    CHECK(value == UINT64_C(0x0102030405060708));

    CHECK(db_linux_arm64_guest_memory_read_u64(
              &f.memory, GATEWAY_BASE, &value) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_OK);
    CHECK(value == UINT64_C(0x11223344efbeadde));
    memcpy(gateway_before, f.gateway, sizeof(gateway_before));
    CHECK(db_linux_arm64_guest_memory_write_u64(
              &f.memory, GATEWAY_BASE, UINT64_C(1)) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_ACCESS_DENIED);
    CHECK(same(gateway_before, f.gateway, sizeof(gateway_before)));

    CHECK(db_linux_arm64_guest_memory_read_u64(
              &f.memory, UINT64_C(0x2000), &value) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_NOT_MAPPED);
    CHECK(db_linux_arm64_guest_memory_read_u64(
              &f.memory, UINT64_C(0x0000800000000000), &value) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_NONCANONICAL_ADDRESS);
    return 1;
}

static int bind_context(fixture *f,
                        db_linux_arm64_translation_context *context)
{
    CHECK(db_linux_arm64_translation_context_init(context) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
    CHECK(db_linux_arm64_translation_context_bind_guest_memory(
              context, &f->memory) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
    CHECK(context->guest_memory_bound == 1u);
    CHECK(context->helper_fault == DB_LINUX_ARM64_HELPER_FAULT_NONE);
    return 1;
}

static int test_address_and_memory_helpers(void)
{
    fixture f;
    db_linux_arm64_translation_context context;
    db_x64_scalar_state state;
    uint64_t address;
    uint64_t value;
    uint8_t before[8];

    CHECK(init_fixture(&f));
    CHECK(bind_context(&f, &context));
    memset(&state, 0, sizeof(state));
    state.registers[0] = IMAGE_BASE + PAGE_SIZE;
    state.registers[1] = UINT64_C(3);
    state.rflags = DB_X64_RFLAGS_FIXED_ONE;

    address = db_linux_arm64_helper_form_guest_address_core(
        &state,
        &context,
        0u,
        (uint64_t)(int64_t)-4,
        DB_IR_ADDRESS_BASE_INDEX,
        0u,
        1u,
        4u);
    CHECK(address == IMAGE_BASE + PAGE_SIZE + UINT64_C(8));
    CHECK(context.helper_fault == DB_LINUX_ARM64_HELPER_FAULT_NONE);

    db_linux_arm64_helper_write_guest_u64_core(
        &context, address, UINT64_C(0xfeedfacecafebeef));
    CHECK(context.helper_fault == DB_LINUX_ARM64_HELPER_FAULT_NONE);
    value = db_linux_arm64_helper_read_guest_u64_core(&context, address);
    CHECK(value == UINT64_C(0xfeedfacecafebeef));

    address = db_linux_arm64_helper_form_guest_address_core(
        &state,
        &context,
        IMAGE_BASE + UINT64_C(6),
        UINT64_C(10),
        DB_IR_ADDRESS_IP_RELATIVE,
        DB_IR_REGISTER_NONE,
        DB_IR_REGISTER_NONE,
        1u);
    CHECK(address == IMAGE_BASE + UINT64_C(16));
    CHECK(db_linux_arm64_helper_read_guest_u64_core(&context, address) ==
          UINT64_C(0x1122334455667788));

    memcpy(before, &f.image[16], sizeof(before));
    db_linux_arm64_helper_write_guest_u64_core(
        &context, IMAGE_BASE + UINT64_C(16), UINT64_C(0));
    CHECK(context.helper_fault == DB_LINUX_ARM64_HELPER_FAULT_WRITE);
    CHECK(context.helper_access == DB_LINUX_ARM64_GUEST_ACCESS_WRITE);
    CHECK(context.helper_fault_address == IMAGE_BASE + UINT64_C(16));
    CHECK(same(before, &f.image[16], sizeof(before)));

    db_linux_arm64_helper_write_guest_u64_core(
        &context, address + PAGE_SIZE, UINT64_C(0));
    CHECK(same(before, &f.image[16], sizeof(before)));
    CHECK(db_linux_arm64_helper_read_guest_u64_core(&context, address) == 0u);
    return 1;
}

static int test_stack_helpers(void)
{
    fixture f;
    db_linux_arm64_translation_context context;
    db_x64_scalar_state state;
    uint64_t value;

    CHECK(init_fixture(&f));
    CHECK(bind_context(&f, &context));
    memset(&state, 0, sizeof(state));
    state.rflags = DB_X64_RFLAGS_FIXED_ONE;
    state.registers[4] = STACK_BASE + STACK_SIZE;

    db_linux_arm64_helper_push_guest_u64_core(
        &state, &context, UINT64_C(0x123456789abcdef0));
    CHECK(context.helper_fault == DB_LINUX_ARM64_HELPER_FAULT_NONE);
    CHECK(state.registers[4] ==
          STACK_BASE + STACK_SIZE - UINT64_C(8));
    CHECK(db_linux_arm64_guest_memory_read_u64(
              &f.memory, state.registers[4], &value) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_OK);
    CHECK(value == UINT64_C(0x123456789abcdef0));

    value = db_linux_arm64_helper_pop_guest_u64_core(&state, &context);
    CHECK(value == UINT64_C(0x123456789abcdef0));
    CHECK(state.registers[4] == STACK_BASE + STACK_SIZE);

    state.registers[4] = STACK_BASE;
    db_linux_arm64_helper_push_guest_u64_core(
        &state, &context, UINT64_C(1));
    CHECK(context.helper_fault == DB_LINUX_ARM64_HELPER_FAULT_STACK);
    CHECK(state.registers[4] == STACK_BASE);
    return 1;
}

static int test_flags_and_conditions(void)
{
    fixture f;
    db_linux_arm64_translation_context context;
    db_x64_scalar_state state;
    static const uint8_t expected[16] = {
        1u, 0u, 1u, 0u, 1u, 0u, 1u, 0u,
        1u, 0u, 1u, 0u, 0u, 1u, 1u, 0u
    };
    uint32_t index;

    CHECK(init_fixture(&f));
    CHECK(bind_context(&f, &context));
    memset(&state, 0, sizeof(state));
    state.rflags = DB_X64_RFLAGS_FIXED_ONE |
                   DB_X64_RFLAGS_CF |
                   DB_X64_RFLAGS_PF |
                   DB_X64_RFLAGS_ZF |
                   DB_X64_RFLAGS_SF |
                   DB_X64_RFLAGS_OF |
                   DB_X64_RFLAGS_AF;
    state.registers[3] = 0u;
    db_linux_arm64_helper_update_logic_flags_core(
        &state, &context, DB_IR_WIDTH_64, 3u);
    CHECK(context.helper_fault == DB_LINUX_ARM64_HELPER_FAULT_NONE);
    CHECK((state.rflags & DB_X64_RFLAGS_ZF) != 0u);
    CHECK((state.rflags & DB_X64_RFLAGS_PF) != 0u);
    CHECK((state.rflags & DB_X64_RFLAGS_CF) == 0u);
    CHECK((state.rflags & DB_X64_RFLAGS_OF) == 0u);
    CHECK((state.rflags & DB_X64_RFLAGS_AF) == 0u);

    state.rflags = DB_X64_RFLAGS_FIXED_ONE |
                   DB_X64_RFLAGS_CF |
                   DB_X64_RFLAGS_PF |
                   DB_X64_RFLAGS_ZF |
                   DB_X64_RFLAGS_SF |
                   DB_X64_RFLAGS_OF;
    for (index = 0u; index < 16u; ++index) {
        CHECK(db_linux_arm64_helper_test_guest_condition_core(
                  &state, &context, index) == expected[index]);
    }
    return 1;
}

static int test_validation_and_targets(void)
{
    fixture f;
    db_linux_arm64_guest_memory bad;
    db_linux_arm64_translation_context context;
    db_linux_arm64_translation_context before;
    db_x64_arm64_helper_targets targets;

    CHECK(init_fixture(&f));
    CHECK(db_linux_arm64_translation_context_init(&context) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
    before = context;
    bad = f.memory;
    bad.stack_guest_base = IMAGE_BASE;
    CHECK(db_linux_arm64_guest_memory_validate(&bad) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_BAD_MEMORY);
    CHECK(db_linux_arm64_translation_context_bind_guest_memory(
              &context, &bad) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_GUEST_MEMORY);
    CHECK(same(&context, &before, sizeof(context)));

    memset(&targets, 0xa5, sizeof(targets));
#if defined(__aarch64__)
    CHECK(db_linux_arm64_guest_runtime_helper_targets(&targets) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_OK);
    CHECK(targets.addresses[DB_X64_ARM64_HELPER_NONE] == 0u);
    CHECK(targets.addresses[DB_X64_ARM64_HELPER_FORM_GUEST_ADDRESS] != 0u);
    CHECK(targets.addresses[DB_X64_ARM64_HELPER_RAISE_TRAP] != 0u);
#else
    db_x64_arm64_helper_targets targets_before = targets;
    CHECK(db_linux_arm64_guest_runtime_helper_targets(&targets) ==
          DB_LINUX_ARM64_GUEST_RUNTIME_UNSUPPORTED_HOST);
    CHECK(same(&targets, &targets_before, sizeof(targets)));
#endif
    return 1;
}

int main(void)
{
    CHECK(test_memory_permissions());
    CHECK(test_address_and_memory_helpers());
    CHECK(test_stack_helpers());
    CHECK(test_flags_and_conditions());
    CHECK(test_validation_and_targets());

    printf("LINUX ARM64 GUEST RUNTIME TEST: PASS\n");
    printf("GUEST ADDRESS SPACE PERMISSIONS: PASS\n");
    printf("FORM ADDRESS HELPER CORE: PASS\n");
    printf("READ WRITE U64 HELPER CORE: PASS\n");
    printf("PUSH POP STACK HELPER CORE: PASS\n");
    printf("LOGIC FLAGS HELPER CORE: PASS\n");
    printf("CONDITION HELPER CORE 16 OF 16: PASS\n");
    printf("STICKY HELPER FAULT: PASS\n");
    printf("AARCH64 HELPER WRAPPER EXECUTION: NOT RUN\n");
    printf("WIN32 DEMAND GUEST MEMORY BINDING: NOT IMPLEMENTED\n");
    printf("MULTI REGION GATEWAY CODE FETCH: NOT IMPLEMENTED\n");
    printf("NATIVE ARM64 PE EXECUTION: NOT RUN\n");
    return 0;
}
