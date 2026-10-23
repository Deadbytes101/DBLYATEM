#define _GNU_SOURCE
#include "dblyat/arm64_block_cache.h"
#include "dblyat/arm64_code_image.h"
#include "dblyat/arm64_published_block.h"
#include "dblyat/arm64_select.h"
#include "dblyat/linux_arm64_translation_entry.h"
#include "dblyat/linux_arm64_wx_provider.h"
#include "dblyat/translated_terminal_exit.h"
#include "dblyat/x64_arm64_encode.h"
#include "dblyat/x64_arm64_plan.h"
#include "dblyat/x64_arm64_relocate.h"
#include "dblyat/x64_block.h"
#include "dblyat/x64_ir_lower.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__aarch64__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("LINUX ARM64 TRANSLATED ENTRY TEST: FAIL line=%d\n",     \
                   __LINE__);                                                \
            return 0;                                                        \
        }                                                                    \
    } while (0)

#define RECORD_CAPACITY 8u
#define IR_CAPACITY 8u
#define SELECT_CAPACITY 16u
#define PLAN_CAPACITY 32u
#define WORD_CAPACITY 64u
#define RELOCATION_CAPACITY 16u
#define IMAGE_CAPACITY (WORD_CAPACITY * sizeof(uint32_t))

static int same_bytes(const void *left, const void *right, size_t size)
{
    return memcmp(left, right, size) == 0;
}

static int test_terminal_exit_records(void)
{
    db_translated_terminal_exit planned;
    db_translated_terminal_exit observed;

    memset(&planned, 0xa5, sizeof(planned));
    CHECK(db_translated_terminal_exit_plan_direct(
              UINT64_C(0x1000), UINT64_C(0x2000), &planned) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    CHECK(planned.kind == DB_TRANSLATED_TERMINAL_EXIT_DIRECT);
    CHECK(planned.source_rip == UINT64_C(0x1000));
    CHECK(planned.direct_target == UINT64_C(0x2000));
    CHECK(db_translated_terminal_exit_validate_planned(&planned) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    observed = planned;
    CHECK(db_translated_terminal_exit_validate_observed(&observed) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    CHECK(db_translated_terminal_exit_match(&planned, &observed) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);

    CHECK(db_translated_terminal_exit_plan_target(
              UINT64_C(0x3000), &planned) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    observed = planned;
    observed.runtime_target = UINT64_C(0x4000);
    observed.runtime_target_valid = 1u;
    CHECK(db_translated_terminal_exit_validate_observed(&observed) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    CHECK(db_translated_terminal_exit_match(&planned, &observed) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    observed.runtime_target = UINT64_C(0x0000800000000000);
    CHECK(db_translated_terminal_exit_validate_observed(&observed) ==
          DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD);

    CHECK(db_translated_terminal_exit_plan_conditional(
              UINT64_C(0x5000),
              UINT64_C(0x6000),
              UINT64_C(0x5002),
              (uint32_t)DB_IR_CONDITION_EQUAL,
              &planned) == DB_TRANSLATED_TERMINAL_EXIT_OK);
    observed = planned;
    observed.runtime_target = planned.direct_target;
    observed.runtime_value = UINT64_C(1);
    observed.runtime_target_valid = 1u;
    observed.runtime_value_valid = 1u;
    observed.condition_taken = 1u;
    observed.condition_taken_valid = 1u;
    CHECK(db_translated_terminal_exit_validate_observed(&observed) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    CHECK(db_translated_terminal_exit_match(&planned, &observed) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    observed.runtime_value = UINT64_C(2);
    CHECK(db_translated_terminal_exit_validate_observed(&observed) ==
          DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD);

    CHECK(db_translated_terminal_exit_plan_trap(
              UINT64_C(0x7000),
              (uint32_t)DB_IR_TRAP_INVALID_OPCODE,
              &planned) == DB_TRANSLATED_TERMINAL_EXIT_OK);
    observed = planned;
    CHECK(db_translated_terminal_exit_validate_observed(&observed) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    CHECK(db_translated_terminal_exit_match(&planned, &observed) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    observed.source_rip += UINT64_C(1);
    CHECK(db_translated_terminal_exit_match(&planned, &observed) ==
          DB_TRANSLATED_TERMINAL_EXIT_MISMATCH);

    CHECK(db_translated_terminal_exit_plan_direct(
              UINT64_C(0x0000800000000000),
              UINT64_C(0x2000),
              &planned) == DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD);
    CHECK(db_translated_terminal_exit_plan_trap(
              UINT64_C(0x7000), UINT32_C(99), &planned) ==
          DB_TRANSLATED_TERMINAL_EXIT_BAD_RECORD);
    CHECK(db_translated_terminal_exit_kind_name(
              DB_TRANSLATED_TERMINAL_EXIT_DIRECT)[0] == 'd');
    CHECK(db_translated_terminal_exit_status_name(
              DB_TRANSLATED_TERMINAL_EXIT_MISMATCH)[0] == 't');
    return 1;
}

static int test_validation(void)
{
    db_x64_scalar_state state;
    db_linux_arm64_translation_context context;
    db_linux_arm64_translation_context context_before;
    db_linux_arm64_translation_entry_audit audit;
    db_linux_arm64_translation_entry_audit audit_before;
    db_translated_terminal_exit planned;
    db_translated_terminal_exit bad_planned;

    memset(&state, 0, sizeof(state));
    memset(&context, 0xa5, sizeof(context));
    memset(&audit, 0x5a, sizeof(audit));

    CHECK(db_linux_arm64_translation_context_init(NULL) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_NULL_ARGUMENT);
    CHECK(db_linux_arm64_translation_context_init(&context) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
    CHECK(context.format_version ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_FORMAT_VERSION);
    CHECK(context.terminal_return_count == 0u);
    CHECK(same_bytes(&context.planned_exit,
                     &(db_translated_terminal_exit){0},
                     sizeof(context.planned_exit)));
    CHECK(same_bytes(&context.observed_exit,
                     &(db_translated_terminal_exit){0},
                     sizeof(context.observed_exit)));
    CHECK(same_bytes(context.reserved,
                     (uint8_t[4]){0},
                     sizeof(context.reserved)));

    CHECK(db_translated_terminal_exit_plan_direct(
              UINT64_C(0x1000), UINT64_C(0x2000), &planned) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    context_before = context;
    CHECK(db_linux_arm64_translation_context_bind_terminal(
              NULL, &planned) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_NULL_ARGUMENT);
    CHECK(db_linux_arm64_translation_context_bind_terminal(
              &context, NULL) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_NULL_ARGUMENT);
    CHECK(db_linux_arm64_translation_context_bind_terminal(
              &context,
              (const db_translated_terminal_exit *)(const void *)&context) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BUFFER_OVERLAP);
    CHECK(same_bytes(&context, &context_before, sizeof(context)));

    bad_planned = planned;
    bad_planned.format_version += 1u;
    CHECK(db_linux_arm64_translation_context_bind_terminal(
              &context, &bad_planned) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_TERMINAL_EXIT);
    CHECK(same_bytes(&context, &context_before, sizeof(context)));

    CHECK(db_linux_arm64_translation_context_bind_terminal(
              &context, &planned) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
    CHECK(same_bytes(&context.planned_exit,
                     &planned,
                     sizeof(planned)));
    CHECK(same_bytes(&context.observed_exit,
                     &(db_translated_terminal_exit){0},
                     sizeof(context.observed_exit)));

    context_before = context;
    audit_before = audit;
    CHECK(db_linux_arm64_translation_enter(
              0u, &state, &context, &audit) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_ENTRY_ADDRESS);
    CHECK(same_bytes(&context, &context_before, sizeof(context)));
    CHECK(same_bytes(&audit, &audit_before, sizeof(audit)));

    CHECK(db_linux_arm64_translation_enter(
              UINT64_C(0x1001), &state, &context, &audit) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_ENTRY_ADDRESS);
    CHECK(same_bytes(&context, &context_before, sizeof(context)));
    CHECK(same_bytes(&audit, &audit_before, sizeof(audit)));

    CHECK(db_linux_arm64_translation_enter(
              UINT64_C(0x0001000000000000),
              &state, &context, &audit) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_ENTRY_ADDRESS);
    CHECK(same_bytes(&context, &context_before, sizeof(context)));
    CHECK(same_bytes(&audit, &audit_before, sizeof(audit)));

    context.format_version += 1u;
    CHECK(db_linux_arm64_translation_enter(
              UINT64_C(0x0000ffff00000000),
              &state, &context, &audit) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_CONTEXT);
    CHECK(same_bytes(&audit, &audit_before, sizeof(audit)));
    context = context_before;

    context.reserved[0] = 1u;
    CHECK(db_linux_arm64_translation_enter(
              UINT64_C(0x1000), &state, &context, &audit) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_CONTEXT);
    context = context_before;

    context.terminal_return_count = UINT64_MAX;
    CHECK(db_linux_arm64_translation_enter(
              UINT64_C(0x1000), &state, &context, &audit) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_CONTEXT);
    context = context_before;

    context.planned_exit.format_version += 1u;
    CHECK(db_linux_arm64_translation_enter(
              UINT64_C(0x1000), &state, &context, &audit) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_CONTEXT);
    context = context_before;

    CHECK(db_linux_arm64_translation_enter(
              UINT64_C(0x1000), NULL, &context, &audit) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_NULL_ARGUMENT);
    CHECK(db_linux_arm64_translation_enter(
              UINT64_C(0x1000), &state, NULL, &audit) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_NULL_ARGUMENT);
    CHECK(db_linux_arm64_translation_enter(
              UINT64_C(0x1000), &state, &context, NULL) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_NULL_ARGUMENT);

    CHECK(db_linux_arm64_translation_enter(
              UINT64_C(0x1000),
              &state,
              (db_linux_arm64_translation_context *)(void *)&state,
              &audit) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BUFFER_OVERLAP);
    CHECK(db_linux_arm64_translation_enter(
              UINT64_C(0x1000),
              &state,
              &context,
              (db_linux_arm64_translation_entry_audit *)(void *)&state) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BUFFER_OVERLAP);

#if !defined(__aarch64__)
    context_before = context;
    audit_before = audit;
    CHECK(db_linux_arm64_translation_enter(
              UINT64_C(0x1000), &state, &context, &audit) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_UNSUPPORTED_HOST);
    CHECK(db_linux_arm64_translation_return_helper_address() == 0u);
    CHECK(db_linux_arm64_translation_terminal_helper_address(
              DB_TRANSLATED_TERMINAL_EXIT_TRAP) == 0u);
    CHECK(same_bytes(&context, &context_before, sizeof(context)));
    CHECK(same_bytes(&audit, &audit_before, sizeof(audit)));
#endif

    CHECK(db_linux_arm64_translation_entry_status_name(
              DB_LINUX_ARM64_TRANSLATION_ENTRY_OK)[0] == 'o');
    CHECK(db_linux_arm64_translation_entry_status_name(
              (db_linux_arm64_translation_entry_status)999)[0] == 'u');
    return 1;
}

#if defined(__aarch64__)
typedef struct translated_pipeline {
    db_x64_block_record block_records[RECORD_CAPACITY];
    db_ir_instruction ir[IR_CAPACITY];
    db_arm64_select_record selections[SELECT_CAPACITY];
    db_x64_arm64_plan_record plans[PLAN_CAPACITY];
    uint32_t source_words[PLAN_CAPACITY];
    db_x64_arm64_relocation relocations[RELOCATION_CAPACITY];
    uint32_t output_words[WORD_CAPACITY];
    db_x64_arm64_resolution decisions[RELOCATION_CAPACITY];
    uint8_t image[IMAGE_CAPACITY];
    db_x64_block_summary block_summary;
    db_x64_ir_lower_summary ir_summary;
    db_arm64_select_summary select_summary;
    db_x64_arm64_plan_summary plan_summary;
    db_x64_arm64_encode_summary encode_summary;
    db_x64_arm64_relocate_summary relocate_summary;
    db_arm64_code_image_plan image_plan;
} translated_pipeline;

static int build_pipeline(const uint8_t *x64_code,
                          size_t x64_size,
                          uint32_t instruction_budget,
                          db_x64_arm64_helper expected_helper,
                          db_translated_terminal_exit_kind relocation_kind,
                          uint64_t code_base,
                          uint64_t page_size,
                          translated_pipeline *pipeline)
{
    db_x64_code_view code;
    db_x64_arm64_helper_targets targets;
    const uint64_t guest_base = UINT64_C(0x100000);

    memset(pipeline, 0, sizeof(*pipeline));
    memset(&code, 0, sizeof(code));
    memset(&targets, 0, sizeof(targets));

    code.bytes = x64_code;
    code.size = x64_size;
    code.guest_base = guest_base;

    CHECK(x64_size <= UINT32_MAX);
    CHECK(db_x64_block_build(
              &code,
              guest_base,
              instruction_budget,
              (uint32_t)x64_size,
              pipeline->block_records,
              RECORD_CAPACITY,
              &pipeline->block_summary) == DB_X64_BLOCK_OK);
    CHECK(pipeline->block_summary.byte_count == x64_size);
    CHECK(pipeline->block_summary.stop_reason ==
          DB_X64_BLOCK_STOP_TERMINATOR);

    CHECK(db_x64_ir_lower_block(
              pipeline->block_records,
              pipeline->block_summary.record_count,
              pipeline->ir,
              IR_CAPACITY,
              &pipeline->ir_summary) == DB_X64_IR_LOWER_OK);

    CHECK(db_arm64_select_block(
              pipeline->ir,
              pipeline->ir_summary.instruction_count,
              pipeline->selections,
              SELECT_CAPACITY,
              &pipeline->select_summary) == DB_ARM64_SELECT_OK);

    CHECK(db_x64_arm64_plan_block(
              pipeline->selections,
              pipeline->select_summary.operation_count,
              pipeline->plans,
              PLAN_CAPACITY,
              &pipeline->plan_summary) == DB_X64_ARM64_PLAN_OK);
    CHECK(pipeline->plan_summary.has_terminal == 1u);
    CHECK(pipeline->plan_summary.terminal_helper == expected_helper);

    CHECK(db_x64_arm64_encode_block(
              pipeline->plans,
              pipeline->plan_summary.plan_count,
              pipeline->source_words,
              PLAN_CAPACITY,
              pipeline->relocations,
              RELOCATION_CAPACITY,
              &pipeline->encode_summary) == DB_X64_ARM64_ENCODE_OK);
    CHECK(pipeline->encode_summary.relocation_count == 1u);
    CHECK(pipeline->encode_summary.terminal_helper == expected_helper);

    targets.addresses[expected_helper] =
        db_linux_arm64_translation_terminal_helper_address(
            relocation_kind);
    CHECK(targets.addresses[expected_helper] != 0u);

    CHECK(db_x64_arm64_resolve_helpers(
              pipeline->source_words,
              pipeline->encode_summary.word_count,
              pipeline->relocations,
              pipeline->encode_summary.relocation_count,
              &targets,
              code_base,
              pipeline->output_words,
              WORD_CAPACITY,
              pipeline->decisions,
              RELOCATION_CAPACITY,
              &pipeline->relocate_summary) ==
          DB_X64_ARM64_RELOCATE_OK);
    CHECK(pipeline->relocate_summary.relocation_count == 1u);
    CHECK(pipeline->relocate_summary.output_word_count <= WORD_CAPACITY);

    CHECK(db_arm64_code_image_build(
              pipeline->output_words,
              pipeline->relocate_summary.output_word_count,
              code_base,
              0u,
              page_size,
              pipeline->image,
              (size_t)pipeline->relocate_summary.output_word_count *
                  sizeof(uint32_t),
              &pipeline->image_plan) == DB_ARM64_CODE_IMAGE_OK);
    return 1;
}

static int cache_roundtrip(const db_arm64_published_block *published,
                           db_arm64_published_block *cached)
{
    db_arm64_block_cache cache;
    db_arm64_block_cache_slot slots[4];
    db_arm64_block_cache_result result;

    CHECK(db_arm64_block_cache_init(&cache, slots, 4u) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(db_arm64_block_cache_insert(
              &cache, published, &result) ==
          DB_ARM64_BLOCK_CACHE_OK);
    CHECK(result.inserted == 1u);
    CHECK(db_arm64_block_cache_lookup(
              &cache,
              published->guest_source_rip,
              published->source_generation,
              published->translation_key,
              cached,
              &result) == DB_ARM64_BLOCK_CACHE_OK);
    CHECK(same_bytes(cached, published, sizeof(*cached)));
    CHECK(same_bytes(&cached->terminal_exit,
                     &published->terminal_exit,
                     sizeof(cached->terminal_exit)));
    return 1;
}

static int publish_pipeline(const db_linux_arm64_wx_platform *platform,
                            translated_pipeline *pipeline,
                            uint32_t cycle,
                            db_linux_arm64_wx_handle *handle,
                            db_linux_arm64_wx_audit *wx_audit,
                            db_arm64_published_block *published)
{
    db_linux_arm64_wx_status publish_status;
    const db_x64_arm64_plan_record *terminal_plan;

    db_linux_arm64_wx_handle_init(handle);
    memset(wx_audit, 0xa5, sizeof(*wx_audit));
    publish_status = db_linux_arm64_wx_publish(
        platform,
        &pipeline->image_plan,
        pipeline->image,
        (size_t)pipeline->image_plan.byte_count,
        handle,
        wx_audit);
    if (publish_status == DB_LINUX_ARM64_WX_MAP_FAILED) {
        return 0;
    }
    CHECK(publish_status == DB_LINUX_ARM64_WX_OK);

    CHECK(db_arm64_published_block_build(
              &pipeline->block_summary,
              &pipeline->image_plan,
              handle,
              wx_audit,
              (uint64_t)cycle + UINT64_C(1),
              UINT64_C(0xabc000) + (uint64_t)cycle,
              published) == DB_ARM64_PUBLISHED_BLOCK_OK);
    CHECK(pipeline->plan_summary.plan_count != 0u);
    terminal_plan =
        &pipeline->plans[pipeline->plan_summary.plan_count - 1u];
    CHECK(db_arm64_published_block_bind_terminal(
              terminal_plan, published) ==
          DB_ARM64_PUBLISHED_BLOCK_OK);
    CHECK(db_translated_terminal_exit_validate_planned(
              &published->terminal_exit) ==
          DB_TRANSLATED_TERMINAL_EXIT_OK);
    return 1;
}

static int execute_direct_cycle(uint32_t cycle)
{
    static const uint8_t x64_code[15] = {
        0x48u, 0xb8u,
        0x88u, 0x77u, 0x66u, 0x55u,
        0x44u, 0x33u, 0x22u, 0x11u,
        0xe9u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    const db_linux_arm64_wx_platform *platform =
        db_linux_arm64_wx_posix_platform();
    const long page_result = sysconf(_SC_PAGESIZE);
    const uint64_t expected_rax = UINT64_C(0x1122334455667788);
    uint32_t attempt;

    CHECK(platform != NULL);
    CHECK(page_result >= 4096);

    for (attempt = 0u; attempt < 64u; ++attempt) {
        const size_t page_size = (size_t)page_result;
        void *probe = mmap(NULL,
                           page_size,
                           PROT_NONE,
                           MAP_PRIVATE | MAP_ANONYMOUS,
                           -1,
                           0);
        translated_pipeline pipeline;
        db_linux_arm64_wx_handle handle;
        db_linux_arm64_wx_audit wx_audit;
        db_arm64_published_block published;
        db_arm64_published_block cached;
        db_x64_scalar_state state;
        db_linux_arm64_translation_context context;
        db_linux_arm64_translation_entry_audit entry_audit;

        CHECK(probe != MAP_FAILED);
        CHECK(munmap(probe, page_size) == 0);
        CHECK(build_pipeline(
                  x64_code,
                  sizeof(x64_code),
                  4u,
                  DB_X64_ARM64_HELPER_DISPATCH_DIRECT,
                  DB_TRANSLATED_TERMINAL_EXIT_DIRECT,
                  (uint64_t)(uintptr_t)probe,
                  (uint64_t)page_size,
                  &pipeline));
        if (!publish_pipeline(platform, &pipeline, cycle,
                              &handle, &wx_audit, &published)) {
            continue;
        }
        CHECK(published.terminal_exit.kind ==
              DB_TRANSLATED_TERMINAL_EXIT_DIRECT);
        CHECK(cache_roundtrip(&published, &cached));

        memset(&state, 0, sizeof(state));
        state.rip = pipeline.block_summary.source_rip;
        state.rflags = DB_X64_RFLAGS_FIXED_ONE;
        CHECK(db_linux_arm64_translation_context_init(&context) ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
        CHECK(db_linux_arm64_translation_context_bind_terminal(
                  &context, &cached.terminal_exit) ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
        memset(&entry_audit, 0xa5, sizeof(entry_audit));

        CHECK(db_linux_arm64_translation_enter(
                  cached.host_entry_address,
                  &state,
                  &context,
                  &entry_audit) ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
        CHECK(state.registers[0] == expected_rax);
        CHECK(context.terminal_return_count == 1u);
        CHECK(context.observed_exit.kind ==
              DB_TRANSLATED_TERMINAL_EXIT_DIRECT);
        CHECK(context.observed_exit.source_rip ==
              cached.terminal_exit.source_rip);
        CHECK(context.observed_exit.direct_target ==
              cached.terminal_exit.direct_target);
        CHECK(db_translated_terminal_exit_match(
                  &context.planned_exit,
                  &context.observed_exit) ==
              DB_TRANSLATED_TERMINAL_EXIT_OK);
        CHECK(entry_audit.entry_address == cached.host_entry_address);
        CHECK(entry_audit.state_address == (uint64_t)(uintptr_t)&state);
        CHECK(entry_audit.context_address ==
              (uint64_t)(uintptr_t)&context);
        CHECK(entry_audit.return_helper_address ==
              db_linux_arm64_translation_terminal_helper_address(
                  DB_TRANSLATED_TERMINAL_EXIT_DIRECT));
        CHECK(entry_audit.terminal_return_count_before == 0u);
        CHECK(entry_audit.terminal_return_count_after == 1u);
        CHECK(entry_audit.planned_kind ==
              DB_TRANSLATED_TERMINAL_EXIT_DIRECT);
        CHECK(entry_audit.observed_kind ==
              DB_TRANSLATED_TERMINAL_EXIT_DIRECT);
        CHECK(entry_audit.version ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_AUDIT_VERSION);
        CHECK(entry_audit.status ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
        CHECK(entry_audit.entry_invoked == 1u);
        CHECK(entry_audit.terminal_return_observed == 1u);
        CHECK(entry_audit.terminal_exit_matched == 1u);
        CHECK(same_bytes(entry_audit.reserved,
                         (uint8_t[5]){0},
                         sizeof(entry_audit.reserved)));

        CHECK(db_linux_arm64_wx_release(
                  platform,
                  &handle,
                  &wx_audit) == DB_LINUX_ARM64_WX_OK);
        CHECK(handle.state == DB_LINUX_ARM64_WX_EMPTY);
        return 1;
    }

    return 0;
}

static int execute_trap_cycle(uint32_t cycle)
{
    static const uint8_t x64_code[2] = {0x0fu, 0x0bu};
    const db_linux_arm64_wx_platform *platform =
        db_linux_arm64_wx_posix_platform();
    const long page_result = sysconf(_SC_PAGESIZE);
    uint32_t attempt;

    CHECK(platform != NULL);
    CHECK(page_result >= 4096);

    for (attempt = 0u; attempt < 64u; ++attempt) {
        const size_t page_size = (size_t)page_result;
        void *probe = mmap(NULL,
                           page_size,
                           PROT_NONE,
                           MAP_PRIVATE | MAP_ANONYMOUS,
                           -1,
                           0);
        translated_pipeline pipeline;
        db_linux_arm64_wx_handle handle;
        db_linux_arm64_wx_audit wx_audit;
        db_arm64_published_block published;
        db_arm64_published_block cached;
        db_x64_scalar_state state;
        db_linux_arm64_translation_context context;
        db_linux_arm64_translation_entry_audit entry_audit;

        CHECK(probe != MAP_FAILED);
        CHECK(munmap(probe, page_size) == 0);
        CHECK(build_pipeline(
                  x64_code,
                  sizeof(x64_code),
                  2u,
                  DB_X64_ARM64_HELPER_RAISE_TRAP,
                  DB_TRANSLATED_TERMINAL_EXIT_TRAP,
                  (uint64_t)(uintptr_t)probe,
                  (uint64_t)page_size,
                  &pipeline));
        if (!publish_pipeline(platform, &pipeline,
                              cycle + UINT32_C(100),
                              &handle, &wx_audit, &published)) {
            continue;
        }
        CHECK(published.terminal_exit.kind ==
              DB_TRANSLATED_TERMINAL_EXIT_TRAP);
        CHECK(published.terminal_exit.trap ==
              (uint32_t)DB_IR_TRAP_INVALID_OPCODE);
        CHECK(cache_roundtrip(&published, &cached));

        memset(&state, 0, sizeof(state));
        state.rip = pipeline.block_summary.source_rip;
        state.rflags = DB_X64_RFLAGS_FIXED_ONE;
        CHECK(db_linux_arm64_translation_context_init(&context) ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
        CHECK(db_linux_arm64_translation_context_bind_terminal(
                  &context, &cached.terminal_exit) ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
        memset(&entry_audit, 0xa5, sizeof(entry_audit));

        CHECK(db_linux_arm64_translation_enter(
                  cached.host_entry_address,
                  &state,
                  &context,
                  &entry_audit) ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
        CHECK(context.terminal_return_count == 1u);
        CHECK(context.observed_exit.kind ==
              DB_TRANSLATED_TERMINAL_EXIT_TRAP);
        CHECK(context.observed_exit.trap ==
              (uint32_t)DB_IR_TRAP_INVALID_OPCODE);
        CHECK(context.observed_exit.source_rip ==
              cached.terminal_exit.source_rip);
        CHECK(db_translated_terminal_exit_match(
                  &context.planned_exit,
                  &context.observed_exit) ==
              DB_TRANSLATED_TERMINAL_EXIT_OK);
        CHECK(entry_audit.return_helper_address ==
              db_linux_arm64_translation_terminal_helper_address(
                  DB_TRANSLATED_TERMINAL_EXIT_TRAP));
        CHECK(entry_audit.planned_kind ==
              DB_TRANSLATED_TERMINAL_EXIT_TRAP);
        CHECK(entry_audit.observed_kind ==
              DB_TRANSLATED_TERMINAL_EXIT_TRAP);
        CHECK(entry_audit.terminal_exit_matched == 1u);

        CHECK(db_linux_arm64_wx_release(
                  platform,
                  &handle,
                  &wx_audit) == DB_LINUX_ARM64_WX_OK);
        CHECK(handle.state == DB_LINUX_ARM64_WX_EMPTY);
        return 1;
    }

    return 0;
}

static int test_native_mismatch_is_transactional(void)
{
    static const uint8_t x64_code[15] = {
        0x48u, 0xb8u,
        0x88u, 0x77u, 0x66u, 0x55u,
        0x44u, 0x33u, 0x22u, 0x11u,
        0xe9u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    const db_linux_arm64_wx_platform *platform =
        db_linux_arm64_wx_posix_platform();
    const long page_result = sysconf(_SC_PAGESIZE);
    uint32_t attempt;

    CHECK(platform != NULL);
    CHECK(page_result >= 4096);

    for (attempt = 0u; attempt < 64u; ++attempt) {
        const size_t page_size = (size_t)page_result;
        void *probe = mmap(NULL,
                           page_size,
                           PROT_NONE,
                           MAP_PRIVATE | MAP_ANONYMOUS,
                           -1,
                           0);
        translated_pipeline pipeline;
        db_linux_arm64_wx_handle handle;
        db_linux_arm64_wx_audit wx_audit;
        db_arm64_published_block published;
        db_x64_scalar_state state;
        db_x64_scalar_state state_before;
        db_linux_arm64_translation_context context;
        db_linux_arm64_translation_context context_before;
        db_linux_arm64_translation_entry_audit entry_audit;
        db_linux_arm64_translation_entry_audit audit_before;

        CHECK(probe != MAP_FAILED);
        CHECK(munmap(probe, page_size) == 0);
        CHECK(build_pipeline(
                  x64_code,
                  sizeof(x64_code),
                  4u,
                  DB_X64_ARM64_HELPER_DISPATCH_DIRECT,
                  DB_TRANSLATED_TERMINAL_EXIT_TRAP,
                  (uint64_t)(uintptr_t)probe,
                  (uint64_t)page_size,
                  &pipeline));
        if (!publish_pipeline(platform, &pipeline,
                              UINT32_C(500),
                              &handle, &wx_audit, &published)) {
            continue;
        }
        CHECK(published.terminal_exit.kind ==
              DB_TRANSLATED_TERMINAL_EXIT_DIRECT);

        memset(&state, 0, sizeof(state));
        state.rip = pipeline.block_summary.source_rip;
        state.rflags = DB_X64_RFLAGS_FIXED_ONE;
        state_before = state;
        CHECK(db_linux_arm64_translation_context_init(&context) ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
        CHECK(db_linux_arm64_translation_context_bind_terminal(
                  &context, &published.terminal_exit) ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
        context_before = context;
        memset(&entry_audit, 0x5a, sizeof(entry_audit));
        audit_before = entry_audit;

        CHECK(db_linux_arm64_translation_enter(
                  published.host_entry_address,
                  &state,
                  &context,
                  &entry_audit) ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_TERMINAL_EXIT_MISMATCH);
        CHECK(same_bytes(&state, &state_before, sizeof(state)));
        CHECK(same_bytes(&context, &context_before, sizeof(context)));
        CHECK(same_bytes(&entry_audit,
                         &audit_before,
                         sizeof(entry_audit)));

        CHECK(db_linux_arm64_wx_release(
                  platform,
                  &handle,
                  &wx_audit) == DB_LINUX_ARM64_WX_OK);
        return 1;
    }

    return 0;
}

static int test_native_translated_execution(void)
{
    uint32_t cycle;

    for (cycle = 0u; cycle < 4u; ++cycle) {
        CHECK(execute_direct_cycle(cycle));
        CHECK(execute_trap_cycle(cycle));
    }
    CHECK(test_native_mismatch_is_transactional());
    return 1;
}
#endif

int main(void)
{
    if (!test_terminal_exit_records()) {
        return 1;
    }
    if (!test_validation()) {
        return 2;
    }

#if defined(__aarch64__)
    if (!test_native_translated_execution()) {
        return 3;
    }
    printf("X64 BYTES TO NATIVE ARM64 EXECUTION: PASS\n");
    printf("TRANSLATED DIRECT EXIT IDENTITY: PASS\n");
    printf("TRANSLATED INVALID OPCODE TRAP IDENTITY: PASS\n");
    printf("TRANSLATED BLOCK CACHE EXIT PERSISTENCE: PASS\n");
    printf("TRANSLATED EXIT MISMATCH TRANSACTIONALITY: PASS\n");
    printf("TRANSLATED W^X RELEASE CYCLES: PASS\n");
#else
    printf("X64 BYTES TO NATIVE ARM64 EXECUTION: NOT CLAIMED\n");
#endif

    printf("TRANSLATED TERMINAL EXIT RECORD: PASS\n");
    printf("LINUX ARM64 TRANSLATED ENTRY TEST: PASS\n");
    return 0;
}
