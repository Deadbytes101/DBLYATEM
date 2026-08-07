#ifndef DBLYAT_TRANSLATED_TERMINAL_EXIT_H
#define DBLYAT_TRANSLATED_TERMINAL_EXIT_H

#include <stdint.h>

#define DB_TRANSLATED_TERMINAL_EXIT_FORMAT_VERSION UINT32_C(1)
#define DB_TRANSLATED_TERMINAL_EXIT_CONDITION_NONE UINT32_MAX

typedef enum db_translated_terminal_exit_kind {
    DB_TRANSLATED_TERMINAL_EXIT_NONE = 0,
    DB_TRANSLATED_TERMINAL_EXIT_DIRECT,
    DB_TRANSLATED_TERMINAL_EXIT_TARGET,
    DB_TRANSLATED_TERMINAL_EXIT_CONDITIONAL,
    DB_TRANSLATED_TERMINAL_EXIT_TRAP
} db_translated_terminal_exit_kind;

typedef enum db_translated_terminal_exit_status {
    DB_TRANSLATED_TERMINAL_EXIT_OK = 0,
    DB_TRANSLATED_TERMINAL_EXIT_NULL_ARGUMENT,
    DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD,
    DB_TRANSLATED_TERMINAL_EXIT_MISMATCH
} db_translated_terminal_exit_status;

typedef struct db_translated_terminal_exit {
    uint64_t source_rip;
    uint64_t direct_target;
    uint64_t fallthrough_target;
    uint64_t runtime_target;
    uint64_t runtime_value;
    uint32_t kind;
    uint32_t trap;
    uint32_t condition;
    uint32_t format_version;
    uint8_t runtime_target_valid;
    uint8_t runtime_value_valid;
    uint8_t condition_taken;
    uint8_t condition_taken_valid;
    uint8_t reserved[4];
} db_translated_terminal_exit;

db_translated_terminal_exit_status
 db_translated_terminal_exit_plan_direct(
    uint64_t source_rip,
    uint64_t direct_target,
    db_translated_terminal_exit *exit_record);

db_translated_terminal_exit_status
 db_translated_terminal_exit_plan_target(
    uint64_t source_rip,
    db_translated_terminal_exit *exit_record);

db_translated_terminal_exit_status
 db_translated_terminal_exit_plan_conditional(
    uint64_t source_rip,
    uint64_t direct_target,
    uint64_t fallthrough_target,
    uint32_t condition,
    db_translated_terminal_exit *exit_record);

db_translated_terminal_exit_status
 db_translated_terminal_exit_plan_trap(
    uint64_t source_rip,
    uint32_t trap,
    db_translated_terminal_exit *exit_record);

db_translated_terminal_exit_status
 db_translated_terminal_exit_validate_planned(
    const db_translated_terminal_exit *exit_record);

db_translated_terminal_exit_status
 db_translated_terminal_exit_validate_observed(
    const db_translated_terminal_exit *exit_record);

db_translated_terminal_exit_status
 db_translated_terminal_exit_match(
    const db_translated_terminal_exit *planned,
    const db_translated_terminal_exit *observed);

const char *db_translated_terminal_exit_kind_name(
    db_translated_terminal_exit_kind kind);

const char *db_translated_terminal_exit_status_name(
    db_translated_terminal_exit_status status);

#endif
