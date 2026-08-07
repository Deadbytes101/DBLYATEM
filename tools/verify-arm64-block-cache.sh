#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/dblyatem-arm64-block-cache.XXXXXX")
trap 'rm -rf "$temporary"' 0 HUP INT TERM

common_flags='-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
source_file="$root/src/arm64_block_cache.c"
terminal_source="$root/src/translated_terminal_exit.c"
test_file="$root/tests/arm64_block_cache_test.c"
terminal_test_file="$root/tests/arm64_block_cache_terminal_exit_test.c"
lookup_test_file="$root/tests/arm64_block_cache_lookup_validation_test.c"
adversarial_test_file="$root/tests/arm64_block_cache_adversarial_test.c"
header_file="$root/include/dblyat/arm64_block_cache.h"
terminal_header="$root/include/dblyat/translated_terminal_exit.h"

for compiler in gcc clang
do
    command -v "$compiler" >/dev/null 2>&1 || {
        printf 'ARM64 BLOCK CACHE VERIFY: compiler missing: %s\n' "$compiler" >&2
        exit 1
    }

    # shellcheck disable=SC2086
    "$compiler" -I"$root/include" $common_flags \
        "$test_file" "$source_file" "$terminal_source" \
        -o "$temporary/test-$compiler"
    "$temporary/test-$compiler"

    # shellcheck disable=SC2086
    "$compiler" -I"$root/include" $common_flags \
        "$terminal_test_file" "$source_file" "$terminal_source" \
        -o "$temporary/terminal-test-$compiler"
    "$temporary/terminal-test-$compiler"

    # shellcheck disable=SC2086
    "$compiler" -I"$root/include" $common_flags \
        "$lookup_test_file" "$source_file" "$terminal_source" \
        -o "$temporary/lookup-test-$compiler"
    "$temporary/lookup-test-$compiler"

    # shellcheck disable=SC2086
    "$compiler" -I"$root/include" $common_flags \
        "$adversarial_test_file" "$source_file" "$terminal_source" \
        -o "$temporary/adversarial-test-$compiler"
    "$temporary/adversarial-test-$compiler"
done

for sanitizer_test in \
    "$test_file" \
    "$terminal_test_file" \
    "$lookup_test_file" \
    "$adversarial_test_file"
do
    sanitizer_name=$(basename -- "$sanitizer_test" .c)
    # shellcheck disable=SC2086
    gcc -I"$root/include" $common_flags \
        -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
        "$sanitizer_test" "$source_file" "$terminal_source" \
        -o "$temporary/$sanitizer_name-sanitize"
    ASAN_OPTIONS=detect_leaks=1 \
        "$temporary/$sanitizer_name-sanitize"
done

# shellcheck disable=SC2086
clang -I"$root/include" $common_flags \
    --analyze \
    "$test_file" \
    "$terminal_test_file" \
    "$lookup_test_file" \
    "$adversarial_test_file" \
    "$source_file" "$terminal_source"

if grep -E \
    '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|munmap|mprotect|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync|PROT_EXEC|MAP_JIT|clear_cache)([^[:alnum:]_]|$)' \
    "$source_file" "$terminal_source" "$header_file" "$terminal_header" >/dev/null
then
    printf '%s\n' \
        'ARM64 BLOCK CACHE VERIFY: forbidden ownership, concurrency, or execution API' >&2
    exit 1
fi

grep -F 'terminal_exit_binding_valid' "$source_file" >/dev/null
grep -F 'exit_record->source_rip < block->guest_source_rip' \
    "$source_file" >/dev/null
grep -F 'test_bound_direct_roundtrip' \
    "$terminal_test_file" >/dev/null
grep -F 'test_foreign_source_rejected' \
    "$terminal_test_file" >/dev/null
grep -F 'test_conditional_fallthrough_mismatch_rejected' \
    "$terminal_test_file" >/dev/null
grep -F 'second_result.probe_count == 2u' \
    "$test_file" >/dev/null
grep -F 'DB_ARM64_BLOCK_CACHE_KEY_CONFLICT' \
    "$test_file" >/dev/null
grep -F 'DB_ARM64_BLOCK_CACHE_FULL' \
    "$test_file" >/dev/null
grep -F 'DB_ARM64_BLOCK_CACHE_NOT_FOUND' \
    "$test_file" >/dev/null
grep -F 'DB_ARM64_BLOCK_CACHE_BAD_CACHE' \
    "$lookup_test_file" >/dev/null
grep -F 'test_alias_matrix' \
    "$adversarial_test_file" >/dev/null
grep -F 'test_malformed_resident_entries' \
    "$adversarial_test_file" >/dev/null
grep -F 'test_bad_block_matrix' \
    "$adversarial_test_file" >/dev/null

printf '%s\n' 'ARM64 BLOCK CACHE VERIFY: PASS'
printf '%s\n' 'DETERMINISTIC BLOCK CACHE PROBE VERIFY: PASS'
printf '%s\n' 'ARM64 BLOCK CACHE TERMINAL EXIT VALIDATION: PASS'
printf '%s\n' 'ARM64 BLOCK CACHE TERMINAL BINDING INTEGRITY: PASS'
printf '%s\n' 'ARM64 BLOCK CACHE FULL-TABLE LOOKUP VALIDATION: PASS'
printf '%s\n' 'ARM64 BLOCK CACHE ADVERSARIAL CONTRACT: PASS'
printf '%s\n' 'ARM64 BLOCK CACHE NONEXECUTING BOUNDARY: PASS'
