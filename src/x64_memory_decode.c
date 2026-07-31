#include "dblyat/x64_memory_decode.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_x64_memory_decode_overlap(const void *left,
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

static uint32_t db_x64_memory_decode_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}

static db_x64_memory_operand db_x64_memory_decode_initial(void)
{
    db_x64_memory_operand operand;

    operand.address.displacement = 0;
    operand.address.mode = DB_X64_ADDRESS_BASE_INDEX;
    operand.address.base_register = DB_X64_REGISTER_NONE;
    operand.address.index_register = DB_X64_REGISTER_NONE;
    operand.address.scale = UINT8_C(1);
    operand.address.instruction_length = 0u;
    operand.address.reserved[0] = 0u;
    operand.address.reserved[1] = 0u;
    operand.address.reserved[2] = 0u;
    operand.address.reserved[3] = 0u;
    operand.register_operand = DB_X64_REGISTER_NONE;
    operand.encoded_length = 0u;
    operand.instruction_length = 0u;
    operand.modrm = 0u;
    operand.sib = DB_X64_MEMORY_NO_SIB;
    operand.reserved[0] = 0u;
    operand.reserved[1] = 0u;
    operand.reserved[2] = 0u;
    return operand;
}

db_x64_memory_decode_status db_x64_memory_decode_one(
    const uint8_t *bytes,
    size_t available_size,
    uint8_t rex,
    uint8_t leading_length,
    db_x64_memory_operand *operand)
{
    db_x64_memory_operand planned;
    size_t cursor = 0u;
    size_t displacement_size = 0u;
    size_t encoded_length;
    size_t instruction_length;
    uint8_t modrm;
    uint8_t mod;
    uint8_t rm;
    uint8_t raw_base = 0u;
    bool has_sib = false;
    bool rip_relative = false;
    bool sib_no_base = false;

    if (bytes == NULL || operand == NULL) {
        return DB_X64_MEMORY_DECODE_NULL_ARGUMENT;
    }

    if (db_x64_memory_decode_overlap(bytes,
                                     available_size,
                                     operand,
                                     sizeof(*operand))) {
        return DB_X64_MEMORY_DECODE_BUFFER_OVERLAP;
    }

    if (available_size == 0u) {
        return DB_X64_MEMORY_DECODE_EMPTY_INPUT;
    }

    if (rex != 0u && (rex < UINT8_C(0x40) || rex > UINT8_C(0x4f))) {
        return DB_X64_MEMORY_DECODE_BAD_REX;
    }

    if (leading_length == 0u ||
        leading_length >= (uint8_t)DB_X64_DECODE_MAX_LENGTH) {
        return DB_X64_MEMORY_DECODE_BAD_LEADING_LENGTH;
    }

    planned = db_x64_memory_decode_initial();
    modrm = bytes[cursor++];
    mod = (uint8_t)(modrm >> 6u);
    rm = (uint8_t)(modrm & UINT8_C(0x07));

    if (mod == UINT8_C(3)) {
        return DB_X64_MEMORY_DECODE_REGISTER_ENCODING;
    }

    planned.modrm = modrm;
    planned.register_operand =
        (uint8_t)(((modrm >> 3u) & UINT8_C(0x07)) |
                  ((rex & UINT8_C(0x04)) != 0u ? UINT8_C(8) : UINT8_C(0)));

    if (rm == UINT8_C(4)) {
        uint8_t sib;
        uint8_t raw_index;
        uint8_t encoded_scale;

        if (available_size - cursor < 1u) {
            return DB_X64_MEMORY_DECODE_TRUNCATED;
        }

        has_sib = true;
        sib = bytes[cursor++];
        planned.sib = sib;
        raw_index = (uint8_t)((sib >> 3u) & UINT8_C(0x07));
        raw_base = (uint8_t)(sib & UINT8_C(0x07));
        encoded_scale = (uint8_t)(UINT8_C(1) << (sib >> 6u));

        if (raw_index == UINT8_C(4) && (rex & UINT8_C(0x02)) == 0u) {
            planned.address.index_register = DB_X64_REGISTER_NONE;
            planned.address.scale = UINT8_C(1);
        } else {
            planned.address.index_register =
                (uint8_t)(raw_index |
                          ((rex & UINT8_C(0x02)) != 0u
                               ? UINT8_C(8)
                               : UINT8_C(0)));
            planned.address.scale = encoded_scale;
        }

        if (mod == UINT8_C(0) && raw_base == UINT8_C(5)) {
            sib_no_base = true;
            planned.address.base_register = DB_X64_REGISTER_NONE;
        } else {
            planned.address.base_register =
                (uint8_t)(raw_base |
                          ((rex & UINT8_C(0x01)) != 0u
                               ? UINT8_C(8)
                               : UINT8_C(0)));
        }
    } else if (mod == UINT8_C(0) && rm == UINT8_C(5)) {
        rip_relative = true;
        planned.address.mode = DB_X64_ADDRESS_RIP_RELATIVE;
    } else {
        planned.address.base_register =
            (uint8_t)(rm |
                      ((rex & UINT8_C(0x01)) != 0u
                           ? UINT8_C(8)
                           : UINT8_C(0)));
    }

    if (mod == UINT8_C(1)) {
        displacement_size = 1u;
    } else if (mod == UINT8_C(2) || rip_relative || sib_no_base) {
        displacement_size = 4u;
    }

    if (available_size - cursor < displacement_size) {
        return DB_X64_MEMORY_DECODE_TRUNCATED;
    }

    if (displacement_size == 1u) {
        planned.address.displacement = (int64_t)(int8_t)bytes[cursor];
    } else if (displacement_size == 4u) {
        planned.address.displacement =
            (int64_t)(int32_t)db_x64_memory_decode_u32(&bytes[cursor]);
    }
    cursor += displacement_size;

    encoded_length = cursor;
    instruction_length = (size_t)leading_length + encoded_length;
    if (instruction_length > DB_X64_DECODE_MAX_LENGTH) {
        return DB_X64_MEMORY_DECODE_INSTRUCTION_TOO_LONG;
    }

    if (rip_relative) {
        planned.address.instruction_length = (uint8_t)instruction_length;
    } else if (has_sib && sib_no_base &&
               planned.address.index_register == DB_X64_REGISTER_NONE) {
        planned.address.mode = DB_X64_ADDRESS_ABSOLUTE;
    }

    planned.encoded_length = (uint8_t)encoded_length;
    planned.instruction_length = (uint8_t)instruction_length;
    *operand = planned;
    return DB_X64_MEMORY_DECODE_OK;
}

const char *db_x64_memory_decode_status_name(
    db_x64_memory_decode_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "empty input",
        "bad REX prefix",
        "bad leading instruction length",
        "register-only ModRM encoding",
        "truncated memory operand",
        "instruction too long"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 memory decode status";
    }

    return names[(size_t)status];
}
