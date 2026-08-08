#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/win32-demand-guest-memory-check"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
test_source='tests/win32_translated_demand_guest_memory_check.c'
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

stage()
{
    printf 'WIN32_DEMAND_GUEST_MEMORY_STAGE=%s\n' "$1"
}

fail()
{
    printf 'WIN32_DEMAND_GUEST_MEMORY_FAILURE=%s\n' "$1" >&2
    exit 1
}

stage files
for required in \
    include/dblyat/win32_translated_execution.h \
    include/dblyat/win32_translated_run.h \
    include/dblyat/win32_translated_demand_run.h \
    src/win32_translated_execution.c \
    src/win32_translated_run.c \
    src/win32_translated_demand_run.c \
    "$test_source" \
    tools/check-win32-translated-demand-run.sh \
    tools/check-linux-arm64-guest-runtime.sh
do
    [ -f "$required" ] || fail "missing file $required"
done

stage static-contract
for requirement in \
    'db_win32_translated_execution_step_with_guest_memory' \
    'db_linux_arm64_translation_context_bind_guest_memory' \
    'db_linux_arm64_guest_memory_init' \
    'DB_WIN32_TRANSLATED_RUN_FORMAT_VERSION UINT32_C(2)' \
    'DB_WIN32_TRANSLATED_DEMAND_RUN_FORMAT_VERSION UINT32_C(2)' \
    'DB_WIN32_TRANSLATED_DEMAND_RUN_BAD_GUEST_MEMORY' \
    'DB_WIN32_TRANSLATED_DEMAND_RUN_CODE_NOT_MAPPED' \
    'DB_WIN32_TRANSLATED_DEMAND_CODE_IMAGE' \
    'DB_WIN32_TRANSLATED_DEMAND_CODE_GATEWAY'
do
    grep -F "$requirement" \
        include/dblyat/win32_translated_execution.h \
        include/dblyat/win32_translated_run.h \
        include/dblyat/win32_translated_demand_run.h \
        src/win32_translated_execution.c \
        src/win32_translated_run.c \
        src/win32_translated_demand_run.c >/dev/null ||
        fail "missing static contract token $requirement"
done

grep -F 'return db_win32_translated_execution_step_with_guest_memory(' \
    src/win32_translated_execution.c >/dev/null ||
    fail 'old execution-step API is not a compatibility wrapper'

grep -F 'selected->code = gateway_code;' \
    src/win32_translated_demand_run.c >/dev/null ||
    fail 'gateway temporary code view is not selected'

grep -F 'config->translation->code' \
    src/win32_translated_demand_run.c >/dev/null ||
    fail 'image code view missing'

if grep -F 'db_win32_translated_execution_step_with_guest_memory' \
    src/arm64_translation_store.c src/x64_block.c >/dev/null
then
    fail 'generic translation store or block layer gained Win32 execution semantics'
fi

if grep -E '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|mprotect|munmap|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync)([^[:alnum:]_]|$)' \
    include/dblyat/win32_translated_execution.h \
    include/dblyat/win32_translated_run.h \
    include/dblyat/win32_translated_demand_run.h \
    src/win32_translated_execution.c \
    src/win32_translated_run.c \
    src/win32_translated_demand_run.c >/dev/null
then
    fail 'composition crossed allocation mapping or concurrency boundary'
fi

stage demand-regression
sh tools/check-win32-translated-demand-run.sh \
    > "$build/demand-regression.log" 2>&1 || {
    cat "$build/demand-regression.log" >&2
    exit 1
}
grep -Fqx 'WIN32 TRANSLATED DEMAND RUN FOCUSED VERIFY: PASS' \
    "$build/demand-regression.log" || {
    cat "$build/demand-regression.log" >&2
    exit 1
}

stage guest-runtime-regression
sh tools/check-linux-arm64-guest-runtime.sh \
    > "$build/guest-runtime-regression.log" 2>&1 || {
    cat "$build/guest-runtime-regression.log" >&2
    exit 1
}
grep -Fqx 'LINUX ARM64 GUEST RUNTIME FOCUSED VERIFY: PASS' \
    "$build/guest-runtime-regression.log" || {
    cat "$build/guest-runtime-regression.log" >&2
    exit 1
}

for compiler in gcc clang
do
    binary="$build/$compiler-check"
    compile_log="$build/$compiler-compile.log"
    run_log="$build/$compiler.log"

    stage "compile-$compiler"
    set +e
    # shellcheck disable=SC2086
    "$compiler" $common "$test_source" $sources -o "$binary" \
        > "$compile_log" 2>&1
    compile_status=$?
    set -e
    if [ "$compile_status" -ne 0 ]; then
        printf 'WIN32_DEMAND_GUEST_MEMORY_COMPILE_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$compile_status" >&2
        cat "$compile_log" >&2
        exit "$compile_status"
    fi

    stage "run-$compiler"
    set +e
    "$binary" > "$run_log" 2>&1
    run_status=$?
    set -e
    if [ "$run_status" -ne 0 ]; then
        printf 'WIN32_DEMAND_GUEST_MEMORY_TEST_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$run_status" >&2
        cat "$run_log" >&2
        exit "$run_status"
    fi
done

stage marker-validation
for log in "$build/gcc.log" "$build/clang.log"
do
    for marker in \
        'WIN32 DEMAND GUEST MEMORY CHECK: PASS' \
        'DEMAND GUEST MEMORY DESCRIPTOR PROPAGATION: PASS' \
        'IMAGE GATEWAY TWO REGION TRANSLATION: PASS' \
        'UNMAPPED CODE REGION ARCH ROLLBACK: PASS' \
        'VALID CACHE PUBLICATION SURVIVES LATER MISS: PASS' \
        'BAD GUEST MEMORY PREEXECUTION REJECTION: PASS' \
        'OLD EXECUTION STEP API COMPATIBILITY: STATIC CONTRACT' \
        'NATIVE ARM64 PE EXECUTION: NOT RUN' \
        'HOST RUN CLI: NOT IMPLEMENTED' \
        'PRECISE X86 PAGE FAULT MODEL: NOT IMPLEMENTED' \
        'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN' \
        'WINLATOR COMPARISON: NOT RUN'
    do
        count=$(grep -Fxc "$marker" "$log" || true)
        [ "$count" -eq 1 ] || {
            printf 'WIN32_DEMAND_GUEST_MEMORY_MARKER_MISMATCH log=%s marker=[%s] count=%s\n' \
                "$log" "$marker" "$count" >&2
            cat "$log" >&2
            exit 1
        }
    done
done

stage compile-sanitize
set +e
# shellcheck disable=SC2086
gcc -Iinclude -Isrc -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$test_source" $sources -o "$build/sanitize-check" \
    > "$build/sanitize-compile.log" 2>&1
sanitize_compile_status=$?
set -e
if [ "$sanitize_compile_status" -ne 0 ]; then
    printf 'WIN32_DEMAND_GUEST_MEMORY_SANITIZER_COMPILE_FAILED exit=%s\n' \
        "$sanitize_compile_status" >&2
    cat "$build/sanitize-compile.log" >&2
    exit "$sanitize_compile_status"
fi

stage run-sanitize
set +e
ASAN_OPTIONS=detect_leaks=1 "$build/sanitize-check" \
    > "$build/sanitize.log" 2>&1
sanitize_status=$?
set -e
if [ "$sanitize_status" -ne 0 ]; then
    printf 'WIN32_DEMAND_GUEST_MEMORY_SANITIZER_TEST_FAILED exit=%s\n' \
        "$sanitize_status" >&2
    cat "$build/sanitize.log" >&2
    exit "$sanitize_status"
fi
grep -Fqx 'WIN32 DEMAND GUEST MEMORY CHECK: PASS' \
    "$build/sanitize.log" || {
    cat "$build/sanitize.log" >&2
    exit 1
}

stage clang-analyze
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
    printf 'WIN32_DEMAND_GUEST_MEMORY_CLANG_ANALYZE_FAILED exit=%s\n' \
        "$analyze_status" >&2
    cat "$build/clang-analyze.log" >&2
    exit "$analyze_status"
fi

printf '%s\n' 'WIN32 DEMAND GUEST MEMORY FOCUSED VERIFY: PASS'
printf '%s\n' 'GUEST MEMORY EXECUTION BINDING: PASS'
printf '%s\n' 'IMAGE GATEWAY MULTI REGION CODE: PASS'
printf '%s\n' 'UNMAPPED CODE FAIL CLOSED: PASS'
printf '%s\n' 'CPU KERNEL32 INFRA FAILURE ROLLBACK: PASS'
printf '%s\n' 'VALID TRANSLATION CACHE DURABILITY: PASS'
printf '%s\n' 'LEGACY TERMINAL ONLY DEMAND REGRESSION: PASS'
printf '%s\n' 'GENERIC TRANSLATION STORE API UNCHANGED: PASS'
printf '%s\n' 'GENERIC X64 BLOCK API UNCHANGED: PASS'
printf '%s\n' 'NATIVE ARM64 HELPER EXECUTION: NOT RUN'
printf '%s\n' 'NATIVE ARM64 PE EXECUTION: NOT RUN'
printf '%s\n' 'HOST RUN CLI: NOT IMPLEMENTED'
printf '%s\n' 'PRECISE X86 PAGE FAULT MODEL: NOT IMPLEMENTED'
printf '%s\n' 'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN'
printf '%s\n' 'WINLATOR COMPARISON: NOT RUN'
