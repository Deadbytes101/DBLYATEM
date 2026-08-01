#include "dblyat/linux_arm64_wx_provider.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

typedef enum mock_failure {
    MOCK_FAIL_NONE = 0,
    MOCK_FAIL_MAP,
    MOCK_FAIL_COPY,
    MOCK_FAIL_CORRUPT,
    MOCK_FAIL_PROTECT,
    MOCK_FAIL_CACHE,
    MOCK_FAIL_UNMAP
} mock_failure;

typedef struct mock_context {
    uint8_t *page;
    size_t page_size;
    mock_failure failure;
    uint32_t map_calls;
    uint32_t copy_calls;
    uint32_t protect_calls;
    uint32_t cache_calls;
    uint32_t unmap_calls;
    uint8_t mapped;
    uint8_t writable;
    uint8_t executable;
    uint8_t force_unmap_fail;
} mock_context;

_Alignas(4096) static uint8_t mock_page[4096];

#define CHECK(value) do { if (!(value)) return 0; } while (0)

static void fill(void *data, size_t size, uint8_t value)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        bytes[index] = value;
    }
}

static db_arm64_code_image_plan valid_plan(uint64_t base,
                                            size_t image_size)
{
    db_arm64_code_image_plan plan;

    fill(&plan, sizeof(plan), 0u);
    plan.code_base = base;
    plan.entry_address = base;
    plan.image_end_address = base + image_size - 1u;
    plan.allocation_end_address = base + 4096u - 1u;
    plan.page_size = 4096u;
    plan.allocation_size = 4096u;
    plan.instruction_cache_offset = 0u;
    plan.instruction_cache_size = image_size;
    plan.format_version = DB_ARM64_CODE_IMAGE_FORMAT_VERSION;
    plan.word_count = (uint32_t)(image_size / 4u);
    plan.byte_count = image_size;
    plan.entry_byte_offset = 0u;
    plan.entry_word_index = 0u;
    plan.initial_permissions =
        DB_ARM64_CODE_PERMISSION_READ |
        DB_ARM64_CODE_PERMISSION_WRITE;
    plan.final_permissions =
        DB_ARM64_CODE_PERMISSION_READ |
        DB_ARM64_CODE_PERMISSION_EXECUTE;
    return plan;
}

static void reset_context(mock_context *context, mock_failure failure)
{
    fill(context, sizeof(*context), 0u);
    fill(mock_page, sizeof(mock_page), UINT8_C(0xcc));
    context->page = mock_page;
    context->page_size = sizeof(mock_page);
    context->failure = failure;
}

static void *mock_map(void *opaque,
                      void *requested_address,
                      size_t allocation_size)
{
    mock_context *context = (mock_context *)opaque;

    ++context->map_calls;
    if (context->failure == MOCK_FAIL_MAP ||
        requested_address != context->page ||
        allocation_size != context->page_size ||
        context->mapped != 0u) {
        return NULL;
    }
    context->mapped = 1u;
    context->writable = 1u;
    context->executable = 0u;
    return context->page;
}

static int mock_copy(void *opaque,
                     void *destination,
                     const void *source,
                     size_t byte_count)
{
    mock_context *context = (mock_context *)opaque;

    ++context->copy_calls;
    if (context->failure == MOCK_FAIL_COPY ||
        context->mapped == 0u || context->writable == 0u ||
        context->executable != 0u || destination != context->page) {
        return -1;
    }
    (void)memcpy(destination, source, byte_count);
    if (context->failure == MOCK_FAIL_CORRUPT && byte_count != 0u) {
        context->page[0] ^= UINT8_C(1);
    }
    return 0;
}

static int mock_protect(void *opaque,
                        void *mapping,
                        size_t allocation_size)
{
    mock_context *context = (mock_context *)opaque;

    ++context->protect_calls;
    if (context->failure == MOCK_FAIL_PROTECT ||
        mapping != context->page || allocation_size != context->page_size ||
        context->mapped == 0u || context->writable == 0u ||
        context->executable != 0u) {
        return -1;
    }
    context->writable = 0u;
    context->executable = 1u;
    return 0;
}

static int mock_cache(void *opaque, void *begin, size_t byte_count)
{
    mock_context *context = (mock_context *)opaque;

    ++context->cache_calls;
    if (context->failure == MOCK_FAIL_CACHE ||
        begin != context->page || byte_count != 8u ||
        context->mapped == 0u || context->writable != 0u ||
        context->executable == 0u) {
        return -1;
    }
    return 0;
}

static int mock_unmap(void *opaque,
                      void *mapping,
                      size_t allocation_size)
{
    mock_context *context = (mock_context *)opaque;

    ++context->unmap_calls;
    if (context->failure == MOCK_FAIL_UNMAP ||
        context->force_unmap_fail != 0u ||
        mapping != context->page || allocation_size != context->page_size ||
        context->mapped == 0u) {
        return -1;
    }
    context->mapped = 0u;
    context->writable = 0u;
    context->executable = 0u;
    return 0;
}

static db_linux_arm64_wx_platform platform_for(mock_context *context)
{
    db_linux_arm64_wx_platform platform;

    fill(&platform, sizeof(platform), 0u);
    platform.context = context;
    platform.map_rw = mock_map;
    platform.copy_bytes = mock_copy;
    platform.protect_rx = mock_protect;
    platform.sync_icache = mock_cache;
    platform.unmap = mock_unmap;
    return platform;
}

static int test_success_and_release(void)
{
    static const uint8_t image[8] = {
        0x1fu, 0x20u, 0x03u, 0xd5u,
        0xc0u, 0x03u, 0x5fu, 0xd6u
    };
    mock_context context;
    db_linux_arm64_wx_platform platform;
    db_linux_arm64_wx_handle handle;
    db_linux_arm64_wx_audit audit;
    db_arm64_code_image_plan plan;

    reset_context(&context, MOCK_FAIL_NONE);
    platform = platform_for(&context);
    plan = valid_plan((uint64_t)(uintptr_t)mock_page, sizeof(image));
    db_linux_arm64_wx_handle_init(&handle);

    CHECK(db_linux_arm64_wx_publish(&platform, &plan,
                                    image, sizeof(image),
                                    &handle, &audit) ==
          DB_LINUX_ARM64_WX_OK);
    CHECK(handle.mapping == mock_page);
    CHECK(handle.entry == mock_page);
    CHECK(handle.allocation_size == sizeof(mock_page));
    CHECK(handle.state == DB_LINUX_ARM64_WX_PUBLISHED);
    CHECK(context.mapped == 1u);
    CHECK(context.writable == 0u);
    CHECK(context.executable == 1u);
    CHECK(memcmp(mock_page, image, sizeof(image)) == 0);
    CHECK(audit.completed_state == DB_LINUX_ARM64_WX_PUBLISHED);
    CHECK(audit.map_observed == 1u);
    CHECK(audit.copy_verified == 1u);
    CHECK(audit.rx_observed == 1u);
    CHECK(audit.icache_observed == 1u);
    CHECK(audit.rollback_observed == 0u);

    CHECK(db_linux_arm64_wx_publish(&platform, &plan,
                                    image, sizeof(image),
                                    &handle, &audit) ==
          DB_LINUX_ARM64_WX_HANDLE_NOT_EMPTY);
    CHECK(context.map_calls == 1u);

    CHECK(db_linux_arm64_wx_release(
              &platform,
              &handle,
              (db_linux_arm64_wx_audit *)(void *)&handle) ==
          DB_LINUX_ARM64_WX_BUFFER_OVERLAP);
    CHECK(handle.state == DB_LINUX_ARM64_WX_PUBLISHED);
    CHECK(context.mapped == 1u);

    CHECK(db_linux_arm64_wx_release(
              &platform,
              &handle,
              (db_linux_arm64_wx_audit *)(void *)mock_page) ==
          DB_LINUX_ARM64_WX_BUFFER_OVERLAP);
    CHECK(handle.state == DB_LINUX_ARM64_WX_PUBLISHED);
    CHECK(context.mapped == 1u);

    context.failure = MOCK_FAIL_UNMAP;
    CHECK(db_linux_arm64_wx_release(&platform, &handle, &audit) ==
          DB_LINUX_ARM64_WX_UNMAP_FAILED);
    CHECK(handle.mapping == mock_page);
    CHECK(handle.entry == mock_page);
    CHECK(handle.allocation_size == sizeof(mock_page));
    CHECK(handle.state == DB_LINUX_ARM64_WX_PUBLISHED);
    CHECK(context.mapped == 1u);
    CHECK(context.writable == 0u);
    CHECK(context.executable == 1u);
    CHECK(audit.release_observed == 0u);

    context.failure = MOCK_FAIL_NONE;
    CHECK(db_linux_arm64_wx_release(&platform, &handle, &audit) ==
          DB_LINUX_ARM64_WX_OK);
    CHECK(handle.state == DB_LINUX_ARM64_WX_EMPTY);
    CHECK(handle.mapping == NULL);
    CHECK(audit.release_observed == 1u);
    CHECK(context.mapped == 0u);
    CHECK(db_linux_arm64_wx_release(&platform, &handle, &audit) ==
          DB_LINUX_ARM64_WX_HANDLE_NOT_PUBLISHED);
    return 1;
}

static int failure_case(mock_failure failure,
                        uint8_t force_unmap_fail,
                        db_linux_arm64_wx_status expected,
                        uint8_t rollback_expected)
{
    static const uint8_t image[8] = {
        0x1fu, 0x20u, 0x03u, 0xd5u,
        0xc0u, 0x03u, 0x5fu, 0xd6u
    };
    mock_context context;
    db_linux_arm64_wx_platform platform;
    db_linux_arm64_wx_handle handle;
    db_linux_arm64_wx_audit audit;
    db_arm64_code_image_plan plan;

    reset_context(&context, failure);
    context.force_unmap_fail = force_unmap_fail;
    platform = platform_for(&context);
    plan = valid_plan((uint64_t)(uintptr_t)mock_page, sizeof(image));
    db_linux_arm64_wx_handle_init(&handle);

    CHECK(db_linux_arm64_wx_publish(&platform, &plan,
                                    image, sizeof(image),
                                    &handle, &audit) == expected);
    CHECK(audit.rollback_observed == rollback_expected);

    if (force_unmap_fail != 0u) {
        CHECK(handle.state == DB_LINUX_ARM64_WX_ROLLBACK_PENDING);
        CHECK(handle.mapping == mock_page);
        CHECK(handle.entry == NULL);
        CHECK(handle.allocation_size == sizeof(mock_page));
        CHECK(context.mapped == 1u);
        CHECK(audit.completed_state ==
              DB_LINUX_ARM64_WX_ROLLBACK_PENDING);
        CHECK(audit.primary_status == DB_LINUX_ARM64_WX_COPY_FAILED);

        context.force_unmap_fail = 0u;
        CHECK(db_linux_arm64_wx_release(&platform, &handle, &audit) ==
              DB_LINUX_ARM64_WX_OK);
        CHECK(handle.state == DB_LINUX_ARM64_WX_EMPTY);
        CHECK(handle.mapping == NULL);
        CHECK(context.mapped == 0u);
    } else {
        CHECK(handle.state == DB_LINUX_ARM64_WX_EMPTY);
        CHECK(handle.mapping == NULL);
    }
    return 1;
}

static int test_failure_matrix(void)
{
    CHECK(failure_case(MOCK_FAIL_MAP, 0u,
                       DB_LINUX_ARM64_WX_MAP_FAILED, 0u));
    CHECK(failure_case(MOCK_FAIL_COPY, 0u,
                       DB_LINUX_ARM64_WX_COPY_FAILED, 1u));
    CHECK(failure_case(MOCK_FAIL_CORRUPT, 0u,
                       DB_LINUX_ARM64_WX_COPY_MISMATCH, 1u));
    CHECK(failure_case(MOCK_FAIL_PROTECT, 0u,
                       DB_LINUX_ARM64_WX_PROTECT_FAILED, 1u));
    CHECK(failure_case(MOCK_FAIL_CACHE, 0u,
                       DB_LINUX_ARM64_WX_ICACHE_FAILED, 1u));
    CHECK(failure_case(MOCK_FAIL_COPY, 1u,
                       DB_LINUX_ARM64_WX_ROLLBACK_FAILED, 0u));
    return 1;
}

static int test_plan_guards(void)
{
    static const uint8_t image[8] = {0u};
    mock_context context;
    db_linux_arm64_wx_platform platform;
    db_linux_arm64_wx_handle handle;
    db_linux_arm64_wx_audit audit;
    db_arm64_code_image_plan plan;
    union handle_audit_alias {
        db_linux_arm64_wx_handle handle;
        db_linux_arm64_wx_audit audit;
    } alias;

    reset_context(&context, MOCK_FAIL_NONE);
    platform = platform_for(&context);
    plan = valid_plan((uint64_t)(uintptr_t)mock_page, sizeof(image));
    db_linux_arm64_wx_handle_init(&handle);

    plan.final_permissions |= DB_ARM64_CODE_PERMISSION_WRITE;
    CHECK(db_linux_arm64_wx_publish(&platform, &plan,
                                    image, sizeof(image),
                                    &handle, &audit) ==
          DB_LINUX_ARM64_WX_BAD_PLAN);
    CHECK(context.map_calls == 0u);

    plan = valid_plan((uint64_t)(uintptr_t)mock_page, sizeof(image));
    plan.allocation_size = 8192u;
    plan.allocation_end_address = plan.code_base + 8192u - 1u;
    CHECK(db_linux_arm64_wx_publish(&platform, &plan,
                                    image, sizeof(image),
                                    &handle, &audit) ==
          DB_LINUX_ARM64_WX_BAD_PLAN);
    CHECK(context.map_calls == 0u);

    plan = valid_plan((uint64_t)(uintptr_t)mock_page, sizeof(image));
    plan.instruction_cache_offset = 4u;
    plan.instruction_cache_size = 4u;
    CHECK(db_linux_arm64_wx_publish(&platform, &plan,
                                    image, sizeof(image),
                                    &handle, &audit) ==
          DB_LINUX_ARM64_WX_BAD_PLAN);
    CHECK(context.map_calls == 0u);

    plan = valid_plan((uint64_t)(uintptr_t)mock_page, sizeof(image));
    CHECK(db_linux_arm64_wx_publish(&platform, &plan,
                                    image, sizeof(image) - 1u,
                                    &handle, &audit) ==
          DB_LINUX_ARM64_WX_BAD_IMAGE_SIZE);
    CHECK(context.map_calls == 0u);

    fill(&alias, sizeof(alias), 0u);
    db_linux_arm64_wx_handle_init(&alias.handle);
    CHECK(db_linux_arm64_wx_publish(
              &platform, &plan, image, sizeof(image),
              &alias.handle, &alias.audit) ==
          DB_LINUX_ARM64_WX_BUFFER_OVERLAP);
    CHECK(context.map_calls == 0u);

    reset_context(&context, MOCK_FAIL_NONE);
    platform = platform_for(&context);
    plan = valid_plan((uint64_t)(uintptr_t)mock_page, sizeof(image));
    {
        db_linux_arm64_wx_handle *mapped_handle =
            (db_linux_arm64_wx_handle *)(void *)mock_page;
        db_linux_arm64_wx_handle_init(mapped_handle);
        CHECK(db_linux_arm64_wx_publish(
                  &platform, &plan, image, sizeof(image),
                  mapped_handle, &audit) ==
              DB_LINUX_ARM64_WX_BUFFER_OVERLAP);
        CHECK(context.map_calls == 0u);
    }

    reset_context(&context, MOCK_FAIL_NONE);
    platform = platform_for(&context);
    plan = valid_plan((uint64_t)(uintptr_t)mock_page, sizeof(image));
    db_linux_arm64_wx_handle_init(&handle);
    CHECK(db_linux_arm64_wx_publish(
              &platform, &plan, mock_page, sizeof(image),
              &handle, &audit) ==
          DB_LINUX_ARM64_WX_BUFFER_OVERLAP);
    CHECK(context.map_calls == 0u);

    platform.reserved[0] = 1u;
    CHECK(db_linux_arm64_wx_publish(&platform, &plan,
                                    image, sizeof(image),
                                    &handle, &audit) ==
          DB_LINUX_ARM64_WX_BAD_PLATFORM);
    return 1;
}

int main(void)
{
    static const char pass[] =
        "LINUX ARM64 W^X MOCK TEST: PASS\n";

    if (!test_success_and_release()) return 11;
    if (!test_failure_matrix()) return 12;
    if (!test_plan_guards()) return 13;
    if (db_linux_arm64_wx_status_name(DB_LINUX_ARM64_WX_OK)[0] != 'o')
        return 14;

    (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
    return 0;
}
