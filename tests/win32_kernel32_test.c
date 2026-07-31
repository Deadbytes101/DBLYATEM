#include "dblyat/win32_kernel32.h"

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

static db_win32_kernel32_state make_state(void)
{
    db_win32_kernel32_state state;

    state.process_id = 0x1234u;
    state.thread_id = 0x5678u;
    state.last_error = 5u;
    state.reserved = 0u;
    return state;
}

static int preserved(db_win32_kernel32_state *state,
                     uint32_t handler_id,
                     const uint64_t *arguments,
                     size_t argument_count,
                     db_win32_kernel32_status expected)
{
    db_win32_kernel32_state state_before = *state;
    db_win32_kernel32_result result;
    db_win32_kernel32_result result_before;

    fill(&result, sizeof(result), UINT8_C(0xa5));
    result_before = result;

    return db_win32_kernel32_execute(state,
                                     handler_id,
                                     arguments,
                                     argument_count,
                                     &result) == expected &&
           same(state, &state_before, sizeof(*state)) &&
           same(&result, &result_before, sizeof(result));
}

static int test_getters(void)
{
    db_win32_kernel32_state state = make_state();
    db_win32_kernel32_state before = state;
    db_win32_kernel32_result result;

    CHECK(db_win32_kernel32_execute(
              &state,
              DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_PROCESS_ID,
              NULL,
              0u,
              &result) == DB_WIN32_KERNEL32_OK);
    CHECK(result.raw_result == state.process_id);
    CHECK(result.handler_id ==
          DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_PROCESS_ID);
    CHECK(result.reserved == 0u);
    CHECK(same(&state, &before, sizeof(state)));

    CHECK(db_win32_kernel32_execute(
              &state,
              DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_THREAD_ID,
              NULL,
              0u,
              &result) == DB_WIN32_KERNEL32_OK);
    CHECK(result.raw_result == state.thread_id);
    CHECK(result.handler_id ==
          DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_THREAD_ID);
    CHECK(same(&state, &before, sizeof(state)));

    CHECK(db_win32_kernel32_execute(
              &state,
              DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR,
              NULL,
              0u,
              &result) == DB_WIN32_KERNEL32_OK);
    CHECK(result.raw_result == 5u);
    CHECK(result.handler_id ==
          DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR);
    CHECK(same(&state, &before, sizeof(state)));
    return 1;
}

static int test_set_last_error(void)
{
    db_win32_kernel32_state state = make_state();
    db_win32_kernel32_result result;
    uint64_t argument = UINT64_C(0xdeadbeef);

    CHECK(db_win32_kernel32_execute(
              &state,
              DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
              &argument,
              1u,
              &result) == DB_WIN32_KERNEL32_OK);
    CHECK(state.process_id == 0x1234u);
    CHECK(state.thread_id == 0x5678u);
    CHECK(state.last_error == UINT32_C(0xdeadbeef));
    CHECK(state.reserved == 0u);
    CHECK(result.raw_result == 0u);
    CHECK(result.handler_id == DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR);
    CHECK(result.reserved == 0u);

    CHECK(db_win32_kernel32_execute(
              &state,
              DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR,
              NULL,
              0u,
              &result) == DB_WIN32_KERNEL32_OK);
    CHECK(result.raw_result == UINT32_C(0xdeadbeef));
    return 1;
}

static int test_failures(void)
{
    db_win32_kernel32_state state = make_state();
    uint64_t argument = UINT64_C(0x100000000);
    uint64_t extra[2] = {1u, 2u};

    CHECK(preserved(&state,
                    UINT32_C(0xffffffff),
                    NULL,
                    0u,
                    DB_WIN32_KERNEL32_BAD_HANDLER));

    CHECK(preserved(&state,
                    DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR,
                    &extra[0],
                    1u,
                    DB_WIN32_KERNEL32_ARGUMENT_COUNT_MISMATCH));

    CHECK(preserved(&state,
                    DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                    NULL,
                    0u,
                    DB_WIN32_KERNEL32_ARGUMENT_COUNT_MISMATCH));

    CHECK(preserved(&state,
                    DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                    &extra[0],
                    2u,
                    DB_WIN32_KERNEL32_ARGUMENT_COUNT_MISMATCH));

    CHECK(preserved(&state,
                    DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
                    &argument,
                    1u,
                    DB_WIN32_KERNEL32_ARGUMENT_OUT_OF_RANGE));

    state.process_id = 0u;
    CHECK(preserved(&state,
                    DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_PROCESS_ID,
                    NULL,
                    0u,
                    DB_WIN32_KERNEL32_BAD_STATE));

    state = make_state();
    state.thread_id = 0u;
    CHECK(preserved(&state,
                    DB_WIN32_HANDLER_KERNEL32_GET_CURRENT_THREAD_ID,
                    NULL,
                    0u,
                    DB_WIN32_KERNEL32_BAD_STATE));

    state = make_state();
    state.reserved = 1u;
    CHECK(preserved(&state,
                    DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR,
                    NULL,
                    0u,
                    DB_WIN32_KERNEL32_BAD_STATE));
    return 1;
}

static int test_guards(void)
{
    db_win32_kernel32_state state = make_state();
    db_win32_kernel32_result result;
    union state_result_alias {
        db_win32_kernel32_state state;
        db_win32_kernel32_result result;
    } state_result;
    union state_argument_alias {
        db_win32_kernel32_state state;
        uint64_t argument;
    } state_argument;

    state_result.state = state;
    CHECK(db_win32_kernel32_execute(
              &state_result.state,
              DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR,
              NULL,
              0u,
              &state_result.result) == DB_WIN32_KERNEL32_BUFFER_OVERLAP);

    state_argument.state = state;
    CHECK(db_win32_kernel32_execute(
              &state_argument.state,
              DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
              &state_argument.argument,
              1u,
              &result) == DB_WIN32_KERNEL32_BUFFER_OVERLAP);

    CHECK(db_win32_kernel32_execute(
              NULL,
              DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR,
              NULL,
              0u,
              &result) == DB_WIN32_KERNEL32_NULL_ARGUMENT);
    CHECK(db_win32_kernel32_execute(
              &state,
              DB_WIN32_HANDLER_KERNEL32_SET_LAST_ERROR,
              NULL,
              1u,
              &result) == DB_WIN32_KERNEL32_NULL_ARGUMENT);
    CHECK(db_win32_kernel32_execute(
              &state,
              DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR,
              NULL,
              0u,
              NULL) == DB_WIN32_KERNEL32_NULL_ARGUMENT);

    CHECK(preserved(&state,
                    DB_WIN32_HANDLER_KERNEL32_GET_LAST_ERROR,
                    (const uint64_t *)&state,
                    SIZE_MAX / sizeof(uint64_t) + 1u,
                    DB_WIN32_KERNEL32_COUNT_OVERFLOW));
    return 1;
}

static int test_names(void)
{
    CHECK(db_win32_kernel32_status_name(DB_WIN32_KERNEL32_OK)[0] == 'o');
    CHECK(db_win32_kernel32_status_name(DB_WIN32_KERNEL32_BAD_HANDLER)[0] == 'b');
    CHECK(db_win32_kernel32_status_name((db_win32_kernel32_status)99)[0] == 'u');
    return 1;
}

int main(void)
{
    static const char pass[] = "WIN32 KERNEL32 TEST: PASS\n";
    static const char fail[] = "WIN32 KERNEL32 TEST: FAIL\n";

    if (test_getters() &&
        test_set_last_error() &&
        test_failures() &&
        test_guards() &&
        test_names()) {
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
        return 0;
    }

    (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
    return 1;
}
