#include "dblyat/ir.h"
#include "dblyat/win32_translated_demand_run.h"
#include "win32_translated_demand_run_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PAGE_SIZE UINT64_C(4096)
#define STORE_CAPACITY 8u
#define RECORD_CAPACITY 8u
#define IR_CAPACITY 8u
#define SELECT_CAPACITY 16u
#define PLAN_CAPACITY 32u
#define SOURCE_WORD_CAPACITY 32u
#define RELOCATION_CAPACITY 16u
#define OUTPUT_WORD_CAPACITY 64u
#define RESOLUTION_CAPACITY 16u
#define IMAGE_CAPACITY (OUTPUT_WORD_CAPACITY * sizeof(uint32_t))

#define TEST_GENERATION UINT64_C(7)
#define TEST_TRANSLATION_KEY UINT64_C(0x11223344)
#define TEST_GUEST_BASE UINT64_C(0x100000)

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            printf("WIN32 TRANSLATED DEMAND RUN TEST: FAIL line=%d\n",       \
                   __LINE__);                                                 \
            return 0;                                                         \
        }                                                                     \
    } while (0)

static _Alignas(4096) uint8_t fake_arena[PAGE_SIZE * UINT64_C(4)];

struct fixture_workspace {
    db_x64_block_record block_records[RECORD_CAPACITY];
    db_ir_instruction ir[IR_CAPACITY];
    db_arm64_select_record selections[SELECT_CAPACITY];
    db_x64_arm64_plan_record plans[PLAN_CAPACITY];
    uint32_t source_words[SOURCE_WORD_CAPACITY];
    db_x64_arm64_relocation relocations[RELOCATION_CAPACITY];
    uint32_t output_words[OUTPUT_WORD_CAPACITY];
    db_x64_arm64_resolution resolutions[RESOLUTION_CAPACITY];
    uint8_t image_bytes[IMAGE_CAPACITY];
};

struct fake_platform_context {
    uint8_t *arena;
    size_t arena_size;
    uint32_t map_count;
    uint32_t copy_count;
    uint32_t protect_count;
    uint32_t sync_count;
    uint32_t unmap_count;
};

struct code_base_context {
    uint8_t *arena;
    size_t next_page;
    uint32_t call_count;
    uint8_t force_misaligned;
};

typedef struct fake_step {
    uint64_t expected_rip;
    uint64_t next_rip;
    uint64_t rax_value;
    uint32_t last_error;
    uint32_t outcome;
    uint32_t trap;
    uint32_t exit_code;
    db_win32_translated_execution_status status;
} fake_step;

typedef struct fake_script {
    const fake_step *steps;
    size_t count;
    size_t index;
} fake_script;

typedef struct demand_fixture {
    db_arm64_translation_store store;
    db_arm64_block_cache_slot slots[STORE_CAPACITY];
    db_linux_arm64_wx_handle handles[STORE_CAPACITY];
    uint8_t owned[STORE_CAPACITY];
    struct fixture_workspace storage;
    db_arm64_translation_workspace workspace;
    struct fake_platform_context platform_context;
    struct code_base_context base_context;
    db_linux_arm64_wx_platform platform;
    db_x64_arm64_helper_targets targets;
    db_x64_code_view code;
    db_arm64_translation_store_config translation;
    db_win32_translated_demand_run_config demand;
} demand_fixture;

static int same(const void *left, const void *right, size_t size)
{
    return memcmp(left, right, size) == 0;
}

static void fill_bytes(void *data, size_t size, uint8_t value)
{
    memset(data, value, size);
}

static int within_arena(const struct fake_platform_context *context,
                        const void *address,
                        size_t size)
{
    const uintptr_t begin = (uintptr_t)context->arena;
    const uintptr_t end = begin + context->arena_size;
    const uintptr_t value = (uintptr_t)address;

    return value >= begin && size <= context->arena_size &&
           value <= end - size;
}

static void *fake_map_rw(void *opaque,
                         void *requested_address,
                         size_t allocation_size)
{
    struct fake_platform_context *context =
        (struct fake_platform_context *)opaque;

    ++context->map_count;
    if (!within_arena(context, requested_address, allocation_size)) {
        return NULL;
    }
    return requested_address;
}

static int fake_copy(void *opaque,
                     void *destination,
                     const void *source,
                     size_t byte_count)
{
    struct fake_platform_context *context =
        (struct fake_platform_context *)opaque;

    ++context->copy_count;
    if (!within_arena(context, destination, byte_count)) {
        return -1;
    }
    memcpy(destination, source, byte_count);
    return 0;
}

static int fake_protect_rx(void *opaque,
                           void *mapping,
                           size_t allocation_size)
{
    struct fake_platform_context *context =
        (struct fake_platform_context *)opaque;

    ++context->protect_count;
    return within_arena(context, mapping, allocation_size) ? 0 : -1;
}

static int fake_sync_icache(void *opaque,
                            void *begin,
                            size_t byte_count)
{
    struct fake_platform_context *context =
        (struct fake_platform_context *)opaque;

    ++context->sync_count;
    return within_arena(context, begin, byte_count) ? 0 : -1;
}

static int fake_unmap(void *opaque,
                      void *mapping,
                      size_t allocation_size)
{
    struct fake_platform_context *context =
        (struct fake_platform_context *)opaque;

    ++context->unmap_count;
    return within_arena(context, mapping, allocation_size) ? 0 : -1;
}

static int reserve_code_base(void *opaque,
                             uint64_t guest_source_rip,
                             uint64_t source_generation,
                             uint64_t translation_key,
                             uint64_t page_size,
                             uint64_t *code_base)
{
    struct code_base_context *context =
        (struct code_base_context *)opaque;
    uintptr_t address;

    (void)guest_source_rip;
    (void)source_generation;
    (void)translation_key;

    ++context->call_count;
    if (page_size != PAGE_SIZE || context->next_page >= 4u) {
        return -1;
    }

    address = (uintptr_t)context->arena +
              context->next_page * (size_t)page_size;
    ++context->next_page;
    if (context->force_misaligned != 0u) {
        ++address;
    }
    *code_base = (uint64_t)address;
    return 0;
}

static db_arm64_translation_workspace make_workspace(
    struct fixture_workspace *storage)
{
    db_arm64_translation_workspace workspace;

    memset(&workspace, 0, sizeof(workspace));
    workspace.block_records = storage->block_records;
    workspace.ir = storage->ir;
    workspace.selections = storage->selections;
    workspace.plans = storage->plans;
    workspace.source_words = storage->source_words;
    workspace.relocations = storage->relocations;
    workspace.output_words = storage->output_words;
    workspace.resolutions = storage->resolutions;
    workspace.image_bytes = storage->image_bytes;
    workspace.block_record_capacity = RECORD_CAPACITY;
    workspace.ir_capacity = IR_CAPACITY;
    workspace.selection_capacity = SELECT_CAPACITY;
    workspace.plan_capacity = PLAN_CAPACITY;
    workspace.source_word_capacity = SOURCE_WORD_CAPACITY;
    workspace.relocation_capacity = RELOCATION_CAPACITY;
    workspace.output_word_capacity = OUTPUT_WORD_CAPACITY;
    workspace.resolution_capacity = RESOLUTION_CAPACITY;
    workspace.image_capacity = IMAGE_CAPACITY;
    return workspace;
}

static db_linux_arm64_wx_platform make_platform(
    struct fake_platform_context *context)
{
    db_linux_arm64_wx_platform platform;

    memset(&platform, 0, sizeof(platform));
    platform.context = context;
    platform.map_rw = fake_map_rw;
    platform.copy_bytes = fake_copy;
    platform.protect_rx = fake_protect_rx;
    platform.sync_icache = fake_sync_icache;
    platform.unmap = fake_unmap;
    return platform;
}

static db_x64_arm64_helper_targets make_targets(uint64_t target)
{
    db_x64_arm64_helper_targets targets;
    uint32_t index;

    memset(&targets, 0, sizeof(targets));
    for (index = (uint32_t)DB_X64_ARM64_HELPER_FORM_GUEST_ADDRESS;
         index < DB_X64_ARM64_HELPER_TARGET_COUNT;
         ++index) {
        targets.addresses[index] = target;
    }
    return targets;
}

static void kernel_init(db_win32_kernel32_state *state)
{
    memset(state, 0, sizeof(*state));
    state->process_id = UINT32_C(1001);
    state->thread_id = UINT32_C(2002);
}

static void scalar_init(db_x64_scalar_state *state, uint64_t rip)
{
    memset(state, 0, sizeof(*state));
    state->rip = rip;
    state->rflags = DB_X64_RFLAGS_FIXED_ONE;
    state->registers[0] = UINT64_C(0x1111111111111111);
}

static int fixture_init(demand_fixture *fixture,
                        const uint8_t *code_bytes,
                        size_t code_size,
                        uint32_t block_budget)
{
    memset(fake_arena, 0, sizeof(fake_arena));
    memset(fixture, 0, sizeof(*fixture));

    fixture->workspace = make_workspace(&fixture->storage);
    fixture->platform_context.arena = fake_arena;
    fixture->platform_context.arena_size = sizeof(fake_arena);
    fixture->base_context.arena = fake_arena;
    fixture->platform = make_platform(&fixture->platform_context);
    fixture->targets = make_targets(
        (uint64_t)(uintptr_t)(fake_arena + 2048u));

    fixture->code.bytes = code_bytes;
    fixture->code.size = code_size;
    fixture->code.guest_base = TEST_GUEST_BASE;

    memset(&fixture->translation, 0, sizeof(fixture->translation));
    fixture->translation.code = &fixture->code;
    fixture->translation.helper_targets = &fixture->targets;
    fixture->translation.wx_platform = &fixture->platform;
    fixture->translation.code_base_context = &fixture->base_context;
    fixture->translation.reserve_code_base = reserve_code_base;
    fixture->translation.source_generation = TEST_GENERATION;
    fixture->translation.translation_key = TEST_TRANSLATION_KEY;
    fixture->translation.page_size = PAGE_SIZE;
    fixture->translation.instruction_budget = 4u;
    fixture->translation.byte_budget = 32u;

    if (db_arm64_translation_store_init(
            &fixture->store,
            fixture->slots,
            fixture->handles,
            fixture->owned,
            STORE_CAPACITY) != DB_ARM64_TRANSLATION_STORE_OK) {
        return 0;
    }

    memset(&fixture->demand, 0, sizeof(fixture->demand));
    fixture->demand.store = &fixture->store;
    fixture->demand.translation = &fixture->translation;
    fixture->demand.workspace = &fixture->workspace;
    fixture->demand.block_budget = block_budget;
    fixture->demand.format_version =
        DB_WIN32_TRANSLATED_DEMAND_RUN_FORMAT_VERSION;
    return 1;
}

static int fixture_dispose(demand_fixture *fixture)
{
    return db_arm64_translation_store_dispose(
               &fixture->store, &fixture->platform) ==
           DB_ARM64_TRANSLATION_STORE_OK;
}

static db_win32_translated_execution_status fake_executor(
    void *context,
    const db_arm64_published_block *published,
    const db_win32_translated_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_execution_summary *summary)
{
    fake_script *script = (fake_script *)context;
    const fake_step *step;

    if (script == NULL || published == NULL || config == NULL ||
        kernel32_state == NULL || scalar_state == NULL || summary == NULL ||
        script->index >= script->count) {
        return DB_WIN32_TRANSLATED_EXECUTION_NULL_ARGUMENT;
    }

    step = &script->steps[script->index++];
    if (published->guest_source_rip != step->expected_rip ||
        scalar_state->rip != step->expected_rip ||
        published->source_generation != TEST_GENERATION ||
        published->translation_key != TEST_TRANSLATION_KEY ||
        config->source_generation != TEST_GENERATION ||
        config->translation_key != TEST_TRANSLATION_KEY) {
        return DB_WIN32_TRANSLATED_EXECUTION_BAD_PUBLISHED_BLOCK;
    }

    scalar_state->registers[0] = step->rax_value;
    kernel32_state->last_error = step->last_error;

    if (step->status != DB_WIN32_TRANSLATED_EXECUTION_OK) {
        scalar_state->rip = UINT64_C(0x77770000);
        fill_bytes(summary, sizeof(*summary), UINT8_C(0x5a));
        return step->status;
    }

    memset(summary, 0, sizeof(*summary));
    summary->observed_exit = published->terminal_exit;
    summary->outcome = step->outcome;
    summary->trap = step->trap;
    summary->exit_code = step->exit_code;
    summary->format_version = DB_WIN32_TRANSLATED_EXECUTION_FORMAT_VERSION;
    summary->entry_executed = 1u;

    switch ((db_win32_translated_execution_outcome)step->outcome) {
    case DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE:
        scalar_state->rip = step->next_rip;
        summary->next_rip = step->next_rip;
        break;
    case DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP:
        scalar_state->rip = published->terminal_exit.source_rip;
        summary->next_rip = scalar_state->rip;
        break;
    case DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED:
        scalar_state->rip = published->terminal_exit.source_rip;
        summary->next_rip = 0u;
        break;
    case DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_NONE:
    default:
        break;
    }

    return DB_WIN32_TRANSLATED_EXECUTION_OK;
}

static int test_initial_miss_publish_retry_terminate(void)
{
    static const uint8_t code_bytes[] = {
        0x0fu, 0x0bu, 0x0fu, 0x0bu, 0x0fu, 0x0bu
    };
    demand_fixture fixture;
    db_win32_kernel32_state kernel32;
    db_x64_scalar_state scalar;
    db_win32_translated_demand_run_summary summary;
    const fake_step steps[] = {
        {
            TEST_GUEST_BASE, 0u, UINT64_C(11), 1u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED,
            0u, 37u, DB_WIN32_TRANSLATED_EXECUTION_OK
        }
    };
    fake_script script = {steps, 1u, 0u};

    CHECK(fixture_init(&fixture, code_bytes, sizeof(code_bytes), 4u));
    kernel_init(&kernel32);
    scalar_init(&scalar, TEST_GUEST_BASE);
    fill_bytes(&summary, sizeof(summary), UINT8_C(0xa5));

    CHECK(db_win32_translated_demand_run_with_executor(
              &fixture.demand,
              &kernel32,
              &scalar,
              &summary,
              fake_executor,
              &script) == DB_WIN32_TRANSLATED_DEMAND_RUN_OK);
    CHECK(script.index == 1u);
    CHECK(summary.stop_reason ==
          (uint32_t)DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TERMINATED);
    CHECK(summary.run_count == 2u);
    CHECK(summary.fill_count == 1u);
    CHECK(summary.published_fill_count == 1u);
    CHECK(summary.cache_hit_fill_count == 0u);
    CHECK(summary.completed_blocks == 1u);
    CHECK(summary.lookup_count == 2u);
    CHECK(summary.exit_code == 37u);
    CHECK(summary.source_rip == TEST_GUEST_BASE);
    CHECK(summary.source_generation == TEST_GENERATION);
    CHECK(summary.translation_key == TEST_TRANSLATION_KEY);
    CHECK(summary.format_version ==
          DB_WIN32_TRANSLATED_DEMAND_RUN_FORMAT_VERSION);
    CHECK(fixture.store.cache.count == 1u);
    CHECK(fixture.base_context.call_count == 1u);
    CHECK(fixture.platform_context.map_count == 1u);
    CHECK(fixture.platform_context.protect_count == 1u);
    CHECK(fixture.platform_context.sync_count == 1u);
    CHECK(kernel32.last_error == 1u);
    CHECK(scalar.registers[0] == UINT64_C(11));
    CHECK(fixture_dispose(&fixture));
    CHECK(fixture.platform_context.unmap_count == 1u);
    return 1;
}

static int test_existing_hit_does_not_republish(void)
{
    static const uint8_t code_bytes[] = {
        0x0fu, 0x0bu, 0x0fu, 0x0bu
    };
    demand_fixture fixture;
    db_arm64_published_block published;
    db_arm64_translation_store_summary fill_summary;
    db_win32_kernel32_state kernel32;
    db_x64_scalar_state scalar;
    db_win32_translated_demand_run_summary summary;
    const fake_step steps[] = {
        {
            TEST_GUEST_BASE, 0u, UINT64_C(22), 2u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP,
            DB_IR_TRAP_BREAKPOINT, 0u,
            DB_WIN32_TRANSLATED_EXECUTION_OK
        }
    };
    fake_script script = {steps, 1u, 0u};
    uint32_t reserve_before;
    uint32_t map_before;

    CHECK(fixture_init(&fixture, code_bytes, sizeof(code_bytes), 4u));
    memset(&published, 0, sizeof(published));
    memset(&fill_summary, 0, sizeof(fill_summary));
    CHECK(db_arm64_translation_store_fill(
              &fixture.store,
              &fixture.translation,
              &fixture.workspace,
              TEST_GUEST_BASE,
              &published,
              &fill_summary) == DB_ARM64_TRANSLATION_STORE_OK);
    CHECK(fill_summary.outcome ==
          (uint32_t)DB_ARM64_TRANSLATION_STORE_OUTCOME_PUBLISHED);
    reserve_before = fixture.base_context.call_count;
    map_before = fixture.platform_context.map_count;

    kernel_init(&kernel32);
    scalar_init(&scalar, TEST_GUEST_BASE);
    memset(&summary, 0, sizeof(summary));

    CHECK(db_win32_translated_demand_run_with_executor(
              &fixture.demand,
              &kernel32,
              &scalar,
              &summary,
              fake_executor,
              &script) == DB_WIN32_TRANSLATED_DEMAND_RUN_OK);
    CHECK(summary.fill_count == 0u);
    CHECK(summary.published_fill_count == 0u);
    CHECK(summary.completed_blocks == 1u);
    CHECK(summary.stop_reason ==
          (uint32_t)DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TRAP);
    CHECK(fixture.base_context.call_count == reserve_before);
    CHECK(fixture.platform_context.map_count == map_before);
    CHECK(fixture.store.cache.count == 1u);
    CHECK(fixture_dispose(&fixture));
    return 1;
}

static int test_two_misses_global_budget(void)
{
    static const uint8_t code_bytes[] = {
        0x0fu, 0x0bu, 0x0fu, 0x0bu, 0x0fu, 0x0bu
    };
    demand_fixture fixture;
    db_win32_kernel32_state kernel32;
    db_x64_scalar_state scalar;
    db_win32_translated_demand_run_summary summary;
    const fake_step steps[] = {
        {
            TEST_GUEST_BASE,
            TEST_GUEST_BASE + UINT64_C(2),
            UINT64_C(31), 3u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
            0u, 0u, DB_WIN32_TRANSLATED_EXECUTION_OK
        },
        {
            TEST_GUEST_BASE + UINT64_C(2),
            TEST_GUEST_BASE + UINT64_C(4),
            UINT64_C(32), 4u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
            0u, 0u, DB_WIN32_TRANSLATED_EXECUTION_OK
        }
    };
    fake_script script = {steps, 2u, 0u};

    CHECK(fixture_init(&fixture, code_bytes, sizeof(code_bytes), 2u));
    kernel_init(&kernel32);
    scalar_init(&scalar, TEST_GUEST_BASE);
    memset(&summary, 0, sizeof(summary));

    CHECK(db_win32_translated_demand_run_with_executor(
              &fixture.demand,
              &kernel32,
              &scalar,
              &summary,
              fake_executor,
              &script) == DB_WIN32_TRANSLATED_DEMAND_RUN_OK);
    CHECK(script.index == 2u);
    CHECK(summary.stop_reason ==
          (uint32_t)DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_BLOCK_BUDGET);
    CHECK(summary.block_budget == 2u);
    CHECK(summary.completed_blocks == 2u);
    CHECK(summary.fill_count == 2u);
    CHECK(summary.published_fill_count == 2u);
    CHECK(summary.run_count == 3u);
    CHECK(summary.lookup_count == 4u);
    CHECK(fixture.store.cache.count == 2u);
    CHECK(fixture.base_context.call_count == 2u);
    CHECK(fixture.platform_context.map_count == 2u);
    CHECK(scalar.rip == TEST_GUEST_BASE + UINT64_C(4));
    CHECK(scalar.registers[0] == UINT64_C(32));
    CHECK(kernel32.last_error == 4u);
    CHECK(fixture_dispose(&fixture));
    return 1;
}

static int test_store_failure_rolls_back_caller(void)
{
    static const uint8_t code_bytes[] = {0x0fu, 0x0bu};
    demand_fixture fixture;
    db_win32_kernel32_state kernel32;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar;
    db_x64_scalar_state scalar_before;
    db_win32_translated_demand_run_summary summary;
    db_win32_translated_demand_run_summary summary_before;
    const fake_step unused_steps[] = {
        {
            TEST_GUEST_BASE, 0u, 0u, 0u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP,
            DB_IR_TRAP_INVALID_OPCODE, 0u,
            DB_WIN32_TRANSLATED_EXECUTION_OK
        }
    };
    fake_script script = {unused_steps, 1u, 0u};

    CHECK(fixture_init(&fixture, code_bytes, sizeof(code_bytes), 2u));
    fixture.base_context.force_misaligned = 1u;
    kernel_init(&kernel32);
    scalar_init(&scalar, TEST_GUEST_BASE);
    fill_bytes(&summary, sizeof(summary), UINT8_C(0x5a));
    kernel_before = kernel32;
    scalar_before = scalar;
    summary_before = summary;

    CHECK(db_win32_translated_demand_run_with_executor(
              &fixture.demand,
              &kernel32,
              &scalar,
              &summary,
              fake_executor,
              &script) == DB_WIN32_TRANSLATED_DEMAND_RUN_STORE_FAILED);
    CHECK(script.index == 0u);
    CHECK(same(&kernel32, &kernel_before, sizeof(kernel32)));
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    CHECK(fixture.store.cache.count == 0u);
    CHECK(fixture.base_context.call_count == 1u);
    CHECK(fixture.platform_context.map_count == 0u);
    CHECK(fixture_dispose(&fixture));
    return 1;
}

static int test_execution_failure_keeps_fill_but_rolls_back_caller(void)
{
    static const uint8_t code_bytes[] = {0x0fu, 0x0bu};
    demand_fixture fixture;
    db_win32_kernel32_state kernel32;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar;
    db_x64_scalar_state scalar_before;
    db_win32_translated_demand_run_summary summary;
    db_win32_translated_demand_run_summary summary_before;
    const fake_step steps[] = {
        {
            TEST_GUEST_BASE, 0u, UINT64_C(99), 99u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_NONE,
            0u, 0u, DB_WIN32_TRANSLATED_EXECUTION_BAD_PUBLISHED_BLOCK
        }
    };
    fake_script script = {steps, 1u, 0u};

    CHECK(fixture_init(&fixture, code_bytes, sizeof(code_bytes), 2u));
    kernel_init(&kernel32);
    scalar_init(&scalar, TEST_GUEST_BASE);
    fill_bytes(&summary, sizeof(summary), UINT8_C(0xa5));
    kernel_before = kernel32;
    scalar_before = scalar;
    summary_before = summary;

    CHECK(db_win32_translated_demand_run_with_executor(
              &fixture.demand,
              &kernel32,
              &scalar,
              &summary,
              fake_executor,
              &script) == DB_WIN32_TRANSLATED_DEMAND_RUN_RUN_FAILED);
    CHECK(script.index == 1u);
    CHECK(same(&kernel32, &kernel_before, sizeof(kernel32)));
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    CHECK(fixture.store.cache.count == 1u);
    CHECK(fixture.base_context.call_count == 1u);
    CHECK(fixture.platform_context.map_count == 1u);
    CHECK(fixture.owned[0] <= 1u);
    CHECK(fixture_dispose(&fixture));
    CHECK(fixture.platform_context.unmap_count == 1u);
    return 1;
}

static int test_bad_config_and_alias_preserve(void)
{
    static const uint8_t code_bytes[] = {0x0fu, 0x0bu};
    demand_fixture fixture;
    db_win32_kernel32_state kernel32;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar;
    db_x64_scalar_state scalar_before;
    db_win32_translated_demand_run_summary summary;
    db_win32_translated_demand_run_summary summary_before;
    union {
        db_win32_kernel32_state kernel;
        db_win32_translated_demand_run_summary summary;
    } overlap;
    const fake_step steps[] = {
        {
            TEST_GUEST_BASE, 0u, 0u, 0u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP,
            DB_IR_TRAP_INVALID_OPCODE, 0u,
            DB_WIN32_TRANSLATED_EXECUTION_OK
        }
    };
    fake_script script = {steps, 1u, 0u};

    CHECK(fixture_init(&fixture, code_bytes, sizeof(code_bytes), 2u));
    kernel_init(&kernel32);
    scalar_init(&scalar, TEST_GUEST_BASE);
    fill_bytes(&summary, sizeof(summary), UINT8_C(0x3c));
    kernel_before = kernel32;
    scalar_before = scalar;
    summary_before = summary;

    fixture.store.state = (uint32_t)DB_ARM64_TRANSLATION_STORE_POISONED;
    CHECK(db_win32_translated_demand_run_with_executor(
              &fixture.demand,
              &kernel32,
              &scalar,
              &summary,
              fake_executor,
              &script) == DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_CONFIG);
    CHECK(same(&kernel32, &kernel_before, sizeof(kernel32)));
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    fixture.store.state = (uint32_t)DB_ARM64_TRANSLATION_STORE_READY;

    memset(&overlap, 0, sizeof(overlap));
    kernel_init(&overlap.kernel);
    CHECK(db_win32_translated_demand_run_with_executor(
              &fixture.demand,
              &overlap.kernel,
              &scalar,
              &overlap.summary,
              fake_executor,
              &script) == DB_WIN32_TRANSLATED_DEMAND_RUN_BUFFER_OVERLAP);
    CHECK(fixture_dispose(&fixture));
    return 1;
}

static int test_public_real_executor_nonaarch64_rollback(void)
{
#if defined(__aarch64__)
    return 1;
#else
    static const uint8_t code_bytes[] = {0x0fu, 0x0bu};
    demand_fixture fixture;
    db_win32_kernel32_state kernel32;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar;
    db_x64_scalar_state scalar_before;
    db_win32_translated_demand_run_summary summary;
    db_win32_translated_demand_run_summary summary_before;

    CHECK(fixture_init(&fixture, code_bytes, sizeof(code_bytes), 1u));
    kernel_init(&kernel32);
    scalar_init(&scalar, TEST_GUEST_BASE);
    fill_bytes(&summary, sizeof(summary), UINT8_C(0x7a));
    kernel_before = kernel32;
    scalar_before = scalar;
    summary_before = summary;

    CHECK(db_win32_translated_demand_run(
              &fixture.demand,
              &kernel32,
              &scalar,
              &summary) == DB_WIN32_TRANSLATED_DEMAND_RUN_RUN_FAILED);
    CHECK(same(&kernel32, &kernel_before, sizeof(kernel32)));
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    CHECK(fixture.store.cache.count == 1u);
    CHECK(fixture.platform_context.map_count == 1u);
    CHECK(fixture_dispose(&fixture));
    return 1;
#endif
}

int main(void)
{
    if (!test_initial_miss_publish_retry_terminate()) return 11;
    if (!test_existing_hit_does_not_republish()) return 12;
    if (!test_two_misses_global_budget()) return 13;
    if (!test_store_failure_rolls_back_caller()) return 14;
    if (!test_execution_failure_keeps_fill_but_rolls_back_caller()) return 15;
    if (!test_bad_config_and_alias_preserve()) return 16;
    if (!test_public_real_executor_nonaarch64_rollback()) return 17;

    CHECK(db_win32_translated_demand_run_status_name(
              DB_WIN32_TRANSLATED_DEMAND_RUN_OK)[0] == 'o');
    CHECK(db_win32_translated_demand_run_stop_reason_name(
              DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_BLOCK_BUDGET)[0] == 'b');

    puts("CACHE MISS TRANSLATE RETRY: PASS");
    puts("DEMAND CACHE HIT NO REPUBLISH: PASS");
    puts("DEMAND GLOBAL BLOCK BUDGET: PASS");
    puts("DEMAND STORE FAILURE ROLLBACK: PASS");
    puts("DEMAND EXECUTION FAILURE STATE ROLLBACK: PASS");
    puts("DEMAND VALID FILLS REMAIN OWNED: PASS");
    puts("DEMAND RUN ADVERSARIAL CONTRACT: PASS");
    puts("PUBLIC DEMAND REAL EXECUTOR NONAARCH64 ROLLBACK: PASS");
    puts("NATIVE DEMAND MULTI BLOCK ARM64 EXECUTION: NOT RUN");
    puts("PRODUCTION PE STARTUP RUN: NOT IMPLEMENTED");
    puts("DBLYATEM IDENTICAL PE EXECUTION: NOT RUN");
    puts("WINLATOR COMPARISON: NOT RUN");
    puts("WIN32 TRANSLATED DEMAND RUN TEST: PASS");
    return 0;
}
