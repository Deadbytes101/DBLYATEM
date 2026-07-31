#ifndef DBLYAT_X64_ADDRESS_H
#define DBLYAT_X64_ADDRESS_H

#include "dblyat/x64_interpret.h"

#include <stdint.h>

typedef enum db_x64_address_status {
    DB_X64_ADDRESS_OK = 0,
    DB_X64_ADDRESS_NULL_ARGUMENT,
    DB_X64_ADDRESS_BUFFER_OVERLAP,
    DB_X64_ADDRESS_BAD_EXPRESSION,
    DB_X64_ADDRESS_NONCANONICAL_ADDRESS
} db_x64_address_status;

typedef enum db_x64_address_mode {
    DB_X64_ADDRESS_BASE_INDEX = 0,
    DB_X64_ADDRESS_RIP_RELATIVE,
    DB_X64_ADDRESS_ABSOLUTE
} db_x64_address_mode;

typedef struct db_x64_address_expression {
    int64_t displacement;
    db_x64_address_mode mode;
    uint8_t base_register;
    uint8_t index_register;
    uint8_t scale;
    uint8_t instruction_length;
    uint8_t reserved[4];
} db_x64_address_expression;

typedef struct db_x64_address_summary {
    uint64_t base_value;
    uint64_t index_value;
    uint64_t scaled_index;
    uint64_t linear_address;
    int64_t displacement;
    db_x64_address_mode mode;
    uint8_t base_register;
    uint8_t index_register;
    uint8_t scale;
    uint8_t instruction_length;
    uint8_t reserved[4];
} db_x64_address_summary;

db_x64_address_status db_x64_address_plan(
    const db_x64_address_expression *expression,
    const db_x64_scalar_state *state,
    db_x64_address_summary *summary);

const char *db_x64_address_status_name(db_x64_address_status status);

#endif
