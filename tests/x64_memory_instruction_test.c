#include "dblyat/x64_memory_instruction.h"

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
                     db_x64_memory_instruction_status expected)
{
    db_x64_memory_instruction instruction;
    db_x64_memory_instruction before;

    fill(&instruction, sizeof(instruction), UINT8_C(0xa5));
    before = instruction;

    return db_x64_memory_instruction_decode_one(
               bytes, size, &instruction) == expected &&
           same(&instruction, &before, sizeof(instruction));
}

static int test_base_load_and_trailing(void)
{
    static const uint8_t bytes[] = {
        UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x03),
        UINT8_C(0xcc), UINT8_C(0x90)
    };
    db_x64_memory_instruction instruction;

    CHECK(db_x64_memory_instruction_decode_one(
              bytes, sizeof(bytes), &instruction) ==
          DB_X64_MEMORY_INSTRUCTION_OK);
    CHECK(instruction.kind == DB_X64_MEMORY_INSTRUCTION_LOAD_R64);
    CHECK(instruction.rex == UINT8_C(0x48));
    CHECK(instruction.opcode == UINT8_C(0x8b));
    CHECK(instruction.register_operand == 0u);
    CHECK(instruction.instruction_length == 3u);
    CHECK(instruction.memory.encoded_length == 1u);
    CHECK(instruction.memory.instruction_length == 3u);
    CHECK(instruction.memory.modrm == UINT8_C(0x03));
    CHECK(instruction.memory.sib == DB_X64_MEMORY_NO_SIB);
    CHECK(instruction.memory.address.mode == DB_X64_ADDRESS_BASE_INDEX);
    CHECK(instruction.memory.address.base_register == 3u);
    CHECK(instruction.memory.address.index_register == DB_X64_REGISTER_NONE);
    CHECK(instruction.memory.address.scale == 1u);
    CHECK(instruction.memory.address.displacement == 0);
    CHECK(instruction.reserved[0] == 0u);
    CHECK(instruction.reserved[1] == 0u);
    CHECK(instruction.reserved[2] == 0u);
    CHECK(instruction.reserved[3] == 0u);
    return 1;
}

static int test_extended_sib_store(void)
{
    static const uint8_t bytes[] = {
        UINT8_C(0x4f), UINT8_C(0x89), UINT8_C(0x4c),
        UINT8_C(0xac), UINT8_C(0x7f)
    };
    db_x64_memory_instruction instruction;

    CHECK(db_x64_memory_instruction_decode_one(
              bytes, sizeof(bytes), &instruction) ==
          DB_X64_MEMORY_INSTRUCTION_OK);
    CHECK(instruction.kind == DB_X64_MEMORY_INSTRUCTION_STORE_R64);
    CHECK(instruction.register_operand == 9u);
    CHECK(instruction.instruction_length == 5u);
    CHECK(instruction.memory.modrm == UINT8_C(0x4c));
    CHECK(instruction.memory.sib == UINT8_C(0xac));
    CHECK(instruction.memory.address.mode == DB_X64_ADDRESS_BASE_INDEX);
    CHECK(instruction.memory.address.base_register == 12u);
    CHECK(instruction.memory.address.index_register == 13u);
    CHECK(instruction.memory.address.scale == 4u);
    CHECK(instruction.memory.address.displacement == 127);
    return 1;
}

static int test_rip_relative_load(void)
{
    static const uint8_t bytes[] = {
        UINT8_C(0x4c), UINT8_C(0x8b), UINT8_C(0x15),
        UINT8_C(0xfc), UINT8_C(0xff), UINT8_C(0xff), UINT8_C(0xff)
    };
    db_x64_memory_instruction instruction;

    CHECK(db_x64_memory_instruction_decode_one(
              bytes, sizeof(bytes), &instruction) ==
          DB_X64_MEMORY_INSTRUCTION_OK);
    CHECK(instruction.kind == DB_X64_MEMORY_INSTRUCTION_LOAD_R64);
    CHECK(instruction.register_operand == 10u);
    CHECK(instruction.instruction_length == 7u);
    CHECK(instruction.memory.address.mode == DB_X64_ADDRESS_RIP_RELATIVE);
    CHECK(instruction.memory.address.base_register == DB_X64_REGISTER_NONE);
    CHECK(instruction.memory.address.index_register == DB_X64_REGISTER_NONE);
    CHECK(instruction.memory.address.instruction_length == 7u);
    CHECK(instruction.memory.address.displacement == -4);
    return 1;
}

static int test_index_only_load(void)
{
    static const uint8_t bytes[] = {
        UINT8_C(0x4a), UINT8_C(0x8b), UINT8_C(0x04),
        UINT8_C(0xcd), UINT8_C(0x78), UINT8_C(0x56),
        UINT8_C(0x34), UINT8_C(0x12)
    };
    db_x64_memory_instruction instruction;

    CHECK(db_x64_memory_instruction_decode_one(
              bytes, sizeof(bytes), &instruction) ==
          DB_X64_MEMORY_INSTRUCTION_OK);
    CHECK(instruction.kind == DB_X64_MEMORY_INSTRUCTION_LOAD_R64);
    CHECK(instruction.register_operand == 0u);
    CHECK(instruction.instruction_length == 8u);
    CHECK(instruction.memory.address.mode == DB_X64_ADDRESS_BASE_INDEX);
    CHECK(instruction.memory.address.base_register == DB_X64_REGISTER_NONE);
    CHECK(instruction.memory.address.index_register == 9u);
    CHECK(instruction.memory.address.scale == 8u);
    CHECK(instruction.memory.address.displacement == INT64_C(0x12345678));
    return 1;
}

static int test_failures_preserve_output(void)
{
    static const uint8_t no_rex[] = {
        UINT8_C(0x8b), UINT8_C(0x03)
    };
    static const uint8_t rex_without_w[] = {
        UINT8_C(0x40), UINT8_C(0x8b), UINT8_C(0x03)
    };
    static const uint8_t legacy[] = {
        UINT8_C(0x66), UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x03)
    };
    static const uint8_t double_rex[] = {
        UINT8_C(0x48), UINT8_C(0x49), UINT8_C(0x8b), UINT8_C(0x03)
    };
    static const uint8_t unsupported[] = {
        UINT8_C(0x48), UINT8_C(0x31), UINT8_C(0x03)
    };
    static const uint8_t register_only[] = {
        UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0xc3)
    };
    static const uint8_t rex_only[] = {
        UINT8_C(0x48)
    };
    static const uint8_t opcode_only[] = {
        UINT8_C(0x48), UINT8_C(0x8b)
    };
    static const uint8_t missing_sib[] = {
        UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x04)
    };
    static const uint8_t missing_disp[] = {
        UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x05),
        UINT8_C(0x01), UINT8_C(0x02)
    };

    CHECK(preserved(no_rex,
                    sizeof(no_rex),
                    DB_X64_MEMORY_INSTRUCTION_BAD_REX));
    CHECK(preserved(rex_without_w,
                    sizeof(rex_without_w),
                    DB_X64_MEMORY_INSTRUCTION_BAD_REX));
    CHECK(preserved(legacy,
                    sizeof(legacy),
                    DB_X64_MEMORY_INSTRUCTION_UNSUPPORTED_PREFIX));
    CHECK(preserved(double_rex,
                    sizeof(double_rex),
                    DB_X64_MEMORY_INSTRUCTION_BAD_REX));
    CHECK(preserved(unsupported,
                    sizeof(unsupported),
                    DB_X64_MEMORY_INSTRUCTION_UNSUPPORTED_OPCODE));
    CHECK(preserved(register_only,
                    sizeof(register_only),
                    DB_X64_MEMORY_INSTRUCTION_REGISTER_ENCODING));
    CHECK(preserved(rex_only,
                    sizeof(rex_only),
                    DB_X64_MEMORY_INSTRUCTION_TRUNCATED));
    CHECK(preserved(opcode_only,
                    sizeof(opcode_only),
                    DB_X64_MEMORY_INSTRUCTION_TRUNCATED));
    CHECK(preserved(missing_sib,
                    sizeof(missing_sib),
                    DB_X64_MEMORY_INSTRUCTION_TRUNCATED));
    CHECK(preserved(missing_disp,
                    sizeof(missing_disp),
                    DB_X64_MEMORY_INSTRUCTION_TRUNCATED));
    CHECK(preserved(no_rex,
                    0u,
                    DB_X64_MEMORY_INSTRUCTION_EMPTY_INPUT));
    return 1;
}

static int test_guards_and_names(void)
{
    static const uint8_t load[] = {
        UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x03)
    };
    union alias_buffer {
        uint8_t bytes[sizeof(db_x64_memory_instruction)];
        db_x64_memory_instruction instruction;
    } alias;
    db_x64_memory_instruction instruction;

    alias.bytes[0] = UINT8_C(0x48);
    alias.bytes[1] = UINT8_C(0x8b);
    alias.bytes[2] = UINT8_C(0x03);

    CHECK(db_x64_memory_instruction_decode_one(
              alias.bytes,
              sizeof(alias.bytes),
              &alias.instruction) ==
          DB_X64_MEMORY_INSTRUCTION_BUFFER_OVERLAP);
    CHECK(db_x64_memory_instruction_decode_one(
              NULL,
              sizeof(load),
              &instruction) ==
          DB_X64_MEMORY_INSTRUCTION_NULL_ARGUMENT);
    CHECK(db_x64_memory_instruction_decode_one(
              load,
              sizeof(load),
              NULL) ==
          DB_X64_MEMORY_INSTRUCTION_NULL_ARGUMENT);
    CHECK(db_x64_memory_instruction_status_name(
              DB_X64_MEMORY_INSTRUCTION_OK)[0] == 'o');
    CHECK(db_x64_memory_instruction_status_name(
              DB_X64_MEMORY_INSTRUCTION_INVALID_ENCODING)[0] == 'i');
    CHECK(db_x64_memory_instruction_status_name(
              (db_x64_memory_instruction_status)99)[0] == 'u');
    CHECK(db_x64_memory_instruction_kind_name(
              DB_X64_MEMORY_INSTRUCTION_LOAD_R64)[0] == 'l');
    CHECK(db_x64_memory_instruction_kind_name(
              DB_X64_MEMORY_INSTRUCTION_STORE_R64)[0] == 's');
    CHECK(db_x64_memory_instruction_kind_name(
              (db_x64_memory_instruction_kind)99)[0] == 'u');
    return 1;
}

int main(void)
{
    static const char pass[] = "X64 MEMORY INSTRUCTION TEST: PASS\n";
    static const char fail[] = "X64 MEMORY INSTRUCTION TEST: FAIL\n";

    if (test_base_load_and_trailing() &&
        test_extended_sib_store() &&
        test_rip_relative_load() &&
        test_index_only_load() &&
        test_failures_preserve_output() &&
        test_guards_and_names()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
