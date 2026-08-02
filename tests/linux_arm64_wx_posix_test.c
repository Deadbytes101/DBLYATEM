#define _GNU_SOURCE
#include "dblyat/linux_arm64_wx_provider.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct permission_observation {
    const db_linux_arm64_wx_platform *inner;
    uint8_t rw_observed;
    uint8_t rx_observed;
    uint8_t rw_failed;
    uint8_t rx_failed;
} permission_observation;

static db_arm64_code_image_plan valid_plan(uint64_t base,
                                            size_t image_size,
                                            size_t page_size)
{
    db_arm64_code_image_plan plan;
    const size_t allocation_size =
        (image_size + page_size - 1u) & ~(page_size - 1u);

    (void)memset(&plan, 0, sizeof(plan));
    plan.code_base = base;
    plan.entry_address = base;
    plan.image_end_address = base + image_size - 1u;
    plan.allocation_end_address = base + allocation_size - 1u;
    plan.page_size = page_size;
    plan.allocation_size = allocation_size;
    plan.instruction_cache_offset = 0u;
    plan.instruction_cache_size = image_size;
    plan.format_version = DB_ARM64_CODE_IMAGE_FORMAT_VERSION;
    plan.word_count = (uint32_t)(image_size / 4u);
    plan.byte_count = image_size;
    plan.entry_word_index = 0u;
    plan.initial_permissions =
        DB_ARM64_CODE_PERMISSION_READ |
        DB_ARM64_CODE_PERMISSION_WRITE;
    plan.final_permissions =
        DB_ARM64_CODE_PERMISSION_READ |
        DB_ARM64_CODE_PERMISSION_EXECUTE;
    return plan;
}

static int maps_permissions(void *address,
                            int *readable,
                            int *writable,
                            int *executable)
{
    FILE *file = fopen("/proc/self/maps", "r");
    char line[512];
    const uintptr_t target = (uintptr_t)address;

    if (file == NULL) return 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned long long begin;
        unsigned long long end;
        char permissions[5] = {0};

        if (sscanf(line, "%llx-%llx %4s", &begin, &end, permissions) == 3 &&
            target >= (uintptr_t)begin && target < (uintptr_t)end) {
            *readable = permissions[0] == 'r';
            *writable = permissions[1] == 'w';
            *executable = permissions[2] == 'x';
            (void)fclose(file);
            return 1;
        }
    }
    (void)fclose(file);
    return 0;
}

static int range_permissions(void *mapping,
                             size_t allocation_size,
                             int expected_readable,
                             int expected_writable,
                             int expected_executable)
{
    const long page_result = sysconf(_SC_PAGESIZE);
    size_t offset;

    if (page_result < 4096) return 0;
    for (offset = 0u;
         offset < allocation_size;
         offset += (size_t)page_result) {
        int readable = 0;
        int writable = 0;
        int executable = 0;

        if (!maps_permissions((uint8_t *)mapping + offset,
                              &readable,
                              &writable,
                              &executable) ||
            readable != expected_readable ||
            writable != expected_writable ||
            executable != expected_executable) {
            return 0;
        }
    }
    return 1;
}

static int range_absent(void *mapping, size_t allocation_size)
{
    const long page_result = sysconf(_SC_PAGESIZE);
    size_t offset;

    if (page_result < 4096) return 0;
    for (offset = 0u;
         offset < allocation_size;
         offset += (size_t)page_result) {
        int readable = 0;
        int writable = 0;
        int executable = 0;

        if (maps_permissions((uint8_t *)mapping + offset,
                             &readable,
                             &writable,
                             &executable)) {
            return 0;
        }
    }
    return 1;
}

static void *observed_map_rw(void *opaque,
                             void *requested_address,
                             size_t allocation_size)
{
    permission_observation *observation =
        (permission_observation *)opaque;
    void *mapping = observation->inner->map_rw(
        observation->inner->context,
        requested_address,
        allocation_size);

    if (mapping == NULL) return NULL;
    if (!range_permissions(mapping, allocation_size, 1, 1, 0)) {
        observation->rw_failed = 1u;
        if (observation->inner->unmap(
                observation->inner->context,
                mapping,
                allocation_size) != 0) {
            return mapping;
        }
        return NULL;
    }
    observation->rw_observed = 1u;
    return mapping;
}

static int observed_copy(void *opaque,
                         void *destination,
                         const void *source,
                         size_t byte_count)
{
    permission_observation *observation =
        (permission_observation *)opaque;

    return observation->inner->copy_bytes(
        observation->inner->context,
        destination,
        source,
        byte_count);
}

static int observed_protect_rx(void *opaque,
                               void *mapping,
                               size_t allocation_size)
{
    permission_observation *observation =
        (permission_observation *)opaque;

    if (observation->inner->protect_rx(
            observation->inner->context,
            mapping,
            allocation_size) != 0) {
        return -1;
    }
    if (!range_permissions(mapping, allocation_size, 1, 0, 1)) {
        observation->rx_failed = 1u;
        return -1;
    }
    observation->rx_observed = 1u;
    return 0;
}

static int observed_sync_icache(void *opaque,
                                void *begin,
                                size_t byte_count)
{
    permission_observation *observation =
        (permission_observation *)opaque;

    return observation->inner->sync_icache(
        observation->inner->context,
        begin,
        byte_count);
}

static int observed_unmap(void *opaque,
                          void *mapping,
                          size_t allocation_size)
{
    permission_observation *observation =
        (permission_observation *)opaque;

    return observation->inner->unmap(
        observation->inner->context,
        mapping,
        allocation_size);
}

static db_linux_arm64_wx_platform observed_platform(
    permission_observation *observation)
{
    db_linux_arm64_wx_platform platform;

    (void)memset(&platform, 0, sizeof(platform));
    platform.context = observation;
    platform.map_rw = observed_map_rw;
    platform.copy_bytes = observed_copy;
    platform.protect_rx = observed_protect_rx;
    platform.sync_icache = observed_sync_icache;
    platform.unmap = observed_unmap;
    return platform;
}

static int run_permission_case(const uint8_t *image,
                               size_t image_size,
                               size_t page_size)
{
    const db_linux_arm64_wx_platform *inner =
        db_linux_arm64_wx_posix_platform();
    int attempt;

    for (attempt = 0; attempt < 64; ++attempt) {
        const size_t allocation_size =
            (image_size + page_size - 1u) & ~(page_size - 1u);
        void *probe = mmap(NULL, allocation_size, PROT_NONE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        permission_observation observation;
        db_linux_arm64_wx_platform platform;
        db_arm64_code_image_plan plan;
        db_linux_arm64_wx_handle handle;
        db_linux_arm64_wx_audit audit;
        db_linux_arm64_wx_status status;

        if (probe == MAP_FAILED) return 0;
        if (munmap(probe, allocation_size) != 0) return 0;

        (void)memset(&observation, 0, sizeof(observation));
        observation.inner = inner;
        platform = observed_platform(&observation);
        plan = valid_plan((uint64_t)(uintptr_t)probe,
                          image_size, page_size);
        db_linux_arm64_wx_handle_init(&handle);
        status = db_linux_arm64_wx_publish(&platform, &plan,
                                           image, image_size,
                                           &handle, &audit);
        if (status == DB_LINUX_ARM64_WX_MAP_FAILED &&
            observation.rw_failed == 0u) {
            continue;
        }
        if (observation.rw_failed != 0u ||
            observation.rx_failed != 0u ||
            status != DB_LINUX_ARM64_WX_OK ||
            observation.rw_observed != 1u ||
            observation.rx_observed != 1u ||
            handle.state != DB_LINUX_ARM64_WX_PUBLISHED ||
            memcmp(handle.mapping, image, image_size) != 0 ||
            !range_permissions(handle.mapping,
                               allocation_size,
                               1, 0, 1)) {
            return 0;
        }
        if (db_linux_arm64_wx_release(&platform, &handle, &audit) !=
            DB_LINUX_ARM64_WX_OK) {
            return 0;
        }
        if (!range_absent(probe, allocation_size)) {
            return 0;
        }
        return 1;
    }
    return 0;
}

int main(void)
{
    static const uint8_t one_page_image[8] = {
        0x1fu, 0x20u, 0x03u, 0xd5u,
        0xc0u, 0x03u, 0x5fu, 0xd6u
    };
    static const uint8_t two_page_image[4100] = {0};
    const long page_result = sysconf(_SC_PAGESIZE);

    if (page_result < 4096) return 11;
    if (!run_permission_case(one_page_image,
                             sizeof(one_page_image),
                             (size_t)page_result)) {
        return 12;
    }
    if (!run_permission_case(two_page_image,
                             sizeof(two_page_image),
                             (size_t)page_result)) {
        return 13;
    }

    if (write(STDOUT_FILENO,
              "LINUX W^X PERMISSION OBSERVATION: PASS\n",
              sizeof("LINUX W^X PERMISSION OBSERVATION: PASS\n") - 1u) !=
        (ssize_t)(sizeof(
            "LINUX W^X PERMISSION OBSERVATION: PASS\n") - 1u)) {
        return 14;
    }
    return 0;
}
