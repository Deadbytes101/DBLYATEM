#ifndef DBLYAT_HOST_PE_INPUT_H
#define DBLYAT_HOST_PE_INPUT_H

#include <stddef.h>
#include <stdint.h>

typedef enum db_host_pe_input_status {
    DB_HOST_PE_INPUT_OK = 0,
    DB_HOST_PE_INPUT_NULL_ARGUMENT,
    DB_HOST_PE_INPUT_BAD_OWNER,
    DB_HOST_PE_INPUT_BAD_LIMIT,
    DB_HOST_PE_INPUT_OPEN_FAILED,
    DB_HOST_PE_INPUT_INVALID_FILE,
    DB_HOST_PE_INPUT_TOO_LARGE,
    DB_HOST_PE_INPUT_MAP_FAILED,
    DB_HOST_PE_INPUT_SOURCE_CHANGED,
    DB_HOST_PE_INPUT_UNMAP_FAILED
} db_host_pe_input_status;

typedef struct db_host_pe_input {
    const void *data;
    size_t size;
    const struct db_host_pe_input *identity;
} db_host_pe_input;

/* Initialize once before open. A copied owner is rejected on open/close. */
void db_host_pe_input_init(db_host_pe_input *input);

/*
 * Borrow a read-only MAP_PRIVATE view of a regular, nonempty host file.
 * Symlinks to regular files are allowed, matching the existing verify CLI.
 * A file truncated after mapping can still cause SIGBUS; callers must not
 * claim that mmap prevents concurrent modification of the source.
 */
db_host_pe_input_status db_host_pe_input_open(
    const char *path,
    size_t max_bytes,
    db_host_pe_input *input);

/* Unmap a live view. Empty initialized owners can be closed repeatedly. */
db_host_pe_input_status db_host_pe_input_close(db_host_pe_input *input);

#endif
