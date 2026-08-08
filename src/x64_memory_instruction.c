#include "dblyat/x64_memory_instruction.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_x64_memory_instruction_overlap(const void *left,
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

static bool db_x64_memory_instruction_legacy_prefix(uint8_t value)
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

static db_x64_memory_instruction_status
db_x64_memory_instruction_map_memory_status(
    db_x64_memory_decode_status status)
{
    switch (status) {
    case DB_X64_MEMORY_DECODE_EMPTY_INPUT:
    case DB_X64_MEMORY_DECODE_TRUNCATED:
        return DB_X64_MEMORY_INSTRUCTION_TRUNCATED;
    case DB_X64_MEMORY_DECODE_REGISTER_ENCODING:
        return DB_X64_MEMORY_INSTRUCTION_REGISTER_ENCODING;
    case DB_X64_MEMORY_DECODE_INSTRUCTION_TOO_LONG:
        return DB_X64_MEMORY_INSTRUCTION_INSTRUCTION_TOO_LONG;
    case DB_X64_MEMORY_DECODE_BAD_REX:
    case DB_X64_MEMORY_DECODE_BAD_LEADING_LENGTH:
    case DB_X64_MEMORY_DECODE_NULL_ARGUMENT:
    case DB_X64_MEMORY_DECODE_BUFFER_OVERLAP:
    case DB_X64_MEMORY_DECODE_OK:
    default:
        return DB_X64_MEMORY_INSTRUCTION_INVALID_ENCODING;
    }
}

db_x64_memory_instruction_status db_x64_memory_instruction_decode_one(
    const uint8_t *bytes,
    size_t available_size,
    db_x64_memory_instruction *instruction)
{
    db_x64_memory_instruction planned;
    db_x64_memory_decode_status memory_status;
    size_t opcode_index = 0u;
    size_t modrm_index;
    uint8_t rex = 0u;
    uint8_t opcode;
    uint8_t opcode_extension = 0u;
    uint8_t leading_length;
    uint8_t minimum_length;

    if (bytes == NULL || instruction == NULL) {
        return DB_X64_MEMORY_INSTRUCTION_NULL_ARGUMENT;
    }

    if (db_x64_memory_instruction_overlap(bytes,
                                          available_size,
                                          instruction,
                                          sizeof(*instruction))) {
        return DB_X64_MEMORY_INSTRUCTION_BUFFER_OVERLAP;
    }

    if (available_size == 0u) {
        return DB_X64_MEMORY_INSTRUCTION_EMPTY_INPUT;
    }

    if (db_x64_memory_instruction_legacy_prefix(bytes[0])) {
        return DB_X64_MEMORY_INSTRUCTION_UNSUPPORTED_PREFIX;
    }

    if (bytes[0] >= UINT8_C(0x40) && bytes[0] <= UINT8_C(0x4f)) {
        rex = bytes[0];
        opcode_index = 1u;

        if (available_size == 1u) {
            return DB_X64_MEMORY_INSTRUCTION_TRUNCATED;
        }

        if (bytes[1] >= UINT8_C(0x40) && bytes[1] <= UINT8_C(0x4f)) {
            return DB_X64_MEMORY_INSTRUCTION_BAD_REX;
        }

        if (db_x64_memory_instruction_legacy_prefix(bytes[1])) {
            return DB_X64_MEMORY_INSTRUCTION_UNSUPPORTED_PREFIX;
        }
    }

    opcode = bytes[opcode_index];
    if (opcode != UINT8_C(0x8b) &&
        opcode != UINT8_C(0x89) &&
        opcode != UINT8_C(0xff)) {
        return DB_X64_MEMORY_INSTRUCTION_UNSUPPORTED_OPCODE;
    }

    if ((opcode == UINT8_C(0x8b) || opcode == UINT8_C(0x89)) &&
        (rex == 0u || (rex & UINT8_C(0x08)) == 0u)) {
        return DB_X64_MEMORY_INSTRUCTION_BAD_REX;
    }

    if (opcode == UINT8_C(0xff) &&
        (rex & UINT8_C(0x04)) != 0u) {
        return DB_X64_MEMORY_INSTRUCTION_BAD_REX;
    }

    modrm_index = opcode_index + 1u;
    if (available_size <= modrm_index) {
        return DB_X64_MEMORY_INSTRUCTION_TRUNCATED;
    }

    if (opcode == UINT8_C(0xff)) {
        opcode_extension =
            (uint8_t)((bytes[modrm_index] >> 3u) & UINT8_C(0x07));
        if (opcode_extension != UINT8_C(2) &&
            opcode_extension != UINT8_C(4)) {
            return DB_X64_MEMORY_INSTRUCTION_UNSUPPORTED_OPCODE;
        }
    }

    leading_length = (uint8_t)modrm_index;
    memory_status = db_x64_memory_decode_one(
        &bytes[modrm_index],
        available_size - modrm_index,
        rex,
        leading_length,
        &planned.memory);
    if (memory_status != DB_X64_MEMORY_DECODE_OK) {
        return db_x64_memory_instruction_map_memory_status(memory_status);
    }

    minimum_length = opcode == UINT8_C(0xff)
                         ? UINT8_C(2)
                         : UINT8_C(3);
    if (planned.memory.instruction_length < minimum_length ||
        planned.memory.instruction_length > (uint8_t)DB_X64_DECODE_MAX_LENGTH ||
        planned.memory.register_operand >= UINT8_C(16) ||
        planned.memory.address.reserved[0] != 0u ||
        planned.memory.address.reserved[1] != 0u ||
        planned.memory.address.reserved[2] != 0u ||
        planned.memory.address.reserved[3] != 0u ||
        planned.memory.reserved[0] != 0u ||
        planned.memory.reserved[1] != 0u ||
        planned.memory.reserved[2] != 0u) {
        return DB_X64_MEMORY_INSTRUCTION_INVALID_ENCODING;
    }

    if (opcode == UINT8_C(0xff) &&
        planned.memory.register_operand != opcode_extension) {
        return DB_X64_MEMORY_INSTRUCTION_INVALID_ENCODING;
    }

    if (opcode == UINT8_C(0x8b)) {
        planned.kind = DB_X64_MEMORY_INSTRUCTION_LOAD_R64;
        planned.register_operand = planned.memory.register_operand;
    } else if (opcode == UINT8_C(0x89)) {
        planned.kind = DB_X64_MEMORY_INSTRUCTION_STORE_R64;
        planned.register_operand = planned.memory.register_operand;
    } else if (opcode_extension == UINT8_C(2)) {
        planned.kind = DB_X64_MEMORY_INSTRUCTION_CALL_RM64;
        planned.register_operand = UINT8_C(0xff);
    } else {
        planned.kind = DB_X64_MEMORY_INSTRUCTION_JMP_RM64;
        planned.register_operand = UINT8_C(0xff);
    }

    planned.rex = rex;
    planned.opcode = opcode;
    planned.instruction_length = planned.memory.instruction_length;
    planned.opcode_extension = opcode_extension;
    planned.reserved[0] = 0u;
    planned.reserved[1] = 0u;
    planned.reserved[2] = 0u;

    *instruction = planned;
    return DB_X64_MEMORY_INSTRUCTION_OK;
}

const char *db_x64_memory_instruction_status_name(
    db_x64_memory_instruction_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "empty input",
        "truncated instruction",
        "unsupported prefix",
        "bad REX prefix",
        "unsupported opcode",
        "register-only ModRM encoding",
        "instruction too long",
        "invalid encoding"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 memory instruction status";
    }

    return names[(size_t)status];
}

const char *db_x64_memory_instruction_kind_name(
    db_x64_memory_instruction_kind kind)
{
    static const char *const names[] = {
        "load r64 from memory",
        "store r64 to memory",
        "call memory r/m64",
        "jump memory r/m64"
    };

    if ((size_t)kind >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 memory instruction kind";
    }

    return names[(size_t)kind];
}
