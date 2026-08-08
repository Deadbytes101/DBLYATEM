#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/win32-pe-startup-check"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
startup_test='tests/win32_pe_startup_test.c'
provider_test='tests/win32_provider_tables_test.c'
startup_sources='
src/pe.c
src/map.c
src/reloc.c
src/reloc_apply.c
src/import.c
src/bind.c
src/iat_plan.c
src/iat_apply.c
src/gateway_plan.c
src/gateway_emit.c
src/gateway_decode.c
src/win32_provider.c
src/win32_provider_tables.c
src/win32_pe_startup.c
src/x64_block.c
src/x64_decode.c
src/x64_memory_instruction.c
src/x64_memory_decode.c
'
provider_sources='src/win32_provider.c src/win32_provider_tables.c'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

printf '%s\n' 'WIN32_PE_STARTUP_STAGE=files'
for required in \
    include/dblyat/win32_provider_tables.h \
    include/dblyat/win32_pe_startup.h \
    src/win32_provider_tables.c \
    src/win32_pe_startup.c \
    "$provider_test" \
    "$startup_test"
do
    [ -f "$required" ] || {
        printf 'WIN32_PE_STARTUP_REQUIRED_FILE_MISSING path=%s\n' \
            "$required" >&2
        exit 1
    }
done

printf '%s\n' 'WIN32_PE_STARTUP_STAGE=static-contract'
for requirement in \
    'db_win32_provider_tables_build' \
    'db_win32_pe_startup_prepare' \
    'db_pe_verify' \
    'db_map_pe64' \
    'db_reloc_plan_pe64' \
    'db_reloc_apply_pe64' \
    'db_import_plan_pe64' \
    'db_bind_plan_imports' \
    'db_gateway_plan_targets' \
    'db_gateway_emit_slots' \
    'db_iat_plan_patches' \
    'db_iat_apply_patches' \
    'db_win32_kernel32_catalog' \
    'DB_X64_RFLAGS_FIXED_ONE'
do
    grep -R -F "$requirement" \
        include/dblyat/win32_provider_tables.h \
        include/dblyat/win32_pe_startup.h \
        src/win32_provider_tables.c \
        src/win32_pe_startup.c >/dev/null || {
            printf 'WIN32_PE_STARTUP_STATIC_REQUIREMENT_MISSING token=%s\n' \
                "$requirement" >&2
            exit 1
        }
done

if grep -E 'KERNEL32|GetLastError|SetLastError|ExitProcess|GetCurrentProcessId|GetCurrentThreadId' \
    src/win32_pe_startup.c >/dev/null
then
    printf '%s\n' 'WIN32 PE STARTUP DUPLICATED PROVIDER STRING TRUTH' >&2
    exit 1
fi

if grep -E '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|mprotect|munmap|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync)([^[:alnum:]_]|$)' \
    src/win32_provider_tables.c src/win32_pe_startup.c >/dev/null
then
    printf '%s\n' 'WIN32 PE STARTUP CROSSED OWNERSHIP OR MAPPING BOUNDARY' >&2
    exit 1
fi

for compiler in gcc clang
do
    printf 'WIN32_PE_STARTUP_STAGE=compile-provider-%s\n' "$compiler"
    set +e
    # shellcheck disable=SC2086
    "$compiler" $common "$provider_test" $provider_sources \
        -o "$build/$compiler-provider-test" \
        > "$build/$compiler-provider-compile.log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf 'WIN32_PE_STARTUP_PROVIDER_COMPILE_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$status" >&2
        cat "$build/$compiler-provider-compile.log" >&2
        exit "$status"
    fi

    printf 'WIN32_PE_STARTUP_STAGE=run-provider-%s\n' "$compiler"
    set +e
    "$build/$compiler-provider-test" \
        > "$build/$compiler-provider.log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf 'WIN32_PE_STARTUP_PROVIDER_TEST_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$status" >&2
        cat "$build/$compiler-provider.log" >&2
        exit "$status"
    fi

    printf 'WIN32_PE_STARTUP_STAGE=compile-startup-%s\n' "$compiler"
    set +e
    # shellcheck disable=SC2086
    "$compiler" $common "$startup_test" $startup_sources \
        -o "$build/$compiler-startup-test" \
        > "$build/$compiler-startup-compile.log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf 'WIN32_PE_STARTUP_COMPILE_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$status" >&2
        cat "$build/$compiler-startup-compile.log" >&2
        exit "$status"
    fi

    printf 'WIN32_PE_STARTUP_STAGE=run-startup-%s\n' "$compiler"
    set +e
    "$build/$compiler-startup-test" \
        > "$build/$compiler-startup.log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf 'WIN32_PE_STARTUP_TEST_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$status" >&2
        cat "$build/$compiler-startup.log" >&2
        exit "$status"
    fi
done

printf '%s\n' 'WIN32_PE_STARTUP_STAGE=marker-validation'
for log in "$build/gcc-startup.log" "$build/clang-startup.log"
do
    for marker in \
        'WIN32 PE STARTUP TEST: PASS' \
        'PE MAP RELOC IMPORT BOOTSTRAP: PASS' \
        'CATALOG DERIVED KERNEL32 BINDING: PASS' \
        'DBGT IAT PATCH AGREEMENT: PASS' \
        'ENTRY FF2 IAT CONTROL FLOW: PASS' \
        'INITIAL X64 STATE CONTRACT: PASS' \
        'WHOLE STARTUP TRANSACTIONALITY: PASS' \
        'HOST FILE LOADING CLI: NOT IMPLEMENTED' \
        'NATIVE GUEST MEMORY BACKEND: NOT IMPLEMENTED' \
        'NATIVE ARM64 PE EXECUTION: NOT RUN' \
        'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN' \
        'WINLATOR COMPARISON: NOT RUN'
    do
        count=$(grep -Fxc "$marker" "$log" || true)
        [ "$count" -eq 1 ] || {
            printf 'WIN32_PE_STARTUP_MARKER_MISMATCH log=%s marker=[%s] count=%s\n' \
                "$log" "$marker" "$count" >&2
            cat "$log" >&2
            exit 1
        }
    done
done

for log in "$build/gcc-provider.log" "$build/clang-provider.log"
do
    count=$(grep -Fxc 'WIN32 PROVIDER TABLES TEST: PASS' "$log" || true)
    [ "$count" -eq 1 ] || {
        printf 'WIN32_PE_STARTUP_PROVIDER_MARKER_MISMATCH log=%s count=%s\n' \
            "$log" "$count" >&2
        cat "$log" >&2
        exit 1
    }
done

printf '%s\n' 'WIN32_PE_STARTUP_STAGE=compile-sanitize'
set +e
# shellcheck disable=SC2086
gcc -Iinclude -Isrc -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$startup_test" $startup_sources -o "$build/sanitize-startup-test" \
    > "$build/sanitize-compile.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'WIN32_PE_STARTUP_SANITIZER_COMPILE_FAILED exit=%s\n' \
        "$status" >&2
    cat "$build/sanitize-compile.log" >&2
    exit "$status"
fi

printf '%s\n' 'WIN32_PE_STARTUP_STAGE=run-sanitize'
set +e
ASAN_OPTIONS=detect_leaks=1 "$build/sanitize-startup-test" \
    > "$build/sanitize.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'WIN32_PE_STARTUP_SANITIZER_TEST_FAILED exit=%s\n' \
        "$status" >&2
    cat "$build/sanitize.log" >&2
    exit "$status"
fi

printf '%s\n' 'WIN32_PE_STARTUP_STAGE=clang-analyze'
set +e
# shellcheck disable=SC2086
clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    src/win32_provider_tables.c src/win32_pe_startup.c \
    > "$build/clang-analyze.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'WIN32_PE_STARTUP_CLANG_ANALYZE_FAILED exit=%s\n' \
        "$status" >&2
    cat "$build/clang-analyze.log" >&2
    exit "$status"
fi

printf '%s\n' 'WIN32 PE STARTUP FOCUSED VERIFY: PASS'
printf '%s\n' 'PROVIDER CATALOG ADAPTER: PASS'
printf '%s\n' 'PE STAGING TRANSACTION: PASS'
printf '%s\n' 'RELOC IMPORT DBGT IAT COMPOSITION: PASS'
printf '%s\n' 'ENTRY FF2 IAT BOOTSTRAP: PASS'
printf '%s\n' 'FINAL OUTPUT FAILURE PRESERVATION: PASS'
printf '%s\n' 'HOST FILE LOADING CLI: NOT IMPLEMENTED'
printf '%s\n' 'NATIVE GUEST MEMORY BACKEND: NOT IMPLEMENTED'
printf '%s\n' 'NATIVE ARM64 PE EXECUTION: NOT RUN'
printf '%s\n' 'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN'
printf '%s\n' 'WINLATOR COMPARISON: NOT RUN'
