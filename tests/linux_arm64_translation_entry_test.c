#define _GNU_SOURCE
#include "dblyat/arm64_code_image.h"
#include "dblyat/arm64_published_block.h"
#include "dblyat/arm64_select.h"
#include "dblyat/linux_arm64_translation_entry.h"
#include "dblyat/linux_arm64_wx_provider.h"
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

static int test_validation(void)
{
    db_x64_scalar_state state;
    db_linux_arm64_translation_context context;
    db_linux_arm64_translation_context context_before;
    db_linux_arm64_translation_entry_audit audit;
    db_linux_arm64_translation_entry_audit audit_before;

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
    CHECK(same_bytes(context.reserved,
                     (uint8_t[4]){0},
                     sizeof(context.reserved)));

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
              UINT64_C(0x0000800000000000),
              &state, &context, &audit) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_ENTRY_ADDRESS);
    CHECK(same_bytes(&context, &context_before, sizeof(context)));
    CHECK(same_bytes(&audit, &audit_before, sizeof(audit)));

    context.format_version += 1u;
    CHECK(db_linux_arm64_translation_enter(
              UINT64_C(0x1000), &state, &context, &audit) ==
          DB_LINUX_ARM64_TRANSLATION_ENTRY_BAD_CONTEXT);
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

static int build_pipeline(uint64_t code_base,
                          uint64_t page_size,
                          translated_pipeline *pipeline)
{
    static const uint8_t x64_code[15] = {
        0x48u, 0xb8u,
        0x88u, 0x77u, 0x66u, 0x55u,
        0x44u, 0x33u, 0x22u, 0x11u,
        0xe9u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    db_x64_code_view code;
    db_x64_arm64_helper_targets targets;
    const uint64_t guest_base = UINT64_C(0x100000);

    memset(pipeline, 0, sizeof(*pipeline));
    memset(&code, 0, sizeof(code));
    memset(&targets, 0, sizeof(targets));

    code.bytes = x64_code;
    code.size = sizeof(x64_code);
    code.guest_base = guest_base;

    CHECK(db_x64_block_build(
              &code,
              guest_base,
              4u,
              (uint32_t)sizeof(x64_code),
              pipeline->block_records,
              RECORD_CAPACITY,
              &pipeline->block_summary) == DB_X64_BLOCK_OK);
    CHECK(pipeline->block_summary.record_count == 2u);
    CHECK(pipeline->block_summary.byte_count == sizeof(x64_code));
    CHECK(pipeline->block_summary.stop_reason ==
          DB_X64_BLOCK_STOP_TERMINATOR);

    CHECK(db_x64_ir_lower_block(
              pipeline->block_records,
              pipeline->block_summary.record_count,
              pipeline->ir,
              IR_CAPACITY,
              &pipeline->ir_summary) == DB_X64_IR_LOWER_OK);
    CHECK(pipeline->ir_summary.instruction_count == 2u);

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

    CHECK(db_x64_arm64_encode_block(
              pipeline->plans,
              pipeline->plan_summary.plan_count,
              pipeline->source_words,
              PLAN_CAPACITY,
              pipeline->relocations,
              RELOCATION_CAPACITY,
              &pipeline->encode_summary) == DB_X64_ARM64_ENCODE_OK);
    CHECK(pipeline->encode_summary.relocation_count == 1u);
    CHECK(pipeline->encode_summary.terminal_helper ==
          DB_X64_ARM64_HELPER_DISPATCH_DIRECT);

    targets.addresses[DB_X64_ARM64_HELPER_DISPATCH_DIRECT] =
        db_linux_arm64_translation_return_helper_address();
    CHECK(targets.addresses[DB_X64_ARM64_HELPER_DISPATCH_DIRECT] != 0u);

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
              sizeof(pipeline->image),
              &pipeline->image_plan) == DB_ARM64_CODE_IMAGE_OK);
    return 1;
}

static int execute_one_cycle(uint32_t cycle)
{
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
        db_x64_scalar_state state;
        db_linux_arm64_translation_context context;
        db_linux_arm64_translation_entry_audit entry_audit;
        db_linux_arm64_wx_status publish_status;

        CHECK(probe != MAP_FAILED);
        CHECK(munmap(probe, page_size) == 0);
        CHECK(build_pipeline((uint64_t)(uintptr_t)probe,
                             (uint64_t)page_size,
                             &pipeline));

        db_linux_arm64_wx_handle_init(&handle);
        memset(&wx_audit, 0xa5, sizeof(wx_audit));
        publish_status = db_linux_arm64_wx_publish(
            platform,
            &pipeline.image_plan,
            pipeline.image,
            (size_t)pipeline.image_plan.byte_count,
            &handle,
            &wx_audit);

        if (publish_status == DB_LINUX_ARM64_WX_MAP_FAILED) {
            continue;
        }
        CHECK(publish_status == DB_LINUX_ARM64_WX_OK);

        CHECK(db_arm64_published_block_build(
                  &pipeline.block_summary,
                  &pipeline.image_plan,
                  &handle,
                  &wx_audit,
                  (uint64_t)cycle + UINT64_C(1),
                  UINT64_C(0xabc000) + (uint64_t)cycle,
                  &published) == DB_ARM64_PUBLISHED_BLOCK_OK);
        CHECK(published.host_entry_address ==
              (uint64_t)(uintptr_t)handle.entry);

        memset(&state, 0, sizeof(state));
        state.rip = pipeline.block_summary.source_rip;
        state.rflags = DB_X64_RFLAGS_FIXED_ONE;
        CHECK(db_linux_arm64_translation_context_init(&context) ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
        memset(&entry_audit, 0xa5, sizeof(entry_audit));

        CHECK(db_linux_arm64_translation_enter(
                  published.host_entry_address,
                  &state,
                  &context,
                  &entry_audit) ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
        CHECK(state.registers[0] == expected_rax);
        CHECK(context.terminal_return_count == 1u);
        CHECK(entry_audit.entry_address == published.host_entry_address);
        CHECK(entry_audit.state_address == (uint64_t)(uintptr_t)&state);
        CHECK(entry_audit.context_address ==
              (uint64_t)(uintptr_t)&context);
        CHECK(entry_audit.return_helper_address ==
              db_linux_arm64_translation_return_helper_address());
        CHECK(entry_audit.terminal_return_count_before == 0u);
        CHECK(entry_audit.terminal_return_count_after == 1u);
        CHECK(entry_audit.version ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_AUDIT_VERSION);
        CHECK(entry_audit.status ==
              DB_LINUX_ARM64_TRANSLATION_ENTRY_OK);
        CHECK(entry_audit.entry_invoked == 1u);
        CHECK(entry_audit.terminal_return_observed == 1u);
        CHECK(same_bytes(entry_audit.reserved,
                         (uint8_t[6]){0},
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

static int test_native_translated_execution(void)
{
    uint32_t cycle;

    for (cycle = 0u; cycle < 8u; ++cycle) {
        CHECK(execute_one_cycle(cycle));
    }

    return 1;
}
#endif

int main(void)
{
    if (!test_validation()) {
        return 1;
    }

#if defined(__aarch64__)
    if (!test_native_translated_execution()) {
        return 2;
    }
    printf("X64 BYTES TO NATIVE ARM64 EXECUTION: PASS\n");
    printf("TRANSLATED GUEST RAX RESULT: PASS\n");
    printf("TRANSLATED TERMINAL RETURN HELPER: PASS\n");
    printf("TRANSLATED W^X RELEASE CYCLES: PASS\n");
#else
    printf("X64 BYTES TO NATIVE ARM64 EXECUTION: NOT CLAIMED\n");
#endif

    printf("LINUX ARM64 TRANSLATED ENTRY TEST: PASS\n");
    return 0;
}
