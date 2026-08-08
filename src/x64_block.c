#include "dblyat/x64_block.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool db_x64_block_overlap(const void *left,
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

static bool db_x64_block_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return upper == (sign != 0u ? UINT64_C(0xffff) : UINT64_C(0));
}

static bool db_x64_block_canonical_nonzero(uint64_t address)
{
    return address != 0u && db_x64_block_canonical(address);
}

static bool db_x64_block_code_reserved_zero(const db_x64_code_view *code)
{
    size_t index;

    for (index = 0u; index < sizeof(code->reserved); ++index) {
        if (code->reserved[index] != 0u) {
            return false;
        }
    }

    return true;
}

static bool db_x64_block_code_valid(const db_x64_code_view *code)
{
    uint64_t last_address;

    if (code->bytes == NULL ||
        code->size == 0u ||
        !db_x64_block_canonical_nonzero(code->guest_base) ||
        !db_x64_block_code_reserved_zero(code) ||
        db_x64_block_overlap(code->bytes, code->size,
                             code, sizeof(*code))) {
        return false;
    }

    if (code->size - 1u > (size_t)(UINT64_MAX - code->guest_base)) {
        return false;
    }

    last_address = code->guest_base + (uint64_t)(code->size - 1u);
    return db_x64_block_canonical_nonzero(last_address);
}

static bool db_x64_block_aliases(const db_x64_code_view *code,
                                 const db_x64_block_record *records,
                                 size_t record_bytes,
                                 const db_x64_block_summary *summary)
{
    return db_x64_block_overlap(code, sizeof(*code),
                                records, record_bytes) ||
           db_x64_block_overlap(code, sizeof(*code),
                                summary, sizeof(*summary)) ||
           db_x64_block_overlap(records, record_bytes,
                                summary, sizeof(*summary)) ||
           db_x64_block_overlap(code->bytes, code->size,
                                records, record_bytes) ||
           db_x64_block_overlap(code->bytes, code->size,
                                summary, sizeof(*summary));
}

static bool db_x64_block_locate(const db_x64_code_view *code,
                                uint64_t rip,
                                const uint8_t **bytes,
                                size_t *available_size)
{
    uint64_t delta;
    size_t offset;
    size_t remaining;

    if (!db_x64_block_canonical_nonzero(rip) ||
        rip < code->guest_base) {
        return false;
    }

    delta = rip - code->guest_base;
    if (delta > (uint64_t)SIZE_MAX ||
        (size_t)delta >= code->size) {
        return false;
    }

    offset = (size_t)delta;
    remaining = code->size - offset;

    *bytes = code->bytes + offset;
    *available_size =
        remaining < (size_t)DB_X64_DECODE_MAX_LENGTH
            ? remaining
            : (size_t)DB_X64_DECODE_MAX_LENGTH;
    return true;
}

static bool db_x64_block_add_unsigned(uint64_t base,
                                      uint64_t value,
                                      uint64_t *result)
{
    uint64_t planned;

    if (UINT64_MAX - base < value) {
        return false;
    }

    planned = base + value;
    if (!db_x64_block_canonical_nonzero(planned)) {
        return false;
    }

    *result = planned;
    return true;
}

static bool db_x64_block_add_relative(uint64_t base,
                                      int64_t relative,
                                      uint64_t *result)
{
    uint64_t magnitude;
    uint64_t planned;

    if (relative >= 0) {
        return db_x64_block_add_unsigned(base,
                                         (uint64_t)relative,
                                         result);
    }

    magnitude = (uint64_t)(-(relative + 1)) + UINT64_C(1);
    if (base < magnitude) {
        return false;
    }

    planned = base - magnitude;
    if (!db_x64_block_canonical_nonzero(planned)) {
        return false;
    }

    *result = planned;
    return true;
}

static db_x64_block_control db_x64_block_classify_scalar(
    const db_x64_instruction *instruction,
    bool *has_direct_target)
{
    *has_direct_target = false;

    switch (instruction->kind) {
    case DB_X64_INSTRUCTION_RET:
        return DB_X64_BLOCK_CONTROL_RETURN;

    case DB_X64_INSTRUCTION_CALL_REL32:
        *has_direct_target = true;
        return DB_X64_BLOCK_CONTROL_DIRECT_CALL;

    case DB_X64_INSTRUCTION_JMP_REL32:
    case DB_X64_INSTRUCTION_JMP_REL8:
        *has_direct_target = true;
        return DB_X64_BLOCK_CONTROL_DIRECT_JUMP;

    case DB_X64_INSTRUCTION_JCC_REL8:
    case DB_X64_INSTRUCTION_JCC_REL32:
        *has_direct_target = true;
        return DB_X64_BLOCK_CONTROL_CONDITIONAL_JUMP;

    case DB_X64_INSTRUCTION_INT3:
    case DB_X64_INSTRUCTION_UD2:
    case DB_X64_INSTRUCTION_SYSCALL:
        return DB_X64_BLOCK_CONTROL_TRAP;

    case DB_X64_INSTRUCTION_NOP:
    case DB_X64_INSTRUCTION_MOV_R64_IMM64:
    case DB_X64_INSTRUCTION_MOV_R64_R64:
    case DB_X64_INSTRUCTION_XOR_R32_R32:
    case DB_X64_INSTRUCTION_XOR_R64_R64:
        return DB_X64_BLOCK_CONTROL_LINEAR;
    }

    return DB_X64_BLOCK_CONTROL_TRAP;
}

static db_x64_block_control db_x64_block_classify_memory(
    const db_x64_memory_instruction *instruction)
{
    switch (instruction->kind) {
    case DB_X64_MEMORY_INSTRUCTION_LOAD_R64:
    case DB_X64_MEMORY_INSTRUCTION_STORE_R64:
        return DB_X64_BLOCK_CONTROL_LINEAR;
    case DB_X64_MEMORY_INSTRUCTION_CALL_RM64:
        return DB_X64_BLOCK_CONTROL_INDIRECT_CALL;
    case DB_X64_MEMORY_INSTRUCTION_JMP_RM64:
        return DB_X64_BLOCK_CONTROL_INDIRECT_JUMP;
    }

    return DB_X64_BLOCK_CONTROL_TRAP;
}

static void db_x64_block_reset_decode_status(
    db_x64_block_summary *completed)
{
    completed->scalar_decode_status = DB_X64_DECODE_OK;
    completed->memory_decode_status = DB_X64_MEMORY_INSTRUCTION_OK;
}

static bool db_x64_block_is_terminator(db_x64_block_control control)
{
    return control != DB_X64_BLOCK_CONTROL_LINEAR;
}

db_x64_block_status db_x64_block_build(
    const db_x64_code_view *code,
    uint64_t start_rip,
    uint32_t instruction_budget,
    uint32_t byte_budget,
    db_x64_block_record *records,
    uint32_t record_capacity,
    db_x64_block_summary *summary)
{
    db_x64_block_summary completed = {0};
    db_x64_block_record record;
    db_x64_instruction scalar;
    db_x64_memory_instruction memory;
    const uint8_t *instruction_bytes;
    size_t available_size;
    size_t record_bytes;
    uint64_t current_rip;
    uint64_t fallthrough_rip;
    uint64_t direct_target;
    uint32_t instruction_length;
    bool has_direct_target;

    if (code == NULL || records == NULL || summary == NULL) {
        return DB_X64_BLOCK_NULL_ARGUMENT;
    }

    if (!db_x64_block_code_valid(code)) {
        return DB_X64_BLOCK_BAD_CODE_VIEW;
    }

    if (!db_x64_block_canonical_nonzero(start_rip)) {
        return DB_X64_BLOCK_BAD_START_RIP;
    }

    if (instruction_budget == 0u ||
        byte_budget == 0u ||
        record_capacity == 0u) {
        return DB_X64_BLOCK_BAD_LIMIT;
    }

#if SIZE_MAX < UINT64_MAX
    if ((uint64_t)record_capacity >
        (uint64_t)(SIZE_MAX / sizeof(*records))) {
        return DB_X64_BLOCK_BAD_LIMIT;
    }
#endif

    record_bytes = (size_t)record_capacity * sizeof(*records);
    if (db_x64_block_aliases(code,
                             records,
                             record_bytes,
                             summary)) {
        return DB_X64_BLOCK_BUFFER_OVERLAP;
    }

    completed.source_rip = start_rip;
    completed.final_fallthrough_rip = start_rip;
    completed.instruction_budget = instruction_budget;
    completed.byte_budget = byte_budget;
    completed.output_capacity = record_capacity;
    completed.record_count = 0u;
    completed.byte_count = 0u;
    completed.stop_reason = DB_X64_BLOCK_STOP_INSTRUCTION_BUDGET;
    db_x64_block_reset_decode_status(&completed);

    current_rip = start_rip;

    for (;;) {
        if (completed.record_count >= instruction_budget) {
            completed.stop_reason =
                DB_X64_BLOCK_STOP_INSTRUCTION_BUDGET;
            break;
        }

        if (completed.byte_count >= byte_budget) {
            completed.stop_reason = DB_X64_BLOCK_STOP_BYTE_BUDGET;
            break;
        }

        if (completed.record_count >= record_capacity) {
            completed.stop_reason =
                DB_X64_BLOCK_STOP_OUTPUT_CAPACITY;
            break;
        }

        db_x64_block_reset_decode_status(&completed);

        if (!db_x64_block_locate(code,
                                 current_rip,
                                 &instruction_bytes,
                                 &available_size)) {
            completed.stop_reason =
                DB_X64_BLOCK_STOP_CODE_OUT_OF_BOUNDS;
            break;
        }

        completed.scalar_decode_status =
            db_x64_decode_one(instruction_bytes,
                              available_size,
                              &scalar);

        record = (db_x64_block_record){0};
        record.rip = current_rip;
        record.path = DB_X64_BLOCK_PATH_SCALAR;

        if (completed.scalar_decode_status == DB_X64_DECODE_OK) {
            record.scalar = scalar;
            record.control =
                db_x64_block_classify_scalar(&scalar,
                                             &has_direct_target);
            instruction_length = scalar.length;
        } else {
            completed.memory_decode_status =
                db_x64_memory_instruction_decode_one(
                    instruction_bytes,
                    available_size,
                    &memory);

            if (completed.memory_decode_status !=
                DB_X64_MEMORY_INSTRUCTION_OK) {
                completed.stop_reason =
                    DB_X64_BLOCK_STOP_DECODE_FAILED;
                break;
            }

            record.path = DB_X64_BLOCK_PATH_MEMORY;
            record.control = db_x64_block_classify_memory(&memory);
            record.memory = memory;
            has_direct_target = false;
            instruction_length = memory.instruction_length;
        }

        if (instruction_length >
            byte_budget - completed.byte_count) {
            completed.stop_reason =
                DB_X64_BLOCK_STOP_BYTE_BUDGET;
            break;
        }

        if (!db_x64_block_add_unsigned(
                current_rip,
                instruction_length,
                &fallthrough_rip)) {
            completed.stop_reason =
                DB_X64_BLOCK_STOP_ADDRESS_INVALID;
            break;
        }

        direct_target = 0u;
        if (has_direct_target &&
            !db_x64_block_add_relative(
                fallthrough_rip,
                scalar.relative,
                &direct_target)) {
            completed.stop_reason =
                DB_X64_BLOCK_STOP_ADDRESS_INVALID;
            break;
        }

        record.fallthrough_rip = fallthrough_rip;
        record.direct_target = direct_target;
        record.has_direct_target =
            has_direct_target ? UINT8_C(1) : UINT8_C(0);

        records[completed.record_count] = record;
        ++completed.record_count;
        completed.byte_count += instruction_length;
        completed.final_fallthrough_rip = fallthrough_rip;
        current_rip = fallthrough_rip;

        if (db_x64_block_is_terminator(record.control)) {
            completed.stop_reason =
                DB_X64_BLOCK_STOP_TERMINATOR;
            break;
        }
    }

    *summary = completed;
    return DB_X64_BLOCK_OK;
}

const char *db_x64_block_status_name(db_x64_block_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad code view",
        "bad start rip",
        "bad limit"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 block status";
    }

    return names[(size_t)status];
}

const char *db_x64_block_stop_reason_name(
    db_x64_block_stop_reason reason)
{
    static const char *const names[] = {
        "terminator emitted",
        "instruction budget exhausted",
        "byte budget exhausted",
        "output capacity exhausted",
        "code rip out of bounds",
        "decode failed",
        "address invalid"
    };

    if ((size_t)reason >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 block stop reason";
    }

    return names[(size_t)reason];
}

const char *db_x64_block_decode_path_name(
    db_x64_block_decode_path path)
{
    static const char *const names[] = {
        "scalar",
        "memory"
    };

    if ((size_t)path >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 block decode path";
    }

    return names[(size_t)path];
}

const char *db_x64_block_control_name(
    db_x64_block_control control)
{
    static const char *const names[] = {
        "linear",
        "return",
        "direct call",
        "direct jump",
        "conditional jump",
        "indirect call",
        "indirect jump",
        "trap"
    };

    if ((size_t)control >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 block control";
    }

    return names[(size_t)control];
}
