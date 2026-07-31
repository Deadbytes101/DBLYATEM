#include "dblyat/x64_memory_view.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define CHECK(value) do { if (!(value)) return 0; } while (0)

static void fill(void *data, size_t size, uint8_t value)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        bytes[index] = value;
    }
}

static int same(const void *left, const void *right, size_t size)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (a[index] != b[index]) {
            return 0;
        }
    }

    return 1;
}

static db_x64_memory_view make_view(uint8_t *bytes, size_t size)
{
    db_x64_memory_view view;

    view.bytes = bytes;
    view.size = size;
    view.guest_base = UINT64_C(0x4000);
    view.permissions = DB_X64_MEMORY_PERMISSION_MASK;
    fill(view.reserved, sizeof(view.reserved), 0u);
    return view;
}

static int test_read_little_endian(void)
{
    uint8_t bytes[32];
    db_x64_memory_view view = make_view(bytes, sizeof(bytes));
    db_x64_memory_access_summary summary;
    uint64_t value = 0u;
    size_t index;

    fill(bytes, sizeof(bytes), 0u);
    for (index = 0u; index < 8u; ++index) {
        bytes[8u + index] = (uint8_t)(index + 1u);
    }

    CHECK(db_x64_memory_view_read_u64(&view,
                                      UINT64_C(0x4008),
                                      &value,
                                      &summary) == DB_X64_MEMORY_VIEW_OK);
    CHECK(value == UINT64_C(0x0807060504030201));
    CHECK(summary.guest_address == UINT64_C(0x4008));
    CHECK(summary.offset == UINT64_C(8));
    CHECK(summary.value == value);
    CHECK(summary.kind == DB_X64_MEMORY_ACCESS_READ_U64);
    CHECK(summary.width == DB_X64_MEMORY_ACCESS_WIDTH);
    CHECK(summary.reserved[0] == 0u &&
          summary.reserved[1] == 0u &&
          summary.reserved[2] == 0u);
    return 1;
}

static int test_write_little_endian(void)
{
    uint8_t bytes[32];
    db_x64_memory_view view = make_view(bytes, sizeof(bytes));
    db_x64_memory_access_summary summary;
    static const uint8_t expected[8] = {
        0x88u, 0x77u, 0x66u, 0x55u, 0x44u, 0x33u, 0x22u, 0x11u
    };

    fill(bytes, sizeof(bytes), UINT8_C(0xa5));
    CHECK(db_x64_memory_view_write_u64(
              &view,
              UINT64_C(0x4010),
              UINT64_C(0x1122334455667788),
              &summary) == DB_X64_MEMORY_VIEW_OK);
    CHECK(same(&bytes[16], expected, sizeof(expected)));
    CHECK(bytes[15] == UINT8_C(0xa5));
    CHECK(bytes[24] == UINT8_C(0xa5));
    CHECK(summary.guest_address == UINT64_C(0x4010));
    CHECK(summary.offset == UINT64_C(16));
    CHECK(summary.value == UINT64_C(0x1122334455667788));
    CHECK(summary.kind == DB_X64_MEMORY_ACCESS_WRITE_U64);
    CHECK(summary.width == DB_X64_MEMORY_ACCESS_WIDTH);
    return 1;
}

static int read_failure_preserved(db_x64_memory_view *view,
                                  uint64_t address,
                                  db_x64_memory_view_status expected)
{
    uint64_t value = UINT64_C(0xaaaaaaaaaaaaaaaa);
    uint64_t value_before = value;
    db_x64_memory_access_summary summary;
    db_x64_memory_access_summary summary_before;

    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    summary_before = summary;
    return db_x64_memory_view_read_u64(view,
                                       address,
                                       &value,
                                       &summary) == expected &&
           value == value_before &&
           same(&summary, &summary_before, sizeof(summary));
}

static int write_failure_preserved(db_x64_memory_view *view,
                                   uint64_t address,
                                   db_x64_memory_view_status expected)
{
    uint8_t before[32];
    db_x64_memory_access_summary summary;
    db_x64_memory_access_summary summary_before;
    size_t index;

    CHECK(view->size <= sizeof(before));
    fill(&summary, sizeof(summary), UINT8_C(0xa5));
    summary_before = summary;
    for (index = 0u; index < view->size; ++index) {
        before[index] = view->bytes[index];
    }

    return db_x64_memory_view_write_u64(
               view,
               address,
               UINT64_C(0x1122334455667788),
               &summary) == expected &&
           same(view->bytes, before, view->size) &&
           same(&summary, &summary_before, sizeof(summary));
}

static int test_bounds_and_permissions(void)
{
    uint8_t bytes[16];
    db_x64_memory_view view = make_view(bytes, sizeof(bytes));
    db_x64_memory_access_summary summary;
    uint64_t value;

    fill(bytes, sizeof(bytes), 0u);
    CHECK(db_x64_memory_view_read_u64(&view,
                                      UINT64_C(0x4008),
                                      &value,
                                      &summary) == DB_X64_MEMORY_VIEW_OK);
    CHECK(read_failure_preserved(&view,
                                 UINT64_C(0x4009),
                                 DB_X64_MEMORY_VIEW_OUT_OF_BOUNDS));
    CHECK(read_failure_preserved(&view,
                                 UINT64_C(0x3fff),
                                 DB_X64_MEMORY_VIEW_OUT_OF_BOUNDS));
    CHECK(write_failure_preserved(&view,
                                  UINT64_C(0x4009),
                                  DB_X64_MEMORY_VIEW_OUT_OF_BOUNDS));

    view.permissions = DB_X64_MEMORY_PERMISSION_WRITE;
    CHECK(read_failure_preserved(&view,
                                 UINT64_C(0x4000),
                                 DB_X64_MEMORY_VIEW_PERMISSION_DENIED));

    view.permissions = DB_X64_MEMORY_PERMISSION_READ;
    CHECK(write_failure_preserved(&view,
                                  UINT64_C(0x4000),
                                  DB_X64_MEMORY_VIEW_PERMISSION_DENIED));
    return 1;
}

static int test_bad_views_and_addresses(void)
{
    uint8_t bytes[16];
    db_x64_memory_view view = make_view(bytes, sizeof(bytes));
    db_x64_memory_view saved;

    fill(bytes, sizeof(bytes), 0u);
    saved = view;
    view.bytes = NULL;
    CHECK(read_failure_preserved(&view,
                                 UINT64_C(0x4000),
                                 DB_X64_MEMORY_VIEW_BAD_VIEW));

    view = saved;
    view.size = 0u;
    CHECK(read_failure_preserved(&view,
                                 UINT64_C(0x4000),
                                 DB_X64_MEMORY_VIEW_BAD_VIEW));

    view = saved;
    view.permissions = 0u;
    CHECK(read_failure_preserved(&view,
                                 UINT64_C(0x4000),
                                 DB_X64_MEMORY_VIEW_BAD_VIEW));

    view = saved;
    view.permissions = UINT8_C(0x80);
    CHECK(read_failure_preserved(&view,
                                 UINT64_C(0x4000),
                                 DB_X64_MEMORY_VIEW_BAD_VIEW));

    view = saved;
    view.reserved[3] = 1u;
    CHECK(read_failure_preserved(&view,
                                 UINT64_C(0x4000),
                                 DB_X64_MEMORY_VIEW_BAD_VIEW));

    view = saved;
    view.guest_base = UINT64_C(0x0000800000000000);
    CHECK(read_failure_preserved(&view,
                                 view.guest_base,
                                 DB_X64_MEMORY_VIEW_BAD_VIEW));

    view = saved;
    CHECK(read_failure_preserved(&view,
                                 UINT64_C(0x0000800000000000),
                                 DB_X64_MEMORY_VIEW_NONCANONICAL_ADDRESS));
    return 1;
}

static int test_zero_and_high_canonical_ranges(void)
{
    uint8_t zero_bytes[8];
    uint8_t high_bytes[8];
    db_x64_memory_view zero_view = make_view(zero_bytes, sizeof(zero_bytes));
    db_x64_memory_view high_view = make_view(high_bytes, sizeof(high_bytes));
    db_x64_memory_access_summary summary;
    uint64_t value;

    fill(zero_bytes, sizeof(zero_bytes), 0u);
    zero_view.guest_base = 0u;
    CHECK(db_x64_memory_view_write_u64(&zero_view,
                                       0u,
                                       UINT64_C(0x1234),
                                       &summary) == DB_X64_MEMORY_VIEW_OK);
    CHECK(db_x64_memory_view_read_u64(&zero_view,
                                      0u,
                                      &value,
                                      &summary) == DB_X64_MEMORY_VIEW_OK);
    CHECK(value == UINT64_C(0x1234));

    fill(high_bytes, sizeof(high_bytes), 0u);
    high_view.guest_base = UINT64_C(0xfffffffffffffff8);
    CHECK(db_x64_memory_view_write_u64(
              &high_view,
              UINT64_C(0xfffffffffffffff8),
              UINT64_C(0x8877665544332211),
              &summary) == DB_X64_MEMORY_VIEW_OK);
    CHECK(db_x64_memory_view_read_u64(
              &high_view,
              UINT64_C(0xfffffffffffffff8),
              &value,
              &summary) == DB_X64_MEMORY_VIEW_OK);
    CHECK(value == UINT64_C(0x8877665544332211));
    return 1;
}

static int test_aliases_and_names(void)
{
    uint8_t bytes[64];
    db_x64_memory_view view = make_view(bytes, sizeof(bytes));
    db_x64_memory_access_summary summary;
    uint64_t value;
    union value_summary_alias {
        uint64_t value;
        db_x64_memory_access_summary summary;
    } value_summary;
    union view_summary_alias {
        db_x64_memory_view view;
        db_x64_memory_access_summary summary;
    } view_summary;

    fill(bytes, sizeof(bytes), 0u);
    CHECK(db_x64_memory_view_read_u64(
              &view,
              UINT64_C(0x4000),
              &value_summary.value,
              &value_summary.summary) == DB_X64_MEMORY_VIEW_BUFFER_OVERLAP);

    view_summary.view = view;
    CHECK(db_x64_memory_view_write_u64(
              &view_summary.view,
              UINT64_C(0x4000),
              0u,
              &view_summary.summary) == DB_X64_MEMORY_VIEW_BUFFER_OVERLAP);

    CHECK(db_x64_memory_view_read_u64(
              &view,
              UINT64_C(0x4000),
              (uint64_t *)&bytes[0],
              &summary) == DB_X64_MEMORY_VIEW_BUFFER_OVERLAP);
    CHECK(db_x64_memory_view_write_u64(
              &view,
              UINT64_C(0x4000),
              0u,
              (db_x64_memory_access_summary *)&bytes[0]) ==
          DB_X64_MEMORY_VIEW_BUFFER_OVERLAP);

    CHECK(db_x64_memory_view_read_u64(NULL,
                                      UINT64_C(0x4000),
                                      &value,
                                      &summary) ==
          DB_X64_MEMORY_VIEW_NULL_ARGUMENT);
    CHECK(db_x64_memory_view_read_u64(&view,
                                      UINT64_C(0x4000),
                                      NULL,
                                      &summary) ==
          DB_X64_MEMORY_VIEW_NULL_ARGUMENT);
    CHECK(db_x64_memory_view_write_u64(&view,
                                       UINT64_C(0x4000),
                                       0u,
                                       NULL) ==
          DB_X64_MEMORY_VIEW_NULL_ARGUMENT);
    CHECK(db_x64_memory_view_status_name(DB_X64_MEMORY_VIEW_OK)[0] == 'o');
    CHECK(db_x64_memory_view_status_name(
              DB_X64_MEMORY_VIEW_PERMISSION_DENIED)[0] == 'p');
    CHECK(db_x64_memory_view_status_name(
              (db_x64_memory_view_status)99)[0] == 'u');
    CHECK(db_x64_memory_access_kind_name(
              DB_X64_MEMORY_ACCESS_READ_U64)[0] == 'r');
    CHECK(db_x64_memory_access_kind_name(
              DB_X64_MEMORY_ACCESS_WRITE_U64)[0] == 'w');
    CHECK(db_x64_memory_access_kind_name(
              (db_x64_memory_access_kind)99)[0] == 'u');
    return 1;
}

int main(void)
{
    static const char pass[] = "X64 MEMORY VIEW TEST: PASS\n";
    static const char fail[] = "X64 MEMORY VIEW TEST: FAIL\n";

    if (test_read_little_endian() &&
        test_write_little_endian() &&
        test_bounds_and_permissions() &&
        test_bad_views_and_addresses() &&
        test_zero_and_high_canonical_ranges() &&
        test_aliases_and_names()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
