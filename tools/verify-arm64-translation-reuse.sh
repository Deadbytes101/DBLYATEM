#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/dblyatem-arm64-translation-reuse.XXXXXX")
trap 'rm -rf "$temporary"' 0 HUP INT TERM

common_flags='-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
source_files="
$root/src/arm64_translation_reuse_common.c
$root/src/arm64_translation_reuse_probe.c
$root/src/arm64_translation_reuse_session.c
$root/src/arm64_translation_reuse_lookup.c
$root/src/arm64_translation_reuse_publish.c
$root/src/arm64_translation_reuse_names.c
$root/src/arm64_block_cache.c
$root/src/translated_terminal_exit.c
$root/tests/arm64_translation_reuse_test_support.c
"
test_files="
$root/tests/arm64_translation_reuse_test.c
$root/tests/arm64_translation_reuse_outcome_test.c
$root/tests/arm64_translation_reuse_replay_test.c
$root/tests/arm64_translation_reuse_validation_test.c
$root/tests/arm64_translation_reuse_lookup_alias_test.c
$root/tests/arm64_translation_reuse_publish_alias_test.c
"
header_file="$root/include/dblyat/arm64_translation_reuse.h"
internal_header="$root/src/arm64_translation_reuse_internal.h"
terminal_header="$root/include/dblyat/translated_terminal_exit.h"

for compiler in gcc clang
do
    command -v "$compiler" >/dev/null 2>&1 || {
        printf 'ARM64 TRANSLATION REUSE VERIFY: compiler missing: %s\n' "$compiler" >&2
        exit 1
    }

    index=0
    for test_file in $test_files
    do
        index=$((index + 1))
        # shellcheck disable=SC2086
        "$compiler" -I"$root/include" -I"$root/src" -I"$root/tests" \
            $common_flags "$test_file" $source_files \
            -o "$temporary/test-$compiler-$index"
        "$temporary/test-$compiler-$index"
    done
done

index=0
for test_file in $test_files
do
    index=$((index + 1))
    # shellcheck disable=SC2086
    gcc -I"$root/include" -I"$root/src" -I"$root/tests" \
        $common_flags \
        -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
        "$test_file" $source_files \
        -o "$temporary/test-sanitize-$index"
    ASAN_OPTIONS=detect_leaks=1 "$temporary/test-sanitize-$index"
done

# shellcheck disable=SC2086
clang -I"$root/include" -I"$root/src" -I"$root/tests" \
    $common_flags --analyze $test_files $source_files

if grep -E \
    '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|munmap|mprotect|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync|clock|clock_gettime|gettimeofday|time|rdtsc|PROT_EXEC|MAP_JIT|clear_cache)([^[:alnum:]_]|$)' \
    $source_files "$header_file" "$internal_header" "$terminal_header" >/dev/null
then
    printf '%s\n' \
        'ARM64 TRANSLATION REUSE VERIFY: forbidden timing, ownership, concurrency, or execution API' >&2
    exit 1
fi

grep -F 'total_lookup_probes' "$header_file" >/dev/null
grep -F 'maximum_publication_probes' "$header_file" >/dev/null
grep -F 'rejected_operation_count' "$header_file" >/dev/null
grep -F 'test_deterministic_replay' \
    "$root/tests/arm64_translation_reuse_replay_test.c" >/dev/null
grep -F 'operation.probe_count == 2u' \
    "$root/tests/arm64_translation_reuse_outcome_test.c" >/dev/null
grep -F 'DB_ARM64_BLOCK_CACHE_KEY_CONFLICT' \
    "$root/tests/arm64_translation_reuse_outcome_test.c" >/dev/null
grep -F 'DB_ARM64_BLOCK_CACHE_FULL' \
    "$root/tests/arm64_translation_reuse_outcome_test.c" >/dev/null
grep -F 'test_counter_overflow' \
    "$root/tests/arm64_translation_reuse_validation_test.c" >/dev/null
grep -F 'test_lookup_alias_matrix' \
    "$root/tests/arm64_translation_reuse_lookup_alias_test.c" >/dev/null
grep -F 'test_publish_alias_matrix' \
    "$root/tests/arm64_translation_reuse_publish_alias_test.c" >/dev/null

printf '%s\n' 'ARM64 TRANSLATION REUSE VERIFY: PASS'
printf '%s\n' 'ARM64 TRANSLATION REUSE WORKLOAD METRICS: PASS'
printf '%s\n' 'ARM64 TRANSLATION REUSE DETERMINISTIC REPLAY: PASS'
printf '%s\n' 'ARM64 TRANSLATION REUSE TRANSACTIONAL BOUNDARY: PASS'
printf '%s\n' 'ARM64 TRANSLATION REUSE TERMINAL METADATA COMPATIBILITY: PASS'
printf '%s\n' 'ARM64 TRANSLATION REUSE NONEXECUTING BOUNDARY: PASS'
