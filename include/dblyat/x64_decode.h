#ifndef DBLYAT_X64_DECODE_H
#define DBLYAT_X64_DECODE_H

#include <stddef.h>
#include <stdint.h>

#define DB_X64_DECODE_MAX_LENGTH 15u
#define DB_X64_REGISTER_NONE UINT8_C(0xff)
#define DB_X64_CONDITION_NONE UINT8_C(0xff)

typedef enum db_x64_decode_status {
    DB_X64_DECODE_OK = 0,
    DB_X64_DECODE_NULL_ARGUMENT,
    DB_X64_DECODE_BUFFER_OVERLAP,
    DB_X64_DECODE_EMPTY_INPUT,
    DB_X64_DECODE_TRUNCATED,
    DB_X64_DECODE_UNSUPPORTED_PREFIX,
    DB_X64_DECODE_UNSUPPORTED_OPCODE,
    DB_X64_DECODE_UNSUPPORTED_ADDRESSING,
    DB_X64_DECODE_INVALID_ENCODING
} db_x64_decode_status;

typedef enum db_x64_instruction_kind {
    DB_X64_INSTRUCTION_NOP = 0,
    DB_X64_INSTRUCTION_RET,
    DB_X64_INSTRUCTION_INT3,
    DB_X64_INSTRUCTION_UD2,
    DB_X64_INSTRUCTION_SYSCALL,
    DB_X64_INSTRUCTION_CALL_REL32,
    DB_X64_INSTRUCTION_JMP_REL32,
    DB_X64_INSTRUCTION_JMP_REL8,
    DB_X64_INSTRUCTION_JCC_REL8,
    DB_X64_INSTRUCTION_JCC_REL32,
    DB_X64_INSTRUCTION_MOV_R64_IMM64,
    DB_X64_INSTRUCTION_MOV_R64_R64,
    DB_X64_INSTRUCTION_XOR_R32_R32,
    DB_X64_INSTRUCTION_XOR_R64_R64
} db_x64_instruction_kind;

typedef struct db_x64_instruction {
    db_x64_instruction_kind kind;
    uint64_t immediate;
    int64_t relative;
    uint8_t length;
    uint8_t rex;
    uint8_t destination_register;
    uint8_t source_register;
    uint8_t condition;
    uint8_t reserved[3];
} db_x64_instruction;

db_x64_decode_status db_x64_decode_one(
    const uint8_t *bytes,
    size_t available_size,
    db_x64_instruction *instruction);

const char *db_x64_decode_status_name(db_x64_decode_status status);
const char *db_x64_instruction_kind_name(db_x64_instruction_kind kind);

#endif
