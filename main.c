static int db_verify_file(const char *path)
{
    struct stat metadata;
    db_pe_image image;
    db_pe_status status;
    void *mapping;
    int fd;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        db_puts_fd(STDERR_FILENO, "DblyatEM: open failed\n");
        return 2;
    }

    if (fstat(fd, &metadata) != 0 || metadata.st_size <= 0) {
        db_puts_fd(STDERR_FILENO, "DblyatEM: invalid input size\n");
        (void)close(fd);
        return 2;
    }

    if ((uintmax_t)metadata.st_size > (uintmax_t)SIZE_MAX) {
        db_puts_fd(STDERR_FILENO, "DblyatEM: input is too large\n");
        (void)close(fd);
        return 2;
    }

    mapping = mmap(NULL, (size_t)metadata.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    (void)close(fd);
    if (mapping == MAP_FAILED) {
        db_puts_fd(STDERR_FILENO, "DblyatEM: mmap failed\n");
        return 2;
    }

    status = db_pe_verify(mapping, (size_t)metadata.st_size, &image);
    (void)munmap(mapping, (size_t)metadata.st_size);

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
    if (argc != 3 || argv[1][0] != 'v' || argv[1][1] != 'e' ||
        argv[1][2] != 'r' || argv[1][3] != 'i' || argv[1][4] != 'f' ||
        argv[1][5] != 'y' || argv[1][6] != '\0') {
        db_puts_fd(STDERR_FILENO, "usage: dblyatem verify FILE.exe\n");
        return 2;
    }

    return db_verify_file(argv[2]);
}
//GODSAYS: FUCKYOUTOO
