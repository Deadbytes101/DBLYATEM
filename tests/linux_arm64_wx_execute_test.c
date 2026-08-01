#define _GNU_SOURCE
#include "dblyat/linux_arm64_wx_provider.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__aarch64__)
typedef int (*test_entry_fn)(void);

static db_arm64_code_image_plan valid_plan(uint64_t base, size_t page_size)
{
    db_arm64_code_image_plan plan;

    (void)memset(&plan, 0, sizeof(plan));
    plan.code_base = base;
    plan.entry_address = base;
    plan.image_end_address = base + 8u - 1u;
    plan.allocation_end_address = base + page_size - 1u;
    plan.page_size = page_size;
    plan.allocation_size = page_size;
    plan.instruction_cache_size = 8u;
    plan.format_version = DB_ARM64_CODE_IMAGE_FORMAT_VERSION;
    plan.word_count = 2u;
    plan.byte_count = 8u;
    plan.initial_permissions =
        DB_ARM64_CODE_PERMISSION_READ |
        DB_ARM64_CODE_PERMISSION_WRITE;
    plan.final_permissions =
        DB_ARM64_CODE_PERMISSION_READ |
        DB_ARM64_CODE_PERMISSION_EXECUTE;
    return plan;
}
#endif

int main(void)
{
#if !defined(__aarch64__)
    return 77;
#else
    static const uint8_t image[8] = {
        0x40u, 0x05u, 0x80u, 0x52u,
        0xc0u, 0x03u, 0x5fu, 0xd6u
    };
    const db_linux_arm64_wx_platform *platform =
        db_linux_arm64_wx_posix_platform();
    const long page_result = sysconf(_SC_PAGESIZE);
    db_linux_arm64_wx_handle handle;
    db_linux_arm64_wx_audit audit;
    test_entry_fn entry = NULL;
    int attempt;

    _Static_assert(sizeof(entry) == sizeof(void *),
                   "function and object pointers differ");
    if (page_result < 4096) return 11;

    for (attempt = 0; attempt < 64; ++attempt) {
        const size_t page_size = (size_t)page_result;
        void *probe = mmap(NULL, page_size, PROT_NONE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        db_arm64_code_image_plan plan;
        db_linux_arm64_wx_status status;
        int result;

        if (probe == MAP_FAILED) return 12;
        if (munmap(probe, page_size) != 0) return 13;
        plan = valid_plan((uint64_t)(uintptr_t)probe, page_size);
        db_linux_arm64_wx_handle_init(&handle);
        status = db_linux_arm64_wx_publish(platform, &plan,
                                           image, sizeof(image),
                                           &handle, &audit);
        if (status == DB_LINUX_ARM64_WX_MAP_FAILED) {
            continue;
        }
        if (status != DB_LINUX_ARM64_WX_OK) return 14;
        if (handle.state != DB_LINUX_ARM64_WX_PUBLISHED ||
            handle.entry == NULL) return 15;

        (void)memcpy(&entry, &handle.entry, sizeof(entry));
        result = entry();
        if (result != 42) return 16;
        if (db_linux_arm64_wx_release(platform, &handle, &audit) !=
            DB_LINUX_ARM64_WX_OK) return 17;
        (void)write(STDOUT_FILENO,
                    "NATIVE AARCH64 EXECUTION: PASS\n",
                    sizeof("NATIVE AARCH64 EXECUTION: PASS\n") - 1u);
        return 0;
    }
    return 18;
#endif
}
