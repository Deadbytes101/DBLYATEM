#ifndef DBLYAT_X64_INTERPRET_H
#define DBLYAT_X64_INTERPRET_H

#include "dblyat/x64_decode.h"

#include <stdint.h>

#define DB_X64_RFLAGS_CF UINT64_C(0x0000000000000001)
#define DB_X64_RFLAGS_FIXED_ONE UINT64_C(0x0000000000000002)
#define DB_X64_RFLAGS_PF UINT64_C(0x0000000000000004)
#define DB_X64_RFLAGS_AF UINT64_C(0x0000000000000010)
#define DB_X64_RFLAGS_ZF UINT64_C(0x0000000000000040)
#define DB_X64_RFLAGS_SF UINT64_C(0x0000000000000080)
#define DB_X64_RFLAGS_OF UINT64_C(0x0000000000000800)

typedef enum db_x64_interpret_status {
    DB_X64_INTERPRET_OK = 0,
    DB_X64_INTERPRET_NULL_ARGUMENT,
    DB_X64_INTERPRET_BUFFER_OVERLAP,
    DB_X64_INTERPRET_BAD_STATE,
    DB_X64_INTERPRET_BAD_INSTRUCTION,
    DB_X64_INTERPRET_NONCANONICAL_ADDRESS,
    DB_X64_INTERPRET_ADDRESS_OVERFLOW,
    DB_X64_INTERPRET_UNSUPPORTED_INSTRUCTION
} db_x64_interpret_status;

typedef struct db_x64_scalar_state {
    uint64_t registers[16];
    uint64_t rip;
    uint64_t rflags;
} db_x64_scalar_state;

typedef struct db_x64_step_summary {
    uint64_t source_rip;
    uint64_t fallthrough_rip;
    uint64_t next_rip;
    uint64_t previous_rflags;
    uint64_t next_rflags;
    uint64_t previous_destination;
    uint64_t next_destination;
    db_x64_instruction_kind kind;
    uint8_t instruction_length;
    uint8_t destination_register;
    uint8_t branch_taken;
    uint8_t reserved;
} db_x64_step_summary;

db_x64_interpret_status db_x64_interpret_one(
    const db_x64_instruction *instruction,
    db_x64_scalar_state *state,
    db_x64_step_summary *summary);

const char *db_x64_interpret_status_name(db_x64_interpret_status status);

#endif
