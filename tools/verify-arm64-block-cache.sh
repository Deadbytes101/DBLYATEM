#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/dblyatem-arm64-block-cache.XXXXXX")
trap 'rm -rf "$temporary"' 0 HUP INT TERM

common_flags='-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
source_file="$root/src/arm64_block_cache.c"
test_file="$root/tests/arm64_block_cache_test.c"
header_file="$root/include/dblyat/arm64_block_cache.h"

for compiler in gcc clang
do
    command -v "$compiler" >/dev/null 2>&1 || {
        printf 'ARM64 BLOCK CACHE VERIFY: compiler missing: %s\n' "$compiler" >&2
        exit 1
    }

    # shellcheck disable=SC2086
    "$compiler" -I"$root/include" $common_flags \
        "$test_file" "$source_file" \
        -o "$temporary/test-$compiler"
    "$temporary/test-$compiler"
done

# shellcheck disable=SC2086
gcc -I"$root/include" $common_flags \
    -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$test_file" "$source_file" \
    -o "$temporary/test-sanitize"
ASAN_OPTIONS=detect_leaks=1 "$temporary/test-sanitize"

# shellcheck disable=SC2086
clang -I"$root/include" $common_flags \
    --analyze "$test_file" "$source_file"

if grep -E \
    '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|munmap|mprotect|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync|PROT_EXEC|MAP_JIT|clear_cache)([^[:alnum:]_]|$)' \
    "$source_file" "$header_file" >/dev/null
then
    printf '%s\n' \
        'ARM64 BLOCK CACHE VERIFY: forbidden ownership, concurrency, or execution API' >&2
    exit 1
fi

grep -F \
    'second_result.probe_count == 2u' \
    "$test_file" >/dev/null
grep -F \
    'DB_ARM64_BLOCK_CACHE_KEY_CONFLICT' \
    "$test_file" >/dev/null
grep -F \
    'DB_ARM64_BLOCK_CACHE_FULL' \
    "$test_file" >/dev/null
grep -F \
    'DB_ARM64_BLOCK_CACHE_NOT_FOUND' \
    "$test_file" >/dev/null

printf '%s\n' 'ARM64 BLOCK CACHE VERIFY: PASS'
printf '%s\n' 'DETERMINISTIC BLOCK CACHE PROBE VERIFY: PASS'
printf '%s\n' 'ARM64 BLOCK CACHE NONEXECUTING BOUNDARY: PASS'
