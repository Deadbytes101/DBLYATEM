#include "dblyat/arm64_translation_store.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("ARM64 TRANSLATION STORE TEST: FAIL line=%d\n",          \
                   __LINE__);                                                \
            return 0;                                                        \
        }                                                                    \
    } while (0)

#define PAGE_SIZE UINT64_C(4096)
#define RECORD_CAPACITY 8u
#define IR_CAPACITY 8u
#define SELECT_CAPACITY 16u
#define PLAN_CAPACITY 32u
#define SOURCE_WORD_CAPACITY 32u
#define RELOCATION_CAPACITY 16u
#define OUTPUT_WORD_CAPACITY 64u
#define RESOLUTION_CAPACITY 16u
#define IMAGE_CAPACITY (OUTPUT_WORD_CAPACITY * sizeof(uint32_t))

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
    uint32_t fail_unmap_count;
};

struct code_base_context {
    uint8_t *arena;
    size_t next_page;
    uint32_t call_count;
    uint8_t force_misaligned;
};

static int same_bytes(const void *left, const void *right, size_t size)
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

    return value >= begin &&
           size <= context->arena_size &&
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
    if (!within_arena(context, mapping, allocation_size)) {
        return -1;
    }
    if (context->fail_unmap_count != 0u) {
        --context->fail_unmap_count;
        return -1;
    }
    return 0;
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

static db_arm64_translation_store_config make_config(
    const db_x64_code_view *code,
    const db_x64_arm64_helper_targets *targets,
    const db_linux_arm64_wx_platform *platform,
    struct code_base_context *base_context)
{
    db_arm64_translation_store_config config;

    memset(&config, 0, sizeof(config));
    config.code = code;
    config.helper_targets = targets;
    config.wx_platform = platform;
    config.code_base_context = base_context;
    config.reserve_code_base = reserve_code_base;
    config.source_generation = UINT64_C(7);
    config.translation_key = UINT64_C(0x11223344);
    config.page_size = PAGE_SIZE;
    config.instruction_budget = 4u;
    config.byte_budget = 32u;
    return config;
}

static int test_publish_hit_dispose(void)
{
    static const uint8_t code_bytes[4] = {0x0fu, 0x0bu, 0x0fu, 0x0bu};
    db_arm64_translation_store store;
    db_arm64_block_cache_slot slots[4];
    db_linux_arm64_wx_handle handles[4];
    uint8_t owned[4];
    struct fixture_workspace storage;
    db_arm64_translation_workspace workspace = make_workspace(&storage);
    struct fake_platform_context platform_context;
    struct code_base_context base_context;
    db_linux_arm64_wx_platform platform;
    db_x64_arm64_helper_targets targets;
    db_x64_code_view code;
    db_arm64_translation_store_config config;
    db_arm64_published_block first;
    db_arm64_published_block second;
    db_arm64_translation_store_summary first_summary;
    db_arm64_translation_store_summary second_summary;
    size_t slot_index;

    memset(fake_arena, 0, sizeof(fake_arena));
    memset(&platform_context, 0, sizeof(platform_context));
    platform_context.arena = fake_arena;
    platform_context.arena_size = sizeof(fake_arena);
    memset(&base_context, 0, sizeof(base_context));
    base_context.arena = fake_arena;
    platform = make_platform(&platform_context);
    targets = make_targets((uint64_t)(uintptr_t)(fake_arena + 2048u));
    memset(&code, 0, sizeof(code));
    code.bytes = code_bytes;
    code.size = sizeof(code_bytes);
    code.guest_base = UINT64_C(0x100000);
    config = make_config(&code, &targets, &platform, &base_context);

    memset(&store, 0xa5, sizeof(store));
    memset(slots, 0xa5, sizeof(slots));
    memset(handles, 0xa5, sizeof(handles));
    memset(owned, 0xa5, sizeof(owned));
    CHECK(db_arm64_translation_store_init(
              &store, slots, handles, owned, 4u) ==
          DB_ARM64_TRANSLATION_STORE_OK);
    CHECK(store.state == DB_ARM64_TRANSLATION_STORE_READY);
    CHECK(store.cache.count == 0u);

    memset(&first, 0xa5, sizeof(first));
    memset(&first_summary, 0xa5, sizeof(first_summary));
    CHECK(db_arm64_translation_store_fill(
              &store,
              &config,
              &workspace,
              UINT64_C(0x100000),
              &first,
              &first_summary) == DB_ARM64_TRANSLATION_STORE_OK);
    CHECK(first_summary.outcome ==
          DB_ARM64_TRANSLATION_STORE_OUTCOME_PUBLISHED);
    CHECK(first_summary.requested_code_base ==
          (uint64_t)(uintptr_t)fake_arena);
    CHECK(first.terminal_exit.kind == DB_TRANSLATED_TERMINAL_EXIT_TRAP);
    CHECK(first.terminal_exit.trap == DB_IR_TRAP_INVALID_OPCODE);
    CHECK(store.cache.count == 1u);
    CHECK(base_context.call_count == 1u);
    CHECK(platform_context.map_count == 1u);
    CHECK(platform_context.copy_count == 1u);
    CHECK(platform_context.protect_count == 1u);
    CHECK(platform_context.sync_count == 1u);
    CHECK(platform_context.unmap_count == 0u);
    slot_index = (size_t)first_summary.cache_result.slot_index;
    CHECK(slot_index < 4u);
    CHECK(store.owned[slot_index] == 1u);
    CHECK(store.handles[slot_index].state == DB_LINUX_ARM64_WX_PUBLISHED);

    memset(&second, 0xa5, sizeof(second));
    memset(&second_summary, 0xa5, sizeof(second_summary));
    CHECK(db_arm64_translation_store_fill(
              &store,
              &config,
              &workspace,
              UINT64_C(0x100000),
              &second,
              &second_summary) == DB_ARM64_TRANSLATION_STORE_OK);
    CHECK(second_summary.outcome ==
          DB_ARM64_TRANSLATION_STORE_OUTCOME_HIT);
    CHECK(same_bytes(&first, &second, sizeof(first)));
    CHECK(base_context.call_count == 1u);
    CHECK(platform_context.map_count == 1u);

    CHECK(db_arm64_translation_store_dispose(&store, &platform) ==
          DB_ARM64_TRANSLATION_STORE_OK);
    CHECK(platform_context.unmap_count == 1u);
    CHECK(store.state == DB_ARM64_TRANSLATION_STORE_EMPTY);
    CHECK(store.format_version == 0u);
    return 1;
}

static int test_prepublish_failure(void)
{
    static const uint8_t code_bytes[1] = {0x90u};
    db_arm64_translation_store store;
    db_arm64_block_cache_slot slots[2];
    db_linux_arm64_wx_handle handles[2];
    uint8_t owned[2];
    struct fixture_workspace storage;
    db_arm64_translation_workspace workspace = make_workspace(&storage);
    struct fake_platform_context platform_context;
    struct code_base_context base_context;
    db_linux_arm64_wx_platform platform;
    db_x64_arm64_helper_targets targets;
    db_x64_code_view code;
    db_arm64_translation_store_config config;
    db_arm64_published_block output;
    db_arm64_published_block output_before;
    db_arm64_translation_store_summary summary;
    db_arm64_translation_store_summary summary_before;

    memset(&platform_context, 0, sizeof(platform_context));
    platform_context.arena = fake_arena;
    platform_context.arena_size = sizeof(fake_arena);
    memset(&base_context, 0, sizeof(base_context));
    base_context.arena = fake_arena;
    platform = make_platform(&platform_context);
    targets = make_targets((uint64_t)(uintptr_t)(fake_arena + 2048u));
    memset(&code, 0, sizeof(code));
    code.bytes = code_bytes;
    code.size = sizeof(code_bytes);
    code.guest_base = UINT64_C(0x200000);
    config = make_config(&code, &targets, &platform, &base_context);
    config.instruction_budget = 1u;
    config.byte_budget = 1u;

    CHECK(db_arm64_translation_store_init(
              &store, slots, handles, owned, 2u) ==
          DB_ARM64_TRANSLATION_STORE_OK);
    memset(&output, 0xa5, sizeof(output));
    memset(&summary, 0x5a, sizeof(summary));
    output_before = output;
    summary_before = summary;

    CHECK(db_arm64_translation_store_fill(
              &store,
              &config,
              &workspace,
              UINT64_C(0x200000),
              &output,
              &summary) ==
          DB_ARM64_TRANSLATION_STORE_BLOCK_NOT_TERMINATED);
    CHECK(same_bytes(&output, &output_before, sizeof(output)));
    CHECK(same_bytes(&summary, &summary_before, sizeof(summary)));
    CHECK(store.cache.count == 0u);
    CHECK(platform_context.map_count == 0u);
    CHECK(store.state == DB_ARM64_TRANSLATION_STORE_READY);
    CHECK(db_arm64_translation_store_dispose(&store, &platform) ==
          DB_ARM64_TRANSLATION_STORE_OK);
    return 1;
}

static int test_postpublish_rollback(int poison)
{
    static const uint8_t code_bytes[4] = {0x0fu, 0x0bu, 0x0fu, 0x0bu};
    db_arm64_translation_store store;
    db_arm64_block_cache_slot slots[1];
    db_linux_arm64_wx_handle handles[1];
    uint8_t owned[1];
    struct fixture_workspace storage;
    db_arm64_translation_workspace workspace = make_workspace(&storage);
    struct fake_platform_context platform_context;
    struct code_base_context base_context;
    db_linux_arm64_wx_platform platform;
    db_x64_arm64_helper_targets targets;
    db_x64_code_view code;
    db_arm64_translation_store_config config;
    db_arm64_published_block output;
    db_arm64_translation_store_summary summary;
    db_arm64_translation_store_status status;

    memset(fake_arena, 0, sizeof(fake_arena));
    memset(&platform_context, 0, sizeof(platform_context));
    platform_context.arena = fake_arena;
    platform_context.arena_size = sizeof(fake_arena);
    memset(&base_context, 0, sizeof(base_context));
    base_context.arena = fake_arena;
    platform = make_platform(&platform_context);
    targets = make_targets((uint64_t)(uintptr_t)(fake_arena + 2048u));
    memset(&code, 0, sizeof(code));
    code.bytes = code_bytes;
    code.size = sizeof(code_bytes);
    code.guest_base = UINT64_C(0x300000);
    config = make_config(&code, &targets, &platform, &base_context);

    CHECK(db_arm64_translation_store_init(
              &store, slots, handles, owned, 1u) ==
          DB_ARM64_TRANSLATION_STORE_OK);
    CHECK(db_arm64_translation_store_fill(
              &store,
              &config,
              &workspace,
              UINT64_C(0x300000),
              &output,
              &summary) == DB_ARM64_TRANSLATION_STORE_OK);
    CHECK(store.cache.count == 1u);

    if (poison != 0) {
        platform_context.fail_unmap_count = 1u;
    }
    memset(&output, 0xa5, sizeof(output));
    memset(&summary, 0x5a, sizeof(summary));
    status = db_arm64_translation_store_fill(
        &store,
        &config,
        &workspace,
        UINT64_C(0x300002),
        &output,
        &summary);

    if (poison == 0) {
        CHECK(status == DB_ARM64_TRANSLATION_STORE_INSERT_FAILED);
        CHECK(store.state == DB_ARM64_TRANSLATION_STORE_READY);
        CHECK(store.cache.count == 1u);
        CHECK(platform_context.map_count == 2u);
        CHECK(platform_context.unmap_count == 1u);
        CHECK(db_arm64_translation_store_dispose(&store, &platform) ==
              DB_ARM64_TRANSLATION_STORE_OK);
        CHECK(platform_context.unmap_count == 2u);
    } else {
        CHECK(status == DB_ARM64_TRANSLATION_STORE_ROLLBACK_FAILED);
        CHECK(store.state == DB_ARM64_TRANSLATION_STORE_POISONED);
        CHECK(store.rollback_handle.mapping != NULL);
        CHECK(store.cache.count == 1u);
        CHECK(db_arm64_translation_store_fill(
                  &store,
                  &config,
                  &workspace,
                  UINT64_C(0x300000),
                  &output,
                  &summary) == DB_ARM64_TRANSLATION_STORE_BAD_STORE);
        platform_context.fail_unmap_count = 0u;
        CHECK(db_arm64_translation_store_dispose(&store, &platform) ==
              DB_ARM64_TRANSLATION_STORE_OK);
        CHECK(store.state == DB_ARM64_TRANSLATION_STORE_EMPTY);
        CHECK(platform_context.unmap_count == 3u);
    }
    return 1;
}

static int test_code_base_rejection(void)
{
    static const uint8_t code_bytes[2] = {0x0fu, 0x0bu};
    db_arm64_translation_store store;
    db_arm64_block_cache_slot slots[2];
    db_linux_arm64_wx_handle handles[2];
    uint8_t owned[2];
    struct fixture_workspace storage;
    db_arm64_translation_workspace workspace = make_workspace(&storage);
    struct fake_platform_context platform_context;
    struct code_base_context base_context;
    db_linux_arm64_wx_platform platform;
    db_x64_arm64_helper_targets targets;
    db_x64_code_view code;
    db_arm64_translation_store_config config;
    db_arm64_published_block output;
    db_arm64_translation_store_summary summary;

    memset(&platform_context, 0, sizeof(platform_context));
    platform_context.arena = fake_arena;
    platform_context.arena_size = sizeof(fake_arena);
    memset(&base_context, 0, sizeof(base_context));
    base_context.arena = fake_arena;
    base_context.force_misaligned = 1u;
    platform = make_platform(&platform_context);
    targets = make_targets((uint64_t)(uintptr_t)(fake_arena + 2048u));
    memset(&code, 0, sizeof(code));
    code.bytes = code_bytes;
    code.size = sizeof(code_bytes);
    code.guest_base = UINT64_C(0x400000);
    config = make_config(&code, &targets, &platform, &base_context);

    CHECK(db_arm64_translation_store_init(
              &store, slots, handles, owned, 2u) ==
          DB_ARM64_TRANSLATION_STORE_OK);
    CHECK(db_arm64_translation_store_fill(
              &store,
              &config,
              &workspace,
              UINT64_C(0x400000),
              &output,
              &summary) == DB_ARM64_TRANSLATION_STORE_CODE_BASE_FAILED);
    CHECK(store.cache.count == 0u);
    CHECK(platform_context.map_count == 0u);
    CHECK(db_arm64_translation_store_dispose(&store, &platform) ==
          DB_ARM64_TRANSLATION_STORE_OK);
    return 1;
}

int main(void)
{
    if (!test_publish_hit_dispose() ||
        !test_prepublish_failure() ||
        !test_postpublish_rollback(0) ||
        !test_postpublish_rollback(1) ||
        !test_code_base_rejection()) {
        return 1;
    }

    printf("ARM64 TRANSLATION STORE TEST: PASS\n");
    printf("TRANSLATION STORE MISS PUBLISH: PASS\n");
    printf("TRANSLATION STORE CACHE HIT REUSE: PASS\n");
    printf("TRANSLATION STORE PREPUBLISH TRANSACTIONALITY: PASS\n");
    printf("TRANSLATION STORE POSTPUBLISH ROLLBACK: PASS\n");
    printf("TRANSLATION STORE ROLLBACK POISON CLEANUP: PASS\n");
    printf("NATIVE PUBLISHED BLOCK EXECUTION: NOT RUN\n");
    return 0;
}
