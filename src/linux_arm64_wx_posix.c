#define _GNU_SOURCE
#include "dblyat/linux_arm64_wx_provider.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

static void *posix_map_rw(void *context,
                          void *requested_address,
                          size_t allocation_size)
{
    void *mapping;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;

    (void)context;
#ifdef MAP_FIXED_NOREPLACE
    flags |= MAP_FIXED_NOREPLACE;
#endif
    mapping = mmap(requested_address,
                   allocation_size,
                   PROT_READ | PROT_WRITE,
                   flags,
                   -1,
                   0);
    if (mapping == MAP_FAILED) {
        return NULL;
    }
    if (mapping != requested_address) {
        (void)munmap(mapping, allocation_size);
        return NULL;
    }
    return mapping;
}

static int posix_copy(void *context,
                      void *destination,
                      const void *source,
                      size_t byte_count)
{
    (void)context;
    (void)memcpy(destination, source, byte_count);
    return 0;
}

static int posix_protect_rx(void *context,
                            void *mapping,
                            size_t allocation_size)
{
    (void)context;
    return mprotect(mapping,
                    allocation_size,
                    PROT_READ | PROT_EXEC) == 0 ? 0 : -1;
}

static int posix_sync_icache(void *context,
                             void *begin,
                             size_t byte_count)
{
    char *first = (char *)begin;
    char *last = first + byte_count;

    (void)context;
    __builtin___clear_cache(first, last);
    return 0;
}

static int posix_unmap(void *context,
                       void *mapping,
                       size_t allocation_size)
{
    (void)context;
    return munmap(mapping, allocation_size) == 0 ? 0 : -1;
}

const db_linux_arm64_wx_platform *db_linux_arm64_wx_posix_platform(void)
{
    static const db_linux_arm64_wx_platform platform = {
        NULL,
        posix_map_rw,
        posix_copy,
        posix_protect_rx,
        posix_sync_icache,
        posix_unmap,
        {0u, 0u}
    };

    return &platform;
}
