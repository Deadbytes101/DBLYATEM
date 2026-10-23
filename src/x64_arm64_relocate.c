#include "x64_arm64_relocate_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DB_ARM64_BR_X9 UINT32_C(0xd61f0120)

static bool overlap(const void *left, size_t left_size,
                    const void *right, size_t right_size)
{
    const uintptr_t a = (uintptr_t)left;
    const uintptr_t b = (uintptr_t)right;

    if (left_size == 0u || right_size == 0u) {
        return false;
    }
    return a <= b ? b - a < left_size : a - b < right_size;
}

db_x64_arm64_relocate_status db_x64_arm64_resolve_helpers(
    const uint32_t *source_words,
    uint32_t source_word_count,
    const db_x64_arm64_relocation *relocations,
    uint32_t relocation_count,
    const db_x64_arm64_helper_targets *targets,
    uint64_t code_base,
    uint32_t *output_words,
    uint32_t output_word_capacity,
    db_x64_arm64_resolution *decisions,
    uint32_t decision_capacity,
    db_x64_arm64_relocate_summary *summary)
{
    db_x64_arm64_relocate_summary completed;
    uint64_t required_words = source_word_count;
    uint32_t direct_count = 0u;
    uint32_t thunk_count = 0u;
    uint32_t thunk_words = 0u;
    uint32_t index;
    size_t source_bytes;
    size_t relocation_bytes;
    size_t output_bytes;
    size_t decision_bytes;

    if (source_words == NULL || targets == NULL ||
        output_words == NULL || summary == NULL ||
        (relocation_count != 0u &&
         (relocations == NULL || decisions == NULL))) {
        return DB_X64_ARM64_RELOCATE_NULL_ARGUMENT;
    }
    if (source_word_count == 0u) {
        return DB_X64_ARM64_RELOCATE_BAD_WORD_COUNT;
    }
    if (!db_x64_arm64_relocate_canonical_internal(code_base) ||
        (code_base & UINT64_C(3)) != 0u) {
        return DB_X64_ARM64_RELOCATE_BAD_CODE_BASE;
    }
    if (!db_x64_arm64_relocate_target_table_valid_internal(
            targets)) {
        return DB_X64_ARM64_RELOCATE_BAD_TARGET_TABLE;
    }
    if (decision_capacity < relocation_count) {
        return DB_X64_ARM64_RELOCATE_DECISION_CAPACITY;
    }

#if SIZE_MAX < UINT64_MAX
    if ((uint64_t)source_word_count >
            (uint64_t)(SIZE_MAX / sizeof(*source_words)) ||
        (uint64_t)relocation_count >
            (uint64_t)(SIZE_MAX / sizeof(*relocations)) ||
        (uint64_t)output_word_capacity >
            (uint64_t)(SIZE_MAX / sizeof(*output_words)) ||
        (uint64_t)decision_capacity >
            (uint64_t)(SIZE_MAX / sizeof(*decisions))) {
        return DB_X64_ARM64_RELOCATE_ADDRESS_OVERFLOW;
    }
#endif

    source_bytes =
        (size_t)source_word_count * sizeof(*source_words);
    relocation_bytes =
        (size_t)relocation_count * sizeof(*relocations);
    output_bytes =
        (size_t)output_word_capacity * sizeof(*output_words);
    decision_bytes =
        (size_t)decision_capacity * sizeof(*decisions);

    if (overlap(source_words, source_bytes,
                output_words, output_bytes) ||
        overlap(source_words, source_bytes,
                decisions, decision_bytes) ||
        overlap(source_words, source_bytes,
                summary, sizeof(*summary)) ||
        overlap(relocations, relocation_bytes,
                output_words, output_bytes) ||
        overlap(relocations, relocation_bytes,
                decisions, decision_bytes) ||
        overlap(relocations, relocation_bytes,
                summary, sizeof(*summary)) ||
        overlap(targets, sizeof(*targets),
                output_words, output_bytes) ||
        overlap(targets, sizeof(*targets),
                decisions, decision_bytes) ||
        overlap(targets, sizeof(*targets),
                summary, sizeof(*summary)) ||
        overlap(output_words, output_bytes,
                decisions, decision_bytes) ||
        overlap(output_words, output_bytes,
                summary, sizeof(*summary)) ||
        overlap(decisions, decision_bytes,
                summary, sizeof(*summary))) {
        return DB_X64_ARM64_RELOCATE_BUFFER_OVERLAP;
    }

    for (index = 0u; index < relocation_count; ++index) {
        const db_x64_arm64_relocation *relocation =
            &relocations[index];
        uint64_t source_address;
        uint64_t target;
        uint32_t patched;

        if (!db_x64_arm64_relocate_record_valid_internal(
                relocation) ||
            relocation->word_index >= source_word_count) {
            return DB_X64_ARM64_RELOCATE_BAD_RELOCATION;
        }
        if (index != 0u &&
            relocation->word_index <=
                relocations[index - 1u].word_index) {
            return DB_X64_ARM64_RELOCATE_RELOCATION_ORDER;
        }
        if (!db_x64_arm64_relocate_placeholder_matches_internal(
                source_words[relocation->word_index],
                relocation->kind)) {
            return DB_X64_ARM64_RELOCATE_PLACEHOLDER_MISMATCH;
        }

        target = targets->addresses[relocation->helper];
        if (!db_x64_arm64_relocate_canonical_internal(target) ||
            (target & UINT64_C(3)) != 0u) {
            return DB_X64_ARM64_RELOCATE_BAD_TARGET_TABLE;
        }
        if (!db_x64_arm64_relocate_add_scaled_internal(
                code_base,
                relocation->word_index,
                &source_address)) {
            return DB_X64_ARM64_RELOCATE_ADDRESS_OVERFLOW;
        }

        if (db_x64_arm64_relocate_branch_word_internal(
                relocation->kind,
                source_address,
                target,
                &patched)) {
            ++direct_count;
        } else {
            const uint32_t count =
                db_x64_arm64_relocate_materialize_count_internal(
                    target) +
                1u;
            uint64_t thunk_address;

            if (UINT64_MAX - required_words < count ||
                required_words > UINT32_MAX) {
                return DB_X64_ARM64_RELOCATE_ADDRESS_OVERFLOW;
            }
            if (!db_x64_arm64_relocate_add_scaled_internal(
                    code_base,
                    (uint32_t)required_words,
                    &thunk_address)) {
                return DB_X64_ARM64_RELOCATE_ADDRESS_OVERFLOW;
            }
            if (!db_x64_arm64_relocate_branch_word_internal(
                    relocation->kind,
                    source_address,
                    thunk_address,
                    &patched)) {
                return DB_X64_ARM64_RELOCATE_THUNK_OUT_OF_RANGE;
            }

            required_words += count;
            thunk_words += count;
            ++thunk_count;
        }
    }

    if (required_words > output_word_capacity ||
        required_words > UINT32_MAX) {
        return DB_X64_ARM64_RELOCATE_WORD_CAPACITY;
    }

    {
        uint64_t final_address;
        if (!db_x64_arm64_relocate_add_scaled_internal(
                code_base,
                (uint32_t)required_words - 1u,
                &final_address)) {
            return DB_X64_ARM64_RELOCATE_ADDRESS_OVERFLOW;
        }
    }

    for (index = 0u; index < source_word_count; ++index) {
        output_words[index] = source_words[index];
    }

    {
        uint32_t cursor = source_word_count;

        for (index = 0u; index < relocation_count; ++index) {
            const db_x64_arm64_relocation *relocation =
                &relocations[index];
            db_x64_arm64_resolution decision = {0};
            uint64_t source_address;
            uint64_t target =
                targets->addresses[relocation->helper];
            uint32_t patched;

            (void)db_x64_arm64_relocate_add_scaled_internal(
                code_base,
                relocation->word_index,
                &source_address);

            decision.source_address = source_address;
            decision.helper_target = target;
            decision.relocation_index = index;
            decision.source_word_index =
                relocation->word_index;
            decision.relocation_kind = relocation->kind;
            decision.helper = relocation->helper;

            if (db_x64_arm64_relocate_branch_word_internal(
                    relocation->kind,
                    source_address,
                    target,
                    &patched)) {
                decision.resolution =
                    DB_X64_ARM64_RESOLUTION_DIRECT;
            } else {
                uint64_t thunk_address;
                uint32_t count;

                (void)db_x64_arm64_relocate_add_scaled_internal(
                    code_base,
                    cursor,
                    &thunk_address);
                (void)db_x64_arm64_relocate_branch_word_internal(
                    relocation->kind,
                    source_address,
                    thunk_address,
                    &patched);

                decision.resolution =
                    DB_X64_ARM64_RESOLUTION_FAR_THUNK;
                decision.thunk_address = thunk_address;
                decision.thunk_word_index = cursor;

                count =
                    db_x64_arm64_relocate_emit_materialize_internal(
                        target,
                        &output_words[cursor]);
                output_words[cursor + count] =
                    DB_ARM64_BR_X9;
                decision.thunk_word_count = count + 1u;
                cursor += count + 1u;
            }

            decision.patched_word = patched;
            output_words[relocation->word_index] = patched;
            decisions[index] = decision;
        }

        completed.code_base = code_base;
        completed.source_word_count = source_word_count;
        completed.output_word_count = cursor;
        completed.relocation_count = relocation_count;
        completed.direct_count = direct_count;
        completed.thunk_count = thunk_count;
        completed.thunk_word_count = thunk_words;

        for (index = 0u;
             index < sizeof(completed.reserved);
             ++index) {
            completed.reserved[index] = 0u;
        }
        *summary = completed;
    }

    return DB_X64_ARM64_RELOCATE_OK;
}

const char *db_x64_arm64_relocate_status_name(
    db_x64_arm64_relocate_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad source word count",
        "bad code base",
        "bad helper target table",
        "bad relocation",
        "relocation order invalid",
        "branch placeholder mismatch",
        "address overflow",
        "far thunk out of branch range",
        "insufficient output word capacity",
        "insufficient decision capacity"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown x86-64 ARM64 relocate status";
    }
    return names[(size_t)status];
}
