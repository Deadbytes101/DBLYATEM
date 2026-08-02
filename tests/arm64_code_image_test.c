#include "dblyat/arm64_code_image.h"

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

static int test_little_endian_image(void)
{
    const uint32_t words[] = {
        UINT32_C(0xd503201f),
        UINT32_C(0x94000040),
        UINT32_C(0xd61f0200)
    };
    const uint8_t expected[] = {
        UINT8_C(0x1f), UINT8_C(0x20), UINT8_C(0x03), UINT8_C(0xd5),
        UINT8_C(0x40), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x94),
        UINT8_C(0x00), UINT8_C(0x02), UINT8_C(0x1f), UINT8_C(0xd6)
    };
    uint8_t output[sizeof(expected)];
    db_arm64_code_image_plan plan;

    fill(output, sizeof(output), UINT8_C(0xa5));
    fill(&plan, sizeof(plan), UINT8_C(0x5a));

    CHECK(db_arm64_code_image_build(
              words, 3u, UINT64_C(0x100000), 1u, UINT64_C(4096),
              output, sizeof(output), &plan) ==
          DB_ARM64_CODE_IMAGE_OK);
    CHECK(same(output, expected, sizeof(expected)) != 0);
    CHECK(plan.code_base == UINT64_C(0x100000));
    CHECK(plan.entry_address == UINT64_C(0x100004));
    CHECK(plan.image_end_address == UINT64_C(0x10000b));
    CHECK(plan.allocation_end_address == UINT64_C(0x100fff));
    CHECK(plan.page_size == UINT64_C(4096));
    CHECK(plan.allocation_size == UINT64_C(4096));
    CHECK(plan.instruction_cache_offset == 0u);
    CHECK(plan.instruction_cache_size == UINT64_C(12));
    CHECK(plan.format_version == DB_ARM64_CODE_IMAGE_FORMAT_VERSION);
    CHECK(plan.word_count == 3u);
    CHECK(plan.byte_count == 12u);
    CHECK(plan.entry_word_index == 1u);
    CHECK(plan.entry_byte_offset == 4u);
    CHECK(plan.initial_permissions ==
          (DB_ARM64_CODE_PERMISSION_READ |
           DB_ARM64_CODE_PERMISSION_WRITE));
    CHECK(plan.final_permissions ==
          (DB_ARM64_CODE_PERMISSION_READ |
           DB_ARM64_CODE_PERMISSION_EXECUTE));
    return 1;
}

static int test_page_rounding(void)
{
    static uint32_t words[1025];
    static uint8_t output[4100];
    db_arm64_code_image_plan plan;

    fill(words, sizeof(words), 0u);
    fill(output, sizeof(output), 0u);
    fill(&plan, sizeof(plan), 0u);

    CHECK(db_arm64_code_image_build(
              words, 1025u, UINT64_C(0x200000), 1024u,
              UINT64_C(4096), output, sizeof(output), &plan) ==
          DB_ARM64_CODE_IMAGE_OK);
    CHECK(plan.byte_count == 4100u);
    CHECK(plan.allocation_size == UINT64_C(8192));
    CHECK(plan.entry_address == UINT64_C(0x201000));
    CHECK(plan.allocation_end_address == UINT64_C(0x201fff));
    return 1;
}

static int test_failures_preserve(void)
{
    const uint32_t words[] = {
        UINT32_C(0xd503201f),
        UINT32_C(0xd61f0200)
    };
    uint8_t output[8];
    uint8_t output_before[8];
    db_arm64_code_image_plan plan;
    db_arm64_code_image_plan plan_before;

    fill(output, sizeof(output), UINT8_C(0xa5));
    fill(&plan, sizeof(plan), UINT8_C(0x5a));
    fill(output_before, sizeof(output_before), UINT8_C(0xa5));
    plan_before = plan;

    CHECK(db_arm64_code_image_build(
              words, 2u, UINT64_C(0x300000), 2u, UINT64_C(4096),
              output, sizeof(output), &plan) ==
          DB_ARM64_CODE_IMAGE_BAD_ENTRY_INDEX);
    CHECK(same(output, output_before, sizeof(output)) != 0);
    CHECK(same(&plan, &plan_before, sizeof(plan)) != 0);

    CHECK(db_arm64_code_image_build(
              words, 2u, UINT64_C(0x300004), 0u, UINT64_C(4096),
              output, sizeof(output), &plan) ==
          DB_ARM64_CODE_IMAGE_BAD_CODE_BASE);
    CHECK(db_arm64_code_image_build(
              words, 2u, UINT64_C(0x300000), 0u, UINT64_C(3072),
              output, sizeof(output), &plan) ==
          DB_ARM64_CODE_IMAGE_BAD_PAGE_SIZE);
    CHECK(db_arm64_code_image_build(
              words, 2u, UINT64_C(0x300000), 0u, UINT64_C(4096),
              output, sizeof(output) - 1u, &plan) ==
          DB_ARM64_CODE_IMAGE_OUTPUT_CAPACITY);
    CHECK(db_arm64_code_image_build(
              words, 2u, UINT64_C(0x300000), 0u, UINT64_C(4096),
              output, sizeof(output) + 1u, &plan) ==
          DB_ARM64_CODE_IMAGE_OUTPUT_CAPACITY);
    return 1;
}

static int test_alias_and_address_overflow(void)
{
    static uint32_t words[1025];
    static uint8_t output[4100];
    db_arm64_code_image_plan plan;

    fill(words, sizeof(words), 0u);
    fill(output, sizeof(output), 0u);
    fill(&plan, sizeof(plan), 0u);

    CHECK(db_arm64_code_image_build(
              words, 1u, UINT64_C(0x400000), 0u, UINT64_C(4096),
              (uint8_t *)(void *)words, sizeof(uint32_t), &plan) ==
          DB_ARM64_CODE_IMAGE_BUFFER_OVERLAP);
    CHECK(db_arm64_code_image_build(
              (const uint32_t *)(const void *)&plan, 1u,
              UINT64_C(0x400000), 0u, UINT64_C(4096),
              output, sizeof(uint32_t), &plan) ==
          DB_ARM64_CODE_IMAGE_BUFFER_OVERLAP);
    CHECK(db_arm64_code_image_build(
              words, 1u, UINT64_C(0x400000), 0u, UINT64_C(4096),
              (uint8_t *)(void *)&plan, sizeof(uint32_t), &plan) ==
          DB_ARM64_CODE_IMAGE_BUFFER_OVERLAP);
    CHECK(db_arm64_code_image_build(
              words, 1025u, UINT64_C(0x0000fffffffff000), 0u,
              UINT64_C(4096), output, sizeof(output), &plan) ==
          DB_ARM64_CODE_IMAGE_SIZE_OVERFLOW);
    CHECK(db_arm64_code_image_build(
              words, 1u, UINT64_C(0), 0u, UINT64_C(4096),
              output, sizeof(uint32_t), &plan) ==
          DB_ARM64_CODE_IMAGE_BAD_CODE_BASE);
    return 1;
}

int main(void)
{
    if (!test_little_endian_image()) return 11;
    if (!test_page_rounding()) return 12;
    if (!test_failures_preserve()) return 13;
    if (!test_alias_and_address_overflow()) return 14;
    if (db_arm64_code_image_status_name(
            DB_ARM64_CODE_IMAGE_OK)[0] != 'o') return 15;

    (void)write(STDOUT_FILENO,
                "ARM64 CODE IMAGE TEST: PASS\n",
                sizeof("ARM64 CODE IMAGE TEST: PASS\n") - 1u);
    return 0;
}
