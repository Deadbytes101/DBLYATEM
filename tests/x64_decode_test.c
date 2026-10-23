#include "dblyat/x64_decode.h"

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

static int preserved(const uint8_t *bytes,
                     size_t size,
                     db_x64_decode_status expected)
{
    db_x64_instruction instruction;
    db_x64_instruction before;

    fill(&instruction, sizeof(instruction), UINT8_C(0xa5));
    before = instruction;
    return db_x64_decode_one(bytes, size, &instruction) == expected &&
           same(&instruction, &before, sizeof(instruction));
}

static int test_simple(void)
{
    static const uint8_t nop[] = {UINT8_C(0x90)};
    static const uint8_t ret[] = {UINT8_C(0xc3)};
    static const uint8_t int3[] = {UINT8_C(0xcc)};
    static const uint8_t ud2[] = {UINT8_C(0x0f), UINT8_C(0x0b)};
    static const uint8_t syscall[] = {UINT8_C(0x0f), UINT8_C(0x05)};
    db_x64_instruction instruction;

    CHECK(db_x64_decode_one(nop, sizeof(nop), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_NOP);
    CHECK(instruction.length == 1u);
    CHECK(instruction.rex == 0u);
    CHECK(instruction.destination_register == DB_X64_REGISTER_NONE);
    CHECK(instruction.source_register == DB_X64_REGISTER_NONE);
    CHECK(instruction.condition == DB_X64_CONDITION_NONE);

    CHECK(db_x64_decode_one(ret, sizeof(ret), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_RET && instruction.length == 1u);

    CHECK(db_x64_decode_one(int3, sizeof(int3), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_INT3);

    CHECK(db_x64_decode_one(ud2, sizeof(ud2), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_UD2 && instruction.length == 2u);

    CHECK(db_x64_decode_one(syscall, sizeof(syscall), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_SYSCALL);
    return 1;
}

static int test_control_flow(void)
{
    static const uint8_t call[] = {UINT8_C(0xe8), 0x78u, 0x56u, 0x34u, 0x12u};
    static const uint8_t jmp32[] = {UINT8_C(0xe9), 0xfcu, 0xffu, 0xffu, 0xffu};
    static const uint8_t jmp8[] = {UINT8_C(0xeb), UINT8_C(0x80)};
    static const uint8_t jz8[] = {UINT8_C(0x74), UINT8_C(0xfe)};
    static const uint8_t jnz32[] = {
        UINT8_C(0x0f), UINT8_C(0x85), 1u, 0u, 0u, 0u
    };
    db_x64_instruction instruction;

    CHECK(db_x64_decode_one(call, sizeof(call), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_CALL_REL32);
    CHECK(instruction.relative == INT64_C(0x12345678));
    CHECK(instruction.length == 5u);

    CHECK(db_x64_decode_one(jmp32, sizeof(jmp32), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_JMP_REL32);
    CHECK(instruction.relative == -4);

    CHECK(db_x64_decode_one(jmp8, sizeof(jmp8), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_JMP_REL8);
    CHECK(instruction.relative == -128);

    CHECK(db_x64_decode_one(jz8, sizeof(jz8), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_JCC_REL8);
    CHECK(instruction.condition == 4u);
    CHECK(instruction.relative == -2);

    CHECK(db_x64_decode_one(jnz32, sizeof(jnz32), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_JCC_REL32);
    CHECK(instruction.condition == 5u);
    CHECK(instruction.relative == 1);
    return 1;
}

static int test_register_instructions(void)
{
    static const uint8_t mov_rax[] = {
        UINT8_C(0x48), UINT8_C(0xb8),
        0x88u, 0x77u, 0x66u, 0x55u, 0x44u, 0x33u, 0x22u, 0x11u
    };
    static const uint8_t mov_r9[] = {
        UINT8_C(0x49), UINT8_C(0xb9),
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u
    };
    static const uint8_t mov_r8_r9[] = {
        UINT8_C(0x4d), UINT8_C(0x89), UINT8_C(0xc8)
    };
    static const uint8_t mov_r10_r11[] = {
        UINT8_C(0x4d), UINT8_C(0x8b), UINT8_C(0xd3)
    };
    static const uint8_t mov_ecx[] = {
        UINT8_C(0xb9), 42u, 0u, 0u, 0u
    };
    static const uint8_t mov_r8d[] = {
        UINT8_C(0x41), UINT8_C(0xb8), 0xffu, 0xffu, 0xffu, 0xffu
    };
    static const uint8_t mov_r15d[] = {
        UINT8_C(0x41), UINT8_C(0xbf), 0x12u, 0x34u, 0x56u, 0x80u
    };
    static const uint8_t xor_eax[] = {UINT8_C(0x31), UINT8_C(0xc0)};
    static const uint8_t xor_rax[] = {
        UINT8_C(0x48), UINT8_C(0x31), UINT8_C(0xc0)
    };
    static const uint8_t xor_r8d_r9d[] = {
        UINT8_C(0x45), UINT8_C(0x31), UINT8_C(0xc8)
    };
    db_x64_instruction instruction;

    CHECK(db_x64_decode_one(mov_rax, sizeof(mov_rax), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_MOV_R64_IMM64);
    CHECK(instruction.destination_register == 0u);
    CHECK(instruction.immediate == UINT64_C(0x1122334455667788));
    CHECK(instruction.length == 10u);

    CHECK(db_x64_decode_one(mov_r9, sizeof(mov_r9), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.destination_register == 9u);
    CHECK(instruction.immediate == UINT64_C(0x0807060504030201));

    CHECK(db_x64_decode_one(mov_r8_r9, sizeof(mov_r8_r9), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_MOV_R64_R64);
    CHECK(instruction.destination_register == 8u);
    CHECK(instruction.source_register == 9u);

    CHECK(db_x64_decode_one(mov_r10_r11, sizeof(mov_r10_r11), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.destination_register == 10u);
    CHECK(instruction.source_register == 11u);

    CHECK(db_x64_decode_one(mov_ecx, sizeof(mov_ecx), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_MOV_R32_IMM32);
    CHECK(instruction.destination_register == 1u);
    CHECK(instruction.immediate == UINT64_C(42) && instruction.length == 5u);
    CHECK(db_x64_decode_one(mov_r8d, sizeof(mov_r8d), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_MOV_R32_IMM32);
    CHECK(instruction.destination_register == 8u);
    CHECK(instruction.immediate == UINT64_C(0xffffffff));
    CHECK(instruction.length == 6u);
    CHECK(db_x64_decode_one(mov_r15d, sizeof(mov_r15d), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.destination_register == 15u);
    CHECK(instruction.immediate == UINT64_C(0x80563412));

    CHECK(db_x64_decode_one(xor_eax, sizeof(xor_eax), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_XOR_R32_R32);
    CHECK(instruction.destination_register == 0u && instruction.source_register == 0u);

    CHECK(db_x64_decode_one(xor_rax, sizeof(xor_rax), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_XOR_R64_R64);

    CHECK(db_x64_decode_one(xor_r8d_r9d, sizeof(xor_r8d_r9d), &instruction) == DB_X64_DECODE_OK);
    CHECK(instruction.kind == DB_X64_INSTRUCTION_XOR_R32_R32);
    CHECK(instruction.destination_register == 8u);
    CHECK(instruction.source_register == 9u);
    return 1;
}

static int test_failures(void)
{
    static const uint8_t rex_only[] = {UINT8_C(0x48)};
    static const uint8_t double_rex[] = {UINT8_C(0x48), UINT8_C(0x49), UINT8_C(0x90)};
    static const uint8_t legacy[] = {UINT8_C(0x66), UINT8_C(0x90)};
    static const uint8_t rex_nop[] = {UINT8_C(0x48), UINT8_C(0x90)};
    static const uint8_t unsupported[] = {UINT8_C(0xf4)};
    static const uint8_t memory_mov[] = {
        UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x00)
    };
    static const uint8_t mov32_imm[] = {
        UINT8_C(0xb8), 1u, 0u, 0u, 0u
    };
    static const uint8_t short_call[] = {UINT8_C(0xe8), 1u, 2u};
    static const uint8_t short_jcc[] = {
        UINT8_C(0x0f), UINT8_C(0x84), 1u, 2u, 3u
    };

    CHECK(preserved(rex_only, sizeof(rex_only), DB_X64_DECODE_TRUNCATED));
    CHECK(preserved(double_rex, sizeof(double_rex), DB_X64_DECODE_INVALID_ENCODING));
    CHECK(preserved(legacy, sizeof(legacy), DB_X64_DECODE_UNSUPPORTED_PREFIX));
    CHECK(preserved(rex_nop, sizeof(rex_nop), DB_X64_DECODE_INVALID_ENCODING));
    CHECK(preserved(unsupported, sizeof(unsupported), DB_X64_DECODE_UNSUPPORTED_OPCODE));
    CHECK(preserved(memory_mov, sizeof(memory_mov), DB_X64_DECODE_UNSUPPORTED_ADDRESSING));
    CHECK(preserved(mov32_imm, sizeof(mov32_imm) - 1u, DB_X64_DECODE_TRUNCATED));
    CHECK(preserved(mov32_imm, 1u, DB_X64_DECODE_TRUNCATED));
    CHECK(preserved(short_call, sizeof(short_call), DB_X64_DECODE_TRUNCATED));
    CHECK(preserved(short_jcc, sizeof(short_jcc), DB_X64_DECODE_TRUNCATED));
    CHECK(preserved(unsupported, 0u, DB_X64_DECODE_EMPTY_INPUT));
    return 1;
}

static int test_guards_and_names(void)
{
    union alias_buffer {
        uint8_t bytes[64];
        db_x64_instruction instruction;
    } alias;
    db_x64_instruction instruction;
    static const uint8_t nop[] = {UINT8_C(0x90)};

    alias.bytes[0] = UINT8_C(0x90);
    CHECK(db_x64_decode_one(alias.bytes,
                            sizeof(alias.bytes),
                            &alias.instruction) == DB_X64_DECODE_BUFFER_OVERLAP);
    CHECK(db_x64_decode_one(NULL, 1u, &instruction) == DB_X64_DECODE_NULL_ARGUMENT);
    CHECK(db_x64_decode_one(nop, sizeof(nop), NULL) == DB_X64_DECODE_NULL_ARGUMENT);
    CHECK(db_x64_decode_status_name(DB_X64_DECODE_OK)[0] == 'o');
    CHECK(db_x64_decode_status_name(DB_X64_DECODE_INVALID_ENCODING)[0] == 'i');
    CHECK(db_x64_decode_status_name((db_x64_decode_status)99)[0] == 'u');
    CHECK(db_x64_instruction_kind_name(DB_X64_INSTRUCTION_NOP)[0] == 'n');
    CHECK(db_x64_instruction_kind_name(DB_X64_INSTRUCTION_XOR_R64_R64)[0] == 'x');
    CHECK(db_x64_instruction_kind_name(DB_X64_INSTRUCTION_MOV_R32_IMM32)[0] == 'm');
    CHECK(db_x64_instruction_kind_name((db_x64_instruction_kind)99)[0] == 'u');
    return 1;
}

int main(void)
{
    static const char pass[] = "X64 DECODE TEST: PASS\n";
    static const char fail[] = "X64 DECODE TEST: FAIL\n";

    if (test_simple() &&
        test_control_flow() &&
        test_register_instructions() &&
        test_failures() &&
        test_guards_and_names()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
