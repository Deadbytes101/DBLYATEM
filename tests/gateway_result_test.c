#include "dblyat/gateway_result.h"

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

static int equal(const void *left, const void *right, size_t size)
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

static void make_valid(db_gateway_invoke_request *request,
                       db_gateway_invoke_result *result)
{
    size_t index;

    fill(request, sizeof(*request), 0u);
    request->rip = UINT64_C(0x10040);
    request->slot_address = request->rip;
    request->guest_rsp = UINT64_C(0x70000020);
    request->return_address = UINT64_C(0x401000);
    request->slot_index = 1u;
    request->argument_count = 6u;
    request->provider_id = 7u;
    request->symbol_id = 9u;
    request->handler_id = 11u;
    for (index = 0u; index < request->argument_count; ++index) {
        request->arguments[index] = UINT64_C(0x1000) + index;
    }

    fill(result, sizeof(*result), 0u);
    result->rip = request->rip;
    result->slot_address = request->slot_address;
    result->raw_result = UINT64_C(0xdeadbeefcafebabe);
    result->slot_index = request->slot_index;
    result->provider_id = request->provider_id;
    result->symbol_id = request->symbol_id;
    result->handler_id = request->handler_id;
}

static int preserved(const db_gateway_invoke_request *request,
                     const db_gateway_invoke_result *result,
                     db_gateway_result_status expected)
{
    db_gateway_validated_result output;
    db_gateway_validated_result before;

    fill(&output, sizeof(output), UINT8_C(0xa5));
    before = output;

    return db_gateway_result_validate(request, result, &output) == expected &&
           equal(&output, &before, sizeof(output));
}

static int test_success(void)
{
    db_gateway_invoke_request request;
    db_gateway_invoke_result result;
    db_gateway_validated_result output;

    make_valid(&request, &result);
    CHECK(db_gateway_result_validate(&request, &result, &output) ==
          DB_GATEWAY_RESULT_OK);
    CHECK(output.rip == request.rip);
    CHECK(output.slot_address == request.slot_address);
    CHECK(output.guest_rsp == request.guest_rsp);
    CHECK(output.return_address == request.return_address);
    CHECK(output.raw_result == result.raw_result);
    CHECK(output.slot_index == request.slot_index);
    CHECK(output.argument_count == request.argument_count);
    CHECK(output.provider_id == request.provider_id);
    CHECK(output.symbol_id == request.symbol_id);
    CHECK(output.handler_id == request.handler_id);
    CHECK(output.reserved == 0u);

    result.raw_result = 0u;
    CHECK(db_gateway_result_validate(&request, &result, &output) ==
          DB_GATEWAY_RESULT_OK);
    CHECK(output.raw_result == 0u);

    result.raw_result = UINT64_MAX;
    CHECK(db_gateway_result_validate(&request, &result, &output) ==
          DB_GATEWAY_RESULT_OK);
    CHECK(output.raw_result == UINT64_MAX);
    return 1;
}

static int test_bad_request(void)
{
    db_gateway_invoke_request request;
    db_gateway_invoke_result result;

    make_valid(&request, &result);
    request.rip = 0u;
    CHECK(preserved(&request, &result, DB_GATEWAY_RESULT_BAD_REQUEST));

    make_valid(&request, &result);
    request.rip = UINT64_C(0x0000800000000000);
    request.slot_address = request.rip;
    result.rip = request.rip;
    result.slot_address = request.rip;
    CHECK(preserved(&request, &result, DB_GATEWAY_RESULT_BAD_REQUEST));

    make_valid(&request, &result);
    request.argument_count = DB_GATEWAY_FRAME_MAX_ARGUMENTS + 1u;
    CHECK(preserved(&request, &result, DB_GATEWAY_RESULT_BAD_REQUEST));

    make_valid(&request, &result);
    request.arguments[request.argument_count] = 1u;
    CHECK(preserved(&request, &result, DB_GATEWAY_RESULT_BAD_REQUEST));

    make_valid(&request, &result);
    request.reserved = 1u;
    CHECK(preserved(&request, &result, DB_GATEWAY_RESULT_BAD_REQUEST));

    make_valid(&request, &result);
    request.handler_id = 0u;
    CHECK(preserved(&request, &result, DB_GATEWAY_RESULT_BAD_REQUEST));
    return 1;
}

static int test_bad_result(void)
{
    db_gateway_invoke_request request;
    db_gateway_invoke_result result;

    make_valid(&request, &result);
    result.rip = 0u;
    CHECK(preserved(&request, &result, DB_GATEWAY_RESULT_BAD_RESULT));

    make_valid(&request, &result);
    result.slot_address += 1u;
    CHECK(preserved(&request, &result, DB_GATEWAY_RESULT_BAD_RESULT));

    make_valid(&request, &result);
    result.slot_index = SIZE_MAX;
    CHECK(preserved(&request, &result, DB_GATEWAY_RESULT_BAD_RESULT));

    make_valid(&request, &result);
    result.provider_id = 0u;
    CHECK(preserved(&request, &result, DB_GATEWAY_RESULT_BAD_RESULT));

    make_valid(&request, &result);
    result.reserved = 1u;
    CHECK(preserved(&request, &result, DB_GATEWAY_RESULT_BAD_RESULT));
    return 1;
}

static int test_mismatches(void)
{
    db_gateway_invoke_request request;
    db_gateway_invoke_result result;

    make_valid(&request, &result);
    result.rip += UINT64_C(0x40);
    result.slot_address = result.rip;
    CHECK(preserved(&request, &result,
                    DB_GATEWAY_RESULT_IDENTITY_MISMATCH));

    make_valid(&request, &result);
    result.slot_index += 1u;
    CHECK(preserved(&request, &result,
                    DB_GATEWAY_RESULT_IDENTITY_MISMATCH));

    make_valid(&request, &result);
    result.provider_id += 1u;
    CHECK(preserved(&request, &result,
                    DB_GATEWAY_RESULT_IDENTITY_MISMATCH));

    make_valid(&request, &result);
    result.symbol_id += 1u;
    CHECK(preserved(&request, &result,
                    DB_GATEWAY_RESULT_IDENTITY_MISMATCH));

    make_valid(&request, &result);
    result.handler_id += 1u;
    CHECK(preserved(&request, &result,
                    DB_GATEWAY_RESULT_IDENTITY_MISMATCH));
    return 1;
}

static int test_alias_null_and_names(void)
{
    union request_result_alias {
        db_gateway_invoke_request request;
        db_gateway_invoke_result result;
    } request_result;
    union result_output_alias {
        db_gateway_invoke_result result;
        db_gateway_validated_result output;
    } result_output;
    db_gateway_invoke_request request;
    db_gateway_invoke_result result;
    db_gateway_validated_result output;

    make_valid(&request_result.request, &result);
    CHECK(db_gateway_result_validate(&request_result.request,
                                     &request_result.result,
                                     &output) ==
          DB_GATEWAY_RESULT_BUFFER_OVERLAP);

    make_valid(&request, &result_output.result);
    CHECK(db_gateway_result_validate(&request,
                                     &result_output.result,
                                     &result_output.output) ==
          DB_GATEWAY_RESULT_BUFFER_OVERLAP);

    make_valid(&request, &result);
    CHECK(db_gateway_result_validate(NULL, &result, &output) ==
          DB_GATEWAY_RESULT_NULL_ARGUMENT);
    CHECK(db_gateway_result_validate(&request, NULL, &output) ==
          DB_GATEWAY_RESULT_NULL_ARGUMENT);
    CHECK(db_gateway_result_validate(&request, &result, NULL) ==
          DB_GATEWAY_RESULT_NULL_ARGUMENT);
    CHECK(db_gateway_result_status_name(DB_GATEWAY_RESULT_OK)[0] == 'o');
    CHECK(db_gateway_result_status_name((db_gateway_result_status)99)[0] ==
          'u');
    return 1;
}

int main(void)
{
    static const char pass[] = "GATEWAY RESULT TEST: PASS\n";
    static const char fail[] = "GATEWAY RESULT TEST: FAIL\n";

    if (!test_success() ||
        !test_bad_request() ||
        !test_bad_result() ||
        !test_mismatches() ||
        !test_alias_null_and_names()) {
        (void)write(STDERR_FILENO, fail, sizeof(fail) - 1u);
        return 1;
    }

    (void)write(STDOUT_FILENO, pass, sizeof(pass) - 1u);
    return 0;
}
