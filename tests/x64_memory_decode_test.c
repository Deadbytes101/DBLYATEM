#include "dblyat/x64_memory_decode.h"

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
                     uint8_t rex,
                     uint8_t leading_length,
                     db_x64_memory_decode_status expected)
{
    db_x64_memory_operand operand;
    db_x64_memory_operand before;

    fill(&operand, sizeof(operand), UINT8_C(0xa5));
    before = operand;
    return db_x64_memory_decode_one(bytes,
                                    size,
                                    rex,
                                    leading_length,
                                    &operand) == expected &&
           same(&operand, &before, sizeof(operand));
}

static int test_base_and_displacements(void)
{
    static const uint8_t base[] = {UINT8_C(0x10), UINT8_C(0xcc)};
    static const uint8_t extended[] = {UINT8_C(0x08)};
    static const uint8_t disp8[] = {UINT8_C(0x43), UINT8_C(0x80)};
    static const uint8_t disp32[] = {
        UINT8_C(0x82), UINT8_C(0xfc), UINT8_C(0xff),
        UINT8_C(0xff), UINT8_C(0xff)
    };
    db_x64_memory_operand operand;

    CHECK(db_x64_memory_decode_one(base,
                                   sizeof(base),
                                   0u,
                                   1u,
                                   &operand) == DB_X64_MEMORY_DECODE_OK);
    CHECK(operand.register_operand == 2u);
    CHECK(operand.address.mode == DB_X64_ADDRESS_BASE_INDEX);
    CHECK(operand.address.base_register == 0u);
    CHECK(operand.address.index_register == DB_X64_REGISTER_NONE);
    CHECK(operand.address.scale == 1u);
    CHECK(operand.address.displacement == 0);
    CHECK(operand.encoded_length == 1u);
    CHECK(operand.instruction_length == 2u);
    CHECK(operand.sib == DB_X64_MEMORY_NO_SIB);

    CHECK(db_x64_memory_decode_one(extended,
                                   sizeof(extended),
                                   UINT8_C(0x45),
                                   2u,
                                   &operand) == DB_X64_MEMORY_DECODE_OK);
    CHECK(operand.register_operand == 9u);
    CHECK(operand.address.base_register == 8u);

    CHECK(db_x64_memory_decode_one(disp8,
                                   sizeof(disp8),
                                   0u,
                                   2u,
                                   &operand) == DB_X64_MEMORY_DECODE_OK);
    CHECK(operand.address.base_register == 3u);
    CHECK(operand.address.displacement == -128);
    CHECK(operand.encoded_length == 2u);
    CHECK(operand.instruction_length == 4u);

    CHECK(db_x64_memory_decode_one(disp32,
                                   sizeof(disp32),
                                   0u,
                                   1u,
                                   &operand) == DB_X64_MEMORY_DECODE_OK);
    CHECK(operand.address.base_register == 2u);
    CHECK(operand.address.displacement == -4);
    CHECK(operand.encoded_length == 5u);
    CHECK(operand.instruction_length == 6u);
    return 1;
}

static int test_rip_relative(void)
{
    static const uint8_t bytes[] = {
        UINT8_C(0x05), UINT8_C(0x10), UINT8_C(0x00),
        UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0xcc)
    };
    db_x64_memory_operand operand;

    CHECK(db_x64_memory_decode_one(bytes,
                                   sizeof(bytes),
                                   UINT8_C(0x41),
                                   2u,
                                   &operand) == DB_X64_MEMORY_DECODE_OK);
    CHECK(operand.address.mode == DB_X64_ADDRESS_RIP_RELATIVE);
    CHECK(operand.address.base_register == DB_X64_REGISTER_NONE);
    CHECK(operand.address.index_register == DB_X64_REGISTER_NONE);
    CHECK(operand.address.displacement == 16);
    CHECK(operand.address.instruction_length == 7u);
    CHECK(operand.encoded_length == 5u);
    CHECK(operand.instruction_length == 7u);
    return 1;
}

static int test_sib_forms(void)
{
    static const uint8_t base_index[] = {
        UINT8_C(0x04), UINT8_C(0x88)
    };
    static const uint8_t no_index[] = {
        UINT8_C(0x04), UINT8_C(0xe2)
    };
    static const uint8_t extended_index[] = {
        UINT8_C(0x04), UINT8_C(0x20)
    };
    static const uint8_t index_only[] = {
        UINT8_C(0x04), UINT8_C(0x55),
        UINT8_C(0x78), UINT8_C(0x56), UINT8_C(0x34), UINT8_C(0x12)
    };
    static const uint8_t absolute[] = {
        UINT8_C(0x04), UINT8_C(0x25),
        UINT8_C(0xfc), UINT8_C(0xff), UINT8_C(0xff), UINT8_C(0xff)
    };
    static const uint8_t rbp_disp8[] = {
        UINT8_C(0x44), UINT8_C(0x25), UINT8_C(0x7f)
    };
    db_x64_memory_operand operand;

    CHECK(db_x64_memory_decode_one(base_index,
                                   sizeof(base_index),
                                   0u,
                                   1u,
                                   &operand) == DB_X64_MEMORY_DECODE_OK);
    CHECK(operand.address.base_register == 0u);
    CHECK(operand.address.index_register == 1u);
    CHECK(operand.address.scale == 4u);
    CHECK(operand.sib == UINT8_C(0x88));

    CHECK(db_x64_memory_decode_one(no_index,
                                   sizeof(no_index),
                                   0u,
                                   1u,
                                   &operand) == DB_X64_MEMORY_DECODE_OK);
    CHECK(operand.address.base_register == 2u);
    CHECK(operand.address.index_register == DB_X64_REGISTER_NONE);
    CHECK(operand.address.scale == 1u);

    CHECK(db_x64_memory_decode_one(extended_index,
                                   sizeof(extended_index),
                                   UINT8_C(0x42),
                                   1u,
                                   &operand) == DB_X64_MEMORY_DECODE_OK);
    CHECK(operand.address.base_register == 0u);
    CHECK(operand.address.index_register == 12u);
    CHECK(operand.address.scale == 1u);

    CHECK(db_x64_memory_decode_one(index_only,
                                   sizeof(index_only),
                                   0u,
                                   2u,
                                   &operand) == DB_X64_MEMORY_DECODE_OK);
    CHECK(operand.address.mode == DB_X64_ADDRESS_BASE_INDEX);
    CHECK(operand.address.base_register == DB_X64_REGISTER_NONE);
    CHECK(operand.address.index_register == 2u);
    CHECK(operand.address.scale == 2u);
    CHECK(operand.address.displacement == INT64_C(0x12345678));
    CHECK(operand.instruction_length == 8u);

    CHECK(db_x64_memory_decode_one(absolute,
                                   sizeof(absolute),
                                   0u,
                                   1u,
                                   &operand) == DB_X64_MEMORY_DECODE_OK);
    CHECK(operand.address.mode == DB_X64_ADDRESS_ABSOLUTE);
    CHECK(operand.address.base_register == DB_X64_REGISTER_NONE);
    CHECK(operand.address.index_register == DB_X64_REGISTER_NONE);
    CHECK(operand.address.displacement == -4);

    CHECK(db_x64_memory_decode_one(rbp_disp8,
                                   sizeof(rbp_disp8),
                                   UINT8_C(0x41),
                                   1u,
                                   &operand) == DB_X64_MEMORY_DECODE_OK);
    CHECK(operand.address.mode == DB_X64_ADDRESS_BASE_INDEX);
    CHECK(operand.address.base_register == 13u);
    CHECK(operand.address.index_register == DB_X64_REGISTER_NONE);
    CHECK(operand.address.displacement == 127);
    return 1;
}

static int test_failures(void)
{
    static const uint8_t one[] = {UINT8_C(0x00)};
    static const uint8_t register_only[] = {UINT8_C(0xc0)};
    static const uint8_t short_sib[] = {UINT8_C(0x04)};
    static const uint8_t short_disp8[] = {UINT8_C(0x40)};
    static const uint8_t short_disp32[] = {
        UINT8_C(0x80), UINT8_C(0x01), UINT8_C(0x02), UINT8_C(0x03)
    };
    static const uint8_t too_long[] = {
        UINT8_C(0x40), UINT8_C(0x00)
    };

    CHECK(preserved(one,
                    0u,
                    0u,
                    1u,
                    DB_X64_MEMORY_DECODE_EMPTY_INPUT));
    CHECK(preserved(one,
                    sizeof(one),
                    UINT8_C(0x20),
                    1u,
                    DB_X64_MEMORY_DECODE_BAD_REX));
    CHECK(preserved(one,
                    sizeof(one),
                    0u,
                    0u,
                    DB_X64_MEMORY_DECODE_BAD_LEADING_LENGTH));
    CHECK(preserved(one,
                    sizeof(one),
                    0u,
                    15u,
                    DB_X64_MEMORY_DECODE_BAD_LEADING_LENGTH));
    CHECK(preserved(register_only,
                    sizeof(register_only),
                    0u,
                    1u,
                    DB_X64_MEMORY_DECODE_REGISTER_ENCODING));
    CHECK(preserved(short_sib,
                    sizeof(short_sib),
                    0u,
                    1u,
                    DB_X64_MEMORY_DECODE_TRUNCATED));
    CHECK(preserved(short_disp8,
                    sizeof(short_disp8),
                    0u,
                    1u,
                    DB_X64_MEMORY_DECODE_TRUNCATED));
    CHECK(preserved(short_disp32,
                    sizeof(short_disp32),
                    0u,
                    1u,
                    DB_X64_MEMORY_DECODE_TRUNCATED));
    CHECK(preserved(too_long,
                    sizeof(too_long),
                    0u,
                    14u,
                    DB_X64_MEMORY_DECODE_INSTRUCTION_TOO_LONG));
    return 1;
}

static int test_guards_and_names(void)
{
    union alias_buffer {
        uint8_t bytes[128];
        db_x64_memory_operand operand;
    } alias;
    db_x64_memory_operand operand;
    static const uint8_t bytes[] = {UINT8_C(0x00)};

    alias.bytes[0] = UINT8_C(0x00);
    CHECK(db_x64_memory_decode_one(alias.bytes,
                                   sizeof(alias.bytes),
                                   0u,
                                   1u,
                                   &alias.operand) ==
          DB_X64_MEMORY_DECODE_BUFFER_OVERLAP);
    CHECK(db_x64_memory_decode_one(NULL,
                                   sizeof(bytes),
                                   0u,
                                   1u,
                                   &operand) == DB_X64_MEMORY_DECODE_NULL_ARGUMENT);
    CHECK(db_x64_memory_decode_one(bytes,
                                   sizeof(bytes),
                                   0u,
                                   1u,
                                   NULL) == DB_X64_MEMORY_DECODE_NULL_ARGUMENT);
    CHECK(db_x64_memory_decode_status_name(DB_X64_MEMORY_DECODE_OK)[0] == 'o');
    CHECK(db_x64_memory_decode_status_name(
              DB_X64_MEMORY_DECODE_REGISTER_ENCODING)[0] == 'r');
    CHECK(db_x64_memory_decode_status_name(
              (db_x64_memory_decode_status)99)[0] == 'u');
    return 1;
}

int main(void)
{
    static const char pass[] = "X64 MEMORY DECODE TEST: PASS\n";
    static const char fail[] = "X64 MEMORY DECODE TEST: FAIL\n";

    if (test_base_and_displacements() &&
        test_rip_relative() &&
        test_sib_forms() &&
        test_failures() &&
        test_guards_and_names()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
