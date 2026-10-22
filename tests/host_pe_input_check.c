#define _POSIX_C_SOURCE 200809L

#include "dblyat/host_pe_input.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        (void)fprintf(stderr, "HOST PE INPUT CHECK: FAIL line=%d\n", __LINE__); \
        return 1; \
    } \
} while (0)

int main(void)
{
    char path[] = "/tmp/dblyatem-pe-input-XXXXXX";
    char link_path[sizeof(path) + 8u];
    const unsigned char sample[] = {0x4du, 0x5au, 0x90u, 0x00u};
    db_host_pe_input input;
    int fd;
    int count;

    CHECK(db_host_pe_input_open(NULL, 4u, NULL) ==
          DB_HOST_PE_INPUT_NULL_ARGUMENT);
    CHECK(db_host_pe_input_close(NULL) ==
          DB_HOST_PE_INPUT_NULL_ARGUMENT);

    db_host_pe_input_init(&input);
    CHECK(db_host_pe_input_open("/path/that/does/not/exist", 4u, &input) ==
          DB_HOST_PE_INPUT_OPEN_FAILED);
    CHECK(db_host_pe_input_open("/tmp", 4u, &input) ==
          DB_HOST_PE_INPUT_INVALID_FILE);

    fd = mkstemp(path);
    CHECK(fd >= 0);
    CHECK(db_host_pe_input_open(path, 4u, &input) ==
          DB_HOST_PE_INPUT_INVALID_FILE);
    CHECK(write(fd, sample, sizeof(sample)) == (ssize_t)sizeof(sample));
    CHECK(close(fd) == 0);

    CHECK(db_host_pe_input_open(path, 0u, &input) ==
          DB_HOST_PE_INPUT_BAD_LIMIT);
    CHECK(db_host_pe_input_open(path, 3u, &input) ==
          DB_HOST_PE_INPUT_TOO_LARGE);
    CHECK(input.data == NULL && input.size == 0u);
    (void)puts("HOST PE INPUT BOUNDS: PASS");

    CHECK(db_host_pe_input_open(path, sizeof(sample), &input) ==
          DB_HOST_PE_INPUT_OK);
    CHECK(input.data != NULL && input.size == sizeof(sample));
    CHECK(memcmp(input.data, sample, sizeof(sample)) == 0);
    CHECK(db_host_pe_input_open(path, sizeof(sample), &input) ==
          DB_HOST_PE_INPUT_BAD_OWNER);
    CHECK(db_host_pe_input_close(&input) == DB_HOST_PE_INPUT_OK);
    CHECK(input.data == NULL && input.size == 0u);
    CHECK(db_host_pe_input_close(&input) == DB_HOST_PE_INPUT_OK);
    input.size = 1u;
    CHECK(db_host_pe_input_close(&input) == DB_HOST_PE_INPUT_BAD_OWNER);
    db_host_pe_input_init(&input);
    (void)puts("HOST PE INPUT OWNER LIFECYCLE: PASS");

    count = snprintf(link_path, sizeof(link_path), "%s.link", path);
    CHECK(count > 0 && (size_t)count < sizeof(link_path));
    CHECK(symlink(path, link_path) == 0);
    CHECK(db_host_pe_input_open(link_path, sizeof(sample), &input) ==
          DB_HOST_PE_INPUT_OK);
    CHECK(input.size == sizeof(sample));
    CHECK(db_host_pe_input_close(&input) == DB_HOST_PE_INPUT_OK);
    CHECK(unlink(link_path) == 0);
    CHECK(unlink(path) == 0);
    (void)puts("HOST PE INPUT SYMLINK COMPATIBILITY: PASS");

    (void)puts("HOST PE INPUT CHECK: PASS");
    return 0;
}
