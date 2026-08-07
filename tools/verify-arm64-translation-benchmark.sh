#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/dblyatem-arm64-translation-benchmark.XXXXXX")
trap 'rm -rf "$temporary"' 0 HUP INT TERM

common_flags='-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
core_source="$root/src/arm64_translation_benchmark.c"
support_source="$root/tests/arm64_translation_benchmark_test_support.c"
test_files="
$root/tests/arm64_translation_benchmark_test.c
$root/tests/arm64_translation_benchmark_validation_test.c
"
runner_source="$root/tools/arm64-translation-reuse-benchmark.c"
runtime_sources="
$root/src/arm64_translation_benchmark.c
$root/src/arm64_translation_reuse_common.c
$root/src/arm64_translation_reuse_probe.c
$root/src/arm64_translation_reuse_session.c
$root/src/arm64_translation_reuse_lookup.c
$root/src/arm64_translation_reuse_publish.c
$root/src/arm64_translation_reuse_names.c
$root/src/arm64_block_cache.c
$root/src/translated_terminal_exit.c
"
public_header="$root/include/dblyat/arm64_translation_benchmark.h"
manifest_template="$root/benchmarks/arm64-translation-reuse/manifest-v1.txt"
csv_template="$root/benchmarks/arm64-translation-reuse/raw-result-v1.csv"
readme_file="$root/benchmarks/arm64-translation-reuse/README.TXT"

for compiler in gcc clang
do
    command -v "$compiler" >/dev/null 2>&1 || {
        printf 'ARM64 TRANSLATION BENCHMARK VERIFY: compiler missing: %s\n' "$compiler" >&2
        exit 1
    }

    index=0
    for test_file in $test_files
    do
        index=$((index + 1))
        # shellcheck disable=SC2086
        "$compiler" -I"$root/include" -I"$root/tests" \
            $common_flags \
            "$test_file" "$support_source" "$core_source" \
            -o "$temporary/test-$compiler-$index"
        "$temporary/test-$compiler-$index"
    done

    # shellcheck disable=SC2086
    "$compiler" -D_POSIX_C_SOURCE=200809L \
        -I"$root/include" $common_flags \
        "$runner_source" $runtime_sources \
        -o "$temporary/runner-$compiler"
done

index=0
for test_file in $test_files
do
    index=$((index + 1))
    # shellcheck disable=SC2086
    gcc -I"$root/include" -I"$root/tests" \
        $common_flags -O1 -g \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        "$test_file" "$support_source" "$core_source" \
        -o "$temporary/test-sanitize-$index"
    ASAN_OPTIONS=detect_leaks=1 "$temporary/test-sanitize-$index"
done

# shellcheck disable=SC2086
clang -I"$root/include" -I"$root/tests" \
    $common_flags --analyze \
    $test_files "$support_source" "$core_source"

# shellcheck disable=SC2086
clang -D_POSIX_C_SOURCE=200809L \
    -I"$root/include" $common_flags --analyze \
    "$runner_source" $runtime_sources

if grep -E \
    '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|fopen|fprintf|fputs|fflush|fclose|clock|clock_gettime|gettimeofday|time|rdtsc|mmap|munmap|mprotect|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync|PROT_EXEC|MAP_JIT|clear_cache)([^[:alnum:]_]|$)' \
    "$core_source" "$public_header" >/dev/null
then
    printf '%s\n' \
        'ARM64 TRANSLATION BENCHMARK VERIFY: forbidden clock, I/O, ownership, concurrency, or execution API in record core' >&2
    exit 1
fi

grep -F 'CLOCK_MONOTONIC_RAW' "$runner_source" >/dev/null
grep -F 'O_WRONLY | O_CREAT | O_EXCL' "$runner_source" >/dev/null
grep -F '(void)remove(argv[1]);' "$runner_source" >/dev/null
grep -F 'PUBLISHED_BLOCK_EXECUTION=NO' "$runner_source" >/dev/null
grep -F 'WINLATOR_COMPARISON=NOT_RUN' "$runner_source" >/dev/null
grep -F 'test_duration_overflow' \
    "$root/tests/arm64_translation_benchmark_validation_test.c" >/dev/null
grep -F 'test_deterministic_samples' \
    "$root/tests/arm64_translation_benchmark_test.c" >/dev/null
grep -F 'PUBLICATION_CONFLICT' \
    "$root/tests/arm64_translation_benchmark_test.c" >/dev/null
grep -F 'PUBLICATION_FULL' \
    "$root/tests/arm64_translation_benchmark_test.c" >/dev/null

if grep -E \
    'host_entry_address[[:space:]]*\(|(^|[^[:alnum:]_])(mprotect|PROT_EXEC|MAP_JIT|clear_cache)([^[:alnum:]_]|$)' \
    "$runner_source" >/dev/null
then
    printf '%s\n' \
        'ARM64 TRANSLATION BENCHMARK VERIFY: runner crosses execution or permission boundary' >&2
    exit 1
fi

for compiler in gcc clang
do
    collision="$temporary/collision-$compiler"
    existing="$temporary/existing-$compiler"
    unexpected_csv="$temporary/unexpected-$compiler.csv"

    if "$temporary/runner-$compiler" \
        "$collision" \
        "$collision" \
        1 \
        DBLYATEM_VALIDATION \
        validation-build-v1 \
        circleci-arm-medium \
        ubuntu-22.04 \
        linux-kernel \
        fixed-ci \
        cold-ci >/dev/null 2>&1
    then
        printf '%s\n' \
            'ARM64 TRANSLATION BENCHMARK VERIFY: identical output paths accepted' >&2
        exit 1
    fi
    [ ! -e "$collision" ] || {
        printf '%s\n' \
            'ARM64 TRANSLATION BENCHMARK VERIFY: collision output not removed' >&2
        exit 1
    }

    printf '%s\n' 'DO_NOT_OVERWRITE' > "$existing"
    if "$temporary/runner-$compiler" \
        "$existing" \
        "$unexpected_csv" \
        1 \
        DBLYATEM_VALIDATION \
        validation-build-v1 \
        circleci-arm-medium \
        ubuntu-22.04 \
        linux-kernel \
        fixed-ci \
        cold-ci >/dev/null 2>&1
    then
        printf '%s\n' \
            'ARM64 TRANSLATION BENCHMARK VERIFY: existing output accepted' >&2
        exit 1
    fi
    grep -Fx 'DO_NOT_OVERWRITE' "$existing" >/dev/null
    [ ! -e "$unexpected_csv" ] || {
        printf '%s\n' \
            'ARM64 TRANSLATION BENCHMARK VERIFY: partial CSV created' >&2
        exit 1
    }

    manifest="$temporary/manifest-$compiler.txt"
    csv="$temporary/raw-$compiler.csv"
    output="$temporary/runner-$compiler.log"

    "$temporary/runner-$compiler" \
        "$manifest" \
        "$csv" \
        4 \
        DBLYATEM_VALIDATION \
        validation-build-v1 \
        circleci-arm-medium \
        ubuntu-22.04 \
        linux-kernel \
        fixed-ci \
        cold-ci >"$output"

    grep -Fx 'ARM64 TRANSLATION BENCHMARK RUNNER: PASS' "$output" >/dev/null
    grep -Fx 'CLOCK_MONOTONIC_RAW=PASS' "$output" >/dev/null
    grep -Fx 'MANIFEST_V1=PASS' "$output" >/dev/null
    grep -Fx 'RAW_CSV_V1=PASS' "$output" >/dev/null
    grep -Fx 'SAMPLE_COUNT=16' "$output" >/dev/null
    grep -Fx 'PUBLISHED_BLOCK_EXECUTION=NO' "$output" >/dev/null
    grep -Fx 'WINLATOR_COMPARISON=NOT_RUN' "$output" >/dev/null

    grep -Fx 'DBLYATEM_ARM64_TRANSLATION_BENCHMARK_MANIFEST_V1' "$manifest" >/dev/null
    grep -Fx 'FORMAT_VERSION=1' "$manifest" >/dev/null
    grep -Fx 'BENCHMARK=arm64-translation-reuse' "$manifest" >/dev/null
    grep -Fx 'RUNTIME_ID=DBLYATEM_VALIDATION' "$manifest" >/dev/null
    grep -Fx 'RUNTIME_BUILD=validation-build-v1' "$manifest" >/dev/null
    grep -Fx 'DEVICE_ID=circleci-arm-medium' "$manifest" >/dev/null
    grep -Fx 'PLATFORM_BUILD=ubuntu-22.04' "$manifest" >/dev/null
    grep -Fx 'DRIVER_BUILD=linux-kernel' "$manifest" >/dev/null
    grep -Fx 'POWER_MODE=fixed-ci' "$manifest" >/dev/null
    grep -Fx 'THERMAL_START=cold-ci' "$manifest" >/dev/null
    grep -Fx 'WORKLOAD_ID=synthetic-publish-hit-miss-duplicate-v1' "$manifest" >/dev/null
    grep -Fx 'CLOCK=CLOCK_MONOTONIC_RAW' "$manifest" >/dev/null
    grep -Fx 'CLOCK_UNIT=nanoseconds' "$manifest" >/dev/null
    grep -Fx 'ITERATIONS=4' "$manifest" >/dev/null
    grep -Fx 'SAMPLE_COUNT=16' "$manifest" >/dev/null
    grep -Fx 'PUBLISHED_BLOCK_EXECUTION=NO' "$manifest" >/dev/null
    grep -Fx 'WINLATOR_COMPARISON=NOT_RUN' "$manifest" >/dev/null

    head -n 1 "$csv" | cmp -s - "$csv_template"
    test "$(wc -l <"$csv" | tr -d ' ')" = 17
    awk -F, 'NF != 32 { exit 1 }' "$csv"
    awk -F, 'NR > 1 && $2 != NR - 1 { exit 1 }' "$csv"
    awk -F, 'NR > 1 && $4 == "PUBLICATION_INSERT" { count++ }
               END { exit count == 4 ? 0 : 1 }' "$csv"
    awk -F, 'NR > 1 && $4 == "LOOKUP_HIT" { count++ }
               END { exit count == 4 ? 0 : 1 }' "$csv"
    awk -F, 'NR > 1 && $4 == "LOOKUP_MISS" { count++ }
               END { exit count == 4 ? 0 : 1 }' "$csv"
    awk -F, 'NR > 1 && $4 == "PUBLICATION_DUPLICATE" { count++ }
               END { exit count == 4 ? 0 : 1 }' "$csv"
done

grep -Fx 'DBLYATEM_ARM64_TRANSLATION_BENCHMARK_MANIFEST_V1' \
    "$manifest_template" >/dev/null
grep -Fx 'RUNTIME_BUILD=REQUIRED' "$manifest_template" >/dev/null
grep -Fx 'DEVICE_ID=REQUIRED' "$manifest_template" >/dev/null
grep -Fx 'PLATFORM_BUILD=REQUIRED' "$manifest_template" >/dev/null
grep -Fx 'DRIVER_BUILD=REQUIRED' "$manifest_template" >/dev/null
grep -Fx 'POWER_MODE=REQUIRED' "$manifest_template" >/dev/null
grep -Fx 'THERMAL_START=REQUIRED' "$manifest_template" >/dev/null
grep -Fx 'WINLATOR_COMPARISON=NOT_RUN' "$manifest_template" >/dev/null
grep -F 'No comparison has been run by this seed.' "$readme_file" >/dev/null
grep -Fx 'STANDALONE=tools/verify-arm64-translation-benchmark.sh' \
    "$root/tools/verify-manifest.txt" >/dev/null

printf '%s\n' 'ARM64 TRANSLATION BENCHMARK VERIFY: PASS'
printf '%s\n' 'ARM64 TRANSLATION BENCHMARK RECORD CONTRACT: PASS'
printf '%s\n' 'ARM64 TRANSLATION BENCHMARK OUTPUT PAIR: PASS'
printf '%s\n' 'ARM64 TRANSLATION BENCHMARK RAW CSV V1: PASS'
printf '%s\n' 'ARM64 TRANSLATION BENCHMARK MONOTONIC RAW RUNNER: PASS'
printf '%s\n' 'ARM64 TRANSLATION BENCHMARK TERMINAL METADATA COMPATIBILITY: PASS'
printf '%s\n' 'ARM64 TRANSLATION BENCHMARK NONEXECUTING BOUNDARY: PASS'
printf '%s\n' 'ARM64 TRANSLATION BENCHMARK WINLATOR CLAIM: NOT RUN'
