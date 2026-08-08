#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/arm64-translation-store-check"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
test_source='tests/arm64_translation_store_test.c'
store_source='src/arm64_translation_store.c'
store_header='include/dblyat/arm64_translation_store.h'
sources='
src/arm64_translation_store.c
src/arm64_block_cache.c
src/arm64_published_block.c
src/linux_arm64_wx_provider.c
src/translated_terminal_exit.c
src/x64_block.c
src/x64_decode.c
src/x64_memory_instruction.c
src/x64_memory_decode.c
src/ir.c
src/x64_ir_lower.c
src/x64_ir_lower_validate.c
src/x64_ir_lower_emit.c
src/arm64_select.c
src/arm64_select_validate.c
src/arm64_select_emit.c
src/x64_arm64_plan.c
src/x64_arm64_plan_validate.c
src/x64_arm64_plan_group.c
src/x64_arm64_plan_emit.c
src/x64_arm64_encode.c
src/x64_arm64_relocate.c
src/x64_arm64_relocate_validate.c
src/x64_arm64_relocate_emit.c
src/arm64_code_image.c
'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

[ -f "$store_header" ]
[ -f "$store_source" ]
[ -f "$test_source" ]

grep -F 'db_arm64_block_cache_lookup' "$store_source" >/dev/null
grep -F 'db_x64_block_build' "$store_source" >/dev/null
grep -F 'db_x64_ir_lower_block' "$store_source" >/dev/null
grep -F 'db_arm64_select_block' "$store_source" >/dev/null
grep -F 'db_x64_arm64_plan_block' "$store_source" >/dev/null
grep -F 'db_x64_arm64_encode_block' "$store_source" >/dev/null
grep -F 'db_x64_arm64_resolve_helpers' "$store_source" >/dev/null
grep -F 'db_arm64_code_image_build' "$store_source" >/dev/null
grep -F 'db_linux_arm64_wx_publish' "$store_source" >/dev/null
grep -F 'db_arm64_published_block_build' "$store_source" >/dev/null
grep -F 'db_arm64_published_block_bind_terminal' "$store_source" >/dev/null
grep -F 'db_arm64_block_cache_insert' "$store_source" >/dev/null
grep -F 'db_linux_arm64_wx_release' "$store_source" >/dev/null
grep -F 'DB_ARM64_TRANSLATION_STORE_POISONED' "$store_source" >/dev/null
grep -F 'reserve_code_base' "$store_header" >/dev/null

if grep -E 'win32|kernel32|gateway' "$store_source" "$store_header" >/dev/null
then
    printf '%s\n' 'ARM64 TRANSLATION STORE CONTAMINATED WITH WIN32/GATEWAY SEMANTICS' >&2
    exit 1
fi

if grep -E '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|mprotect|munmap|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync)([^[:alnum:]_]|$)' \
    "$store_source" "$store_header" >/dev/null
then
    printf '%s\n' 'ARM64 TRANSLATION STORE CROSSED ALLOCATION, MAPPING, OR CONCURRENCY BOUNDARY' >&2
    exit 1
fi

for compiler in gcc clang
do
    binary="$build/$compiler-test"
    # shellcheck disable=SC2086
    "$compiler" $common "$test_source" $sources -o "$binary"
    "$binary" > "$build/$compiler.log"

done

for log in "$build/gcc.log" "$build/clang.log"
do
    for marker in \
        'ARM64 TRANSLATION STORE TEST: PASS' \
        'TRANSLATION STORE MISS PUBLISH: PASS' \
        'TRANSLATION STORE CACHE HIT REUSE: PASS' \
        'TRANSLATION STORE PREPUBLISH TRANSACTIONALITY: PASS' \
        'TRANSLATION STORE POSTPUBLISH ROLLBACK: PASS' \
        'TRANSLATION STORE ROLLBACK POISON CLEANUP: PASS' \
        'NATIVE PUBLISHED BLOCK EXECUTION: NOT RUN'
    do
        count=$(grep -Fxc "$marker" "$log" || true)
        [ "$count" -eq 1 ] || {
            printf 'ARM64 TRANSLATION STORE MARKER MISMATCH log=%s marker=[%s] count=%s\n' \
                "$log" "$marker" "$count" >&2
            exit 1
        }
    done
done

# shellcheck disable=SC2086
gcc -Iinclude -Isrc -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$test_source" $sources -o "$build/sanitize-test"
ASAN_OPTIONS=detect_leaks=1 "$build/sanitize-test" > "$build/sanitize.log"

for marker in \
    'ARM64 TRANSLATION STORE TEST: PASS' \
    'TRANSLATION STORE MISS PUBLISH: PASS' \
    'TRANSLATION STORE CACHE HIT REUSE: PASS' \
    'TRANSLATION STORE PREPUBLISH TRANSACTIONALITY: PASS' \
    'TRANSLATION STORE POSTPUBLISH ROLLBACK: PASS' \
    'TRANSLATION STORE ROLLBACK POISON CLEANUP: PASS' \
    'NATIVE PUBLISHED BLOCK EXECUTION: NOT RUN'
do
    count=$(grep -Fxc "$marker" "$build/sanitize.log" || true)
    [ "$count" -eq 1 ] || {
        printf 'SANITIZED ARM64 TRANSLATION STORE MARKER MISMATCH marker=[%s] count=%s\n' \
            "$marker" "$count" >&2
        exit 1
    }
done

# shellcheck disable=SC2086
clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    "$test_source" $sources

printf '%s\n' 'ARM64 TRANSLATION STORE FOCUSED VERIFY: PASS'
printf '%s\n' 'CACHE MISS TRANSLATION PIPELINE: PASS'
printf '%s\n' 'W^X HANDLE OWNERSHIP: PASS'
printf '%s\n' 'CACHE HIT NO REPUBLISH: PASS'
printf '%s\n' 'POSTPUBLISH ROLLBACK: PASS'
printf '%s\n' 'ROLLBACK FAILURE POISONING: PASS'
printf '%s\n' 'STORE DISPOSAL CLEANUP: PASS'
printf '%s\n' 'NATIVE PUBLISHED BLOCK EXECUTION: NOT RUN'
printf '%s\n' 'CACHE MISS RUN LOOP RESUME: NOT IMPLEMENTED'
printf '%s\n' 'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN'
printf '%s\n' 'WINLATOR COMPARISON: NOT RUN'
