#ifndef DBLYAT_TEST_WRITE_CONTRACT_H
#define DBLYAT_TEST_WRITE_CONTRACT_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <unistd.h>

static inline ssize_t db_test_write_contract(int descriptor,
                                             const void *buffer,
                                             size_t size)
{
    const unsigned char *bytes = (const unsigned char *)buffer;
    size_t offset = 0u;

    if (descriptor != STDOUT_FILENO && descriptor != STDERR_FILENO) {
        return write(descriptor, buffer, size);
    }

#ifdef SSIZE_MAX
    if (size > (size_t)SSIZE_MAX) {
        _exit(2);
    }
#endif

    while (offset < size) {
        const ssize_t written =
            write(descriptor, bytes + offset, size - offset);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            _exit(2);
        }

        if (written == 0 || (size_t)written > size - offset) {
            _exit(2);
        }

        offset += (size_t)written;
    }

    return (ssize_t)offset;
}

#define write(descriptor, buffer, size) \
    db_test_write_contract((descriptor), (buffer), (size))

#endif
