#include "win32_pe_translated_session_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STORE_CAPACITY 2u

#define CHECK(value)                                                         \
    do {                                                                     \
        if (!(value)) {                                                      \
            printf("WIN32 PE SESSION OWNERSHIP CHECK: FAIL line=%d\n",      \
                   __LINE__);                                                \
            return 0;                                                        \
        }                                                                    \
    } while (0)

typedef struct ownership_fixture {
    db_win32_pe_translated_session session;
    db_arm64_block_cache_slot slots[STORE_CAPACITY];
    db_linux_arm64_wx_handle handles[STORE_CAPACITY];
    uint8_t owned[STORE_CAPACITY];
    db_x64_block_record block_records[1];
    db_ir_instruction ir[1];
    db_arm64_select_record selections[1];
    db_x64_arm64_plan_record plans[1];
    uint32_t source_words[1];
    db_x64_arm64_relocation relocations[1];
    uint32_t output_words[1];
    db_x64_arm64_resolution resolutions[1];
    uint8_t translated_image[4];
    db_arm64_translation_workspace workspace;
    db_linux_arm64_wx_platform platform;
    uint8_t image[64];
    db_map_page_protection pages[1];
    uint8_t gateway[64];
    db_iat_target targets[1];
    db_gateway_dispatch_handler handlers_table[1];
    db_gateway_frame_signature signatures[1];
    _Alignas(db_win32_pe_translated_session_run_summary)
        uint8_t guest_stack[1024];
    uint32_t executor_calls;
    uint32_t unmap_calls;
} ownership_fixture;

static void *fake_map(void *context,
                      void *requested_address,
                      size_t allocation_size)
{
    (void)context;
    (void)requested_address;
    (void)allocation_size;
    return NULL;
}

static int fake_copy(void *context,
                     void *destination,
                     const void *source,
                     size_t byte_count)
{
    (void)context;
    (void)destination;
    (void)source;
    (void)byte_count;
    return -1;
}

static int fake_protect(void *context,
                        void *mapping,
                        size_t allocation_size)
{
    (void)context;
    (void)mapping;
    (void)allocation_size;
    return -1;
}

static int fake_sync(void *context, void *begin, size_t byte_count)
{
    (void)context;
    (void)begin;
    (void)byte_count;
    return -1;
}

static int fake_unmap(void *context,
                      void *mapping,
                      size_t allocation_size)
{
    ownership_fixture *fixture = (ownership_fixture *)context;
    (void)mapping;
    (void)allocation_size;
    ++fixture->unmap_calls;
    return 0;
}

static db_win32_translated_execution_status never_execute(
    void *context,
    const db_arm64_published_block *published,
    const db_win32_translated_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_execution_summary *summary)
{
    ownership_fixture *fixture = (ownership_fixture *)context;
    (void)published;
    (void)config;
    (void)kernel32_state;
    (void)scalar_state;
    (void)summary;
    ++fixture->executor_calls;
    return DB_WIN32_TRANSLATED_EXECUTION_NULL_ARGUMENT;
}

static int init_fixture(ownership_fixture *fixture)
{
    db_win32_pe_translated_session *session;

    (void)memset(fixture, 0, sizeof(*fixture));
    session = &fixture->session;
    db_win32_pe_translated_session_init(session);

    fixture->workspace.block_records = fixture->block_records;
    fixture->workspace.ir = fixture->ir;
    fixture->workspace.selections = fixture->selections;
    fixture->workspace.plans = fixture->plans;
    fixture->workspace.source_words = fixture->source_words;
    fixture->workspace.relocations = fixture->relocations;
    fixture->workspace.output_words = fixture->output_words;
    fixture->workspace.resolutions = fixture->resolutions;
    fixture->workspace.image_bytes = fixture->translated_image;
    fixture->workspace.block_record_capacity = 1u;
    fixture->workspace.ir_capacity = 1u;
    fixture->workspace.selection_capacity = 1u;
    fixture->workspace.plan_capacity = 1u;
    fixture->workspace.source_word_capacity = 1u;
    fixture->workspace.relocation_capacity = 1u;
    fixture->workspace.output_word_capacity = 1u;
    fixture->workspace.resolution_capacity = 1u;
    fixture->workspace.image_capacity = sizeof(fixture->translated_image);

    fixture->platform.context = fixture;
    fixture->platform.map_rw = fake_map;
    fixture->platform.copy_bytes = fake_copy;
    fixture->platform.protect_rx = fake_protect;
    fixture->platform.sync_icache = fake_sync;
    fixture->platform.unmap = fake_unmap;

    if (db_arm64_translation_store_init(
            &session->store,
            fixture->slots,
            fixture->handles,
            fixture->owned,
            STORE_CAPACITY) != DB_ARM64_TRANSLATION_STORE_OK) {
        return 0;
    }

    session->wx_platform = &fixture->platform;
    session->code.bytes = fixture->image;
    session->code.size = sizeof(fixture->image);
    session->code.guest_base = UINT64_C(0x140000000);
    session->translation.code = &session->code;
    session->translation.helper_targets = &session->helper_targets;
    session->translation.wx_platform = session->wx_platform;
    session->demand.store = &session->store;
    session->demand.translation = &session->translation;
    session->demand.workspace = &fixture->workspace;
    session->demand.image_memory = fixture->image;
    session->demand.image_page_plan = fixture->pages;
    session->demand.gateway_buffer = fixture->gateway;
    session->demand.targets = fixture->targets;
    session->demand.handlers = fixture->handlers_table;
    session->demand.signatures = fixture->signatures;
    session->demand.guest_stack = fixture->guest_stack;
    session->demand.image_page_count = 1u;
    session->demand.image_page_size = 4096u;
    session->demand.gateway_buffer_size = sizeof(fixture->gateway);
    session->demand.target_count = 1u;
    session->demand.handler_count = 1u;
    session->demand.signature_count = 1u;
    session->demand.guest_stack_size = sizeof(fixture->guest_stack);
    session->demand.format_version =
        DB_WIN32_TRANSLATED_DEMAND_RUN_FORMAT_VERSION;
    session->kernel32.process_id = 1u;
    session->kernel32.thread_id = 2u;
    session->scalar.rip = session->code.guest_base;
    session->scalar.rflags = DB_X64_RFLAGS_FIXED_ONE;
    session->state = (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_READY;
    session->format_version = DB_WIN32_PE_TRANSLATED_SESSION_FORMAT_VERSION;
    return 1;
}

static int test_run_output_alias_rejected(void)
{
    ownership_fixture fixture;
    db_win32_pe_translated_session_run_summary *aliased;

    CHECK(init_fixture(&fixture));
    aliased = (db_win32_pe_translated_session_run_summary *)(void *)
        fixture.guest_stack;

    CHECK(db_win32_pe_translated_session_run_with_executor(
              &fixture.session,
              aliased,
              never_execute,
              &fixture) == DB_WIN32_PE_TRANSLATED_SESSION_BUFFER_OVERLAP);
    CHECK(fixture.executor_calls == 0u);
    CHECK(fixture.session.store.cache.count == 0u);
    CHECK(fixture.session.state ==
          (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_READY);
    CHECK(db_win32_pe_translated_session_dispose(&fixture.session) ==
          DB_WIN32_PE_TRANSLATED_SESSION_OK);
    return 1;
}

static int test_copied_session_dispose_rejected(void)
{
    ownership_fixture fixture;
    db_win32_pe_translated_session copied;

    CHECK(init_fixture(&fixture));
    copied = fixture.session;

    CHECK(db_win32_pe_translated_session_dispose(&copied) ==
          DB_WIN32_PE_TRANSLATED_SESSION_BAD_SESSION);
    CHECK(fixture.unmap_calls == 0u);
    CHECK(fixture.session.store.state ==
          (uint32_t)DB_ARM64_TRANSLATION_STORE_READY);
    CHECK(fixture.session.state ==
          (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_READY);

    CHECK(db_win32_pe_translated_session_dispose(&fixture.session) ==
          DB_WIN32_PE_TRANSLATED_SESSION_OK);
    CHECK(fixture.unmap_calls == 0u);
    CHECK(fixture.session.state ==
          (uint32_t)DB_WIN32_PE_TRANSLATED_SESSION_EMPTY);
    return 1;
}

int main(void)
{
    CHECK(test_run_output_alias_rejected());
    CHECK(test_copied_session_dispose_rejected());
    printf("SESSION RUN OUTPUT ALIAS REJECTION: PASS\n");
    printf("SESSION COPIED DISPOSE REJECTION: PASS\n");
    printf("WIN32 PE SESSION OWNERSHIP CHECK: PASS\n");
    return 0;
}
