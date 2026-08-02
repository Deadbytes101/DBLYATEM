#include "arm64_translation_benchmark_test_support.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("ARM64 TRANSLATION BENCHMARK FLAG VALIDATION TEST: FAIL line=%d\n", \
                   __LINE__);                                                \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    arm64_translation_benchmark_fixture value;
    arm64_translation_benchmark_fixture before;
    db_arm64_translation_reuse_operation operation;
    db_arm64_translation_reuse_snapshot snapshot;

    CHECK(arm64_translation_benchmark_fixture_init(&value, 1u));
    arm64_translation_benchmark_make_event(
        NULL,
        DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
        1u,
        1u,
        &operation,
        &snapshot);

    operation.miss = UINT32_MAX;
    operation.inserted = 1u;
    before = value;

    CHECK(db_arm64_translation_benchmark_record(
              &value.session,
              DB_ARM64_TRANSLATION_BENCHMARK_LOOKUP_HIT,
              0u,
              10u,
              11u,
              &operation,
              &snapshot) ==
          DB_ARM64_TRANSLATION_BENCHMARK_BAD_OPERATION);
    CHECK(arm64_translation_benchmark_fixture_unchanged(
        &value, &before));

    printf("ARM64 TRANSLATION BENCHMARK FLAG VALIDATION TEST: PASS\n");
    return 0;
}
