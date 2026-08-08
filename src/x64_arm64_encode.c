#include "dblyat/x64_arm64_encode.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool overlap(const void *a, size_t an, const void *b, size_t bn)
{
    const uintptr_t x = (uintptr_t)a;
    const uintptr_t y = (uintptr_t)b;
    if (an == 0u || bn == 0u) return false;
    return x <= y ? y - x < an : x - y < bn;
}

static bool zero_bytes(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0u; i < n; ++i) {
        if (b[i] != 0u) return false;
    }
    return true;
}

static bool canonical(uint64_t x)
{
    const uint64_t u = x >> 48u;
    const uint64_t s = (x >> 47u) & UINT64_C(1);
    return x != 0u && u == (s != 0u ? UINT64_C(0xffff) : UINT64_C(0));
}

static bool host(uint8_t r)
{
    return r < UINT8_C(31);
}

static bool host_scratch(uint8_t r)
{
    return r == DB_X64_ARM64_HOST_TARGET ||
           r == DB_X64_ARM64_HOST_PRIMARY ||
           r == DB_X64_ARM64_HOST_SECONDARY;
}

static bool helper_argument_register(uint8_t r)
{
    return r >= DB_X64_ARM64_HELPER_ARG0 &&
           r <= DB_X64_ARM64_HELPER_ARG5;
}

static bool materialization_register(uint8_t r)
{
    return host_scratch(r) || helper_argument_register(r);
}

static bool host_or_none(uint8_t r)
{
    return host(r) || r == DB_X64_ARM64_HOST_NONE;
}

static bool guest_or_none(uint8_t r)
{
    return r < UINT8_C(16) || r == DB_X64_ARM64_GUEST_NONE;
}

static bool width(db_ir_width w)
{
    return w == DB_IR_WIDTH_NONE || w == DB_IR_WIDTH_32 ||
           w == DB_IR_WIDTH_64;
}

static bool condition(db_ir_condition c)
{
    return (unsigned)c < 16u ||
           c == (db_ir_condition)UINT8_C(0xff);
}

static bool trap(db_ir_trap t)
{
    return t >= DB_IR_TRAP_NONE && t <= DB_IR_TRAP_SYSCALL;
}

static bool mode(db_ir_address_mode m)
{
    return m >= DB_IR_ADDRESS_NONE && m <= DB_IR_ADDRESS_ABSOLUTE;
}

static bool helper(db_x64_arm64_helper h)
{
    return h > DB_X64_ARM64_HELPER_NONE &&
           h <= DB_X64_ARM64_HELPER_RAISE_TRAP;
}

static bool state_offset(const db_x64_arm64_plan_record *p)
{
    size_t expect;
    if (p->guest_register >= 16u) return false;
    expect = offsetof(db_x64_scalar_state, registers) +
             (size_t)p->guest_register * sizeof(uint64_t);
    return expect <= UINT32_MAX &&
           p->guest_state_offset == (uint32_t)expect &&
           (p->guest_state_offset % 8u) == 0u &&
           (p->guest_state_offset / 8u) <= UINT32_C(4095);
}

static bool record_valid(const db_x64_arm64_plan_record *p)
{
    if (!canonical(p->source_rip) ||
        !zero_bytes(p->reserved, sizeof(p->reserved)) ||
        !width(p->width) || !condition(p->condition) ||
        !trap(p->trap) || !mode(p->address_mode) ||
        !host_or_none(p->host_destination) ||
        !host_or_none(p->host_source) ||
        !host_or_none(p->host_auxiliary) ||
        !guest_or_none(p->guest_register) ||
        !guest_or_none(p->base_guest_register) ||
        !guest_or_none(p->index_guest_register) ||
        p->terminal > 1u) {
        return false;
    }

    switch (p->opcode) {
    case DB_X64_ARM64_PLAN_NOP:
        return p->value == 0u && p->displacement == 0 &&
               p->guest_state_offset == 0u &&
               p->helper == DB_X64_ARM64_HELPER_NONE &&
               p->width == DB_IR_WIDTH_NONE &&
               p->condition == (db_ir_condition)UINT8_C(0xff) &&
               p->trap == DB_IR_TRAP_NONE &&
               p->address_mode == DB_IR_ADDRESS_NONE &&
               p->immediate16 == 0u && p->shift == 0u &&
               p->host_destination == DB_X64_ARM64_HOST_NONE &&
               p->host_source == DB_X64_ARM64_HOST_NONE &&
               p->host_auxiliary == DB_X64_ARM64_HOST_NONE &&
               p->guest_register == DB_X64_ARM64_GUEST_NONE &&
               p->base_guest_register == DB_X64_ARM64_GUEST_NONE &&
               p->index_guest_register == DB_X64_ARM64_GUEST_NONE &&
               p->scale == 1u && p->terminal == 0u;

    case DB_X64_ARM64_PLAN_LOAD_STATE_U64:
        return state_offset(p) &&
               p->helper == DB_X64_ARM64_HELPER_NONE &&
               p->width == DB_IR_WIDTH_64 &&
               host_scratch(p->host_destination) &&
               p->host_source == DB_X64_ARM64_HOST_NONE &&
               p->host_auxiliary == DB_X64_ARM64_HOST_NONE &&
               p->terminal == 0u;

    case DB_X64_ARM64_PLAN_STORE_STATE_U64:
        return state_offset(p) &&
               p->helper == DB_X64_ARM64_HELPER_NONE &&
               p->width == DB_IR_WIDTH_64 &&
               p->host_destination == DB_X64_ARM64_HOST_NONE &&
               host_scratch(p->host_source) &&
               p->host_auxiliary == DB_X64_ARM64_HOST_NONE &&
               p->terminal == 0u;

    case DB_X64_ARM64_PLAN_MOVZ_U16:
    case DB_X64_ARM64_PLAN_MOVK_U16:
        return p->helper == DB_X64_ARM64_HELPER_NONE &&
               (p->shift == 0u || p->shift == 16u ||
                p->shift == 32u || p->shift == 48u) &&
               materialization_register(p->host_destination) &&
               p->host_source == DB_X64_ARM64_HOST_NONE &&
               p->host_auxiliary == DB_X64_ARM64_HOST_NONE &&
               p->terminal == 0u;

    case DB_X64_ARM64_PLAN_EOR_REGISTER:
        return p->helper == DB_X64_ARM64_HELPER_NONE &&
               (p->width == DB_IR_WIDTH_32 || p->width == DB_IR_WIDTH_64) &&
               host_scratch(p->host_destination) &&
               host_scratch(p->host_source) &&
               host_scratch(p->host_auxiliary) &&
               p->terminal == 0u;

    case DB_X64_ARM64_PLAN_CALL_HELPER:
        return helper(p->helper) && p->terminal == 0u &&
               p->immediate16 == 0u && p->shift == 0u;

    case DB_X64_ARM64_PLAN_TAIL_HELPER:
        return helper(p->helper) && p->terminal == 1u &&
               p->immediate16 == 0u && p->shift == 0u;
    }

    return false;
}

static uint32_t materialization_count(uint64_t value)
{
    uint32_t count = 0u;
    uint32_t index;

    for (index = 0u; index < 4u; ++index) {
        if (((value >> (index * 16u)) & UINT64_C(0xffff)) != 0u) {
            ++count;
        }
    }
    return count == 0u ? 1u : count;
}

static bool materialization_defaults(const db_x64_arm64_plan_record *p)
{
    return p->value == 0u && p->displacement == 0 &&
           p->guest_state_offset == 0u &&
           p->helper == DB_X64_ARM64_HELPER_NONE &&
           p->width == DB_IR_WIDTH_NONE &&
           p->condition == (db_ir_condition)UINT8_C(0xff) &&
           p->trap == DB_IR_TRAP_NONE &&
           p->address_mode == DB_IR_ADDRESS_NONE &&
           p->host_source == DB_X64_ARM64_HOST_NONE &&
           p->host_auxiliary == DB_X64_ARM64_HOST_NONE &&
           p->guest_register == DB_X64_ARM64_GUEST_NONE &&
           p->base_guest_register == DB_X64_ARM64_GUEST_NONE &&
           p->index_guest_register == DB_X64_ARM64_GUEST_NONE &&
           p->scale == 1u && p->terminal == 0u &&
           zero_bytes(p->reserved, sizeof(p->reserved));
}

static bool match_materialization(
    const db_x64_arm64_plan_record *plans,
    uint32_t helper_index,
    uint32_t *cursor,
    uint64_t value,
    uint8_t destination)
{
    const db_x64_arm64_plan_record *helper_record = &plans[helper_index];
    uint32_t chunk_index;
    bool started = false;

    for (chunk_index = 0u; chunk_index < 4u; ++chunk_index) {
        const uint16_t chunk =
            (uint16_t)((value >> (chunk_index * 16u)) & UINT64_C(0xffff));
        const db_x64_arm64_plan_record *current;

        if (chunk == 0u && (value != 0u || started)) continue;
        if (*cursor >= helper_index) return false;
        current = &plans[*cursor];
        if (current->source_rip != helper_record->source_rip ||
            current->source_select_index != helper_record->source_select_index ||
            current->opcode != (started ? DB_X64_ARM64_PLAN_MOVK_U16
                                        : DB_X64_ARM64_PLAN_MOVZ_U16) ||
            current->immediate16 != chunk ||
            current->shift != (uint8_t)(chunk_index * 16u) ||
            current->host_destination != destination ||
            !materialization_defaults(current)) {
            return false;
        }
        ++*cursor;
        started = true;
    }

    if (!started) {
        const db_x64_arm64_plan_record *current;
        if (*cursor >= helper_index) return false;
        current = &plans[*cursor];
        if (current->source_rip != helper_record->source_rip ||
            current->source_select_index != helper_record->source_select_index ||
            current->opcode != DB_X64_ARM64_PLAN_MOVZ_U16 ||
            current->immediate16 != 0u || current->shift != 0u ||
            current->host_destination != destination ||
            !materialization_defaults(current)) {
            return false;
        }
        ++*cursor;
    }

    return true;
}

static bool helper_operand_abi_valid(
    const db_x64_arm64_plan_record *plans,
    uint32_t helper_index)
{
    const db_x64_arm64_plan_record *record = &plans[helper_index];
    uint64_t values[6];
    uint8_t destinations[6];
    uint32_t argument_count = 0u;
    uint32_t expected_records = 0u;
    uint32_t start;
    uint32_t cursor;
    uint32_t index;

    switch (record->helper) {
    case DB_X64_ARM64_HELPER_FORM_GUEST_ADDRESS:
        values[0] = record->value;
        values[1] = (uint64_t)record->displacement;
        values[2] = (uint64_t)(uint32_t)record->address_mode;
        values[3] = (uint64_t)record->base_guest_register;
        values[4] = (uint64_t)record->index_guest_register;
        values[5] = (uint64_t)record->scale;
        destinations[0] = DB_X64_ARM64_HELPER_ARG0;
        destinations[1] = DB_X64_ARM64_HELPER_ARG1;
        destinations[2] = DB_X64_ARM64_HELPER_ARG2;
        destinations[3] = DB_X64_ARM64_HELPER_ARG3;
        destinations[4] = DB_X64_ARM64_HELPER_ARG4;
        destinations[5] = DB_X64_ARM64_HELPER_ARG5;
        argument_count = 6u;
        break;

    case DB_X64_ARM64_HELPER_UPDATE_LOGIC_FLAGS:
        values[0] = (uint64_t)(uint32_t)record->width;
        values[1] = (uint64_t)record->guest_register;
        destinations[0] = DB_X64_ARM64_HELPER_ARG0;
        destinations[1] = DB_X64_ARM64_HELPER_ARG1;
        argument_count = 2u;
        break;

    case DB_X64_ARM64_HELPER_TEST_GUEST_CONDITION:
        values[0] = (uint64_t)(uint32_t)record->condition;
        destinations[0] = DB_X64_ARM64_HELPER_ARG0;
        argument_count = 1u;
        break;

    default:
        return true;
    }

    for (index = 0u; index < argument_count; ++index) {
        const uint32_t count = materialization_count(values[index]);
        if (UINT32_MAX - expected_records < count) return false;
        expected_records += count;
    }

    if (helper_index < expected_records) return false;
    start = helper_index - expected_records;
    if (start != 0u &&
        plans[start - 1u].source_select_index == record->source_select_index) {
        return false;
    }

    cursor = start;
    for (index = 0u; index < argument_count; ++index) {
        if (!match_materialization(plans,
                                   helper_index,
                                   &cursor,
                                   values[index],
                                   destinations[index])) {
            return false;
        }
    }

    return cursor == helper_index;
}

static uint32_t word_for(const db_x64_arm64_plan_record *p)
{
    uint32_t w;

    switch (p->opcode) {
    case DB_X64_ARM64_PLAN_NOP:
        return UINT32_C(0xd503201f);
    case DB_X64_ARM64_PLAN_LOAD_STATE_U64:
        return UINT32_C(0xf9400000) |
               ((p->guest_state_offset / 8u) << 10u) |
               ((uint32_t)DB_X64_ARM64_HOST_STATE << 5u) |
               p->host_destination;
    case DB_X64_ARM64_PLAN_STORE_STATE_U64:
        return UINT32_C(0xf9000000) |
               ((p->guest_state_offset / 8u) << 10u) |
               ((uint32_t)DB_X64_ARM64_HOST_STATE << 5u) |
               p->host_source;
    case DB_X64_ARM64_PLAN_MOVZ_U16:
        w = UINT32_C(0xd2800000);
        return w | ((uint32_t)(p->shift / 16u) << 21u) |
               ((uint32_t)p->immediate16 << 5u) |
               p->host_destination;
    case DB_X64_ARM64_PLAN_MOVK_U16:
        w = UINT32_C(0xf2800000);
        return w | ((uint32_t)(p->shift / 16u) << 21u) |
               ((uint32_t)p->immediate16 << 5u) |
               p->host_destination;
    case DB_X64_ARM64_PLAN_EOR_REGISTER:
        w = p->width == DB_IR_WIDTH_64
                ? UINT32_C(0xca000000)
                : UINT32_C(0x4a000000);
        return w | ((uint32_t)p->host_auxiliary << 16u) |
               ((uint32_t)p->host_source << 5u) |
               p->host_destination;
    case DB_X64_ARM64_PLAN_CALL_HELPER:
        return UINT32_C(0x94000000);
    case DB_X64_ARM64_PLAN_TAIL_HELPER:
        return UINT32_C(0x14000000);
    }

    return 0u;
}

static db_x64_arm64_relocation relocation_for(
    const db_x64_arm64_plan_record *p,
    uint32_t plan_index)
{
    db_x64_arm64_relocation r = {0};
    r.source_rip = p->source_rip;
    r.value = p->value;
    r.displacement = p->displacement;
    r.word_index = plan_index;
    r.source_plan_index = plan_index;
    r.kind = p->opcode == DB_X64_ARM64_PLAN_CALL_HELPER
                 ? DB_X64_ARM64_RELOCATION_CALL26
                 : DB_X64_ARM64_RELOCATION_JUMP26;
    r.helper = p->helper;
    r.width = p->width;
    r.condition = p->condition;
    r.trap = p->trap;
    r.address_mode = p->address_mode;
    r.host_destination = p->host_destination;
    r.host_source = p->host_source;
    r.host_auxiliary = p->host_auxiliary;
    r.guest_register = p->guest_register;
    r.base_guest_register = p->base_guest_register;
    r.index_guest_register = p->index_guest_register;
    r.scale = p->scale;
    return r;
}

db_x64_arm64_encode_status db_x64_arm64_encode_block(
    const db_x64_arm64_plan_record *plans,
    uint32_t count,
    uint32_t *words,
    uint32_t word_capacity,
    db_x64_arm64_relocation *relocs,
    uint32_t reloc_capacity,
    db_x64_arm64_encode_summary *summary)
{
    db_x64_arm64_encode_summary done;
    db_x64_arm64_helper terminal_helper = DB_X64_ARM64_HELPER_NONE;
    size_t plan_bytes;
    size_t word_bytes;
    size_t relocation_bytes;
    uint32_t index;
    uint32_t relocation_count = 0u;
    bool has_terminal = false;

    if (plans == NULL || words == NULL || relocs == NULL || summary == NULL) {
        return DB_X64_ARM64_ENCODE_NULL_ARGUMENT;
    }
    if (count == 0u) return DB_X64_ARM64_ENCODE_BAD_COUNT;

    plan_bytes = (size_t)count * sizeof(*plans);
    word_bytes = (size_t)word_capacity * sizeof(*words);
    relocation_bytes = (size_t)reloc_capacity * sizeof(*relocs);
    if (overlap(plans, plan_bytes, words, word_bytes) ||
        overlap(plans, plan_bytes, relocs, relocation_bytes) ||
        overlap(plans, plan_bytes, summary, sizeof(*summary)) ||
        overlap(words, word_bytes, relocs, relocation_bytes) ||
        overlap(words, word_bytes, summary, sizeof(*summary)) ||
        overlap(relocs, relocation_bytes, summary, sizeof(*summary))) {
        return DB_X64_ARM64_ENCODE_BUFFER_OVERLAP;
    }

    for (index = 0u; index < count; ++index) {
        if (!record_valid(&plans[index])) {
            return DB_X64_ARM64_ENCODE_BAD_PLAN;
        }
        if (index == 0u) {
            if (plans[index].source_select_index != 0u) {
                return DB_X64_ARM64_ENCODE_BAD_GROUPING;
            }
        } else if (plans[index].source_select_index <
                       plans[index - 1u].source_select_index ||
                   plans[index].source_select_index >
                       plans[index - 1u].source_select_index + 1u) {
            return DB_X64_ARM64_ENCODE_BAD_GROUPING;
        }

        if (plans[index].opcode == DB_X64_ARM64_PLAN_CALL_HELPER &&
            !helper_operand_abi_valid(plans, index)) {
            return DB_X64_ARM64_ENCODE_BAD_HELPER_ABI;
        }

        if (plans[index].opcode == DB_X64_ARM64_PLAN_TAIL_HELPER) {
            if (has_terminal || index + 1u != count) {
                return DB_X64_ARM64_ENCODE_TERMINATOR_ORDER;
            }
            has_terminal = true;
            terminal_helper = plans[index].helper;
        }
        if (plans[index].opcode == DB_X64_ARM64_PLAN_CALL_HELPER ||
            plans[index].opcode == DB_X64_ARM64_PLAN_TAIL_HELPER) {
            ++relocation_count;
        }
    }

    if (word_capacity < count) {
        return DB_X64_ARM64_ENCODE_WORD_CAPACITY;
    }
    if (reloc_capacity < relocation_count) {
        return DB_X64_ARM64_ENCODE_RELOCATION_CAPACITY;
    }

    relocation_count = 0u;
    for (index = 0u; index < count; ++index) {
        words[index] = word_for(&plans[index]);
        if (plans[index].opcode == DB_X64_ARM64_PLAN_CALL_HELPER ||
            plans[index].opcode == DB_X64_ARM64_PLAN_TAIL_HELPER) {
            relocs[relocation_count++] = relocation_for(&plans[index], index);
        }
    }

    done.source_rip = plans[0].source_rip;
    done.plan_count = count;
    done.word_count = count;
    done.relocation_count = relocation_count;
    done.terminal_helper = terminal_helper;
    done.has_terminal = has_terminal ? UINT8_C(1) : UINT8_C(0);
    for (index = 0u; index < sizeof(done.reserved); ++index) {
        done.reserved[index] = 0u;
    }
    *summary = done;
    return DB_X64_ARM64_ENCODE_OK;
}

const char *db_x64_arm64_encode_status_name(db_x64_arm64_encode_status s)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad plan count",
        "bad plan record",
        "bad plan grouping",
        "terminator order invalid",
        "insufficient word capacity",
        "insufficient relocation capacity",
        "bad helper operand ABI"
    };
    return (size_t)s < sizeof(names) / sizeof(names[0])
               ? names[(size_t)s]
               : "unknown x86-64 ARM64 encode status";
}
