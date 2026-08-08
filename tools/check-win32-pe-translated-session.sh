#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/win32-pe-translated-session-check"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
test_source='tests/win32_pe_translated_session_check.c'
sources='
src/win32_pe_translated_session.c
src/win32_pe_startup.c
src/win32_provider_tables.c
src/pe.c
src/map.c
src/reloc.c
src/reloc_apply.c
src/import.c
src/bind.c
src/iat_plan.c
src/iat_apply.c
src/gateway_plan.c
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

stage() { printf 'WIN32_PE_TRANSLATED_SESSION_STAGE=%s\n' "$1"; }
fail() { printf 'WIN32_PE_TRANSLATED_SESSION_FAILURE=%s\n' "$1" >&2; exit 1; }

stage files
for required in \
    include/dblyat/win32_pe_translated_session.h \
    src/win32_pe_translated_session_internal.h \
    src/win32_pe_translated_session.c \
    "$test_source" \
    tools/check-win32-pe-startup.sh \
    tools/check-win32-demand-guest-memory.sh
do
    [ -f "$required" ] || fail "missing file $required"
done

stage static-contract
for requirement in \
    'DB_WIN32_PE_TRANSLATED_SESSION_FORMAT_VERSION UINT32_C(1)' \
    'DB_WIN32_PE_TRANSLATED_SESSION_EMPTY' \
    'DB_WIN32_PE_TRANSLATED_SESSION_READY' \
    'DB_WIN32_PE_TRANSLATED_SESSION_TERMINATED' \
    'DB_WIN32_PE_TRANSLATED_SESSION_CLEANUP_REQUIRED' \
    'db_win32_pe_translated_session_prepare' \
    'db_win32_pe_translated_session_run' \
    'db_win32_pe_translated_session_dispose' \
    'db_linux_arm64_guest_runtime_helper_targets' \
    'db_win32_pe_startup_prepare' \
    'db_win32_translated_demand_run' \
    'db_arm64_translation_store_dispose'
do
    grep -F "$requirement" \
        include/dblyat/win32_pe_translated_session.h \
        src/win32_pe_translated_session.c >/dev/null ||
        fail "missing static contract token $requirement"
done

for derived in \
    'session->startup.page_entries_committed' \
    'session->startup.gateway_bytes_committed' \
    'session->startup.gateway_plan.target_count' \
    'session->startup.provider_tables.handler_count' \
    'session->startup.provider_tables.signature_count'
do
    grep -F "$derived" src/win32_pe_translated_session.c >/dev/null ||
        fail "runtime count not derived from startup summary: $derived"
done

for private_symbol in \
    'db_win32_pe_translated_session_prepare_with_helper_targets' \
    'db_win32_pe_translated_session_run_with_executor'
do
    grep -F "$private_symbol" src/win32_pe_translated_session_internal.h >/dev/null ||
        fail "private seam missing: $private_symbol"
    if grep -F "$private_symbol" include/dblyat/win32_pe_translated_session.h >/dev/null
    then
        fail "private seam leaked into public API: $private_symbol"
    fi
done

if grep -E '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|mprotect|munmap|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync)([^[:alnum:]_]|$)' \
    include/dblyat/win32_pe_translated_session.h \
    src/win32_pe_translated_session_internal.h \
    src/win32_pe_translated_session.c >/dev/null
then
    fail 'session crossed allocation mapping or concurrency boundary'
fi

for production_token in \
    'src/win32_pe_translated_session.c' \
    'include/dblyat/win32_pe_translated_session.h'
do
    grep -F "$production_token" Makefile >/dev/null ||
        fail "production core token missing: $production_token"
done

stage production-link
rm -f build/dblyatem
set +e
make -s build/dblyatem > "$build/production-link.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'WIN32_PE_TRANSLATED_SESSION_PRODUCTION_LINK_FAILED exit=%s\n' "$status" >&2
    cat "$build/production-link.log" >&2
    exit "$status"
fi
[ -x build/dblyatem ] || fail 'production binary not produced'

stage startup-regression
sh tools/check-win32-pe-startup.sh > "$build/startup-regression.log" 2>&1 || {
    cat "$build/startup-regression.log" >&2
    exit 1
}
grep -Fqx 'WIN32 PE STARTUP FOCUSED VERIFY: PASS' "$build/startup-regression.log" || {
    cat "$build/startup-regression.log" >&2
    exit 1
}

stage demand-regression
sh tools/check-win32-demand-guest-memory.sh > "$build/demand-regression.log" 2>&1 || {
    cat "$build/demand-regression.log" >&2
    exit 1
}
grep -Fqx 'WIN32 DEMAND GUEST MEMORY FOCUSED VERIFY: PASS' "$build/demand-regression.log" || {
    cat "$build/demand-regression.log" >&2
    exit 1
}

for compiler in gcc clang
do
    stage "compile-$compiler"
    set +e
    # shellcheck disable=SC2086
    "$compiler" $common "$test_source" $sources -o "$build/$compiler-check" \
        > "$build/$compiler-compile.log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf 'WIN32_PE_TRANSLATED_SESSION_COMPILE_FAILED compiler=%s exit=%s\n' "$compiler" "$status" >&2
        cat "$build/$compiler-compile.log" >&2
        exit "$status"
    fi

    stage "run-$compiler"
    set +e
    "$build/$compiler-check" > "$build/$compiler.log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf 'WIN32_PE_TRANSLATED_SESSION_TEST_FAILED compiler=%s exit=%s\n' "$compiler" "$status" >&2
        cat "$build/$compiler.log" >&2
        exit "$status"
    fi
done

stage marker-validation
for log in "$build/gcc.log" "$build/clang.log"
do
    for marker in \
        'WIN32 PE TRANSLATED SESSION CHECK: PASS' \
        'SESSION STARTUP DERIVED COUNTS: PASS' \
        'SESSION REPEATED RUN CACHE REUSE: PASS' \
        'SESSION READY TO TERMINATED: PASS' \
        'SESSION TERMINATED RUN REJECTION: PASS' \
        'SESSION INFRA FAILURE ARCH ROLLBACK: PASS' \
        'SESSION VALID CACHE DURABILITY: PASS' \
        'SESSION STARTUP FAILURE EMPTY: PASS' \
        'SESSION DISPOSE RETRY: PASS' \
        'HOST FILE LOADING CLI: NOT IMPLEMENTED' \
        'NATIVE ARM64 PE SESSION EXECUTION: NOT RUN' \
        'PRECISE X86 PAGE FAULT MODEL: NOT IMPLEMENTED' \
        'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN' \
        'WINLATOR COMPARISON: NOT RUN'
    do
        count=$(grep -Fxc "$marker" "$log" || true)
        [ "$count" -eq 1 ] || {
            printf 'WIN32_PE_TRANSLATED_SESSION_MARKER_MISMATCH log=%s marker=[%s] count=%s\n' "$log" "$marker" "$count" >&2
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
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'WIN32_PE_TRANSLATED_SESSION_SANITIZER_COMPILE_FAILED exit=%s\n' "$status" >&2
    cat "$build/sanitize-compile.log" >&2
    exit "$status"
fi

stage run-sanitize
set +e
ASAN_OPTIONS=detect_leaks=1 "$build/sanitize-check" > "$build/sanitize.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'WIN32_PE_TRANSLATED_SESSION_SANITIZER_TEST_FAILED exit=%s\n' "$status" >&2
    cat "$build/sanitize.log" >&2
    exit "$status"
fi
grep -Fqx 'WIN32 PE TRANSLATED SESSION CHECK: PASS' "$build/sanitize.log" || {
    cat "$build/sanitize.log" >&2
    exit 1
}

stage clang-analyze
set +e
# shellcheck disable=SC2086
clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    "$test_source" $sources > "$build/clang-analyze.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'WIN32_PE_TRANSLATED_SESSION_CLANG_ANALYZE_FAILED exit=%s\n' "$status" >&2
    cat "$build/clang-analyze.log" >&2
    exit "$status"
fi

printf '%s\n' 'WIN32 PE TRANSLATED SESSION FOCUSED VERIFY: PASS'
printf '%s\n' 'SESSION STARTUP RUNTIME COMPOSITION: PASS'
printf '%s\n' 'SESSION EXACT SUMMARY COUNTS: PASS'
printf '%s\n' 'SESSION MULTI RUN CACHE OWNERSHIP: PASS'
printf '%s\n' 'SESSION TERMINATION LIFECYCLE: PASS'
printf '%s\n' 'SESSION INFRA FAILURE TRANSACTIONALITY: PASS'
printf '%s\n' 'SESSION DISPOSE RETRY CONTRACT: PASS'
printf '%s\n' 'PRIVATE TEST SEAMS NOT PUBLIC: PASS'
printf '%s\n' 'PRODUCTION SESSION LINK CONTRACT: PASS'
printf '%s\n' 'HOST FILE LOADING CLI: NOT IMPLEMENTED'
printf '%s\n' 'NATIVE ARM64 PE SESSION EXECUTION: NOT RUN'
printf '%s\n' 'PRECISE X86 PAGE FAULT MODEL: NOT IMPLEMENTED'
printf '%s\n' 'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN'
printf '%s\n' 'WINLATOR COMPARISON: NOT RUN'
