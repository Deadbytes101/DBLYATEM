#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/win32-translated-demand-run-check"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
header='include/dblyat/win32_translated_demand_run.h'
internal='src/win32_translated_demand_run_internal.h'
source='src/win32_translated_demand_run.c'
test_source='tests/win32_translated_demand_run_test.c'
sources='
src/win32_translated_demand_run.c
src/win32_translated_run.c
src/arm64_translation_store.c
src/arm64_block_cache.c
src/arm64_published_block.c
src/linux_arm64_wx_provider.c
src/win32_translated_execution.c
src/linux_arm64_translation_entry.c
src/win32_translated_gateway_bridge.c
src/win32_runtime_dispatch.c
src/win32_kernel32_gateway.c
src/win32_kernel32_invoke.c
src/win32_kernel32.c
src/win32_provider.c
src/gateway_auth_complete.c
src/gateway_result.c
src/gateway_complete.c
src/gateway_return.c
src/gateway_return_apply.c
src/gateway_decode.c
src/gateway_lookup.c
src/gateway_dispatch.c
src/gateway_frame.c
src/gateway_enter.c
src/gateway_invoke.c
src/gateway_emit.c
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

printf '%s\n' 'DEMAND_RUN_STAGE=files'
for required in "$header" "$internal" "$source" "$test_source" \
    tools/check-arm64-translation-store.sh \
    tools/verify-win32-kernel32-gateway.sh
do
    [ -f "$required" ] || {
        printf 'DEMAND_RUN_REQUIRED_FILE_MISSING path=%s\n' "$required" >&2
        exit 1
    }
done

printf '%s\n' 'DEMAND_RUN_STAGE=static-contract'
for requirement in \
    'db_win32_translated_run_with_executor' \
    'db_arm64_translation_store_fill' \
    'DB_WIN32_TRANSLATED_RUN_STOP_CACHE_MISS' \
    'DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_BLOCK_BUDGET' \
    'DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TRAP' \
    'DB_WIN32_TRANSLATED_DEMAND_RUN_STOP_GUEST_TERMINATED'
do
    grep -F "$requirement" "$source" "$internal" >/dev/null || {
        printf 'DEMAND_RUN_STATIC_REQUIREMENT_MISSING token=%s\n' \
            "$requirement" >&2
        exit 1
    }
done

if grep -F 'db_win32_translated_demand_run_with_executor' "$header" >/dev/null
then
    printf '%s\n' 'DEMAND RUN TEST EXECUTOR SEAM LEAKED INTO PUBLIC API' >&2
    exit 1
fi

if grep -E 'db_arm64_block_cache_insert|db_linux_arm64_wx_publish|db_x64_block_build|db_arm64_published_block_build|db_arm64_published_block_bind_terminal' \
    "$source" "$header" "$internal" >/dev/null
then
    printf '%s\n' 'DEMAND RUN DUPLICATED TRANSLATION/PUBLICATION STORE SEMANTICS' >&2
    exit 1
fi

if grep -E '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|mprotect|munmap|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync)([^[:alnum:]_]|$)' \
    "$source" "$header" "$internal" >/dev/null
then
    printf '%s\n' 'DEMAND RUN CROSSED ALLOCATION, MAPPING, OR CONCURRENCY BOUNDARY' >&2
    exit 1
fi

for production_token in \
    'src/arm64_translation_store.c' \
    'src/win32_translated_demand_run.c' \
    'include/dblyat/arm64_translation_store.h' \
    'include/dblyat/win32_translated_demand_run.h'
do
    grep -F "$production_token" GNUmakefile >/dev/null || {
        printf 'DEMAND_RUN_PRODUCTION_LINK_MISSING token=%s\n' \
            "$production_token" >&2
        exit 1
    }
done

printf '%s\n' 'DEMAND_RUN_STAGE=store-regression'
sh tools/check-arm64-translation-store.sh > "$build/store-regression.log" 2>&1 || {
    cat "$build/store-regression.log" >&2
    exit 1
}
grep -Fqx 'ARM64 TRANSLATION STORE FOCUSED VERIFY: PASS' \
    "$build/store-regression.log" || {
    cat "$build/store-regression.log" >&2
    exit 1
}

printf '%s\n' 'DEMAND_RUN_STAGE=control-plane-regression'
sh tools/verify-win32-kernel32-gateway.sh > "$build/control-regression.log" 2>&1 || {
    cat "$build/control-regression.log" >&2
    exit 1
}
grep -Fqx 'TRANSLATED MULTI BLOCK CACHE LOOP VERIFY: PASS' \
    "$build/control-regression.log" || {
    cat "$build/control-regression.log" >&2
    exit 1
}

for compiler in gcc clang
do
    binary="$build/$compiler-test"
    compile_log="$build/$compiler-compile.log"
    run_log="$build/$compiler.log"

    printf 'DEMAND_RUN_STAGE=compile-%s\n' "$compiler"
    set +e
    # shellcheck disable=SC2086
    "$compiler" $common "$test_source" $sources -o "$binary" \
        > "$compile_log" 2>&1
    compile_status=$?
    set -e
    if [ "$compile_status" -ne 0 ]; then
        printf 'DEMAND_RUN_COMPILE_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$compile_status" >&2
        cat "$compile_log" >&2
        exit "$compile_status"
    fi

    printf 'DEMAND_RUN_STAGE=run-%s\n' "$compiler"
    set +e
    "$binary" > "$run_log" 2>&1
    run_status=$?
    set -e
    if [ "$run_status" -ne 0 ]; then
        printf 'DEMAND_RUN_TEST_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$run_status" >&2
        cat "$run_log" >&2
        exit "$run_status"
    fi
done

printf '%s\n' 'DEMAND_RUN_STAGE=marker-validation'
for log in "$build/gcc.log" "$build/clang.log"
do
    for marker in \
        'CACHE MISS TRANSLATE RETRY: PASS' \
        'DEMAND CACHE HIT NO REPUBLISH: PASS' \
        'DEMAND GLOBAL BLOCK BUDGET: PASS' \
        'DEMAND STORE FAILURE ROLLBACK: PASS' \
        'DEMAND EXECUTION FAILURE STATE ROLLBACK: PASS' \
        'DEMAND VALID FILLS REMAIN OWNED: PASS' \
        'DEMAND RUN ADVERSARIAL CONTRACT: PASS' \
        'PUBLIC DEMAND REAL EXECUTOR NONAARCH64 ROLLBACK: PASS' \
        'NATIVE DEMAND MULTI BLOCK ARM64 EXECUTION: NOT RUN' \
        'PRODUCTION PE STARTUP RUN: NOT IMPLEMENTED' \
        'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN' \
        'WINLATOR COMPARISON: NOT RUN' \
        'WIN32 TRANSLATED DEMAND RUN TEST: PASS'
    do
        count=$(grep -Fxc "$marker" "$log" || true)
        [ "$count" -eq 1 ] || {
            printf 'DEMAND RUN MARKER MISMATCH log=%s marker=[%s] count=%s\n' \
                "$log" "$marker" "$count" >&2
            cat "$log" >&2
            exit 1
        }
    done
done

printf '%s\n' 'DEMAND_RUN_STAGE=compile-sanitize'
set +e
# shellcheck disable=SC2086
gcc -Iinclude -Isrc -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$test_source" $sources -o "$build/sanitize-test" \
    > "$build/sanitize-compile.log" 2>&1
sanitize_compile_status=$?
set -e
if [ "$sanitize_compile_status" -ne 0 ]; then
    printf 'DEMAND_RUN_SANITIZER_COMPILE_FAILED exit=%s\n' \
        "$sanitize_compile_status" >&2
    cat "$build/sanitize-compile.log" >&2
    exit "$sanitize_compile_status"
fi

printf '%s\n' 'DEMAND_RUN_STAGE=run-sanitize'
set +e
ASAN_OPTIONS=detect_leaks=1 "$build/sanitize-test" \
    > "$build/sanitize.log" 2>&1
sanitize_status=$?
set -e
if [ "$sanitize_status" -ne 0 ]; then
    printf 'DEMAND_RUN_SANITIZER_TEST_FAILED exit=%s\n' \
        "$sanitize_status" >&2
    cat "$build/sanitize.log" >&2
    exit "$sanitize_status"
fi

grep -Fqx 'WIN32 TRANSLATED DEMAND RUN TEST: PASS' \
    "$build/sanitize.log" || {
    cat "$build/sanitize.log" >&2
    exit 1
}

printf '%s\n' 'DEMAND_RUN_STAGE=clang-analyze'
set +e
# shellcheck disable=SC2086
clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    "$test_source" $sources \
    > "$build/clang-analyze.log" 2>&1
analyze_status=$?
set -e
if [ "$analyze_status" -ne 0 ]; then
    printf 'DEMAND_RUN_CLANG_ANALYZE_FAILED exit=%s\n' \
        "$analyze_status" >&2
    cat "$build/clang-analyze.log" >&2
    exit "$analyze_status"
fi

printf '%s\n' 'WIN32 TRANSLATED DEMAND RUN FOCUSED VERIFY: PASS'
printf '%s\n' 'CACHE MISS TRANSLATE PUBLISH RETRY: PASS'
printf '%s\n' 'DEMAND CACHE HIT NO REPUBLISH VERIFY: PASS'
printf '%s\n' 'DEMAND GLOBAL BLOCK BUDGET VERIFY: PASS'
printf '%s\n' 'DEMAND CPU PROVIDER TRANSACTIONALITY: PASS'
printf '%s\n' 'DEMAND VALID CACHE FILL DURABILITY: PASS'
printf '%s\n' 'PRODUCTION DEMAND RUN LINK CONTRACT: PASS'
printf '%s\n' 'NATIVE DEMAND MULTI BLOCK ARM64 EXECUTION: NOT RUN'
printf '%s\n' 'PRODUCTION PE STARTUP RUN: NOT IMPLEMENTED'
printf '%s\n' 'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN'
printf '%s\n' 'WINLATOR COMPARISON: NOT RUN'
