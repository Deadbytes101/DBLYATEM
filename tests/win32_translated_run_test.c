#include "dblyat/ir.h"
#include "dblyat/win32_translated_run.h"
#include "win32_translated_run_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_GENERATION UINT64_C(7)
#define TEST_TRANSLATION_KEY UINT64_C(0x11223344)

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("WIN32 TRANSLATED RUN TEST: FAIL line=%d\n", __LINE__); \
            return 0;                                                      \
        }                                                                  \
    } while (0)

typedef struct fake_step {
    uint64_t expected_rip;
    uint64_t next_rip;
    uint64_t rax_value;
    uint32_t last_error;
    uint32_t outcome;
    uint32_t trap;
    uint32_t exit_code;
    uint32_t malformed_summary;
    db_win32_translated_execution_status status;
} fake_step;

typedef struct fake_script {
    const fake_step *steps;
    size_t count;
    size_t index;
} fake_script;

static int same(const void *left, const void *right, size_t size)
{
    return memcmp(left, right, size) == 0;
}

static void fill(void *data, size_t size, uint8_t value)
{
    memset(data, value, size);
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

static db_win32_translated_run_config make_config(
    const db_arm64_block_cache *cache,
    uint32_t budget)
{
    db_win32_translated_run_config config;

    memset(&config, 0, sizeof(config));
    config.cache = cache;
    config.source_generation = TEST_GENERATION;
    config.translation_key = TEST_TRANSLATION_KEY;
    config.block_budget = budget;
    config.format_version = DB_WIN32_TRANSLATED_RUN_FORMAT_VERSION;
    return config;
}

static db_arm64_published_block make_direct_block(
    uint64_t rip,
    uint64_t target,
    uint64_t mapping)
{
    db_arm64_published_block block;

    memset(&block, 0, sizeof(block));
    block.guest_source_rip = rip;
    block.guest_end_rip = rip + UINT64_C(16);
    block.guest_fallthrough_rip = rip + UINT64_C(16);
    block.source_generation = TEST_GENERATION;
    block.translation_key = TEST_TRANSLATION_KEY;
    block.host_mapping_address = mapping;
    block.host_entry_address = mapping + UINT64_C(4);
    block.host_allocation_end = mapping + UINT64_C(4095);
    block.host_allocation_size = UINT64_C(4096);
    block.arm64_image_byte_count = UINT64_C(16);
    block.guest_byte_count = 16u;
    block.guest_instruction_count = 4u;
    block.arm64_word_count = 4u;
    block.block_stop_reason = (uint32_t)DB_X64_BLOCK_STOP_TERMINATOR;
    block.format_version = DB_ARM64_PUBLISHED_BLOCK_FORMAT_VERSION;

    if (db_translated_terminal_exit_plan_direct(
            rip + UINT64_C(12),
            target,
            &block.terminal_exit) != DB_TRANSLATED_TERMINAL_EXIT_OK) {
        memset(&block, 0, sizeof(block));
    }

    return block;
}

static db_arm64_published_block make_trap_block(
    uint64_t rip,
    db_ir_trap trap,
    uint64_t mapping)
{
    db_arm64_published_block block =
        make_direct_block(rip, rip + UINT64_C(16), mapping);

    if (db_translated_terminal_exit_plan_trap(
            rip + UINT64_C(12),
            (uint32_t)trap,
            &block.terminal_exit) != DB_TRANSLATED_TERMINAL_EXIT_OK) {
        memset(&block, 0, sizeof(block));
    }

    return block;
}

static int cache_insert(db_arm64_block_cache *cache,
                        const db_arm64_published_block *block)
{
    db_arm64_block_cache_result result;

    return db_arm64_block_cache_insert(cache, block, &result) ==
           DB_ARM64_BLOCK_CACHE_OK;
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
        published->source_generation != config->source_generation ||
        published->translation_key != config->translation_key) {
        return DB_WIN32_TRANSLATED_EXECUTION_BAD_PUBLISHED_BLOCK;
    }

    scalar_state->registers[0] = step->rax_value;
    kernel32_state->last_error = step->last_error;

    if (step->status != DB_WIN32_TRANSLATED_EXECUTION_OK) {
        scalar_state->rip = UINT64_C(0x77770000);
        fill(summary, sizeof(*summary), UINT8_C(0x5a));
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

    if (step->malformed_summary != 0u) {
        summary->entry_executed = 0u;
    }

    return DB_WIN32_TRANSLATED_EXECUTION_OK;
}

static int test_hit_hit_termination(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[8];
    db_arm64_published_block first;
    db_arm64_published_block second;
    db_win32_kernel32_state kernel32;
    db_x64_scalar_state scalar;
    db_win32_translated_run_summary summary;
    db_win32_translated_run_config config;
    const fake_step steps[] = {
        {
            UINT64_C(0x1000), UINT64_C(0x2000), UINT64_C(11), 1u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
            0u, 0u, 0u, DB_WIN32_TRANSLATED_EXECUTION_OK
        },
        {
            UINT64_C(0x2000), 0u, UINT64_C(22), 2u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED,
            0u, 37u, 0u, DB_WIN32_TRANSLATED_EXECUTION_OK
        }
    };
    fake_script script = {steps, 2u, 0u};

    CHECK(db_arm64_block_cache_init(&cache, slots, 8u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    first = make_direct_block(UINT64_C(0x1000), UINT64_C(0x2000),
                              UINT64_C(0x100000));
    second = make_trap_block(UINT64_C(0x2000),
                             DB_IR_TRAP_INVALID_OPCODE,
                             UINT64_C(0x200000));
    CHECK(cache_insert(&cache, &first));
    CHECK(cache_insert(&cache, &second));

    kernel_init(&kernel32);
    scalar_init(&scalar, UINT64_C(0x1000));
    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    config = make_config(&cache, 8u);

    CHECK(db_win32_translated_run_with_executor(
              &config, &kernel32, &scalar, &summary,
              fake_executor, &script) == DB_WIN32_TRANSLATED_RUN_OK);
    CHECK(script.index == 2u);
    CHECK(summary.stop_reason ==
          (uint32_t)DB_WIN32_TRANSLATED_RUN_STOP_GUEST_TERMINATED);
    CHECK(summary.lookup_count == 2u);
    CHECK(summary.completed_blocks == 2u);
    CHECK(summary.exit_code == 37u);
    CHECK(summary.final_rip == second.terminal_exit.source_rip);
    CHECK(summary.last_cache_status == (uint32_t)DB_ARM64_BLOCK_CACHE_OK);
    CHECK(summary.last_lookup.found == 1u);
    CHECK(summary.last_execution.outcome ==
          DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TERMINATED);
    CHECK(summary.format_version == DB_WIN32_TRANSLATED_RUN_FORMAT_VERSION);
    CHECK(scalar.rip == second.terminal_exit.source_rip);
    CHECK(scalar.registers[0] == UINT64_C(22));
    CHECK(kernel32.last_error == 2u);
    return 1;
}

static int test_hit_then_miss(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_published_block first;
    db_win32_kernel32_state kernel32;
    db_x64_scalar_state scalar;
    db_win32_translated_run_summary summary;
    db_win32_translated_run_config config;
    const fake_step steps[] = {
        {
            UINT64_C(0x3000), UINT64_C(0x4000), UINT64_C(33), 3u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
            0u, 0u, 0u, DB_WIN32_TRANSLATED_EXECUTION_OK
        }
    };
    fake_script script = {steps, 1u, 0u};

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    first = make_direct_block(UINT64_C(0x3000), UINT64_C(0x4000),
                              UINT64_C(0x300000));
    CHECK(cache_insert(&cache, &first));

    kernel_init(&kernel32);
    scalar_init(&scalar, UINT64_C(0x3000));
    memset(&summary, 0, sizeof(summary));
    config = make_config(&cache, 4u);

    CHECK(db_win32_translated_run_with_executor(
              &config, &kernel32, &scalar, &summary,
              fake_executor, &script) == DB_WIN32_TRANSLATED_RUN_OK);
    CHECK(script.index == 1u);
    CHECK(summary.stop_reason ==
          (uint32_t)DB_WIN32_TRANSLATED_RUN_STOP_CACHE_MISS);
    CHECK(summary.lookup_count == 2u);
    CHECK(summary.completed_blocks == 1u);
    CHECK(summary.final_rip == UINT64_C(0x4000));
    CHECK(summary.last_cache_status ==
          (uint32_t)DB_ARM64_BLOCK_CACHE_NOT_FOUND);
    CHECK(scalar.rip == UINT64_C(0x4000));
    CHECK(scalar.registers[0] == UINT64_C(33));
    CHECK(kernel32.last_error == 3u);
    return 1;
}

static int test_guest_trap(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_published_block block;
    db_win32_kernel32_state kernel32;
    db_x64_scalar_state scalar;
    db_win32_translated_run_summary summary;
    db_win32_translated_run_config config;
    const fake_step steps[] = {
        {
            UINT64_C(0x5000), 0u, UINT64_C(44), 4u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP,
            DB_IR_TRAP_BREAKPOINT, 0u, 0u,
            DB_WIN32_TRANSLATED_EXECUTION_OK
        }
    };
    fake_script script = {steps, 1u, 0u};

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    block = make_trap_block(UINT64_C(0x5000), DB_IR_TRAP_BREAKPOINT,
                            UINT64_C(0x400000));
    CHECK(cache_insert(&cache, &block));

    kernel_init(&kernel32);
    scalar_init(&scalar, UINT64_C(0x5000));
    memset(&summary, 0, sizeof(summary));
    config = make_config(&cache, 4u);

    CHECK(db_win32_translated_run_with_executor(
              &config, &kernel32, &scalar, &summary,
              fake_executor, &script) == DB_WIN32_TRANSLATED_RUN_OK);
    CHECK(summary.stop_reason ==
          (uint32_t)DB_WIN32_TRANSLATED_RUN_STOP_GUEST_TRAP);
    CHECK(summary.completed_blocks == 1u);
    CHECK(summary.lookup_count == 1u);
    CHECK(summary.trap == (uint32_t)DB_IR_TRAP_BREAKPOINT);
    CHECK(summary.final_rip == block.terminal_exit.source_rip);
    CHECK(scalar.rip == block.terminal_exit.source_rip);
    CHECK(kernel32.last_error == 4u);
    return 1;
}

static int test_budget_self_loop(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_published_block block;
    db_win32_kernel32_state kernel32;
    db_x64_scalar_state scalar;
    db_win32_translated_run_summary summary;
    db_win32_translated_run_config config;
    const fake_step steps[] = {
        {
            UINT64_C(0x6000), UINT64_C(0x6000), UINT64_C(1), 1u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
            0u, 0u, 0u, DB_WIN32_TRANSLATED_EXECUTION_OK
        },
        {
            UINT64_C(0x6000), UINT64_C(0x6000), UINT64_C(2), 2u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
            0u, 0u, 0u, DB_WIN32_TRANSLATED_EXECUTION_OK
        },
        {
            UINT64_C(0x6000), UINT64_C(0x6000), UINT64_C(3), 3u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
            0u, 0u, 0u, DB_WIN32_TRANSLATED_EXECUTION_OK
        }
    };
    fake_script script = {steps, 3u, 0u};

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    block = make_direct_block(UINT64_C(0x6000), UINT64_C(0x6000),
                              UINT64_C(0x500000));
    CHECK(cache_insert(&cache, &block));

    kernel_init(&kernel32);
    scalar_init(&scalar, UINT64_C(0x6000));
    memset(&summary, 0, sizeof(summary));
    config = make_config(&cache, 3u);

    CHECK(db_win32_translated_run_with_executor(
              &config, &kernel32, &scalar, &summary,
              fake_executor, &script) == DB_WIN32_TRANSLATED_RUN_OK);
    CHECK(script.index == 3u);
    CHECK(summary.stop_reason ==
          (uint32_t)DB_WIN32_TRANSLATED_RUN_STOP_BLOCK_BUDGET);
    CHECK(summary.lookup_count == 3u);
    CHECK(summary.completed_blocks == 3u);
    CHECK(summary.final_rip == UINT64_C(0x6000));
    CHECK(scalar.rip == UINT64_C(0x6000));
    CHECK(scalar.registers[0] == UINT64_C(3));
    CHECK(kernel32.last_error == 3u);
    return 1;
}

static int test_whole_run_failure_transactionality(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_published_block first;
    db_arm64_published_block second;
    db_win32_kernel32_state kernel32;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar;
    db_x64_scalar_state scalar_before;
    db_win32_translated_run_summary summary;
    db_win32_translated_run_summary summary_before;
    db_win32_translated_run_config config;
    const fake_step steps[] = {
        {
            UINT64_C(0x7000), UINT64_C(0x8000), UINT64_C(55), 5u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
            0u, 0u, 0u, DB_WIN32_TRANSLATED_EXECUTION_OK
        },
        {
            UINT64_C(0x8000), 0u, UINT64_C(66), 6u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_NONE,
            0u, 0u, 0u, DB_WIN32_TRANSLATED_EXECUTION_ENTRY_FAILED
        }
    };
    fake_script script = {steps, 2u, 0u};

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    first = make_direct_block(UINT64_C(0x7000), UINT64_C(0x8000),
                              UINT64_C(0x600000));
    second = make_direct_block(UINT64_C(0x8000), UINT64_C(0x9000),
                               UINT64_C(0x700000));
    CHECK(cache_insert(&cache, &first));
    CHECK(cache_insert(&cache, &second));

    kernel_init(&kernel32);
    scalar_init(&scalar, UINT64_C(0x7000));
    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    kernel_before = kernel32;
    scalar_before = scalar;
    summary_before = summary;
    config = make_config(&cache, 4u);

    CHECK(db_win32_translated_run_with_executor(
              &config, &kernel32, &scalar, &summary,
              fake_executor, &script) ==
          DB_WIN32_TRANSLATED_RUN_EXECUTION_FAILED);
    CHECK(script.index == 2u);
    CHECK(same(&kernel32, &kernel_before, sizeof(kernel32)));
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));

    cache.format_version = 0u;
    script.index = 0u;
    CHECK(db_win32_translated_run_with_executor(
              &config, &kernel32, &scalar, &summary,
              fake_executor, &script) == DB_WIN32_TRANSLATED_RUN_CACHE_FAILED);
    CHECK(script.index == 0u);
    CHECK(same(&kernel32, &kernel_before, sizeof(kernel32)));
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}

static int test_bad_outcome_transactionality(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[2];
    db_arm64_published_block block;
    db_win32_kernel32_state kernel32;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar;
    db_x64_scalar_state scalar_before;
    db_win32_translated_run_summary summary;
    db_win32_translated_run_summary summary_before;
    db_win32_translated_run_config config;
    const fake_step steps[] = {
        {
            UINT64_C(0x9000), UINT64_C(0xa000), UINT64_C(77), 7u,
            DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_CONTINUE,
            0u, 0u, 1u, DB_WIN32_TRANSLATED_EXECUTION_OK
        }
    };
    fake_script script = {steps, 1u, 0u};

    CHECK(db_arm64_block_cache_init(&cache, slots, 2u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    block = make_direct_block(UINT64_C(0x9000), UINT64_C(0xa000),
                              UINT64_C(0x800000));
    CHECK(cache_insert(&cache, &block));

    kernel_init(&kernel32);
    scalar_init(&scalar, UINT64_C(0x9000));
    fill(&summary, sizeof(summary), UINT8_C(0x3c));
    kernel_before = kernel32;
    scalar_before = scalar;
    summary_before = summary;
    config = make_config(&cache, 2u);

    CHECK(db_win32_translated_run_with_executor(
              &config, &kernel32, &scalar, &summary,
              fake_executor, &script) ==
          DB_WIN32_TRANSLATED_RUN_BAD_EXECUTION_OUTCOME);
    CHECK(same(&kernel32, &kernel_before, sizeof(kernel32)));
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
}

static int test_validation_transactionality(void)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[2];
    db_win32_kernel32_state kernel32;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar;
    db_x64_scalar_state scalar_before;
    db_win32_translated_run_summary summary;
    db_win32_translated_run_summary summary_before;
    db_win32_translated_run_config config;
    fake_script empty_script = {NULL, 0u, 0u};

    CHECK(db_arm64_block_cache_init(&cache, slots, 2u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    kernel_init(&kernel32);
    scalar_init(&scalar, UINT64_C(0xb000));
    fill(&summary, sizeof(summary), UINT8_C(0xc3));
    kernel_before = kernel32;
    scalar_before = scalar;
    summary_before = summary;

    config = make_config(&cache, 0u);
    CHECK(db_win32_translated_run_with_executor(
              &config, &kernel32, &scalar, &summary,
              fake_executor, &empty_script) ==
          DB_WIN32_TRANSLATED_RUN_BAD_BLOCK_BUDGET);
    CHECK(same(&kernel32, &kernel_before, sizeof(kernel32)));
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));

    config = make_config(&cache, 1u);
    config.format_version = 0u;
    CHECK(db_win32_translated_run_with_executor(
              &config, &kernel32, &scalar, &summary,
              fake_executor, &empty_script) ==
          DB_WIN32_TRANSLATED_RUN_BAD_CONFIG);

    config = make_config(&cache, 1u);
    scalar.rflags = 0u;
    CHECK(db_win32_translated_run_with_executor(
              &config, &kernel32, &scalar, &summary,
              fake_executor, &empty_script) ==
          DB_WIN32_TRANSLATED_RUN_BAD_SCALAR_STATE);
    scalar = scalar_before;

    CHECK(db_win32_translated_run_with_executor(
              NULL, &kernel32, &scalar, &summary,
              fake_executor, &empty_script) ==
          DB_WIN32_TRANSLATED_RUN_NULL_ARGUMENT);
    CHECK(db_win32_translated_run_with_executor(
              &config, &kernel32, &scalar, &summary,
              NULL, &empty_script) ==
          DB_WIN32_TRANSLATED_RUN_NULL_ARGUMENT);
    return 1;
}

static int test_public_wrapper_nonaarch64_rollback(void)
{
#if defined(__aarch64__)
    return 1;
#else
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[2];
    db_arm64_published_block block;
    db_win32_kernel32_state kernel32;
    db_win32_kernel32_state kernel_before;
    db_x64_scalar_state scalar;
    db_x64_scalar_state scalar_before;
    db_win32_translated_run_summary summary;
    db_win32_translated_run_summary summary_before;
    db_win32_translated_run_config config;

    CHECK(db_arm64_block_cache_init(&cache, slots, 2u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    block = make_direct_block(UINT64_C(0xc000), UINT64_C(0xd000),
                              UINT64_C(0x900000));
    CHECK(cache_insert(&cache, &block));
    kernel_init(&kernel32);
    scalar_init(&scalar, UINT64_C(0xc000));
    fill(&summary, sizeof(summary), UINT8_C(0x96));
    kernel_before = kernel32;
    scalar_before = scalar;
    summary_before = summary;
    config = make_config(&cache, 1u);

    CHECK(db_win32_translated_run(
              &config, &kernel32, &scalar, &summary) ==
          DB_WIN32_TRANSLATED_RUN_EXECUTION_FAILED);
    CHECK(same(&kernel32, &kernel_before, sizeof(kernel32)));
    CHECK(same(&scalar, &scalar_before, sizeof(scalar)));
    CHECK(same(&summary, &summary_before, sizeof(summary)));
    return 1;
#endif
}

int main(void)
{
    CHECK(test_hit_hit_termination());
    printf("CACHE HIT CONTINUE TERMINATE: PASS\n");

    CHECK(test_hit_then_miss());
    printf("CACHE HIT MISS COMMIT: PASS\n");

    CHECK(test_guest_trap());
    printf("GUEST TRAP PROPAGATION: PASS\n");

    CHECK(test_budget_self_loop());
    printf("BLOCK BUDGET EXHAUSTION: PASS\n");

    CHECK(test_whole_run_failure_transactionality());
    CHECK(test_bad_outcome_transactionality());
    CHECK(test_validation_transactionality());
    printf("WHOLE RUN FAILURE TRANSACTIONALITY: PASS\n");

    CHECK(test_public_wrapper_nonaarch64_rollback());
#if defined(__aarch64__)
    printf("PUBLIC REAL EXECUTOR NONAARCH64 ROLLBACK: NOT RUN\n");
#else
    printf("PUBLIC REAL EXECUTOR NONAARCH64 ROLLBACK: PASS\n");
#endif

    printf("NATIVE MULTI BLOCK ARM64 EXECUTION: NOT RUN\n");
    printf("CACHE MISS TRANSLATION: NOT IMPLEMENTED\n");
    printf("WIN32 TRANSLATED RUN TEST: PASS\n");
    return 0;
}
