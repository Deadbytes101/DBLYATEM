#include "x64_arm64_relocate_internal.h"

#include <stdbool.h>
#include <stdint.h>

#define DB_ARM64_BL_ZERO UINT32_C(0x94000000)
#define DB_ARM64_B_ZERO UINT32_C(0x14000000)
#define DB_ARM64_BRANCH_MIN (-INT64_C(33554432))
#define DB_ARM64_BRANCH_MAX INT64_C(33554431)

static bool signed_delta(uint64_t target, uint64_t source, int64_t *result)
{
    uint64_t magnitude;

    if (target >= source) {
        magnitude = target - source;
        if (magnitude > (uint64_t)INT64_MAX) {
            return false;
        }
        *result = (int64_t)magnitude;
        return true;
    }

    magnitude = source - target;
    if (magnitude > (uint64_t)INT64_MAX + UINT64_C(1)) {
        return false;
    }
    if (magnitude == (uint64_t)INT64_MAX + UINT64_C(1)) {
        *result = INT64_MIN;
    } else {
        *result = -(int64_t)magnitude;
    }
    return true;
}

bool db_x64_arm64_relocate_branch_word_internal(
    db_x64_arm64_relocation_kind kind,
    uint64_t source,
    uint64_t target,
    uint32_t *word)
{
    int64_t delta;
    int64_t immediate;
    uint32_t base;

    if (!signed_delta(target, source, &delta) ||
        (delta & INT64_C(3)) != 0) {
        return false;
    }

    immediate = delta / INT64_C(4);
    if (immediate < DB_ARM64_BRANCH_MIN ||
        immediate > DB_ARM64_BRANCH_MAX) {
        return false;
    }

    base = kind == DB_X64_ARM64_RELOCATION_CALL26
               ? DB_ARM64_BL_ZERO
               : DB_ARM64_B_ZERO;
    *word = base |
            ((uint32_t)immediate & UINT32_C(0x03ffffff));
    return true;
}

uint32_t db_x64_arm64_relocate_materialize_count_internal(
    uint64_t value)
{
    uint32_t count = 0u;
    uint32_t index;

    for (index = 0u; index < 4u; ++index) {
        if (((value >> (index * 16u)) &
             UINT64_C(0xffff)) != 0u) {
            ++count;
        }
    }
    return count == 0u ? 1u : count;
}

uint32_t db_x64_arm64_relocate_emit_materialize_internal(
    uint64_t value,
    uint32_t *words)
{
    uint32_t cursor = 0u;
    uint32_t index;
    bool started = false;

    for (index = 0u; index < 4u; ++index) {
        const uint16_t chunk =
            (uint16_t)((value >> (index * 16u)) &
                       UINT64_C(0xffff));
        uint32_t base;

        if (chunk == 0u && (value != 0u || started)) {
            continue;
        }

        base = started
                   ? UINT32_C(0xf2800000)
                   : UINT32_C(0xd2800000);
        words[cursor++] =
            base |
            (index << 21u) |
            ((uint32_t)chunk << 5u) |
            UINT32_C(16);
        started = true;
    }

    if (!started) {
        words[cursor++] = UINT32_C(0xd2800010);
    }
    return cursor;
}
