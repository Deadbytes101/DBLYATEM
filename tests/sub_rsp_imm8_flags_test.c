#include "linux_arm64_guest_runtime_internal.h"
#include "dblyat/x64_interpret.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    (void)fprintf(stderr, "SUB RSP FLAGS FAIL line=%d\\n", __LINE__); \
    return 1; \
} } while (0)

static int verify(uint64_t input_rsp, uint64_t imm,
                  uint64_t initial_flags, uint64_t output_rsp,
                  uint64_t output_flags)
{
    db_x64_scalar_state state = {0};
    db_linux_arm64_translation_context context = {0};
    state.registers[4] = input_rsp;
    state.rflags = initial_flags;
    db_linux_arm64_helper_sub_guest_imm8_core(&state, &context, imm, 4u);
    CHECK(context.helper_fault == DB_LINUX_ARM64_HELPER_FAULT_NONE);
    CHECK(state.registers[4] == output_rsp);
    if (state.rflags != output_flags) {
        (void)fprintf(stderr, "FLAGS input=%016llx imm=%016llx actual=%016llx expected=%016llx\n",
                      (unsigned long long)input_rsp,
                      (unsigned long long)imm,
                      (unsigned long long)state.rflags,
                      (unsigned long long)output_flags);
        return 1;
    }
    return 0;
}

static int verify_bad_operand(uint64_t imm, uint64_t index)
{
    db_x64_scalar_state state = {0};
    db_x64_scalar_state before;
    db_linux_arm64_translation_context context = {0};
    state.registers[4] = UINT64_C(0x70010000);
    state.rflags = DB_X64_RFLAGS_FIXED_ONE | DB_X64_RFLAGS_CF;
    before = state;
    db_linux_arm64_helper_sub_guest_imm8_core(&state, &context, imm, index);
    CHECK(context.helper_fault == DB_LINUX_ARM64_HELPER_FAULT_BAD_OPERAND);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    return 0;
}

int main(void)
{
    const uint64_t fixed = DB_X64_RFLAGS_FIXED_ONE;
    const uint64_t carry = DB_X64_RFLAGS_CF;
    const uint64_t parity = DB_X64_RFLAGS_PF;
    const uint64_t aux = DB_X64_RFLAGS_AF;
    const uint64_t zero = DB_X64_RFLAGS_ZF;
    const uint64_t overflow = DB_X64_RFLAGS_OF;
    const uint64_t iflag = UINT64_C(0x200);
    CHECK(verify(UINT64_C(0x70010000), UINT64_C(40),
                 fixed | iflag | carry | zero | overflow,
                 UINT64_C(0x7000ffd8), fixed | iflag | parity | aux) == 0);
    CHECK(verify(UINT64_C(40), UINT64_C(40), fixed,
                 UINT64_C(0), fixed | parity | zero) == 0);
    CHECK(verify(UINT64_C(0x1000), UINT64_MAX, fixed,
                 UINT64_C(0x1001), fixed | carry | aux) == 0);
    CHECK(verify(UINT64_C(0x8000000000000000), UINT64_C(1), fixed,
                 UINT64_C(0x7fffffffffffffff),
                 fixed | overflow | aux | parity) == 0);
    CHECK(verify(UINT64_C(0), UINT64_C(0xffffffffffffff80), fixed,
                 UINT64_C(128), fixed | carry) == 0);
    CHECK(verify_bad_operand(UINT64_C(40), UINT64_C(5)) == 0);
    CHECK(verify_bad_operand(UINT64_C(256), UINT64_C(4)) == 0);
    (void)puts("SUB RSP IMM8 FLAGS: PASS");
    return 0;
}
