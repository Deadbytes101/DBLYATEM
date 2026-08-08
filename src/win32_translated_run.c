#include "win32_translated_run_internal.h"

#include "dblyat/ir.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool db_run_zero_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (bytes[index] != 0u) {
            return false;
        }
    }

    return true;
}

static bool db_run_overlap(const void *left,
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

static bool db_run_mul_size(size_t left, size_t right, size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool db_run_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return address != 0u &&
           (sign == 0u ? upper == 0u : upper == UINT64_C(0xffff));
}

static bool db_run_scalar_valid(const db_x64_scalar_state *state)
{
    return db_run_canonical(state->rip) &&
           (state->rflags & DB_X64_RFLAGS_FIXED_ONE) != 0u;
}

static bool db_run_config_valid(
    const db_win32_translated_run_config *config)
{
    return config->cache != NULL &&
           config->source_generation != 0u &&
           config->format_version == DB_WIN32_TRANSLATED_RUN_FORMAT_VERSION &&
           db_run_zero_bytes(config->reserved, sizeof(config->reserved)) &&
           (config->gateway_buffer_size == 0u ||
            config->gateway_buffer != NULL) &&
           (config->target_count == 0u || config->targets != NULL) &&
           (config->handler_count == 0u || config->handlers != NULL) &&
           (config->signature_count == 0u || config->signatures != NULL) &&
           (config->guest_stack_size == 0u || config->guest_stack != NULL);
}

static db_win32_translated_run_status db_run_validate_aliases(
    const db_win32_translated_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_run_summary *summary)
{
    size_t cache_slot_bytes = 0u;
    size_t target_bytes;
    size_t handler_bytes;
    size_t signature_bytes;

    if (!db_run_mul_size(config->target_count,
                         sizeof(*config->targets),
                         &target_bytes) ||
        !db_run_mul_size(config->handler_count,
                         sizeof(*config->handlers),
                         &handler_bytes) ||
        !db_run_mul_size(config->signature_count,
                         sizeof(*config->signatures),
                         &signature_bytes) ||
        (config->cache->capacity != 0u &&
         !db_run_mul_size(config->cache->capacity,
                          sizeof(*config->cache->slots),
                          &cache_slot_bytes))) {
        return DB_WIN32_TRANSLATED_RUN_COUNT_OVERFLOW;
    }

    if (db_run_overlap(kernel32_state, sizeof(*kernel32_state),
                       scalar_state, sizeof(*scalar_state)) ||
        db_run_overlap(kernel32_state, sizeof(*kernel32_state),
                       summary, sizeof(*summary)) ||
        db_run_overlap(scalar_state, sizeof(*scalar_state),
                       summary, sizeof(*summary))) {
        return DB_WIN32_TRANSLATED_RUN_BUFFER_OVERLAP;
    }

#define DB_RUN_OUTPUT_INPUT(out, out_size, in, in_size) \
    db_run_overlap((out), (out_size), (in), (in_size))
#define DB_RUN_CHECK_OUTPUT(out, out_size) \
    do { \
        if (DB_RUN_OUTPUT_INPUT((out), (out_size), \
                                config, sizeof(*config)) || \
            DB_RUN_OUTPUT_INPUT((out), (out_size), \
                                config->cache, \
                                sizeof(*config->cache)) || \
            (config->cache->slots != NULL && \
             DB_RUN_OUTPUT_INPUT((out), (out_size), \
                                 config->cache->slots, \
                                 cache_slot_bytes)) || \
            DB_RUN_OUTPUT_INPUT((out), (out_size), \
                                config->gateway_buffer, \
                                config->gateway_buffer_size) || \
            DB_RUN_OUTPUT_INPUT((out), (out_size), \
                                config->targets, target_bytes) || \
            DB_RUN_OUTPUT_INPUT((out), (out_size), \
                                config->handlers, handler_bytes) || \
            DB_RUN_OUTPUT_INPUT((out), (out_size), \
                                config->signatures, signature_bytes) || \
            DB_RUN_OUTPUT_INPUT((out), (out_size), \
                                config->guest_stack, \
                                config->guest_stack_size)) { \
            return DB_WIN32_TRANSLATED_RUN_BUFFER_OVERLAP; \
        } \
    } while (0)

    DB_RUN_CHECK_OUTPUT(kernel32_state, sizeof(*kernel32_state));
    DB_RUN_CHECK_OUTPUT(scalar_state, sizeof(*scalar_state));
    DB_RUN_CHECK_OUTPUT(summary, sizeof(*summary));

#undef DB_RUN_CHECK_OUTPUT
#undef DB_RUN_OUTPUT_INPUT
    return DB_WIN32_TRANSLATED_RUN_OK;
}

static bool db_run_execution_summary_valid(
    const db_arm64_published_block *published,
    const db_x64_scalar_state *scalar_state,
    const db_win32_translated_execution_summary *execution)
{
    if (execution->format_version !=
            DB_WIN32_TRANSLATED_EXECUTION_FORMAT_VERSION ||
        execution->entry_executed != 1u ||
        !db_run_zero_bytes(execution->reserved,
                           sizeof(execution->reserved)) ||
        db_translated_terminal_exit_validate_observed(
            &execution->observed_exit) !=
            DB_TRANSLATED_TERMINAL_EXIT_OK ||
        db_translated_terminal_exit_match(
            &published->terminal_exit,
            &execution->observed_exit) !=
            DB_TRANSLATED_TERMINAL_EXIT_OK ||
        !db_run_scalar_valid(scalar_state)) {
        return false;
    }

    switch ((db_win32_translated_execution_outcome)execution->outcome) {
    case DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE:
        return execution->next_rip != 0u &&
               execution->next_rip == scalar_state->rip &&
               execution->trap == 0u &&
               execution->exit_code == 0u;

    case DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP:
        return execution->next_rip == scalar_state->rip &&
               execution->trap > (uint32_t)DB_IR_TRAP_NONE &&
               execution->trap <= (uint32_t)DB_IR_TRAP_SYSCALL &&
               execution->exit_code == 0u;

    case DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED:
        return execution->next_rip == 0u && execution->trap == 0u;

    case DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_NONE:
    default:
        return false;
    }
}

static void db_run_commit(
    const db_win32_kernel32_state *planned_kernel32,
    const db_x64_scalar_state *planned_scalar,
    const db_win32_translated_run_summary *completed,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_run_summary *summary)
{
    *kernel32_state = *planned_kernel32;
    *scalar_state = *planned_scalar;
    *summary = *completed;
}

static db_win32_translated_execution_status db_run_real_executor(
    void *context,
    const db_arm64_published_block *published,
    const db_win32_translated_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_execution_summary *summary)
{
    (void)context;

    return db_win32_translated_execution_step(
        published,
        config->gateway_buffer,
        config->gateway_buffer_size,
        config->targets,
        config->target_count,
        config->gateway_base,
        config->slot_size,
        config->handlers,
        config->handler_count,
        config->signatures,
        config->signature_count,
        config->guest_stack,
        config->guest_stack_size,
        config->guest_stack_base,
        kernel32_state,
        scalar_state,
        summary);
}

db_win32_translated_run_status db_win32_translated_run_with_executor(
    const db_win32_translated_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_run_summary *summary,
    db_win32_translated_run_executor executor,
    void *executor_context)
{
    db_win32_kernel32_state planned_kernel32;
    db_x64_scalar_state planned_scalar;
    db_win32_translated_run_summary completed;
    db_win32_translated_run_status alias_status;
    uint32_t step;

    if (config == NULL || kernel32_state == NULL || scalar_state == NULL ||
        summary == NULL || executor == NULL) {
        return DB_WIN32_TRANSLATED_RUN_NULL_ARGUMENT;
    }

    if (!db_run_config_valid(config)) {
        return DB_WIN32_TRANSLATED_RUN_BAD_CONFIG;
    }

    if (config->block_budget == 0u) {
        return DB_WIN32_TRANSLATED_RUN_BAD_BLOCK_BUDGET;
    }

    if (!db_run_scalar_valid(scalar_state)) {
        return DB_WIN32_TRANSLATED_RUN_BAD_SCALAR_STATE;
    }

    alias_status = db_run_validate_aliases(
        config, kernel32_state, scalar_state, summary);
    if (alias_status != DB_WIN32_TRANSLATED_RUN_OK) {
        return alias_status;
    }

    planned_kernel32 = *kernel32_state;
    planned_scalar = *scalar_state;
    (void)memset(&completed, 0, sizeof(completed));
    completed.source_rip = scalar_state->rip;
    completed.final_rip = scalar_state->rip;
    completed.source_generation = config->source_generation;
    completed.translation_key = config->translation_key;
    completed.block_budget = config->block_budget;
    completed.stop_reason =
        (uint32_t)DB_WIN32_TRANSLATED_RUN_STOP_BLOCK_BUDGET;
    completed.last_cache_status = (uint32_t)DB_ARM64_BLOCK_CACHE_OK;
    completed.last_execution_status =
        (uint32_t)DB_WIN32_TRANSLATED_EXECUTION_OK;
    completed.format_version = DB_WIN32_TRANSLATED_RUN_FORMAT_VERSION;

    for (step = 0u; step < config->block_budget; ++step) {
        db_arm64_published_block published;
        db_arm64_block_cache_result lookup;
        db_win32_translated_execution_summary execution;
        db_arm64_block_cache_status cache_status;
        db_win32_translated_execution_status execution_status;

        (void)memset(&published, 0, sizeof(published));
        (void)memset(&lookup, 0, sizeof(lookup));
        (void)memset(&execution, 0, sizeof(execution));

        cache_status = db_arm64_block_cache_lookup(
            config->cache,
            planned_scalar.rip,
            config->source_generation,
            config->translation_key,
            &published,
            &lookup);

        if (completed.lookup_count == UINT32_MAX) {
            return DB_WIN32_TRANSLATED_RUN_COUNT_OVERFLOW;
        }
        ++completed.lookup_count;
        completed.last_cache_status = (uint32_t)cache_status;

        if (cache_status == DB_ARM64_BLOCK_CACHE_NOT_FOUND) {
            (void)memset(&completed.last_lookup,
                         0,
                         sizeof(completed.last_lookup));
            completed.final_rip = planned_scalar.rip;
            completed.stop_reason =
                (uint32_t)DB_WIN32_TRANSLATED_RUN_STOP_CACHE_MISS;
            db_run_commit(&planned_kernel32,
                          &planned_scalar,
                          &completed,
                          kernel32_state,
                          scalar_state,
                          summary);
            return DB_WIN32_TRANSLATED_RUN_OK;
        }

        if (cache_status != DB_ARM64_BLOCK_CACHE_OK) {
            return DB_WIN32_TRANSLATED_RUN_CACHE_FAILED;
        }

        completed.last_lookup = lookup;
        execution_status = executor(
            executor_context,
            &published,
            config,
            &planned_kernel32,
            &planned_scalar,
            &execution);
        completed.last_execution_status = (uint32_t)execution_status;

        if (execution_status != DB_WIN32_TRANSLATED_EXECUTION_OK) {
            return DB_WIN32_TRANSLATED_RUN_EXECUTION_FAILED;
        }

        if (!db_run_execution_summary_valid(&published,
                                            &planned_scalar,
                                            &execution)) {
            return DB_WIN32_TRANSLATED_RUN_BAD_EXECUTION_OUTCOME;
        }

        if (completed.completed_blocks == UINT32_MAX) {
            return DB_WIN32_TRANSLATED_RUN_COUNT_OVERFLOW;
        }
        ++completed.completed_blocks;
        completed.last_execution = execution;
        completed.final_rip = planned_scalar.rip;

        switch ((db_win32_translated_execution_outcome)execution.outcome) {
        case DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE:
            break;

        case DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP:
            completed.stop_reason =
                (uint32_t)DB_WIN32_TRANSLATED_RUN_STOP_GUEST_TRAP;
            completed.trap = execution.trap;
            db_run_commit(&planned_kernel32,
                          &planned_scalar,
                          &completed,
                          kernel32_state,
                          scalar_state,
                          summary);
            return DB_WIN32_TRANSLATED_RUN_OK;

        case DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED:
            completed.stop_reason =
                (uint32_t)DB_WIN32_TRANSLATED_RUN_STOP_GUEST_TERMINATED;
            completed.exit_code = execution.exit_code;
            db_run_commit(&planned_kernel32,
                          &planned_scalar,
                          &completed,
                          kernel32_state,
                          scalar_state,
                          summary);
            return DB_WIN32_TRANSLATED_RUN_OK;

        case DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_NONE:
        default:
            return DB_WIN32_TRANSLATED_RUN_BAD_EXECUTION_OUTCOME;
        }
    }

    completed.final_rip = planned_scalar.rip;
    completed.stop_reason =
        (uint32_t)DB_WIN32_TRANSLATED_RUN_STOP_BLOCK_BUDGET;
    db_run_commit(&planned_kernel32,
                  &planned_scalar,
                  &completed,
                  kernel32_state,
                  scalar_state,
                  summary);
    return DB_WIN32_TRANSLATED_RUN_OK;
}

db_win32_translated_run_status db_win32_translated_run(
    const db_win32_translated_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_run_summary *summary)
{
    return db_win32_translated_run_with_executor(
        config,
        kernel32_state,
        scalar_state,
        summary,
        db_run_real_executor,
        NULL);
}

const char *db_win32_translated_run_status_name(
    db_win32_translated_run_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflow",
        "buffer overlap",
        "bad config",
        "bad block budget",
        "bad scalar state",
        "block cache failed",
        "one-block execution failed",
        "bad one-block execution outcome"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown translated Win32 run status";
    }

    return names[(size_t)status];
}

const char *db_win32_translated_run_stop_reason_name(
    db_win32_translated_run_stop_reason reason)
{
    static const char *const names[] = {
        "block budget exhausted",
        "block cache miss",
        "guest trap",
        "guest terminated"
    };

    if ((size_t)reason >= sizeof(names) / sizeof(names[0])) {
        return "unknown translated Win32 run stop reason";
    }

    return names[(size_t)reason];
}
