#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/dblyatem-arm64-translation-benchmark-flags.XXXXXX")
trap 'rm -rf "$temporary"' 0 HUP INT TERM

common_flags='-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
test_file="$root/tests/arm64_translation_benchmark_flag_validation_test.c"
support_source="$root/tests/arm64_translation_benchmark_test_support.c"
core_source="$root/src/arm64_translation_benchmark.c"

for compiler in gcc clang
do
    command -v "$compiler" >/dev/null 2>&1 || {
        printf 'ARM64 TRANSLATION BENCHMARK FLAG VERIFY: compiler missing: %s\n' "$compiler" >&2
        exit 1
    }

    # shellcheck disable=SC2086
    "$compiler" -I"$root/include" -I"$root/tests" \
        $common_flags \
        "$test_file" "$support_source" "$core_source" \
        -o "$temporary/test-$compiler"
    "$temporary/test-$compiler"
done

# shellcheck disable=SC2086
gcc -I"$root/include" -I"$root/tests" \
    $common_flags -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$test_file" "$support_source" "$core_source" \
    -o "$temporary/test-sanitize"
ASAN_OPTIONS=detect_leaks=1 "$temporary/test-sanitize"

# shellcheck disable=SC2086
clang -I"$root/include" -I"$root/tests" \
    $common_flags --analyze \
    "$test_file" "$support_source" "$core_source"

grep -F 'operation->hit > 1u' "$core_source" >/dev/null
grep -F 'operation->miss > 1u' "$core_source" >/dev/null
grep -F 'operation->inserted > 1u' "$core_source" >/dev/null
grep -F 'operation->duplicate > 1u' "$core_source" >/dev/null
grep -F 'operation->conflict > 1u' "$core_source" >/dev/null
grep -F 'operation->full > 1u' "$core_source" >/dev/null
grep -F 'operation.miss = UINT32_MAX' "$test_file" >/dev/null
grep -F 'operation.inserted = 1u' "$test_file" >/dev/null

printf '%s\n' 'ARM64 TRANSLATION BENCHMARK FLAG VALIDATION TEST: PASS'
printf '%s\n' 'ARM64 TRANSLATION BENCHMARK BINARY OUTCOME FLAGS: PASS'
printf '%s\n' 'ARM64 TRANSLATION BENCHMARK WRAPAROUND REGRESSION: PASS'
