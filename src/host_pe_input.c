#define _POSIX_C_SOURCE 200809L

#include "dblyat/host_pe_input.h"

#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

void db_host_pe_input_init(db_host_pe_input *input)
{
    if (input != NULL) {
        input->data = NULL;
        input->size = 0u;
    }
}

db_host_pe_input_status db_host_pe_input_open(
    const char *path,
    size_t max_bytes,
    db_host_pe_input *input)
{
    struct stat before;
    struct stat after;
    void *mapping;
    size_t size;
    int fd;

    if (path == NULL || input == NULL) {
        return DB_HOST_PE_INPUT_NULL_ARGUMENT;
    }
    if (input->data != NULL || input->size != 0u) {
        return DB_HOST_PE_INPUT_BAD_OWNER;
    }
    if (max_bytes == 0u) {
        return DB_HOST_PE_INPUT_BAD_LIMIT;
    }

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return DB_HOST_PE_INPUT_OPEN_FAILED;
    }
    if (fstat(fd, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_size <= 0) {
        (void)close(fd);
        return DB_HOST_PE_INPUT_INVALID_FILE;
    }
    if ((uintmax_t)before.st_size > (uintmax_t)SIZE_MAX ||
        (uintmax_t)before.st_size > (uintmax_t)max_bytes) {
        (void)close(fd);
        return DB_HOST_PE_INPUT_TOO_LARGE;
    }

    size = (size_t)before.st_size;
    mapping = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapping == MAP_FAILED) {
        (void)close(fd);
        return DB_HOST_PE_INPUT_MAP_FAILED;
    }

    /* Reject detectable size/identity changes while acquiring the view. */
    if (fstat(fd, &after) != 0 ||
        before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino ||
        before.st_size != after.st_size) {
        (void)munmap(mapping, size);
        (void)close(fd);
        return DB_HOST_PE_INPUT_SOURCE_CHANGED;
    }
    if (close(fd) != 0) {
        (void)munmap(mapping, size);
        return DB_HOST_PE_INPUT_OPEN_FAILED;
    }

    input->data = mapping;
    input->size = size;
    return DB_HOST_PE_INPUT_OK;
}

db_host_pe_input_status db_host_pe_input_close(db_host_pe_input *input)
{
    if (input == NULL) {
        return DB_HOST_PE_INPUT_NULL_ARGUMENT;
    }
    if (input->data == NULL && input->size == 0u) {
        return DB_HOST_PE_INPUT_OK;
    }
    if (input->data == NULL || input->size == 0u) {
        return DB_HOST_PE_INPUT_BAD_OWNER;
    }
    if (munmap((void *)input->data, input->size) != 0) {
        return DB_HOST_PE_INPUT_UNMAP_FAILED;
    }
    input->data = NULL;
    input->size = 0u;
    return DB_HOST_PE_INPUT_OK;
}
