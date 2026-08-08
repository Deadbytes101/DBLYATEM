#include "win32_translated_demand_run_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct db_demand_region {
    const void *address;
    size_t size;
};

static bool db_demand_zero_bytes(const void *data, size_t size)
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

static bool db_demand_overlap(const void *left,
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

static bool db_demand_multiply(size_t left, size_t right, size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }
    *result = left * right;
    return true;
}

static bool db_demand_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return address != 0u &&
           (sign == 0u ? upper == 0u : upper == UINT64_C(0xffff));
}

static bool db_demand_scalar_valid(const db_x64_scalar_state *state)
{
    return db_demand_canonical(state->rip) &&
           (state->rflags & DB_X64_RFLAGS_FIXED_ONE) != 0u;
}

static bool db_demand_handle_empty(const db_linux_arm64_wx_handle *handle)
{
    return handle->mapping == NULL &&
           handle->entry == NULL &&
           handle->allocation_size == 0u &&
           handle->state == (uint32_t)DB_LINUX_ARM64_WX_EMPTY &&
           handle->reserved == 0u;
}

static bool db_demand_handle_owned(const db_linux_arm64_wx_handle *handle)
{
    return handle->mapping != NULL &&
           handle->entry != NULL &&
           handle->allocation_size != 0u &&
           handle->state == (uint32_t)DB_LINUX_ARM64_WX_PUBLISHED &&
           handle->reserved == 0u;
}

static bool db_demand_store_ready_shape(
    const db_arm64_translation_store *store)
{
    size_t index;
    size_t owned_count = 0u;

    if (store == NULL || store->cache.slots == NULL ||
        store->handles == NULL || store->owned == NULL ||
        store->capacity == 0u ||
        (store->capacity & (store->capacity - 1u)) != 0u ||
        store->cache.capacity != store->capacity ||
        store->cache.count > store->capacity ||
        store->cache.format_version != DB_ARM64_BLOCK_CACHE_FORMAT_VERSION ||
        store->format_version != DB_ARM64_TRANSLATION_STORE_FORMAT_VERSION ||
        store->state != (uint32_t)DB_ARM64_TRANSLATION_STORE_READY ||
        !db_demand_zero_bytes(store->reserved, sizeof(store->reserved)) ||
        !db_demand_zero_bytes(store->cache.reserved,
                              sizeof(store->cache.reserved)) ||
        !db_demand_handle_empty(&store->rollback_handle)) {
        return false;
    }

    for (index = 0u; index < store->capacity; ++index) {
        const uint8_t owned = store->owned[index];
        const db_arm64_block_cache_slot *slot = &store->cache.slots[index];

        if (owned > 1u) {
            return false;
        }
        if (owned == 0u) {
            if (!db_demand_handle_empty(&store->handles[index]) ||
                slot->state ==
                    (uint32_t)DB_ARM64_BLOCK_CACHE_SLOT_OCCUPIED) {
                return false;
            }
            continue;
        }
        if (!db_demand_handle_owned(&store->handles[index]) ||
            slot->state !=
                (uint32_t)DB_ARM64_BLOCK_CACHE_SLOT_OCCUPIED) {
            return false;
        }
        ++owned_count;
    }

    return owned_count == store->cache.count;
}

static bool db_demand_workspace_shape_valid(
    const db_arm64_translation_workspace *workspace)
{
    return workspace != NULL &&
           workspace->block_records != NULL &&
           workspace->ir != NULL &&
           workspace->selections != NULL &&
           workspace->plans != NULL &&
           workspace->source_words != NULL &&
           workspace->relocations != NULL &&
           workspace->output_words != NULL &&
           workspace->resolutions != NULL &&
           workspace->image_bytes != NULL &&
           workspace->block_record_capacity != 0u &&
           workspace->ir_capacity != 0u &&
           workspace->selection_capacity != 0u &&
           workspace->plan_capacity != 0u &&
           workspace->source_word_capacity != 0u &&
           workspace->relocation_capacity != 0u &&
           workspace->output_word_capacity != 0u &&
           workspace->resolution_capacity != 0u &&
           workspace->image_capacity != 0u &&
           db_demand_zero_bytes(workspace->reserved,
                                sizeof(workspace->reserved));
}

static bool db_demand_translation_shape_valid(
    const db_arm64_translation_store_config *translation)
{
    return translation != NULL && translation->code != NULL &&
           translation->helper_targets != NULL &&
           translation->wx_platform != NULL &&
           translation->reserve_code_base != NULL &&
           translation->source_generation != 0u &&
           translation->instruction_budget != 0u &&
           translation->byte_budget != 0u &&
           translation->page_size >= UINT64_C(4096) &&
           (translation->page_size &
            (translation->page_size - UINT64_C(1))) == 0u &&
           db_demand_zero_bytes(translation->reserved,
                                sizeof(translation->reserved));
}

static bool db_demand_config_valid(
    const db_win32_translated_demand_run_config *config)
{
    return config != NULL &&
           db_demand_store_ready_shape(config->store) &&
           db_demand_translation_shape_valid(config->translation) &&
           db_demand_workspace_shape_valid(config->workspace) &&
           config->format_version ==
               DB_WIN32_TRANSLATED_DEMAND_RUN_FORMAT_VERSION &&
           db_demand_zero_bytes(config->reserved, sizeof(config->reserved)) &&
           (config->gateway_buffer_size == 0u ||
            config->gateway_buffer != NULL) &&
           (config->target_count == 0u || config->targets != NULL) &&
           (config->handler_count == 0u || config->handlers != NULL) &&
           (config->signature_count == 0u || config->signatures != NULL) &&
           (config->guest_stack_size == 0u || config->guest_stack != NULL);
}

static bool db_demand_regions_disjoint(
    const struct db_demand_region *regions,
    size_t count)
{
    size_t left;

    for (left = 0u; left < count; ++left) {
        size_t right;

        for (right = left + 1u; right < count; ++right) {
            if (db_demand_overlap(regions[left].address,
                                  regions[left].size,
                                  regions[right].address,
                                  regions[right].size)) {
                return false;
            }
        }
    }
    return true;
}

static db_win32_translated_demand_run_status db_demand_validate_aliases(
    const db_win32_translated_demand_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_demand_run_summary *summary)
{
    const db_arm64_translation_workspace *workspace = config->workspace;
    const db_arm64_translation_store_config *translation = config->translation;
    struct db_demand_region outputs[3];
    struct db_demand_region inputs[22];
    size_t slot_bytes;
    size_t handle_bytes;
    size_t block_bytes;
    size_t ir_bytes;
    size_t selection_bytes;
    size_t plan_bytes;
    size_t source_word_bytes;
    size_t relocation_bytes;
    size_t output_word_bytes;
    size_t resolution_bytes;
    size_t target_bytes;
    size_t handler_bytes;
    size_t signature_bytes;
    size_t input_count = 0u;
    size_t out_index;
    size_t in_index;

    if (!db_demand_multiply(config->store->capacity,
                            sizeof(*config->store->cache.slots),
                            &slot_bytes) ||
        !db_demand_multiply(config->store->capacity,
                            sizeof(*config->store->handles),
                            &handle_bytes) ||
        !db_demand_multiply(workspace->block_record_capacity,
                            sizeof(*workspace->block_records),
                            &block_bytes) ||
        !db_demand_multiply(workspace->ir_capacity,
                            sizeof(*workspace->ir),
                            &ir_bytes) ||
        !db_demand_multiply(workspace->selection_capacity,
                            sizeof(*workspace->selections),
                            &selection_bytes) ||
        !db_demand_multiply(workspace->plan_capacity,
                            sizeof(*workspace->plans),
                            &plan_bytes) ||
        !db_demand_multiply(workspace->source_word_capacity,
                            sizeof(*workspace->source_words),
                            &source_word_bytes) ||
        !db_demand_multiply(workspace->relocation_capacity,
                            sizeof(*workspace->relocations),
                            &relocation_bytes) ||
        !db_demand_multiply(workspace->output_word_capacity,
                            sizeof(*workspace->output_words),
                            &output_word_bytes) ||
        !db_demand_multiply(workspace->resolution_capacity,
                            sizeof(*workspace->resolutions),
                            &resolution_bytes) ||
        !db_demand_multiply(config->target_count,
                            sizeof(*config->targets),
                            &target_bytes) ||
        !db_demand_multiply(config->handler_count,
                            sizeof(*config->handlers),
                            &handler_bytes) ||
        !db_demand_multiply(config->signature_count,
                            sizeof(*config->signatures),
                            &signature_bytes)) {
        return DB_WIN32_TRANSLATED_DEMAND_RUN_COUNT_OVERFLOW;
    }

    outputs[0] = (struct db_demand_region){kernel32_state,
                                           sizeof(*kernel32_state)};
    outputs[1] = (struct db_demand_region){scalar_state,
                                           sizeof(*scalar_state)};
    outputs[2] = (struct db_demand_region){summary, sizeof(*summary)};
    if (!db_demand_regions_disjoint(outputs, 3u)) {
        return DB_WIN32_TRANSLATED_DEMAND_RUN_BUFFER_OVERLAP;
    }

#define DB_DEMAND_INPUT(address_value, size_value)                         \
    do {                                                                   \
        inputs[input_count].address = (address_value);                     \
        inputs[input_count].size = (size_value);                           \
        ++input_count;                                                     \
    } while (0)

    DB_DEMAND_INPUT(config, sizeof(*config));
    DB_DEMAND_INPUT(config->store, sizeof(*config->store));
    DB_DEMAND_INPUT(translation, sizeof(*translation));
    DB_DEMAND_INPUT(workspace, sizeof(*workspace));
    DB_DEMAND_INPUT(config->store->cache.slots, slot_bytes);
    DB_DEMAND_INPUT(config->store->handles, handle_bytes);
    DB_DEMAND_INPUT(config->store->owned, config->store->capacity);
    DB_DEMAND_INPUT(workspace->block_records, block_bytes);
    DB_DEMAND_INPUT(workspace->ir, ir_bytes);
    DB_DEMAND_INPUT(workspace->selections, selection_bytes);
    DB_DEMAND_INPUT(workspace->plans, plan_bytes);
    DB_DEMAND_INPUT(workspace->source_words, source_word_bytes);
    DB_DEMAND_INPUT(workspace->relocations, relocation_bytes);
    DB_DEMAND_INPUT(workspace->output_words, output_word_bytes);
    DB_DEMAND_INPUT(workspace->resolutions, resolution_bytes);
    DB_DEMAND_INPUT(workspace->image_bytes, workspace->image_capacity);
    DB_DEMAND_INPUT(config->gateway_buffer, config->gateway_buffer_size);
    DB_DEMAND_INPUT(config->targets, target_bytes);
    DB_DEMAND_INPUT(config->handlers, handler_bytes);
    DB_DEMAND_INPUT(config->signatures, signature_bytes);
    DB_DEMAND_INPUT(config->guest_stack, config->guest_stack_size);
    DB_DEMAND_INPUT(translation->code, sizeof(*translation->code));

#undef DB_DEMAND_INPUT

    for (out_index = 0u; out_index < 3u; ++out_index) {
        for (in_index = 0u; in_index < input_count; ++in_index) {
            if (db_demand_overlap(outputs[out_index].address,
                                  outputs[out_index].size,
                                  inputs[in_index].address,
                                  inputs[in_index].size)) {
                return DB_WIN32_TRANSLATED_DEMAND_RUN_BUFFER_OVERLAP;
            }
        }
        if (translation->code->bytes != NULL &&
            db_demand_overlap(outputs[out_index].address,
                              outputs[out_index].size,
                              translation->code->bytes,
                              translation->code->size)) {
            return DB_WIN32_TRANSLATED_DEMAND_RUN_BUFFER_OVERLAP;
        }
        if (db_demand_overlap(outputs[out_index].address,
                              outputs[out_index].size,
                              translation->helper_targets,
                              sizeof(*translation->helper_targets)) ||
            db_demand_overlap(outputs[out_index].address,
                              outputs[out_index].size,
                              translation->wx_platform,
                              sizeof(*translation->wx_platform))) {
            return DB_WIN32_TRANSLATED_DEMAND_RUN_BUFFER_OVERLAP;
        }
    }

    return DB_WIN32_TRANSLATED_DEMAND_RUN_OK;
}

static void db_demand_build_run_config(
    const db_win32_translated_demand_run_config *config,
    uint32_t remaining_budget,
    db_win32_translated_run_config *run)
{
    (void)memset(run, 0, sizeof(*run));
    run->cache = &config->store->cache;
    run->gateway_buffer = config->gateway_buffer;
    run->targets = config->targets;
    run->handlers = config->handlers;
    run->signatures = config->signatures;
    run->guest_stack = config->guest_stack;
    run->gateway_buffer_size = config->gateway_buffer_size;
    run->target_count = config->target_count;
    run->handler_count = config->handler_count;
    run->signature_count = config->signature_count;
    run->guest_stack_size = config->guest_stack_size;
    run->source_generation = config->translation->source_generation;
    run->translation_key = config->translation->translation_key;
    run->gateway_base = config->gateway_base;
    run->slot_size = config->slot_size;
    run->guest_stack_base = config->guest_stack_base;
    run->block_budget = remaining_budget;
    run->format_version = DB_WIN32_TRANSLATED_RUN_FORMAT_VERSION;
}

static bool db_demand_run_summary_valid(
    const db_win32_translated_run_config *run,
    uint64_t segment_source_rip,
    uint32_t remaining_budget,
    const db_x64_scalar_state *scalar_state,
    const db_win32_translated_run_summary *summary)
{
    if (summary->format_version != DB_WIN32_TRANSLATED_RUN_FORMAT_VERSION ||
        summary->source_rip != segment_source_rip ||
        summary->source_generation != run->source_generation ||
        summary->translation_key != run->translation_key ||
        summary->block_budget != remaining_budget ||
        summary->completed_blocks > remaining_budget ||
        summary->final_rip != scalar_state->rip) {
        return false;
    }

    switch ((db_win32_translated_run_stop_reason)summary->stop_reason) {
    case DB_WIN32_TRANSLATED_RUN_STOP_CACHE_MISS:
        return summary->completed_blocks < remaining_budget &&
               summary->last_cache_status ==
                   (uint32_t)DB_ARM64_BLOCK_CACHE_NOT_FOUND;

    case DB_WIN32_TRANSLATED_RUN_STOP_BLOCK_BUDGET:
        return summary->completed_blocks == remaining_budget &&
               summary->trap == 0u && summary->exit_code == 0u;

    case DB_WIN32_TRANSLATED_RUN_STOP_GUEST_TRAP:
        return summary->completed_blocks != 0u &&
               summary->trap != 0u && summary->exit_code == 0u;

    case DB_WIN32_TRANSLATED_RUN_STOP_GUEST_TERMINATED:
        return summary->completed_blocks != 0u && summary->trap == 0u;

    default:
        return false;
    }
}

static bool db_demand_fill_summary_valid(
    const db_arm64_translation_store_config *translation,
    uint64_t miss_rip,
    const db_arm64_published_block *published,
    const db_arm64_translation_store_summary *summary)
{
    if (summary->format_version != DB_ARM64_TRANSLATION_STORE_FORMAT_VERSION ||
        summary->cache_status != (uint32_t)DB_ARM64_BLOCK_CACHE_OK ||
        summary->cache_result.found != 1u ||
        published->guest_source_rip != miss_rip ||
        published->source_generation != translation->source_generation ||
        published->translation_key != translation->translation_key ||
        published->format_version != DB_ARM64_PUBLISHED_BLOCK_FORMAT_VERSION) {
        return false;
    }

    switch ((db_arm64_translation_store_outcome)summary->outcome) {
    case DB_ARM64_TRANSLATION_STORE_OUTCOME_HIT:
        return summary->cache_result.inserted == 0u;
    case DB_ARM64_TRANSLATION_STORE_OUTCOME_PUBLISHED:
        return summary->cache_result.inserted == 1u;
    case DB_ARM64_TRANSLATION_STORE_OUTCOME_NONE:
    default:
        return false;
    }
}

static bool db_demand_add_u32(uint32_t left,
                              uint32_t right,
                              uint32_t *result)
{
    if (UINT32_MAX - left < right) {
        return false;
    }
    *result = left + right;
    return true;
}

static void db_demand_commit(
    const db_win32_kernel32_state *planned_kernel32,
    const db_x64_scalar_state *planned_scalar,
    const db_win32_translated_demand_run_summary *completed,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_demand_run_summary *summary)
{
    *kernel32_state = *planned_kernel32;
    *scalar_state = *planned_scalar;
    *summary = *completed;
}

static db_win32_translated_demand_run_status db_demand_run_core(
    const db_win32_translated_demand_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_demand_run_summary *summary,
    db_win32_translated_run_executor executor,
    void *executor_context)
{
    db_win32_kernel32_state planned_kernel32;
    db_x64_scalar_state planned_scalar;
    db_win32_translated_demand_run_summary completed;
    db_win32_translated_demand_run_status alias_status;

    if (config == NULL || kernel32_state == NULL || scalar_state == NULL ||
        summary == NULL) {
        return DB_WIN32_TRANSLATED_DEMAND_RUN_NULL_ARGUMENT;
    }
    if (!db_demand_config_valid(config)) {
        return DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_CONFIG;
    }
    if (config->block_budget == 0u) {
        return DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_BLOCK_BUDGET;
    }
    if (!db_demand_scalar_valid(scalar_state)) {
        return DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_SCALAR_STATE;
    }

    alias_status = db_demand_validate_aliases(
        config, kernel32_state, scalar_state, summary);
    if (alias_status != DB_WIN32_TRANSLATED_DEMAND_RUN_OK) {
        return alias_status;
    }

    planned_kernel32 = *kernel32_state;
    planned_scalar = *scalar_state;
    (void)memset(&completed, 0, sizeof(completed));
    completed.source_rip = scalar_state->rip;
    completed.final_rip = scalar_state->rip;
    completed.source_generation = config->translation->source_generation;
    completed.translation_key = config->translation->translation_key;
    completed.block_budget = config->block_budget;
    completed.stop_reason =
        (uint32_t)DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_BLOCK_BUDGET;
    completed.last_run_status = (uint32_t)DB_WIN32_TRANSLATED_RUN_OK;
    completed.last_store_status = (uint32_t)DB_ARM64_TRANSLATION_STORE_OK;
    completed.format_version = DB_WIN32_TRANSLATED_DEMAND_RUN_FORMAT_VERSION;

    while (completed.completed_blocks < config->block_budget) {
        db_win32_translated_run_config run;
        db_win32_translated_run_summary run_summary;
        db_win32_translated_run_status run_status;
        uint32_t remaining_budget =
            config->block_budget - completed.completed_blocks;
        uint32_t new_total;
        uint32_t new_lookup_total;
        uint64_t segment_source_rip = planned_scalar.rip;

        db_demand_build_run_config(config, remaining_budget, &run);
        (void)memset(&run_summary, 0, sizeof(run_summary));

        if (executor == NULL) {
            run_status = db_win32_translated_run(
                &run, &planned_kernel32, &planned_scalar, &run_summary);
        } else {
            run_status = db_win32_translated_run_with_executor(
                &run,
                &planned_kernel32,
                &planned_scalar,
                &run_summary,
                executor,
                executor_context);
        }

        if (run_status != DB_WIN32_TRANSLATED_RUN_OK) {
            return DB_WIN32_TRANSLATED_DEMAND_RUN_RUN_FAILED;
        }
        if (!db_demand_run_summary_valid(&run,
                                         segment_source_rip,
                                         remaining_budget,
                                         &planned_scalar,
                                         &run_summary)) {
            return DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_RUN_OUTCOME;
        }
        if (completed.run_count == UINT32_MAX ||
            !db_demand_add_u32(completed.completed_blocks,
                               run_summary.completed_blocks,
                               &new_total) ||
            !db_demand_add_u32(completed.lookup_count,
                               run_summary.lookup_count,
                               &new_lookup_total)) {
            return DB_WIN32_TRANSLATED_DEMAND_RUN_COUNT_OVERFLOW;
        }

        ++completed.run_count;
        completed.completed_blocks = new_total;
        completed.lookup_count = new_lookup_total;
        completed.final_rip = planned_scalar.rip;
        completed.last_run = run_summary;
        completed.last_run_status = (uint32_t)run_status;

        switch ((db_win32_translated_run_stop_reason)run_summary.stop_reason) {
        case DB_WIN32_TRANSLATED_RUN_STOP_CACHE_MISS: {
            db_arm64_published_block published;
            db_arm64_translation_store_summary fill_summary;
            db_arm64_translation_store_status store_status;
            uint64_t miss_rip = planned_scalar.rip;

            if (completed.fill_count >= config->block_budget) {
                return DB_WIN32_TRANSLATED_DEMAND_RUN_FILL_STALLED;
            }

            (void)memset(&published, 0, sizeof(published));
            (void)memset(&fill_summary, 0, sizeof(fill_summary));
            store_status = db_arm64_translation_store_fill(
                config->store,
                config->translation,
                config->workspace,
                miss_rip,
                &published,
                &fill_summary);
            if (store_status != DB_ARM64_TRANSLATION_STORE_OK) {
                return DB_WIN32_TRANSLATED_DEMAND_RUN_STORE_FAILED;
            }
            if (!db_demand_fill_summary_valid(config->translation,
                                               miss_rip,
                                               &published,
                                               &fill_summary) ||
                !db_demand_store_ready_shape(config->store) ||
                planned_scalar.rip != miss_rip) {
                return DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_STORE_OUTCOME;
            }

            ++completed.fill_count;
            if (fill_summary.outcome ==
                (uint32_t)DB_ARM64_TRANSLATION_STORE_OUTCOME_PUBLISHED) {
                ++completed.published_fill_count;
            } else {
                ++completed.cache_hit_fill_count;
            }
            completed.last_fill = fill_summary;
            completed.last_store_status = (uint32_t)store_status;
            break;
        }

        case DB_WIN32_TRANSLATED_RUN_STOP_BLOCK_BUDGET:
            completed.stop_reason =
                (uint32_t)DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_BLOCK_BUDGET;
            db_demand_commit(&planned_kernel32,
                             &planned_scalar,
                             &completed,
                             kernel32_state,
                             scalar_state,
                             summary);
            return DB_WIN32_TRANSLATED_DEMAND_RUN_OK;

        case DB_WIN32_TRANSLATED_RUN_STOP_GUEST_TRAP:
            completed.stop_reason =
                (uint32_t)DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TRAP;
            completed.trap = run_summary.trap;
            db_demand_commit(&planned_kernel32,
                             &planned_scalar,
                             &completed,
                             kernel32_state,
                             scalar_state,
                             summary);
            return DB_WIN32_TRANSLATED_DEMAND_RUN_OK;

        case DB_WIN32_TRANSLATED_RUN_STOP_GUEST_TERMINATED:
            completed.stop_reason =
                (uint32_t)DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TERMINATED;
            completed.exit_code = run_summary.exit_code;
            db_demand_commit(&planned_kernel32,
                             &planned_scalar,
                             &completed,
                             kernel32_state,
                             scalar_state,
                             summary);
            return DB_WIN32_TRANSLATED_DEMAND_RUN_OK;

        default:
            return DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_RUN_OUTCOME;
        }
    }

    return DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_RUN_OUTCOME;
}

db_win32_translated_demand_run_status
 db_win32_translated_demand_run_with_executor(
    const db_win32_translated_demand_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_demand_run_summary *summary,
    db_win32_translated_run_executor executor,
    void *executor_context)
{
    if (executor == NULL) {
        return DB_WIN32_TRANSLATED_DEMAND_RUN_NULL_ARGUMENT;
    }
    return db_demand_run_core(config,
                              kernel32_state,
                              scalar_state,
                              summary,
                              executor,
                              executor_context);
}

db_win32_translated_demand_run_status db_win32_translated_demand_run(
    const db_win32_translated_demand_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_demand_run_summary *summary)
{
    return db_demand_run_core(config,
                              kernel32_state,
                              scalar_state,
                              summary,
                              NULL,
                              NULL);
}

const char *db_win32_translated_demand_run_status_name(
    db_win32_translated_demand_run_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflow",
        "buffer overlap",
        "bad config",
        "bad block budget",
        "bad scalar state",
        "cache-only run failed",
        "bad cache-only run outcome",
        "translation store failed",
        "bad translation store outcome",
        "cache-miss fill stalled"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown translated Win32 demand-run status";
    }
    return names[(size_t)status];
}

const char *db_win32_translated_demand_run_stop_reason_name(
    db_win32_translated_demand_run_stop_reason reason)
{
    static const char *const names[] = {
        "block budget exhausted",
        "guest trap",
        "guest terminated"
    };

    if ((size_t)reason >= sizeof(names) / sizeof(names[0])) {
        return "unknown translated Win32 demand-run stop reason";
    }
    return names[(size_t)reason];
}
