#include "dblyat/ir.h"

#include <stddef.h>

const char *db_ir_opcode_name(db_ir_opcode opcode)
{
    static const char *const names[] = {
        "nop",
        "move immediate",
        "move register",
        "xor",
        "load u64",
        "store u64",
        "call direct",
        "call indirect",
        "return",
        "jump direct",
        "jump indirect",
        "jump conditional",
        "trap"
    };

    if ((size_t)opcode >= sizeof(names) / sizeof(names[0])) {
        return "unknown IR opcode";
    }

    return names[(size_t)opcode];
}

const char *db_ir_condition_name(db_ir_condition condition)
{
    static const char *const names[] = {
        "overflow",
        "not overflow",
        "below",
        "above or equal",
        "equal",
        "not equal",
        "below or equal",
        "above",
        "sign",
        "not sign",
        "parity",
        "not parity",
        "less",
        "greater or equal",
        "less or equal",
        "greater"
    };

    if ((size_t)condition >= sizeof(names) / sizeof(names[0])) {
        return condition == (db_ir_condition)DB_IR_CONDITION_NONE
                   ? "none"
                   : "unknown IR condition";
    }

    return names[(size_t)condition];
}

const char *db_ir_trap_name(db_ir_trap trap)
{
    static const char *const names[] = {
        "none",
        "breakpoint",
        "invalid opcode",
        "syscall"
    };

    if ((size_t)trap >= sizeof(names) / sizeof(names[0])) {
        return "unknown IR trap";
    }

    return names[(size_t)trap];
}
