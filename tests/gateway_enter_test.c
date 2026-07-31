#include "dblyat/gateway_enter.h"

#include <stdint.h>
#include <unistd.h>

#define BASE UINT64_C(0x10000)
#define SLOT UINT64_C(64)
#define STACK_BASE UINT64_C(0x70000000)
#define STACK_SIZE 128u
#define CHECK(v) do { if (!(v)) return 0; } while (0)

typedef struct fixture {
    db_iat_target targets[2];
    db_gateway_dispatch_handler handlers[2];
    db_gateway_frame_signature signatures[2];
    db_gateway_x64_guest_state state;
    uint8_t stack[STACK_SIZE];
    uint64_t arguments[8];
    db_gateway_enter_summary summary;
} fixture;

static void fill(void *data, size_t size, uint8_t value)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t i;
    for (i = 0u; i < size; ++i) bytes[i] = value;
}

static int equal(const void *a, const void *b, size_t size)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    size_t i;
    for (i = 0u; i < size; ++i) if (x[i] != y[i]) return 0;
    return 1;
}

static void put_u64(uint8_t *bytes, size_t offset, uint64_t value)
{
    size_t i;
    for (i = 0u; i < 8u; ++i) bytes[offset + i] = (uint8_t)(value >> (i * 8u));
}

static void make_valid(fixture *v)
{
    fill(v, sizeof(*v), 0u);
    v->targets[0] = (db_iat_target){1u, 11u, BASE};
    v->targets[1] = (db_iat_target){2u, 22u, BASE + SLOT};
    v->handlers[0] = (db_gateway_dispatch_handler){1u, 11u, 101u, 0u};
    v->handlers[1] = (db_gateway_dispatch_handler){2u, 22u, 202u, 0u};
    v->signatures[0] = (db_gateway_frame_signature){101u, 2u, 0u};
    v->signatures[1] = (db_gateway_frame_signature){202u, 6u, 0u};
    v->state.rcx = UINT64_C(0x11);
    v->state.rdx = UINT64_C(0x22);
    v->state.r8 = UINT64_C(0x33);
    v->state.r9 = UINT64_C(0x44);
    v->state.rsp = STACK_BASE + 16u;
    v->state.rip = BASE + SLOT;
    fill(v->stack, sizeof(v->stack), UINT8_C(0x5a));
    put_u64(v->stack, 16u, UINT64_C(0x401000));
    put_u64(v->stack, 16u + 0x28u, UINT64_C(0x55));
    put_u64(v->stack, 16u + 0x30u, UINT64_C(0x66));
    fill(v->arguments, sizeof(v->arguments), UINT8_C(0xa5));
    fill(&v->summary, sizeof(v->summary), UINT8_C(0xa5));
}

static db_gateway_enter_status run(fixture *v, size_t capacity)
{
    return db_gateway_enter_plan(v->targets, 2u, BASE, SLOT,
                                 v->handlers, 2u,
                                 v->signatures, 2u,
                                 &v->state,
                                 v->stack, sizeof(v->stack), STACK_BASE,
                                 v->arguments, capacity, &v->summary);
}

static int preserved(fixture *v, size_t capacity, db_gateway_enter_status expected)
{
    uint64_t arguments[8];
    db_gateway_enter_summary summary = v->summary;
    db_gateway_x64_guest_state state = v->state;
    uint8_t stack[STACK_SIZE];
    size_t i;
    for (i = 0u; i < 8u; ++i) arguments[i] = v->arguments[i];
    for (i = 0u; i < STACK_SIZE; ++i) stack[i] = v->stack[i];
    return run(v, capacity) == expected &&
           equal(arguments, v->arguments, sizeof(arguments)) &&
           equal(&summary, &v->summary, sizeof(summary)) &&
           equal(&state, &v->state, sizeof(state)) &&
           equal(stack, v->stack, sizeof(stack));
}

static int test_success(void)
{
    fixture v;
    db_gateway_x64_guest_state state;
    uint8_t stack[STACK_SIZE];
    make_valid(&v);
    state = v.state;
    fill(stack, sizeof(stack), 0u);
    { size_t i; for (i = 0u; i < STACK_SIZE; ++i) stack[i] = v.stack[i]; }
    CHECK(run(&v, 8u) == DB_GATEWAY_ENTER_OK);
    CHECK(v.arguments[0] == UINT64_C(0x11));
    CHECK(v.arguments[1] == UINT64_C(0x22));
    CHECK(v.arguments[2] == UINT64_C(0x33));
    CHECK(v.arguments[3] == UINT64_C(0x44));
    CHECK(v.arguments[4] == UINT64_C(0x55));
    CHECK(v.arguments[5] == UINT64_C(0x66));
    CHECK(v.summary.lookup.kind == DB_GATEWAY_LOOKUP_ENTRY);
    CHECK(v.summary.lookup.slot_index == 1u);
    CHECK(v.summary.dispatch.handler_id == 202u);
    CHECK(v.summary.frame.argument_count == 6u);
    CHECK(v.summary.frame.return_address == UINT64_C(0x401000));
    CHECK(v.summary.frame.provider_id == 2u && v.summary.frame.symbol_id == 22u);
    CHECK(equal(&state, &v.state, sizeof(state)));
    CHECK(equal(stack, v.stack, sizeof(stack)));
    return 1;
}

static int test_failures(void)
{
    fixture v;
    make_valid(&v); v.state.rip = BASE - 1u;
    CHECK(preserved(&v, 8u, DB_GATEWAY_ENTER_NOT_ENTRY));
    make_valid(&v); v.state.rip = BASE + SLOT + 1u;
    CHECK(preserved(&v, 8u, DB_GATEWAY_ENTER_NOT_ENTRY));
    make_valid(&v); v.targets[1].provider_id = 1u; v.targets[1].symbol_id = 11u;
    CHECK(preserved(&v, 8u, DB_GATEWAY_ENTER_LOOKUP_FAILED));
    make_valid(&v); v.handlers[1].handler_id = 101u;
    CHECK(preserved(&v, 8u, DB_GATEWAY_ENTER_DISPATCH_FAILED));
    make_valid(&v); v.signatures[1].handler_id = 101u;
    CHECK(preserved(&v, 8u, DB_GATEWAY_ENTER_FRAME_FAILED));
    make_valid(&v); CHECK(preserved(&v, 5u, DB_GATEWAY_ENTER_FRAME_FAILED));
    make_valid(&v); v.state.rsp = STACK_BASE + STACK_SIZE - 4u;
    CHECK(preserved(&v, 8u, DB_GATEWAY_ENTER_FRAME_FAILED));
    return 1;
}

static int test_guards(void)
{
    fixture v;
    union alias { db_iat_target targets[2]; uint64_t arguments[4]; } alias;
    db_iat_target one;
    make_valid(&v);
    alias.targets[0] = v.targets[0]; alias.targets[1] = v.targets[1];
    CHECK(db_gateway_enter_plan(alias.targets, 2u, BASE, SLOT,
                                v.handlers, 2u, v.signatures, 2u,
                                &v.state, v.stack, sizeof(v.stack), STACK_BASE,
                                alias.arguments, 4u, &v.summary) ==
          DB_GATEWAY_ENTER_BUFFER_OVERLAP);
    CHECK(db_gateway_enter_plan(NULL, 2u, BASE, SLOT,
                                v.handlers, 2u, v.signatures, 2u,
                                &v.state, v.stack, sizeof(v.stack), STACK_BASE,
                                v.arguments, 8u, &v.summary) ==
          DB_GATEWAY_ENTER_NULL_ARGUMENT);
    one = v.targets[0];
    CHECK(db_gateway_enter_plan(&one, SIZE_MAX, BASE, SLOT,
                                v.handlers, 2u, v.signatures, 2u,
                                &v.state, v.stack, sizeof(v.stack), STACK_BASE,
                                v.arguments, 8u, &v.summary) ==
          DB_GATEWAY_ENTER_COUNT_OVERFLOW);
    CHECK(db_gateway_enter_status_name(DB_GATEWAY_ENTER_OK)[0] == 'o');
    CHECK(db_gateway_enter_status_name((db_gateway_enter_status)999)[0] == 'u');
    return 1;
}

int main(void)
{
    if (!test_success() || !test_failures() || !test_guards()) {
        static const char fail[] = "GATEWAY ENTER TEST: FAIL\n";
        (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
        return 1;
    }
    {
        static const char pass[] = "GATEWAY ENTER TEST: PASS\n";
        (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
    }
    return 0;
}
