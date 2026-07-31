#include "dblyat/gateway_roundtrip.h"

#include <stdint.h>
#include <unistd.h>

#define BASE UINT64_C(0x10000)
#define SLOT UINT64_C(64)
#define STACK_BASE UINT64_C(0x70000000)
#define CHECK(v) do { if (!(v)) return 0; } while (0)

typedef struct fixture {
    db_iat_target targets[2];
    db_gateway_dispatch_handler handlers[2];
    db_gateway_frame_signature signatures[2];
    db_gateway_x64_guest_state state;
    uint8_t stack[128];
    uint64_t arguments[8];
    db_gateway_roundtrip_summary summary;
} fixture;

static void fill(void *data, size_t size, uint8_t value)
{
    uint8_t *p = (uint8_t *)data;
    size_t i;
    for (i = 0u; i < size; ++i) p[i] = value;
}

static int same(const void *left, const void *right, size_t size)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    size_t i;
    for (i = 0u; i < size; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static void put64(uint8_t *p, size_t offset, uint64_t value)
{
    size_t i;
    for (i = 0u; i < 8u; ++i) p[offset + i] = (uint8_t)(value >> (i * 8u));
}

static void valid(fixture *v)
{
    fill(v, sizeof(*v), 0u);
    v->targets[0] = (db_iat_target){1u, 11u, BASE};
    v->targets[1] = (db_iat_target){2u, 22u, BASE + SLOT};
    v->handlers[0] = (db_gateway_dispatch_handler){1u, 11u, 101u, 0u};
    v->handlers[1] = (db_gateway_dispatch_handler){2u, 22u, 202u, 0u};
    v->signatures[0] = (db_gateway_frame_signature){101u, 2u, 0u};
    v->signatures[1] = (db_gateway_frame_signature){202u, 6u, 0u};
    v->state.rax = UINT64_C(0xaaaa);
    v->state.rcx = 0x11u; v->state.rdx = 0x22u;
    v->state.r8 = 0x33u; v->state.r9 = 0x44u;
    v->state.rsp = STACK_BASE + 16u;
    v->state.rip = BASE + SLOT;
    fill(v->stack, sizeof(v->stack), UINT8_C(0x5a));
    put64(v->stack, 16u, UINT64_C(0x401000));
    put64(v->stack, 16u + 0x28u, 0x55u);
    put64(v->stack, 16u + 0x30u, 0x66u);
    fill(v->arguments, sizeof(v->arguments), UINT8_C(0xa5));
    fill(&v->summary, sizeof(v->summary), UINT8_C(0xa5));
}

static db_gateway_roundtrip_status run(fixture *v, size_t capacity,
                                       uint64_t result)
{
    return db_gateway_roundtrip_raw(v->targets, 2u, BASE, SLOT,
                                    v->handlers, 2u, v->signatures, 2u,
                                    &v->state, v->stack, sizeof(v->stack),
                                    STACK_BASE, result, v->arguments, capacity,
                                    &v->summary);
}

static int preserved(fixture *v, size_t capacity, uint64_t result,
                     db_gateway_roundtrip_status status)
{
    fixture before = *v;
    return run(v, capacity, result) == status && same(&before, v, sizeof(before));
}

static int test_success(void)
{
    fixture v;
    const uint64_t result = UINT64_C(0xdeadbeefcafebabe);
    uint8_t stack[128];
    valid(&v); { size_t i; for (i = 0u; i < 128u; ++i) stack[i] = v.stack[i]; }
    CHECK(run(&v, 8u, result) == DB_GATEWAY_ROUNDTRIP_OK);
    CHECK(v.arguments[0] == 0x11u && v.arguments[1] == 0x22u);
    CHECK(v.arguments[2] == 0x33u && v.arguments[3] == 0x44u);
    CHECK(v.arguments[4] == 0x55u && v.arguments[5] == 0x66u);
    CHECK(v.state.rax == result && v.state.rsp == STACK_BASE + 24u);
    CHECK(v.state.rip == UINT64_C(0x401000));
    CHECK(v.summary.enter.dispatch.handler_id == 202u);
    CHECK(v.summary.enter.frame.argument_count == 6u);
    CHECK(v.summary.complete.return_plan.rax == result);
    CHECK(v.summary.complete.apply_summary.previous_rax == UINT64_C(0xaaaa));
    CHECK(same(stack, v.stack, sizeof(stack)));
    return 1;
}

static int test_failures(void)
{
    fixture v;
    valid(&v); v.state.rip = BASE - 1u;
    CHECK(preserved(&v, 8u, 1u, DB_GATEWAY_ROUNDTRIP_ENTRY_FAILED));
    valid(&v);
    CHECK(preserved(&v, 5u, 1u,
                    DB_GATEWAY_ROUNDTRIP_ARGUMENT_BUFFER_TOO_SMALL));
    valid(&v); put64(v.stack, 16u, 0u);
    CHECK(preserved(&v, 8u, 1u,
                    DB_GATEWAY_ROUNDTRIP_COMPLETION_FAILED));
    return 1;
}

static int test_guards(void)
{
    fixture v;
    union storage { db_iat_target targets[2]; uint64_t arguments[4]; } u;
    db_iat_target one;
    valid(&v); u.targets[0] = v.targets[0]; u.targets[1] = v.targets[1];
    CHECK(db_gateway_roundtrip_raw(u.targets, 2u, BASE, SLOT,
                                   v.handlers, 2u, v.signatures, 2u,
                                   &v.state, v.stack, sizeof(v.stack), STACK_BASE,
                                   1u, u.arguments, 4u, &v.summary) ==
          DB_GATEWAY_ROUNDTRIP_BUFFER_OVERLAP);
    CHECK(db_gateway_roundtrip_raw(NULL, 2u, BASE, SLOT,
                                   v.handlers, 2u, v.signatures, 2u,
                                   &v.state, v.stack, sizeof(v.stack), STACK_BASE,
                                   1u, v.arguments, 8u, &v.summary) ==
          DB_GATEWAY_ROUNDTRIP_NULL_ARGUMENT);
    one = v.targets[0];
    CHECK(db_gateway_roundtrip_raw(&one, SIZE_MAX, BASE, SLOT,
                                   v.handlers, 2u, v.signatures, 2u,
                                   &v.state, v.stack, sizeof(v.stack), STACK_BASE,
                                   1u, v.arguments, 8u, &v.summary) ==
          DB_GATEWAY_ROUNDTRIP_COUNT_OVERFLOW);
    CHECK(db_gateway_roundtrip_status_name(DB_GATEWAY_ROUNDTRIP_OK)[0] == 'o');
    CHECK(db_gateway_roundtrip_status_name((db_gateway_roundtrip_status)999)[0] == 'u');
    return 1;
}

int main(void)
{
    if (!test_success() || !test_failures() || !test_guards()) {
        static const char fail[] = "GATEWAY ROUNDTRIP TEST: FAIL\n";
        (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
        return 1;
    }
    {
        static const char pass[] = "GATEWAY ROUNDTRIP TEST: PASS\n";
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
    }
    return 0;
}
