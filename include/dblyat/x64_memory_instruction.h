#ifndef DBLYAT_X64_MEMORY_INSTRUCTION_H
#define DBLYAT_X64_MEMORY_INSTRUCTION_H

#include "dblyat/x64_memory_decode.h"

#include <stddef.h>
#include <stdint.h>

typedef enum db_x64_memory_instruction_status {
    DB_X64_MEMORY_INSTRUCTION_OK = 0,
    DB_X64_MEMORY_INSTRUCTION_NULL_ARGUMENT,
    DB_X64_MEMORY_INSTRUCTION_BUFFER_OVERLAP,
    DB_X64_MEMORY_INSTRUCTION_EMPTY_INPUT,
    DB_X64_MEMORY_INSTRUCTION_TRUNCATED,
    DB_X64_MEMORY_INSTRUCTION_UNSUPPORTED_PREFIX,
    DB_X64_MEMORY_INSTRUCTION_BAD_REX,
    DB_X64_MEMORY_INSTRUCTION_UNSUPPORTED_OPCODE,
    DB_X64_MEMORY_INSTRUCTION_REGISTER_ENCODING,
    DB_X64_MEMORY_INSTRUCTION_INSTRUCTION_TOO_LONG,
    DB_X64_MEMORY_INSTRUCTION_INVALID_ENCODING
} db_x64_memory_instruction_status;

typedef enum db_x64_memory_instruction_kind {
    DB_X64_MEMORY_INSTRUCTION_LOAD_R64 = 0,
    DB_X64_MEMORY_INSTRUCTION_STORE_R64
} db_x64_memory_instruction_kind;

typedef struct db_x64_memory_instruction {
    db_x64_memory_operand memory;
    db_x64_memory_instruction_kind kind;
    uint8_t rex;
    uint8_t opcode;
    uint8_t register_operand;
    uint8_t instruction_length;
    uint8_t reserved[4];
} db_x64_memory_instruction;

db_x64_memory_instruction_status db_x64_memory_instruction_decode_one(
    const uint8_t *bytes,
    size_t available_size,
    db_x64_memory_instruction *instruction);

const char *db_x64_memory_instruction_status_name(
    db_x64_memory_instruction_status status);

const char *db_x64_memory_instruction_kind_name(
    db_x64_memory_instruction_kind kind);

#endif
