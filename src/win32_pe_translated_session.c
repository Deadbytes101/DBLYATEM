#include "win32_pe_translated_session_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool db_session_zero_bytes(const void *data, size_t size)
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

static bool db_session_overlap(const void *left,
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

static bool db_session_multiply(size_t left, size_t right, size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }
    *result = left * right;
    return true;
}

static bool db_session_page_size_valid(uint64_t page_size)
{
    return page_size >= UINT64_C(4096) &&
           (page_size & (page_size - UINT64_C(1))) == 0u;
}

static bool db_session_translation_workspace_valid(
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
           db_session_zero_bytes(workspace->reserved,
                                 sizeof(workspace->reserved));
}

static bool db_session_platform_valid(
    const db_linux_arm64_wx_platform *platform)
{
    return platform != NULL &&
           platform->map_rw != NULL &&
           platform->copy_bytes != NULL &&
           platform->protect_rx != NULL &&
           platform->sync_icache != NULL &&
           platform->unmap != NULL &&
           db_session_zero_bytes(platform->reserved,
                                 sizeof(platform->reserved));
}

static bool db_session_startup_workspace_shape_valid(
    const db_win32_pe_startup_workspace *workspace)
{
    return workspace != NULL &&
           workspace->staging_image != NULL &&
           workspace->staging_page_plan != NULL &&
           workspace->staging_gateway != NULL &&
           workspace->relocations != NULL &&
           workspace->modules != NULL &&
           workspace->imports != NULL &&
           workspace->providers != NULL &&
           workspace->provider_symbols != NULL &&
           workspace->handlers != NULL &&
           workspace->signatures != NULL &&
           workspace->bindings != NULL &&
           workspace->targets != NULL &&
           workspace->patches != NULL &&
           workspace->staging_image_size != 0u &&
           workspace->staging_page_capacity != 0u &&
           workspace->staging_gateway_size != 0u &&
           workspace->relocation_capacity != 0u &&
           workspace->module_capacity != 0u &&
           workspace->import_capacity != 0u &&
           workspace->provider_capacity != 0u &&
           workspace->provider_symbol_capacity != 0u &&
           workspace->handler_capacity != 0u &&
           workspace->signature_capacity != 0u &&
           workspace->binding_capacity != 0u &&
           workspace->target_capacity != 0u &&
           workspace->patch_capacity != 0u &&
           db_session_zero_bytes(workspace->reserved,
                                 sizeof(workspace->reserved));
}

static bool db_session_config_shape_valid(
    const db_win32_pe_translated_session_config *config)
{
    const db_win32_pe_startup_config *startup;

    if (config == NULL || config->startup == NULL ||
        config->startup_workspace == NULL ||
        config->translation_workspace == NULL ||
        config->cache_slots == NULL || config->cache_handles == NULL ||
        config->cache_owned == NULL || config->guest_stack == NULL ||
        config->reserve_code_base == NULL ||
        config->format_version !=
            DB_WIN32_PE_TRANSLATED_SESSION_FORMAT_VERSION ||
        !db_session_zero_bytes(config->reserved, sizeof(config->reserved)) ||
        config->cache_capacity == 0u ||
        (config->cache_capacity & (config->cache_capacity - 1u)) != 0u ||
        config->source_generation == 0u || config->translation_key == 0u ||
        !db_session_page_size_valid(config->translation_page_size) ||
        config->translation_instruction_budget == 0u ||
        config->translation_byte_budget == 0u ||
        config->run_block_budget == 0u || config->process_id == 0u ||
        config->thread_id == 0u ||
        !db_session_translation_workspace_valid(
            config->translation_workspace) ||
        !db_session_startup_workspace_shape_valid(
            config->startup_workspace) ||
        !db_session_platform_valid(config->wx_platform)) {
        return false;
    }

    startup = config->startup;
    return startup->format_version == DB_WIN32_PE_STARTUP_FORMAT_VERSION &&
           startup->source != NULL && startup->source_size != 0u &&
           startup->image != NULL && startup->image_size != 0u &&
           startup->page_plan != NULL && startup->page_capacity != 0u &&
           startup->gateway != NULL && startup->gateway_size != 0u &&
           startup->guest_stack_size != 0u && startup->page_size != 0u;
}

static bool db_session_helper_targets_valid(
    const db_x64_arm64_helper_targets *targets)
{
    uint32_t index;

    if (targets == NULL ||
        !db_session_zero_bytes(targets->reserved,
                               sizeof(targets->reserved)) ||
        targets->addresses[DB_X64_ARM64_HELPER_NONE] != 0u) {
        return false;
    }

    for (index = (uint32_t)DB_X64_ARM64_HELPER_FORM_GUEST_ADDRESS;
         index < DB_X64_ARM64_HELPER_TARGET_COUNT;
         ++index) {
        if (targets->addresses[index] == 0u) {
            return false;
        }
    }
    return true;
}

static bool db_session_empty_valid(
    const db_win32_pe_translated_session *session)
{
    db_win32_pe_translated_session expected;

    if (session == NULL) {
        return false;
    }
    (void)memset(&expected, 0, sizeof(expected));
    expected.state = (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_EMPTY;
    expected.format_version = DB_WIN32_PE_TRANSLATED_SESSION_FORMAT_VERSION;
    return memcmp(session, &expected, sizeof(expected)) == 0;
}

static bool db_session_run_shape_valid(
    const db_win32_pe_translated_session *session)
{
    return session != NULL &&
           session->state ==
               (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_READY &&
           session->format_version ==
               DB_WIN32_PE_TRANSLATED_SESSION_FORMAT_VERSION &&
           db_session_zero_bytes(session->reserved,
                                 sizeof(session->reserved)) &&
           session->wx_platform != NULL &&
           session->store.state ==
               (uint32_t)DB_ARM64_TRANSLATION_STORE_READY &&
           session->translation.code == &session->code &&
           session->translation.helper_targets == &session->helper_targets &&
           session->demand.store == &session->store &&
           session->demand.translation == &session->translation &&
           session->demand.workspace != NULL &&
           session->demand.format_version ==
               DB_WIN32_TRANSLATED_DEMAND_RUN_FORMAT_VERSION &&
           session->kernel32.process_id != 0u &&
           session->kernel32.thread_id != 0u &&
           session->kernel32.reserved == 0u;
}

static db_win32_pe_translated_session_status db_session_check_overlap(
    const db_win32_pe_translated_session *session,
    const void *pointer,
    size_t size)
{
    if (pointer != NULL && size != 0u &&
        db_session_overlap(session, sizeof(*session), pointer, size)) {
        return DB_WIN32_PE_TRANSLATED_SESSION_BUFFER_OVERLAP;
    }
    return DB_WIN32_PE_TRANSLATED_SESSION_OK;
}

static db_win32_pe_translated_session_status db_session_validate_aliases(
    const db_win32_pe_translated_session_config *config,
    const db_win32_pe_translated_session *session)
{
    const db_win32_pe_startup_config *startup = config->startup;
    const db_win32_pe_startup_workspace *sw = config->startup_workspace;
    const db_arm64_translation_workspace *tw = config->translation_workspace;
    size_t bytes;
    db_win32_pe_translated_session_status status;

#define DB_SESSION_CHECK(pointer_value, size_value)                          \
    do {                                                                     \
        status = db_session_check_overlap(                                   \
            session, (pointer_value), (size_value));                         \
        if (status != DB_WIN32_PE_TRANSLATED_SESSION_OK) return status;      \
    } while (0)
#define DB_SESSION_CHECK_ARRAY(pointer_value, count_value)                    \
    do {                                                                     \
        if (!db_session_multiply((count_value),                              \
                                 sizeof(*(pointer_value)), &bytes)) {         \
            return DB_WIN32_PE_TRANSLATED_SESSION_COUNT_OVERFLOW;            \
        }                                                                    \
        DB_SESSION_CHECK((pointer_value), bytes);                             \
    } while (0)

    DB_SESSION_CHECK(config, sizeof(*config));
    DB_SESSION_CHECK(startup, sizeof(*startup));
    DB_SESSION_CHECK(sw, sizeof(*sw));
    DB_SESSION_CHECK(tw, sizeof(*tw));
    DB_SESSION_CHECK(config->wx_platform, sizeof(*config->wx_platform));
    DB_SESSION_CHECK_ARRAY(config->cache_slots, config->cache_capacity);
    DB_SESSION_CHECK_ARRAY(config->cache_handles, config->cache_capacity);
    DB_SESSION_CHECK(config->cache_owned, config->cache_capacity);
    DB_SESSION_CHECK(config->guest_stack, startup->guest_stack_size);

    DB_SESSION_CHECK(startup->source, startup->source_size);
    DB_SESSION_CHECK(startup->image, startup->image_size);
    DB_SESSION_CHECK_ARRAY(startup->page_plan, startup->page_capacity);
    DB_SESSION_CHECK(startup->gateway, startup->gateway_size);

    DB_SESSION_CHECK(sw->staging_image, sw->staging_image_size);
    DB_SESSION_CHECK_ARRAY(sw->staging_page_plan,
                           sw->staging_page_capacity);
    DB_SESSION_CHECK(sw->staging_gateway, sw->staging_gateway_size);
    DB_SESSION_CHECK_ARRAY(sw->relocations, sw->relocation_capacity);
    DB_SESSION_CHECK_ARRAY(sw->modules, sw->module_capacity);
    DB_SESSION_CHECK_ARRAY(sw->imports, sw->import_capacity);
    DB_SESSION_CHECK_ARRAY(sw->providers, sw->provider_capacity);
    DB_SESSION_CHECK_ARRAY(sw->provider_symbols,
                           sw->provider_symbol_capacity);
    DB_SESSION_CHECK_ARRAY(sw->handlers, sw->handler_capacity);
    DB_SESSION_CHECK_ARRAY(sw->signatures, sw->signature_capacity);
    DB_SESSION_CHECK_ARRAY(sw->bindings, sw->binding_capacity);
    DB_SESSION_CHECK_ARRAY(sw->targets, sw->target_capacity);
    DB_SESSION_CHECK_ARRAY(sw->patches, sw->patch_capacity);

    DB_SESSION_CHECK_ARRAY(tw->block_records, tw->block_record_capacity);
    DB_SESSION_CHECK_ARRAY(tw->ir, tw->ir_capacity);
    DB_SESSION_CHECK_ARRAY(tw->selections, tw->selection_capacity);
    DB_SESSION_CHECK_ARRAY(tw->plans, tw->plan_capacity);
    DB_SESSION_CHECK_ARRAY(tw->source_words, tw->source_word_capacity);
    DB_SESSION_CHECK_ARRAY(tw->relocations, tw->relocation_capacity);
    DB_SESSION_CHECK_ARRAY(tw->output_words, tw->output_word_capacity);
    DB_SESSION_CHECK_ARRAY(tw->resolutions, tw->resolution_capacity);
    DB_SESSION_CHECK(tw->image_bytes, tw->image_capacity);

#undef DB_SESSION_CHECK_ARRAY
#undef DB_SESSION_CHECK
    return DB_WIN32_PE_TRANSLATED_SESSION_OK;
}

static void db_session_rebind_self(db_win32_pe_translated_session *session)
{
    session->translation.code = &session->code;
    session->translation.helper_targets = &session->helper_targets;
    session->demand.store = &session->store;
    session->demand.translation = &session->translation;
}

static void db_session_build_runtime(
    const db_win32_pe_translated_session_config *config,
    db_win32_pe_translated_session *session)
{
    const db_win32_pe_startup_config *startup = config->startup;
    db_win32_pe_startup_workspace *workspace = config->startup_workspace;

    (void)memset(&session->translation, 0, sizeof(session->translation));
    session->translation.code = &session->code;
    session->translation.helper_targets = &session->helper_targets;
    session->translation.wx_platform = config->wx_platform;
    session->translation.code_base_context = config->code_base_context;
    session->translation.reserve_code_base = config->reserve_code_base;
    session->translation.source_generation = config->source_generation;
    session->translation.translation_key = config->translation_key;
    session->translation.page_size = config->translation_page_size;
    session->translation.instruction_budget =
        config->translation_instruction_budget;
    session->translation.byte_budget = config->translation_byte_budget;

    (void)memset(&session->demand, 0, sizeof(session->demand));
    session->demand.store = &session->store;
    session->demand.translation = &session->translation;
    session->demand.workspace = config->translation_workspace;
    session->demand.image_memory = startup->image;
    session->demand.image_page_plan = startup->page_plan;
    session->demand.gateway_buffer = startup->gateway;
    session->demand.targets = workspace->targets;
    session->demand.handlers = workspace->handlers;
    session->demand.signatures = workspace->signatures;
    session->demand.guest_stack = config->guest_stack;
    session->demand.image_page_count =
        session->startup.page_entries_committed;
    session->demand.image_page_size = startup->page_size;
    session->demand.gateway_buffer_size =
        session->startup.gateway_bytes_committed;
    session->demand.target_count = session->startup.gateway_plan.target_count;
    session->demand.handler_count =
        session->startup.provider_tables.handler_count;
    session->demand.signature_count =
        session->startup.provider_tables.signature_count;
    session->demand.guest_stack_size = startup->guest_stack_size;
    session->demand.gateway_base = startup->gateway_base;
    session->demand.slot_size = startup->slot_size;
    session->demand.guest_stack_base = startup->guest_stack_base;
    session->demand.block_budget = config->run_block_budget;
    session->demand.format_version =
        DB_WIN32_TRANSLATED_DEMAND_RUN_FORMAT_VERSION;
}

static db_win32_pe_translated_session_status db_session_rollback_store(
    db_win32_pe_translated_session *completed,
    db_win32_pe_translated_session *session,
    db_win32_pe_translated_session_status primary)
{
    if (db_arm64_translation_store_dispose(
            &completed->store, completed->wx_platform) !=
        DB_ARM64_TRANSLATION_STORE_OK) {
        completed->state =
            (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_CLEANUP_REQUIRED;
        completed->format_version =
            DB_WIN32_PE_TRANSLATED_SESSION_FORMAT_VERSION;
        *session = *completed;
        db_session_rebind_self(session);
        return DB_WIN32_PE_TRANSLATED_SESSION_ROLLBACK_FAILED;
    }
    return primary;
}

void db_win32_pe_translated_session_init(
    db_win32_pe_translated_session *session)
{
    if (session == NULL) {
        return;
    }
    (void)memset(session, 0, sizeof(*session));
    session->state = (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_EMPTY;
    session->format_version = DB_WIN32_PE_TRANSLATED_SESSION_FORMAT_VERSION;
}

db_win32_pe_translated_session_status
 db_win32_pe_translated_session_prepare_with_helper_targets(
    const db_win32_pe_translated_session_config *config,
    const db_x64_arm64_helper_targets *helper_targets,
    db_win32_pe_translated_session *session)
{
    db_win32_pe_translated_session completed;
    db_win32_pe_translated_session_status alias_status;
    db_linux_arm64_guest_memory guest_memory;
    db_win32_pe_startup_status startup_status;
    db_arm64_translation_store_status store_status;

    if (config == NULL || helper_targets == NULL || session == NULL) {
        return DB_WIN32_PE_TRANSLATED_SESSION_NULL_ARGUMENT;
    }
    if (!db_session_empty_valid(session)) {
        return DB_WIN32_PE_TRANSLATED_SESSION_BAD_SESSION;
    }
    if (!db_session_config_shape_valid(config)) {
        return DB_WIN32_PE_TRANSLATED_SESSION_BAD_CONFIG;
    }
    alias_status = db_session_validate_aliases(config, session);
    if (alias_status != DB_WIN32_PE_TRANSLATED_SESSION_OK) {
        return alias_status;
    }
    if (!db_session_helper_targets_valid(helper_targets)) {
        return DB_WIN32_PE_TRANSLATED_SESSION_BAD_HELPER_TARGETS;
    }

    db_win32_pe_translated_session_init(&completed);
    completed.helper_targets = *helper_targets;
    completed.wx_platform = config->wx_platform;

    store_status = db_arm64_translation_store_init(
        &completed.store,
        config->cache_slots,
        config->cache_handles,
        config->cache_owned,
        config->cache_capacity);
    if (store_status != DB_ARM64_TRANSLATION_STORE_OK) {
        return DB_WIN32_PE_TRANSLATED_SESSION_STORE_INIT_FAILED;
    }

    startup_status = db_win32_pe_startup_prepare(
        config->startup,
        config->startup_workspace,
        &completed.mapped,
        &completed.scalar,
        &completed.code,
        &completed.startup);
    if (startup_status != DB_WIN32_PE_STARTUP_OK) {
        return db_session_rollback_store(
            &completed,
            session,
            DB_WIN32_PE_TRANSLATED_SESSION_STARTUP_FAILED);
    }

    db_session_build_runtime(config, &completed);
    if (db_linux_arm64_guest_memory_init(
            &guest_memory,
            completed.demand.image_memory,
            completed.code.size,
            completed.demand.image_page_plan,
            completed.demand.image_page_count,
            completed.demand.image_page_size,
            completed.code.guest_base,
            completed.demand.guest_stack,
            completed.demand.guest_stack_size,
            completed.demand.guest_stack_base,
            completed.demand.gateway_buffer,
            completed.demand.gateway_buffer_size,
            completed.demand.gateway_base) !=
        DB_LINUX_ARM64_GUEST_RUNTIME_OK) {
        return db_session_rollback_store(
            &completed,
            session,
            DB_WIN32_PE_TRANSLATED_SESSION_BAD_COMPOSITION);
    }

    completed.kernel32.process_id = config->process_id;
    completed.kernel32.thread_id = config->thread_id;
    completed.kernel32.last_error = 0u;
    completed.kernel32.reserved = 0u;
    completed.state = (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_READY;
    completed.format_version = DB_WIN32_PE_TRANSLATED_SESSION_FORMAT_VERSION;

    *session = completed;
    db_session_rebind_self(session);
    return DB_WIN32_PE_TRANSLATED_SESSION_OK;
}

db_win32_pe_translated_session_status
 db_win32_pe_translated_session_prepare(
    const db_win32_pe_translated_session_config *config,
    db_win32_pe_translated_session *session)
{
    db_x64_arm64_helper_targets helper_targets;
    db_linux_arm64_guest_runtime_status target_status;

    if (config == NULL || session == NULL) {
        return DB_WIN32_PE_TRANSLATED_SESSION_NULL_ARGUMENT;
    }
    if (!db_session_empty_valid(session)) {
        return DB_WIN32_PE_TRANSLATED_SESSION_BAD_SESSION;
    }
    if (!db_session_config_shape_valid(config)) {
        return DB_WIN32_PE_TRANSLATED_SESSION_BAD_CONFIG;
    }

    (void)memset(&helper_targets, 0, sizeof(helper_targets));
    target_status = db_linux_arm64_guest_runtime_helper_targets(
        &helper_targets);
    if (target_status == DB_LINUX_ARM64_GUEST_RUNTIME_UNSUPPORTED_HOST) {
        return DB_WIN32_PE_TRANSLATED_SESSION_UNSUPPORTED_HOST;
    }
    if (target_status != DB_LINUX_ARM64_GUEST_RUNTIME_OK ||
        !db_session_helper_targets_valid(&helper_targets)) {
        return DB_WIN32_PE_TRANSLATED_SESSION_BAD_HELPER_TARGETS;
    }

    return db_win32_pe_translated_session_prepare_with_helper_targets(
        config, &helper_targets, session);
}

static bool db_session_run_summary_valid(
    const db_win32_pe_translated_session *session,
    uint64_t source_rip,
    const db_x64_scalar_state *planned_scalar,
    const db_win32_translated_demand_run_summary *demand)
{
    if (demand->format_version !=
            DB_WIN32_TRANSLATED_DEMAND_RUN_FORMAT_VERSION ||
        demand->source_rip != source_rip ||
        demand->final_rip != planned_scalar->rip ||
        demand->source_generation != session->translation.source_generation ||
        demand->translation_key != session->translation.translation_key ||
        demand->block_budget != session->demand.block_budget ||
        demand->guest_memory_bound != 1u) {
        return false;
    }

    switch ((db_win32_translated_demand_run_stop_reason)demand->stop_reason) {
    case DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_BLOCK_BUDGET:
        return demand->completed_blocks == demand->block_budget &&
               demand->trap == 0u && demand->exit_code == 0u;
    case DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TRAP:
        return demand->completed_blocks != 0u && demand->trap != 0u &&
               demand->exit_code == 0u;
    case DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TERMINATED:
        return demand->completed_blocks != 0u && demand->trap == 0u;
    default:
        return false;
    }
}

static db_win32_pe_translated_session_status db_session_run_core(
    db_win32_pe_translated_session *session,
    db_win32_pe_translated_session_run_summary *summary,
    db_win32_translated_run_executor executor,
    void *executor_context)
{
    db_win32_kernel32_state planned_kernel32;
    db_x64_scalar_state planned_scalar;
    db_win32_translated_demand_run_summary demand_summary;
    db_win32_pe_translated_session_run_summary completed;
    db_win32_translated_demand_run_status demand_status;
    uint64_t source_rip;
    uint32_t state_before;

    if (session == NULL || summary == NULL) {
        return DB_WIN32_PE_TRANSLATED_SESSION_NULL_ARGUMENT;
    }
    if (db_session_overlap(session, sizeof(*session),
                           summary, sizeof(*summary))) {
        return DB_WIN32_PE_TRANSLATED_SESSION_BUFFER_OVERLAP;
    }
    if (session->state ==
        (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_TERMINATED) {
        return DB_WIN32_PE_TRANSLATED_SESSION_ALREADY_TERMINATED;
    }
    if (!db_session_run_shape_valid(session)) {
        return DB_WIN32_PE_TRANSLATED_SESSION_BAD_SESSION;
    }
    if (session->run_count == UINT32_MAX) {
        return DB_WIN32_PE_TRANSLATED_SESSION_COUNT_OVERFLOW;
    }

    planned_kernel32 = session->kernel32;
    planned_scalar = session->scalar;
    source_rip = session->scalar.rip;
    state_before = session->state;
    (void)memset(&demand_summary, 0, sizeof(demand_summary));

    if (executor == NULL) {
        demand_status = db_win32_translated_demand_run(
            &session->demand,
            &planned_kernel32,
            &planned_scalar,
            &demand_summary);
    } else {
        demand_status = db_win32_translated_demand_run_with_executor(
            &session->demand,
            &planned_kernel32,
            &planned_scalar,
            &demand_summary,
            executor,
            executor_context);
    }

    if (demand_status != DB_WIN32_TRANSLATED_DEMAND_RUN_OK) {
        return DB_WIN32_PE_TRANSLATED_SESSION_RUN_FAILED;
    }
    if (!db_session_run_summary_valid(session,
                                      source_rip,
                                      &planned_scalar,
                                      &demand_summary)) {
        return DB_WIN32_PE_TRANSLATED_SESSION_BAD_RUN_OUTCOME;
    }

    (void)memset(&completed, 0, sizeof(completed));
    completed.demand = demand_summary;
    completed.source_rip = source_rip;
    completed.final_rip = planned_scalar.rip;
    completed.session_run_count = session->run_count + 1u;
    completed.state_before = state_before;
    completed.stop_reason = demand_summary.stop_reason;
    completed.trap = demand_summary.trap;
    completed.exit_code = demand_summary.exit_code;
    completed.demand_status = (uint32_t)demand_status;
    completed.format_version = DB_WIN32_PE_TRANSLATED_SESSION_RUN_SUMMARY_VERSION;

    session->kernel32 = planned_kernel32;
    session->scalar = planned_scalar;
    session->last_run = demand_summary;
    ++session->run_count;

    if (demand_summary.stop_reason ==
        (uint32_t)DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TERMINATED) {
        session->state =
            (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_TERMINATED;
        session->exit_code = demand_summary.exit_code;
    } else {
        session->state = (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_READY;
        session->exit_code = 0u;
    }

    completed.state_after = session->state;
    *summary = completed;
    return DB_WIN32_PE_TRANSLATED_SESSION_OK;
}

db_win32_pe_translated_session_status
 db_win32_pe_translated_session_run_with_executor(
    db_win32_pe_translated_session *session,
    db_win32_pe_translated_session_run_summary *summary,
    db_win32_translated_run_executor executor,
    void *executor_context)
{
    if (executor == NULL) {
        return DB_WIN32_PE_TRANSLATED_SESSION_NULL_ARGUMENT;
    }
    return db_session_run_core(
        session, summary, executor, executor_context);
}

db_win32_pe_translated_session_status
 db_win32_pe_translated_session_run(
    db_win32_pe_translated_session *session,
    db_win32_pe_translated_session_run_summary *summary)
{
    return db_session_run_core(session, summary, NULL, NULL);
}

db_win32_pe_translated_session_status
 db_win32_pe_translated_session_dispose(
    db_win32_pe_translated_session *session)
{
    db_arm64_translation_store_status store_status;

    if (session == NULL) {
        return DB_WIN32_PE_TRANSLATED_SESSION_NULL_ARGUMENT;
    }
    if (session->format_version !=
            DB_WIN32_PE_TRANSLATED_SESSION_FORMAT_VERSION ||
        db_session_zero_bytes(session->reserved,
                              sizeof(session->reserved)) == false ||
        session->state ==
            (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_EMPTY ||
        session->wx_platform == NULL) {
        return DB_WIN32_PE_TRANSLATED_SESSION_BAD_SESSION;
    }

    store_status = db_arm64_translation_store_dispose(
        &session->store, session->wx_platform);
    if (store_status != DB_ARM64_TRANSLATION_STORE_OK) {
        session->state =
            (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_CLEANUP_REQUIRED;
        return DB_WIN32_PE_TRANSLATED_SESSION_DISPOSE_FAILED;
    }

    db_win32_pe_translated_session_init(session);
    return DB_WIN32_PE_TRANSLATED_SESSION_OK;
}

const char *db_win32_pe_translated_session_status_name(
    db_win32_pe_translated_session_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "count overflow",
        "buffer overlap",
        "bad configuration",
        "bad session",
        "unsupported host",
        "bad helper targets",
        "translation store initialization failed",
        "PE startup failed",
        "bad startup/runtime composition",
        "translated demand run failed",
        "bad translated demand outcome",
        "guest already terminated",
        "translation store disposal failed",
        "prepare rollback failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown Win32 translated PE session status";
    }
    return names[(size_t)status];
}

const char *db_win32_pe_translated_session_state_name(
    db_win32_pe_translated_session_state state)
{
    static const char *const names[] = {
        "empty",
        "ready",
        "terminated",
        "cleanup required"
    };

    if ((size_t)state >= sizeof(names) / sizeof(names[0])) {
        return "unknown Win32 translated PE session state";
    }
    return names[(size_t)state];
}
