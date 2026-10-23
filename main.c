#define _POSIX_C_SOURCE 200809L

#include "dblyat/host_pe_input.h"
#include "dblyat/host_pe_run.h"
#include "dblyat/pe.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static size_t db_cstr_length(const char *text)
{
    size_t length = 0u;

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

static void db_write_all(int fd, const char *text, size_t length)
{
    while (length != 0u) {
        const ssize_t written = write(fd, text, length);

        if (written > 0) {
            text += (size_t)written;
            length -= (size_t)written;
            continue;
        }

        if (written < 0 && errno == EINTR) {
            continue;
        }

        break;
    }
}

static void db_puts_fd(int fd, const char *text)
{
    db_write_all(fd, text, db_cstr_length(text));
}

static void db_put_u64_hex(int fd, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[18];
    size_t index;

    buffer[0] = '0';
    buffer[1] = 'x';
    for (index = 0u; index < 16u; ++index) {
        const unsigned shift = (unsigned)((15u - index) * 4u);
        buffer[index + 2u] = digits[(value >> shift) & 0x0fu];
    }

    db_write_all(fd, buffer, sizeof(buffer));
}

static void db_put_u32_dec(int fd, uint32_t value)
{
    char buffer[10];
    size_t used = 0u;

    do {
        buffer[used++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);

    while (used != 0u) {
        --used;
        db_write_all(fd, &buffer[used], 1u);
    }
}

static int db_verify_file(const char *path)
{
    db_host_pe_input input;
    db_host_pe_input_status input_status;
    db_host_pe_input_status close_status;
    db_pe_image image;
    db_pe_status status;

    db_host_pe_input_init(&input);
    input_status = db_host_pe_input_open(path, SIZE_MAX, &input);
    if (input_status != DB_HOST_PE_INPUT_OK) {
        switch (input_status) {
        case DB_HOST_PE_INPUT_OPEN_FAILED:
            db_puts_fd(STDERR_FILENO, "DblyatEM: open failed\n");
            break;
        case DB_HOST_PE_INPUT_TOO_LARGE:
            db_puts_fd(STDERR_FILENO, "DblyatEM: input is too large\n");
            break;
        case DB_HOST_PE_INPUT_MAP_FAILED:
            db_puts_fd(STDERR_FILENO, "DblyatEM: mmap failed\n");
            break;
        default:
            db_puts_fd(STDERR_FILENO, "DblyatEM: invalid input size\n");
            break;
        }
        return 2;
    }

    status = db_pe_verify(input.data, input.size, &image);
    close_status = db_host_pe_input_close(&input);
    if (close_status != DB_HOST_PE_INPUT_OK) {
        db_puts_fd(STDERR_FILENO, "DblyatEM: munmap failed\n");
        return 2;
    }

    if (status != DB_PE_OK) {
        db_puts_fd(STDERR_FILENO, "PE VERIFY: FAIL: ");
        db_puts_fd(STDERR_FILENO, db_pe_status_name(status));
        db_puts_fd(STDERR_FILENO, "\n");
        return 1;
    }

    db_puts_fd(STDOUT_FILENO, "PE VERIFY: PASS\n");
    db_puts_fd(STDOUT_FILENO, "machine: amd64\nsections: ");
    db_put_u32_dec(STDOUT_FILENO, image.section_count);
    db_puts_fd(STDOUT_FILENO, "\nentry-rva: ");
    db_put_u64_hex(STDOUT_FILENO, image.entry_rva);
    db_puts_fd(STDOUT_FILENO, "\nimage-base: ");
    db_put_u64_hex(STDOUT_FILENO, image.image_base);
    db_puts_fd(STDOUT_FILENO, "\nimage-size: ");
    db_put_u64_hex(STDOUT_FILENO, image.image_size);
    db_puts_fd(STDOUT_FILENO, "\n");

    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "run") == 0) {
        return db_host_pe_run_file(argv[2]);
    }
    if (argc != 3 || argv[1][0] != 'v' || argv[1][1] != 'e' ||
        argv[1][2] != 'r' || argv[1][3] != 'i' || argv[1][4] != 'f' ||
        argv[1][5] != 'y' || argv[1][6] != '\0') {
        db_puts_fd(STDERR_FILENO, "usage: dblyatem verify FILE.exe\n");
        return 2;
    }

    return db_verify_file(argv[2]);
}
