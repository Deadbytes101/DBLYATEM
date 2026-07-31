#ifndef DBLYAT_X64_MEMORY_DECODE_H
#define DBLYAT_X64_MEMORY_DECODE_H

#include "dblyat/x64_address.h"

#include <stddef.h>
#include <stdint.h>

#define DB_X64_MEMORY_NO_SIB UINT8_C(0xff)

typedef enum db_x64_memory_decode_status {
    DB_X64_MEMORY_DECODE_OK = 0,
    DB_X64_MEMORY_DECODE_NULL_ARGUMENT,
    DB_X64_MEMORY_DECODE_BUFFER_OVERLAP,
    DB_X64_MEMORY_DECODE_EMPTY_INPUT,
    DB_X64_MEMORY_DECODE_BAD_REX,
    DB_X64_MEMORY_DECODE_BAD_LEADING_LENGTH,
    DB_X64_MEMORY_DECODE_REGISTER_ENCODING,
    DB_X64_MEMORY_DECODE_TRUNCATED,
    DB_X64_MEMORY_DECODE_INSTRUCTION_TOO_LONG
} db_x64_memory_decode_status;

typedef struct db_x64_memory_operand {
    db_x64_address_expression address;
    uint8_t register_operand;
    uint8_t encoded_length;
    uint8_t instruction_length;
    uint8_t modrm;
    uint8_t sib;
    uint8_t reserved[3];
} db_x64_memory_operand;

db_x64_memory_decode_status db_x64_memory_decode_one(
    const uint8_t *bytes,
    size_t available_size,
    uint8_t rex,
    uint8_t leading_length,
    db_x64_memory_operand *operand);

const char *db_x64_memory_decode_status_name(
    db_x64_memory_decode_status status);

#endif
