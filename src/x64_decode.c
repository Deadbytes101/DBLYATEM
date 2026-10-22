#include "dblyat/x64_decode.h"

#include <stdbool.h>
#include <stdint.h>

static bool db_x64_decode_overlap(const void *left,
                                  size_t left_size,
                                  const void *right,
                                  size_t right_size)
{
    const uintptr_t left_address = (uintptr_t)left;
    const uintptr_t right_address = (uintptr_t)right;

    if (left_size == 0u || right_size == 0u) {
        return false;
    }

    if (left_address <= right_address) {
        return right_address - left_address < left_size;
    }

    return left_address - right_address < right_size;
}

static bool db_x64_decode_legacy_prefix(uint8_t value)
{
    switch (value) {
    case UINT8_C(0x26):
    case UINT8_C(0x2e):
    case UINT8_C(0x36):
    case UINT8_C(0x3e):
    case UINT8_C(0x64):
    case UINT8_C(0x65):
    case UINT8_C(0x66):
    case UINT8_C(0x67):
    case UINT8_C(0xf0):
    case UINT8_C(0xf2):
    case UINT8_C(0xf3):
        return true;
    default:
        return false;
    }
}

static uint32_t db_x64_decode_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}

static uint64_t db_x64_decode_u64(const uint8_t *bytes)
{
    return (uint64_t)bytes[0] |
           ((uint64_t)bytes[1] << 8u) |
           ((uint64_t)bytes[2] << 16u) |
           ((uint64_t)bytes[3] << 24u) |
           ((uint64_t)bytes[4] << 32u) |
           ((uint64_t)bytes[5] << 40u) |
           ((uint64_t)bytes[6] << 48u) |
           ((uint64_t)bytes[7] << 56u);
}

static db_x64_instruction db_x64_decode_initial(void)
{
    db_x64_instruction instruction;

    instruction.kind = DB_X64_INSTRUCTION_NOP;
    instruction.immediate = 0u;
    instruction.relative = 0;
    instruction.length = 0u;
    instruction.rex = 0u;
    instruction.destination_register = DB_X64_REGISTER_NONE;
    instruction.source_register = DB_X64_REGISTER_NONE;
    instruction.condition = DB_X64_CONDITION_NONE;
    instruction.reserved[0] = 0u;
    instruction.reserved[1] = 0u;
    instruction.reserved[2] = 0u;
    return instruction;
}

static db_x64_decode_status db_x64_decode_simple(
    const uint8_t *bytes,
    size_t available_size,
    size_t opcode_index,
    uint8_t rex,
    db_x64_instruction *planned)
{
    const uint8_t opcode = bytes[opcode_index];

    if (rex != 0u) {
        return DB_X64_DECODE_INVALID_ENCODING;
    }

    switch (opcode) {
    case UINT8_C(0x90):
        planned->kind = DB_X64_INSTRUCTION_NOP;
        planned->length = 1u;
        return DB_X64_DECODE_OK;
    case UINT8_C(0xc3):
        planned->kind = DB_X64_INSTRUCTION_RET;
        planned->length = 1u;
        return DB_X64_DECODE_OK;
    case UINT8_C(0xcc):
        planned->kind = DB_X64_INSTRUCTION_INT3;
        planned->length = 1u;
        return DB_X64_DECODE_OK;
    case UINT8_C(0xe8):
    case UINT8_C(0xe9):
        if (available_size - opcode_index < 5u) {
            return DB_X64_DECODE_TRUNCATED;
        }
        planned->kind = opcode == UINT8_C(0xe8)
                            ? DB_X64_INSTRUCTION_CALL_REL32
                            : DB_X64_INSTRUCTION_JMP_REL32;
        planned->relative = (int64_t)(int32_t)db_x64_decode_u32(
            &bytes[opcode_index + 1u]);
        planned->length = 5u;
        return DB_X64_DECODE_OK;
    case UINT8_C(0xeb):
        if (available_size - opcode_index < 2u) {
            return DB_X64_DECODE_TRUNCATED;
        }
        planned->kind = DB_X64_INSTRUCTION_JMP_REL8;
        planned->relative = (int64_t)(int8_t)bytes[opcode_index + 1u];
        planned->length = 2u;
        return DB_X64_DECODE_OK;
    default:
        if (opcode >= UINT8_C(0x70) && opcode <= UINT8_C(0x7f)) {
            if (available_size - opcode_index < 2u) {
                return DB_X64_DECODE_TRUNCATED;
            }
            planned->kind = DB_X64_INSTRUCTION_JCC_REL8;
            planned->condition = (uint8_t)(opcode & UINT8_C(0x0f));
            planned->relative = (int64_t)(int8_t)bytes[opcode_index + 1u];
            planned->length = 2u;
            return DB_X64_DECODE_OK;
        }
        return DB_X64_DECODE_UNSUPPORTED_OPCODE;
    }
}

static db_x64_decode_status db_x64_decode_two_byte(
    const uint8_t *bytes,
    size_t available_size,
    size_t opcode_index,
    uint8_t rex,
    db_x64_instruction *planned)
{
    uint8_t opcode;

    if (rex != 0u) {
        return DB_X64_DECODE_INVALID_ENCODING;
    }

    if (available_size - opcode_index < 2u) {
        return DB_X64_DECODE_TRUNCATED;
    }

    opcode = bytes[opcode_index + 1u];
    if (opcode == UINT8_C(0x0b)) {
        planned->kind = DB_X64_INSTRUCTION_UD2;
        planned->length = 2u;
        return DB_X64_DECODE_OK;
    }

    if (opcode == UINT8_C(0x05)) {
        planned->kind = DB_X64_INSTRUCTION_SYSCALL;
        planned->length = 2u;
        return DB_X64_DECODE_OK;
    }

    if (opcode >= UINT8_C(0x80) && opcode <= UINT8_C(0x8f)) {
        if (available_size - opcode_index < 6u) {
            return DB_X64_DECODE_TRUNCATED;
        }
        planned->kind = DB_X64_INSTRUCTION_JCC_REL32;
        planned->condition = (uint8_t)(opcode & UINT8_C(0x0f));
        planned->relative = (int64_t)(int32_t)db_x64_decode_u32(
            &bytes[opcode_index + 2u]);
        planned->length = 6u;
        return DB_X64_DECODE_OK;
    }

    return DB_X64_DECODE_UNSUPPORTED_OPCODE;
}

static db_x64_decode_status db_x64_decode_register(
    const uint8_t *bytes,
    size_t available_size,
    size_t opcode_index,
    uint8_t rex,
    db_x64_instruction *planned)
{
    const uint8_t opcode = bytes[opcode_index];
    uint8_t modrm;
    uint8_t reg;
    uint8_t rm;

    if (opcode >= UINT8_C(0xb8) && opcode <= UINT8_C(0xbf)) {
        const bool wide = (rex & UINT8_C(0x08)) != 0u;
        const size_t required = wide ? 9u : 5u;
        if (available_size - opcode_index < required) {
            return DB_X64_DECODE_TRUNCATED;
        }
        planned->kind = wide ? DB_X64_INSTRUCTION_MOV_R64_IMM64
                             : DB_X64_INSTRUCTION_MOV_R32_IMM32;
        planned->destination_register =
            (uint8_t)((opcode - UINT8_C(0xb8)) |
                      ((rex & UINT8_C(0x01)) != 0u ? 8u : 0u));
        planned->immediate = wide
            ? db_x64_decode_u64(&bytes[opcode_index + 1u])
            : (uint64_t)db_x64_decode_u32(&bytes[opcode_index + 1u]);
        planned->length = wide ? UINT8_C(10)
                               : (uint8_t)(rex == 0u ? 5u : 6u);
        return DB_X64_DECODE_OK;
    }

    if (opcode != UINT8_C(0x89) &&
        opcode != UINT8_C(0x8b) &&
        opcode != UINT8_C(0x31)) {
        return DB_X64_DECODE_UNSUPPORTED_OPCODE;
    }

    if (available_size - opcode_index < 2u) {
        return DB_X64_DECODE_TRUNCATED;
    }

    modrm = bytes[opcode_index + 1u];
    if ((modrm & UINT8_C(0xc0)) != UINT8_C(0xc0)) {
        return DB_X64_DECODE_UNSUPPORTED_ADDRESSING;
    }

    reg = (uint8_t)(((modrm >> 3u) & UINT8_C(0x07)) |
                    ((rex & UINT8_C(0x04)) != 0u ? 8u : 0u));
    rm = (uint8_t)((modrm & UINT8_C(0x07)) |
                   ((rex & UINT8_C(0x01)) != 0u ? 8u : 0u));

    if (opcode == UINT8_C(0x31)) {
        planned->kind = (rex & UINT8_C(0x08)) != 0u
                            ? DB_X64_INSTRUCTION_XOR_R64_R64
                            : DB_X64_INSTRUCTION_XOR_R32_R32;
        planned->destination_register = rm;
        planned->source_register = reg;
        planned->length = (uint8_t)(rex != 0u ? 3u : 2u);
        return DB_X64_DECODE_OK;
    }

    if ((rex & UINT8_C(0x08)) == 0u) {
        return DB_X64_DECODE_INVALID_ENCODING;
    }

    planned->kind = DB_X64_INSTRUCTION_MOV_R64_R64;
    if (opcode == UINT8_C(0x89)) {
        planned->destination_register = rm;
        planned->source_register = reg;
    } else {
        planned->destination_register = reg;
        planned->source_register = rm;
    }
    planned->length = 3u;
    return DB_X64_DECODE_OK;
}

db_x64_decode_status db_x64_decode_one(
    const uint8_t *bytes,
    size_t available_size,
    db_x64_instruction *instruction)
{
    db_x64_instruction planned;
    db_x64_decode_status status;
    size_t opcode_index = 0u;
    uint8_t rex = 0u;
    uint8_t opcode;

    if (bytes == NULL || instruction == NULL) {
        return DB_X64_DECODE_NULL_ARGUMENT;
    }

    if (db_x64_decode_overlap(bytes,
                              available_size,
                              instruction,
                              sizeof(*instruction))) {
        return DB_X64_DECODE_BUFFER_OVERLAP;
    }

    if (available_size == 0u) {
        return DB_X64_DECODE_EMPTY_INPUT;
    }

    if (db_x64_decode_legacy_prefix(bytes[0])) {
        return DB_X64_DECODE_UNSUPPORTED_PREFIX;
    }

    if (bytes[0] >= UINT8_C(0x40) && bytes[0] <= UINT8_C(0x4f)) {
        rex = bytes[0];
        opcode_index = 1u;
        if (available_size == 1u) {
            return DB_X64_DECODE_TRUNCATED;
        }
        if (bytes[1] >= UINT8_C(0x40) && bytes[1] <= UINT8_C(0x4f)) {
            return DB_X64_DECODE_INVALID_ENCODING;
        }
        if (db_x64_decode_legacy_prefix(bytes[1])) {
            return DB_X64_DECODE_UNSUPPORTED_PREFIX;
        }
    }

    opcode = bytes[opcode_index];
    planned = db_x64_decode_initial();
    planned.rex = rex;

    if (opcode == UINT8_C(0x0f)) {
        status = db_x64_decode_two_byte(bytes,
                                        available_size,
                                        opcode_index,
                                        rex,
                                        &planned);
    } else if ((opcode >= UINT8_C(0xb8) && opcode <= UINT8_C(0xbf)) ||
               opcode == UINT8_C(0x89) ||
               opcode == UINT8_C(0x8b) ||
               opcode == UINT8_C(0x31)) {
        status = db_x64_decode_register(bytes,
                                        available_size,
                                        opcode_index,
                                        rex,
                                        &planned);
    } else {
        status = db_x64_decode_simple(bytes,
                                      available_size,
                                      opcode_index,
                                      rex,
                                      &planned);
    }

    if (status != DB_X64_DECODE_OK) {
        return status;
    }

    if (planned.length == 0u ||
        planned.length > DB_X64_DECODE_MAX_LENGTH ||
        planned.length > available_size) {
        return DB_X64_DECODE_INVALID_ENCODING;
    }

    *instruction = planned;
    return DB_X64_DECODE_OK;
}

const char *db_x64_decode_status_name(db_x64_decode_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "empty input",
        "truncated instruction",
        "unsupported prefix",
        "unsupported opcode",
        "unsupported addressing mode",
        "invalid encoding"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 decode status";
    }

    return names[(size_t)status];
}

const char *db_x64_instruction_kind_name(db_x64_instruction_kind kind)
{
    static const char *const names[] = {
        "nop",
        "ret",
        "int3",
        "ud2",
        "syscall",
        "call rel32",
        "jmp rel32",
        "jmp rel8",
        "jcc rel8",
        "jcc rel32",
        "mov r64, imm64",
        "mov r64, r64",
        "xor r32, r32",
        "xor r64, r64",
        "mov r32, imm32"
    };

    if ((size_t)kind >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 instruction kind";
    }

    return names[(size_t)kind];
}
