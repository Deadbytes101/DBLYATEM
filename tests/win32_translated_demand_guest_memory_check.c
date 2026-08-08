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

#define TEST_GENERATION UINT64_C(17)
#define TEST_TRANSLATION_KEY UINT64_C(0x8899aabb)
#define IMAGE_BASE UINT64_C(0x0000000140000000)
#define GATEWAY_BASE UINT64_C(0x0000000180000000)
#define STACK_BASE UINT64_C(0x0000700000000000)
#define UNMAPPED_BASE UINT64_C(0x00000001c0000000)

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("WIN32 DEMAND GUEST MEMORY CHECK: FAIL line=%d\n",      \
                   __LINE__);                                                \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static _Alignas(4096) uint8_t fake_arena[PAGE_SIZE * UINT64_C(8)];

struct workspace_storage {
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
    uint32_t unmap_count;
};

struct code_base_context {
    uint8_t *arena;
    size_t next_page;
    uint32_t call_count;
};

typedef struct scripted_step {
    uint64_t expected_rip;
    uint64_t next_rip;
    uint32_t outcome;
    uint32_t exit_code;
    uint32_t last_error;
} scripted_step;

typedef struct scripted_executor {
    const scripted_step *steps;
    size_t count;
    size_t index;
    const uint8_t *image;
    size_t image_size;
    const db_map_page_protection *page_plan;
    uint8_t *stack;
    size_t stack_size;
    const uint8_t *gateway;
    size_t gateway_size;
} scripted_executor;

typedef struct fixture {
    uint8_t image[16];
    uint8_t gateway[16];
    uint8_t stack[256];
    db_map_page_protection page_plan[2];
    db_arm64_translation_store store;
    db_arm64_block_cache_slot slots[STORE_CAPACITY];
    db_linux_arm64_wx_handle handles[STORE_CAPACITY];
    uint8_t owned[STORE_CAPACITY];
    struct workspace_storage storage;
    db_arm64_translation_workspace workspace;
    struct fake_platform_context platform_context;
    struct code_base_context base_context;
    db_linux_arm64_wx_platform platform;
    db_x64_arm64_helper_targets helper_targets;
    db_x64_code_view image_code;
    db_arm64_translation_store_config translation;
    db_win32_translated_demand_run_config demand;
} fixture;

static int same(const void *left, const void *right, size_t size)
{
    return memcmp(left, right, size) == 0;
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
    return within_arena(context, requested_address, allocation_size)
               ? requested_address
               : NULL;
}

static int fake_copy(void *opaque,
                     void *destination,
                     const void *source,
                     size_t byte_count)
{
    struct fake_platform_context *context =
        (struct fake_platform_context *)opaque;

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

    return within_arena(context, mapping, allocation_size) ? 0 : -1;
}

static int fake_sync_icache(void *opaque,
                            void *begin,
                            size_t byte_count)
{
    struct fake_platform_context *context =
        (struct fake_platform_context *)opaque;

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
    if (page_size != PAGE_SIZE || context->next_page >= 8u) {
        return -1;
    }

    address = (uintptr_t)context->arena +
              context->next_page * (size_t)page_size;
    ++context->next_page;
    *code_base = (uint64_t)address;
    return 0;
}

static db_arm64_translation_workspace make_workspace(
    struct workspace_storage *storage)
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
    state->process_id = UINT32_C(7001);
    state->thread_id = UINT32_C(7002);
}

static void scalar_init(db_x64_scalar_state *state)
{
    memset(state, 0, sizeof(*state));
    state->rip = IMAGE_BASE;
    state->rflags = DB_X64_RFLAGS_FIXED_ONE;
    state->registers[0] = UINT64_C(0x1111222233334444);
    state->registers[4] = STACK_BASE + UINT64_C(256);
}

static int fixture_init(fixture *f, uint32_t block_budget)
{
    memset(fake_arena, 0, sizeof(fake_arena));
    memset(f, 0, sizeof(*f));

    f->image[0] = UINT8_C(0x0f);
    f->image[1] = UINT8_C(0x0b);
    f->gateway[0] = UINT8_C(0x0f);
    f->gateway[1] = UINT8_C(0x0b);
    f->page_plan[0] = DB_MAP_PAGE_RX;

    f->workspace = make_workspace(&f->storage);
    f->platform_context.arena = fake_arena;
    f->platform_context.arena_size = sizeof(fake_arena);
    f->base_context.arena = fake_arena;
    f->platform = make_platform(&f->platform_context);
    f->helper_targets = make_targets(
        (uint64_t)(uintptr_t)(fake_arena + 2048u));

    memset(&f->image_code, 0, sizeof(f->image_code));
    f->image_code.bytes = f->image;
    f->image_code.size = 2u;
    f->image_code.guest_base = IMAGE_BASE;

    memset(&f->translation, 0, sizeof(f->translation));
    f->translation.code = &f->image_code;
    f->translation.helper_targets = &f->helper_targets;
    f->translation.wx_platform = &f->platform;
    f->translation.code_base_context = &f->base_context;
    f->translation.reserve_code_base = reserve_code_base;
    f->translation.source_generation = TEST_GENERATION;
    f->translation.translation_key = TEST_TRANSLATION_KEY;
    f->translation.page_size = PAGE_SIZE;
    f->translation.instruction_budget = 4u;
    f->translation.byte_budget = 32u;

    if (db_arm64_translation_store_init(
            &f->store,
            f->slots,
            f->handles,
            f->owned,
            STORE_CAPACITY) != DB_ARM64_TRANSLATION_STORE_OK) {
        return 0;
    }

    memset(&f->demand, 0, sizeof(f->demand));
    f->demand.store = &f->store;
    f->demand.translation = &f->translation;
    f->demand.workspace = &f->workspace;
    f->demand.image_memory = f->image;
    f->demand.image_page_plan = f->page_plan;
    f->demand.image_page_count = 1u;
    f->demand.image_page_size = (size_t)PAGE_SIZE;
    f->demand.gateway_buffer = f->gateway;
    f->demand.gateway_buffer_size = 2u;
    f->demand.gateway_base = GATEWAY_BASE;
    f->demand.guest_stack = f->stack;
    f->demand.guest_stack_size = sizeof(f->stack);
    f->demand.guest_stack_base = STACK_BASE;
    f->demand.block_budget = block_budget;
    f->demand.format_version =
        DB_WIN32_TRANSLATED_DEMAND_RUN_FORMAT_VERSION;
    return 1;
}

static int fixture_dispose(fixture *f)
{
    return db_arm64_translation_store_dispose(
               &f->store, &f->platform) == DB_ARM64_TRANSLATION_STORE_OK;
}

static db_win32_translated_execution_status scripted_execute(
    void *opaque,
    const db_arm64_published_block *published,
    const db_win32_translated_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_execution_summary *summary)
{
    scripted_executor *script = (scripted_executor *)opaque;
    const scripted_step *step;
    const db_linux_arm64_guest_memory *memory;

    if (script == NULL || published == NULL || config == NULL ||
        kernel32_state == NULL || scalar_state == NULL || summary == NULL ||
        script->index >= script->count) {
        return DB_WIN32_TRANSLATED_EXECUTION_NULL_ARGUMENT;
    }

    memory = config->guest_memory;
    if (memory == NULL ||
        db_linux_arm64_guest_memory_validate(memory) !=
            DB_LINUX_ARM64_GUEST_RUNTIME_OK ||
        memory->image_host_address !=
            (uint64_t)(uintptr_t)script->image ||
        memory->image_guest_base != IMAGE_BASE ||
        memory->image_size != script->image_size ||
        memory->image_page_plan_address !=
            (uint64_t)(uintptr_t)script->page_plan ||
        memory->stack_host_address !=
            (uint64_t)(uintptr_t)script->stack ||
        memory->stack_guest_base != STACK_BASE ||
        memory->stack_size != script->stack_size ||
        memory->gateway_host_address !=
            (uint64_t)(uintptr_t)script->gateway ||
        memory->gateway_guest_base != GATEWAY_BASE ||
        memory->gateway_size != script->gateway_size) {
        return DB_WIN32_TRANSLATED_EXECUTION_BAD_GUEST_MEMORY;
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

    kernel32_state->last_error = step->last_error;
    scalar_state->registers[0] += UINT64_C(1);

    memset(summary, 0, sizeof(*summary));
    summary->observed_exit = published->terminal_exit;
    summary->outcome = step->outcome;
    summary->exit_code = step->exit_code;
    summary->format_version = DB_WIN32_TRANSLATED_EXECUTION_FORMAT_VERSION;
    summary->entry_executed = 1u;

    if (step->outcome ==
        (uint32_t)DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE) {
        scalar_state->rip = step->next_rip;
        summary->next_rip = step->next_rip;
    } else if (step->outcome ==
               (uint32_t)DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED) {
        scalar_state->rip = published->terminal_exit.source_rip;
        summary->next_rip = 0u;
    } else {
        return DB_WIN32_TRANSLATED_EXECUTION_BAD_TERMINAL_EXIT;
    }

    return DB_WIN32_TRANSLATED_EXECUTION_OK;
}

static scripted_executor make_script(fixture *f,
                                     const scripted_step *steps,
                                     size_t count)
{
    scripted_executor script;

    memset(&script, 0, sizeof(script));
    script.steps = steps;
    script.count = count;
    script.image = f->image;
    script.image_size = f->image_code.size;
    script.page_plan = f->page_plan;
    script.stack = f->stack;
    script.stack_size = sizeof(f->stack);
    script.gateway = f->gateway;
    script.gateway_size = f->demand.gateway_buffer_size;
    return script;
}

static int test_image_gateway_progression(void)
{
    fixture f;
    db_win32_kernel32_state kernel32;
    db_x64_scalar_state scalar;
    db_win32_translated_demand_run_summary summary;
    const scripted_step steps[] = {
        {
            IMAGE_BASE,
            GATEWAY_BASE,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
            0u,
            41u
        },
        {
            GATEWAY_BASE,
            0u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED,
            23u,
            42u
        }
    };
    scripted_executor script;

    CHECK(fixture_init(&f, 4u));
    script = make_script(&f, steps, 2u);
    kernel_init(&kernel32);
    scalar_init(&scalar);
    memset(&summary, 0xa5, sizeof(summary));

    CHECK(db_win32_translated_demand_run_with_executor(
              &f.demand,
              &kernel32,
              &scalar,
              &summary,
              scripted_execute,
              &script) == DB_WIN32_TRANSLATED_DEMAND_RUN_OK);
    CHECK(script.index == 2u);
    CHECK(summary.stop_reason ==
          (uint32_t)DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TERMINATED);
    CHECK(summary.guest_memory_bound == 1u);
    CHECK(summary.fill_count == 2u);
    CHECK(summary.published_fill_count == 2u);
    CHECK(summary.cache_hit_fill_count == 0u);
    CHECK(summary.image_fill_count == 1u);
    CHECK(summary.gateway_fill_count == 1u);
    CHECK(summary.last_fill_code_region ==
          (uint32_t)DB_WIN32_TRANSLATED_DEMAND_CODE_GATEWAY);
    CHECK(summary.completed_blocks == 2u);
    CHECK(summary.exit_code == 23u);
    CHECK(f.store.cache.count == 2u);
    CHECK(f.base_context.call_count == 2u);
    CHECK(f.platform_context.map_count == 2u);
    CHECK(kernel32.last_error == 42u);
    CHECK(db_win32_translated_demand_code_region_name(
              DB_WIN32_TRANSLATED_DEMAND_CODE_GATEWAY)[0] == 'g');
    CHECK(fixture_dispose(&f));
    return 1;
}

static int test_unmapped_second_region_rolls_back_architecture(void)
{
    fixture f;
    db_win32_kernel32_state kernel32;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar;
    db_x64_scalar_state scalar_before;
    db_win32_translated_demand_run_summary summary;
    db_win32_translated_demand_run_summary summary_before;
    const scripted_step steps[] = {
        {
            IMAGE_BASE,
            UNMAPPED_BASE,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
            0u,
            99u
        }
    };
    scripted_executor script;

    CHECK(fixture_init(&f, 4u));
    script = make_script(&f, steps, 1u);
    kernel_init(&kernel32);
    scalar_init(&scalar);
    memset(&summary, 0x5a, sizeof(summary));
    kernel_before = kernel32;
    scalar_before = scalar;
    summary_before = summary;

    CHECK(db_win32_translated_demand_run_with_executor(
              &f.demand,
              &kernel32,
              &scalar,
              &summary,
              scripted_execute,
              &script) == DB_WIN32_TRANSLATED_DEMAND_RUN_CODE_NOT_MAPPED);
    CHECK(script.index == 1u);
    CHECK(same(&kernel32, &kernel_before, sizeof(kernel32)));
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    CHECK(f.store.cache.count == 1u);
    CHECK(f.base_context.call_count == 1u);
    CHECK(f.platform_context.map_count == 1u);
    CHECK(fixture_dispose(&f));
    return 1;
}

static int test_bad_guest_memory_rejected_before_execution(void)
{
    fixture f;
    db_win32_kernel32_state kernel32;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar;
    db_x64_scalar_state scalar_before;
    db_win32_translated_demand_run_summary summary;
    db_win32_translated_demand_run_summary summary_before;
    const scripted_step steps[] = {
        {
            IMAGE_BASE,
            0u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED,
            1u,
            1u
        }
    };
    scripted_executor script;

    CHECK(fixture_init(&f, 2u));
    f.demand.image_page_count = 2u;
    f.page_plan[1] = DB_MAP_PAGE_RX;
    script = make_script(&f, steps, 1u);
    kernel_init(&kernel32);
    scalar_init(&scalar);
    memset(&summary, 0xc3, sizeof(summary));
    kernel_before = kernel32;
    scalar_before = scalar;
    summary_before = summary;

    CHECK(db_win32_translated_demand_run_with_executor(
              &f.demand,
              &kernel32,
              &scalar,
              &summary,
              scripted_execute,
              &script) == DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_GUEST_MEMORY);
    CHECK(script.index == 0u);
    CHECK(same(&kernel32, &kernel_before, sizeof(kernel32)));
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    CHECK(f.store.cache.count == 0u);
    CHECK(f.base_context.call_count == 0u);
    CHECK(f.platform_context.map_count == 0u);
    CHECK(fixture_dispose(&f));
    return 1;
}

int main(void)
{
    CHECK(test_image_gateway_progression());
    CHECK(test_unmapped_second_region_rolls_back_architecture());
    CHECK(test_bad_guest_memory_rejected_before_execution());

    printf("WIN32 DEMAND GUEST MEMORY CHECK: PASS\n");
    printf("DEMAND GUEST MEMORY DESCRIPTOR PROPAGATION: PASS\n");
    printf("IMAGE GATEWAY TWO REGION TRANSLATION: PASS\n");
    printf("UNMAPPED CODE REGION ARCH ROLLBACK: PASS\n");
    printf("VALID CACHE PUBLICATION SURVIVES LATER MISS: PASS\n");
    printf("BAD GUEST MEMORY PREEXECUTION REJECTION: PASS\n");
    printf("OLD EXECUTION STEP API COMPATIBILITY: STATIC CONTRACT\n");
    printf("NATIVE ARM64 PE EXECUTION: NOT RUN\n");
    printf("HOST RUN CLI: NOT IMPLEMENTED\n");
    printf("PRECISE X86 PAGE FAULT MODEL: NOT IMPLEMENTED\n");
    printf("DBLYATEM IDENTICAL PE EXECUTION: NOT RUN\n");
    printf("WINLATOR COMPARISON: NOT RUN\n");
    return 0;
}
