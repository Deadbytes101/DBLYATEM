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

stage()
{
    printf 'ARM64_STORE_STAGE=%s\n' "$1"
}

require_file()
{
    path=$1
    [ -f "$path" ] || {
        printf 'ARM64_STORE_REQUIRED_FILE_MISSING=%s\n' "$path" >&2
        exit 1
    }
}

require_fixed()
{
    pattern=$1
    path=$2
    grep -F "$pattern" "$path" >/dev/null || {
        printf 'ARM64_STORE_STATIC_REQUIREMENT_MISSING file=%s pattern=[%s]\n' \
            "$path" "$pattern" >&2
        exit 1
    }
}

run_compiler_test()
{
    compiler=$1
    binary="$build/$compiler-test"
    compile_log="$build/$compiler-compile.log"
    run_log="$build/$compiler.log"

    stage "compile-$compiler"
    set +e
    # shellcheck disable=SC2086
    "$compiler" $common "$test_source" $sources \
        -o "$binary" >"$compile_log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf 'ARM64_STORE_COMPILE_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$status" >&2
        cat "$compile_log" >&2
        exit "$status"
    fi

    stage "run-$compiler"
    set +e
    "$binary" >"$run_log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf 'ARM64_STORE_TEST_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$status" >&2
        cat "$run_log" >&2
        exit "$status"
    fi
}

check_markers()
{
    log=$1
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
            cat "$log" >&2
            exit 1
        }
    done
}

rm -rf "$build"
mkdir -p "$build"
cd "$root"

stage files
require_file "$store_header"
require_file "$store_source"
require_file "$test_source"

stage static-requirements
require_fixed 'db_arm64_block_cache_lookup' "$store_source"
require_fixed 'db_x64_block_build' "$store_source"
require_fixed 'db_x64_ir_lower_block' "$store_source"
require_fixed 'db_arm64_select_block' "$store_source"
require_fixed 'db_x64_arm64_plan_block' "$store_source"
require_fixed 'db_x64_arm64_encode_block' "$store_source"
require_fixed 'db_x64_arm64_resolve_helpers' "$store_source"
require_fixed 'db_arm64_code_image_build' "$store_source"
require_fixed 'db_linux_arm64_wx_publish' "$store_source"
require_fixed 'db_arm64_published_block_build' "$store_source"
require_fixed 'db_arm64_published_block_bind_terminal' "$store_source"
require_fixed 'db_arm64_block_cache_insert' "$store_source"
require_fixed 'db_linux_arm64_wx_release' "$store_source"
require_fixed 'DB_ARM64_TRANSLATION_STORE_POISONED' "$store_source"
require_fixed 'reserve_code_base' "$store_header"

stage boundaries
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
    run_compiler_test "$compiler"
done

stage marker-validation
check_markers "$build/gcc.log"
check_markers "$build/clang.log"

stage compile-sanitize
set +e
# shellcheck disable=SC2086
gcc -Iinclude -Isrc -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$test_source" $sources \
    -o "$build/sanitize-test" >"$build/sanitize-compile.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_STORE_SANITIZER_COMPILE_FAILED exit=%s\n' "$status" >&2
    cat "$build/sanitize-compile.log" >&2
    exit "$status"
fi

stage run-sanitize
set +e
ASAN_OPTIONS=detect_leaks=1 "$build/sanitize-test" \
    >"$build/sanitize.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_STORE_SANITIZER_TEST_FAILED exit=%s\n' "$status" >&2
    cat "$build/sanitize.log" >&2
    exit "$status"
fi
check_markers "$build/sanitize.log"

stage clang-analyze
set +e
# shellcheck disable=SC2086
clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    "$test_source" $sources >"$build/clang-analyze.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_STORE_CLANG_ANALYZE_FAILED exit=%s\n' "$status" >&2
    cat "$build/clang-analyze.log" >&2
    exit "$status"
fi

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
