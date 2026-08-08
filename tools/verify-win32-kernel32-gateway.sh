#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/win32-kernel32-gateway-verify"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
sources='src/win32_provider.c src/win32_kernel32_gateway.c src/win32_kernel32_invoke.c src/win32_kernel32.c src/gateway_auth_complete.c src/gateway_result.c src/gateway_complete.c src/gateway_return.c src/gateway_return_apply.c'
runtime_sources="$sources src/win32_runtime_dispatch.c"
bridge_sources="$runtime_sources src/win32_translated_gateway_bridge.c src/translated_terminal_exit.c src/gateway_decode.c src/gateway_lookup.c src/gateway_dispatch.c src/gateway_frame.c src/gateway_enter.c src/gateway_invoke.c src/gateway_emit.c"
execution_sources="$bridge_sources src/win32_translated_execution.c src/linux_arm64_translation_entry.c"
run_sources="$execution_sources src/arm64_block_cache.c src/win32_translated_run.c"
bridge_header='include/dblyat/win32_translated_gateway_bridge.h'
bridge_source='src/win32_translated_gateway_bridge.c'
bridge_test='tests/win32_translated_gateway_bridge_test.c'
execution_header='include/dblyat/win32_translated_execution.h'
execution_source='src/win32_translated_execution.c'
execution_test='tests/win32_translated_execution_test.c'
run_header='include/dblyat/win32_translated_run.h'
run_internal='src/win32_translated_run_internal.h'
run_source='src/win32_translated_run.c'
run_test='tests/win32_translated_run_test.c'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

[ -f "$bridge_header" ]
[ -f "$bridge_source" ]
[ -f "$bridge_test" ]
[ -f "$execution_header" ]
[ -f "$execution_source" ]
[ -f "$execution_test" ]
[ -f "$run_header" ]
[ -f "$run_internal" ]
[ -f "$run_source" ]
[ -f "$run_test" ]

grep -F 'db_translated_terminal_exit_validate_observed' "$bridge_source" >/dev/null
grep -F 'DB_IR_TRAP_INVALID_OPCODE' "$bridge_source" >/dev/null
grep -F 'db_gateway_lookup_rip' "$bridge_source" >/dev/null
grep -F 'db_gateway_decode_slots' "$bridge_source" >/dev/null
grep -F 'db_gateway_enter_plan' "$bridge_source" >/dev/null
grep -F 'db_gateway_invoke_plan' "$bridge_source" >/dev/null
grep -F 'db_win32_runtime_dispatch_run' "$bridge_source" >/dev/null
grep -F 'DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_NOT_GATEWAY' "$bridge_source" >/dev/null
grep -F 'DB_WIN32_RUNTIME_OUTCOME_GUEST_TERMINATED' "$bridge_test" >/dev/null
grep -F 'ORDINARY UD2 PRESERVATION: PASS' "$bridge_test" >/dev/null

grep -F 'db_linux_arm64_translation_enter' "$execution_source" >/dev/null
grep -F 'db_win32_translated_gateway_bridge_run' "$execution_source" >/dev/null
grep -F 'DB_TRANSLATED_TERMINAL_EXIT_DIRECT' "$execution_source" >/dev/null
grep -F 'DB_TRANSLATED_TERMINAL_EXIT_TARGET' "$execution_source" >/dev/null
grep -F 'DB_TRANSLATED_TERMINAL_EXIT_CONDITIONAL' "$execution_source" >/dev/null
grep -F 'DB_TRANSLATED_TERMINAL_EXIT_TRAP' "$execution_source" >/dev/null
grep -F 'DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP' "$execution_source" >/dev/null

grep -F 'db_arm64_block_cache_lookup' "$run_source" >/dev/null
grep -F 'db_win32_translated_execution_step' "$run_source" >/dev/null
grep -F 'DB_WIN32_TRANSLATED_RUN_STOP_CACHE_MISS' "$run_source" >/dev/null
grep -F 'DB_WIN32_TRANSLATED_RUN_STOP_GUEST_TRAP' "$run_source" >/dev/null
grep -F 'DB_WIN32_TRANSLATED_RUN_STOP_GUEST_TERMINATED' "$run_source" >/dev/null
grep -F 'DB_WIN32_TRANSLATED_RUN_STOP_BLOCK_BUDGET' "$run_source" >/dev/null
grep -F 'db_win32_translated_run_with_executor' "$run_internal" >/dev/null

if grep -F 'db_win32_translated_run_with_executor' "$run_header" >/dev/null
then
    printf '%s\n' 'TRANSLATED RUN TEST EXECUTOR SEAM LEAKED INTO PUBLIC API' >&2
    exit 1
fi

grep -F 'src/win32_translated_gateway_bridge.c' GNUmakefile >/dev/null
grep -F 'src/win32_translated_execution.c' GNUmakefile >/dev/null
grep -F 'src/translated_terminal_exit.c' GNUmakefile >/dev/null
grep -F 'src/linux_arm64_translation_entry.c' GNUmakefile >/dev/null
grep -F 'src/arm64_block_cache.c' GNUmakefile >/dev/null
grep -F 'src/win32_translated_run.c' GNUmakefile >/dev/null
grep -F 'include/dblyat/win32_translated_execution.h' GNUmakefile >/dev/null
grep -F 'include/dblyat/win32_translated_run.h' GNUmakefile >/dev/null

if grep -F 'db_gateway_executor' "$bridge_source" "$bridge_header" >/dev/null
then
    printf '%s\n' 'TRANSLATED WIN32 BRIDGE CROSSED GENERIC GATEWAY EXECUTOR BOUNDARY' >&2
    exit 1
fi

if grep -F 'db_linux_arm64_' "$bridge_source" "$bridge_header" >/dev/null
then
    printf '%s\n' 'TRANSLATED WIN32 BRIDGE CONTAMINATED ARM64 TERMINAL LAYER' >&2
    exit 1
fi

if grep -F 'db_arm64_block_cache_lookup' "$execution_source" "$execution_header" >/dev/null
then
    printf '%s\n' 'TRANSLATED EXECUTION STEP CROSSED CACHE LOOP BOUNDARY' >&2
    exit 1
fi

if grep -F 'db_x64_block_build' "$execution_source" "$execution_header" >/dev/null
then
    printf '%s\n' 'TRANSLATED EXECUTION STEP CROSSED TRANSLATION BOUNDARY' >&2
    exit 1
fi

if grep -F 'db_arm64_block_cache_insert' \
    "$run_source" "$run_header" "$run_internal" >/dev/null
then
    printf '%s\n' 'TRANSLATED RUN LOOP CROSSED CACHE PUBLICATION BOUNDARY' >&2
    exit 1
fi

if grep -E 'db_x64_block_build|db_arm64_translation_reuse_publish|db_arm64_published_block_build|db_linux_arm64_wx_publish' \
    "$run_source" "$run_header" "$run_internal" >/dev/null
then
    printf '%s\n' 'TRANSLATED RUN LOOP CROSSED CACHE-MISS TRANSLATION BOUNDARY' >&2
    exit 1
fi

if grep -E '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|mprotect|munmap|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync)([^[:alnum:]_]|$)' \
    "$bridge_source" "$bridge_header" \
    "$execution_source" "$execution_header" \
    "$run_source" "$run_header" "$run_internal" >/dev/null
then
    printf '%s\n' 'TRANSLATED WIN32 CONTROL PLANE CROSSED OWNERSHIP, MAPPING, OR CONCURRENCY BOUNDARY' >&2
    exit 1
fi

gcc $common tests/win32_kernel32_gateway_test.c $sources -o "$build/gcc-test"
"$build/gcc-test"

clang $common tests/win32_kernel32_gateway_test.c $sources -o "$build/clang-test"
"$build/clang-test"

gcc $common tests/win32_exit_process_boundary_test.c $sources \
    -o "$build/gcc-exit-process-test"
"$build/gcc-exit-process-test"

clang $common tests/win32_exit_process_boundary_test.c $sources \
    -o "$build/clang-exit-process-test"
"$build/clang-exit-process-test"

gcc $common tests/win32_runtime_dispatch_test.c $runtime_sources \
    -o "$build/gcc-runtime-dispatch-test"
"$build/gcc-runtime-dispatch-test"

clang $common tests/win32_runtime_dispatch_test.c $runtime_sources \
    -o "$build/clang-runtime-dispatch-test"
"$build/clang-runtime-dispatch-test"

gcc $common "$bridge_test" $bridge_sources \
    -o "$build/gcc-translated-bridge-test"
"$build/gcc-translated-bridge-test" > "$build/gcc-translated-bridge.log"

clang $common "$bridge_test" $bridge_sources \
    -o "$build/clang-translated-bridge-test"
"$build/clang-translated-bridge-test" > "$build/clang-translated-bridge.log"

for log in \
    "$build/gcc-translated-bridge.log" \
    "$build/clang-translated-bridge.log"
do
    for marker in \
        'WIN32 TRANSLATED GATEWAY BRIDGE TEST: PASS' \
        'ORDINARY UD2 PRESERVATION: PASS' \
        'DBGT AUTHENTICATED CONTINUE: PASS' \
        'DBGT EXITPROCESS TERMINATION: PASS' \
        'DBGT ADVERSARIAL TRANSACTIONALITY: PASS' \
        'SCALAR GATEWAY REGISTER MAPPING: PASS'
    do
        count=$(grep -Fxc "$marker" "$log" || true)
        [ "$count" -eq 1 ] || {
            printf 'TRANSLATED BRIDGE MARKER MISMATCH log=%s marker=[%s] count=%s\n' \
                "$log" "$marker" "$count" >&2
            exit 1
        }
    done
done

gcc $common "$execution_test" $execution_sources \
    -o "$build/gcc-translated-execution-test"
"$build/gcc-translated-execution-test" > "$build/gcc-translated-execution.log"

clang $common "$execution_test" $execution_sources \
    -o "$build/clang-translated-execution-test"
"$build/clang-translated-execution-test" > "$build/clang-translated-execution.log"

for log in \
    "$build/gcc-translated-execution.log" \
    "$build/clang-translated-execution.log"
do
    for marker in \
        'WIN32 TRANSLATED EXECUTION TEST: PASS' \
        'TRANSLATED DIRECT TARGET CONDITIONAL RESOLUTION: PASS' \
        'TRANSLATED ORDINARY UD2 OUTCOME: PASS' \
        'TRANSLATED DBGT CONTINUE COORDINATION: PASS' \
        'TRANSLATED DBGT TERMINATION COORDINATION: PASS' \
        'TRANSLATED EXECUTION TRANSACTIONALITY: PASS' \
        'TRANSLATED NATIVE ENTRY ON THIS HOST: NOT RUN'
    do
        count=$(grep -Fxc "$marker" "$log" || true)
        [ "$count" -eq 1 ] || {
            printf 'TRANSLATED EXECUTION MARKER MISMATCH log=%s marker=[%s] count=%s\n' \
                "$log" "$marker" "$count" >&2
            exit 1
        }
    done
done

gcc $common "$run_test" $run_sources \
    -o "$build/gcc-translated-run-test"
"$build/gcc-translated-run-test" > "$build/gcc-translated-run.log"

clang $common "$run_test" $run_sources \
    -o "$build/clang-translated-run-test"
"$build/clang-translated-run-test" > "$build/clang-translated-run.log"

for log in \
    "$build/gcc-translated-run.log" \
    "$build/clang-translated-run.log"
do
    for marker in \
        'WIN32 TRANSLATED RUN TEST: PASS' \
        'CACHE HIT CONTINUE TERMINATE: PASS' \
        'CACHE HIT MISS COMMIT: PASS' \
        'GUEST TRAP PROPAGATION: PASS' \
        'BLOCK BUDGET EXHAUSTION: PASS' \
        'WHOLE RUN FAILURE TRANSACTIONALITY: PASS' \
        'PUBLIC REAL EXECUTOR NONAARCH64 ROLLBACK: PASS' \
        'NATIVE MULTI BLOCK ARM64 EXECUTION: NOT RUN' \
        'CACHE MISS TRANSLATION: NOT IMPLEMENTED'
    do
        count=$(grep -Fxc "$marker" "$log" || true)
        [ "$count" -eq 1 ] || {
            printf 'TRANSLATED RUN MARKER MISMATCH log=%s marker=[%s] count=%s\n' \
                "$log" "$marker" "$count" >&2
            exit 1
        }
    done
done

gcc -Iinclude -Isrc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/win32_kernel32_gateway_test.c $sources \
    -o "$build/sanitizer-test"
"$build/sanitizer-test"

gcc -Iinclude -Isrc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/win32_exit_process_boundary_test.c $sources \
    -o "$build/sanitizer-exit-process-test"
"$build/sanitizer-exit-process-test"

gcc -Iinclude -Isrc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/win32_runtime_dispatch_test.c $runtime_sources \
    -o "$build/sanitizer-runtime-dispatch-test"
"$build/sanitizer-runtime-dispatch-test"

gcc -Iinclude -Isrc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$bridge_test" $bridge_sources \
    -o "$build/sanitizer-translated-bridge-test"
ASAN_OPTIONS=detect_leaks=1 \
    "$build/sanitizer-translated-bridge-test" > \
    "$build/sanitizer-translated-bridge.log"

for marker in \
    'WIN32 TRANSLATED GATEWAY BRIDGE TEST: PASS' \
    'ORDINARY UD2 PRESERVATION: PASS' \
    'DBGT AUTHENTICATED CONTINUE: PASS' \
    'DBGT EXITPROCESS TERMINATION: PASS' \
    'DBGT ADVERSARIAL TRANSACTIONALITY: PASS' \
    'SCALAR GATEWAY REGISTER MAPPING: PASS'
do
    count=$(grep -Fxc "$marker" \
        "$build/sanitizer-translated-bridge.log" || true)
    [ "$count" -eq 1 ] || {
        printf 'SANITIZED TRANSLATED BRIDGE MARKER MISMATCH marker=[%s] count=%s\n' \
            "$marker" "$count" >&2
        exit 1
    }
done

gcc -Iinclude -Isrc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$execution_test" $execution_sources \
    -o "$build/sanitizer-translated-execution-test"
ASAN_OPTIONS=detect_leaks=1 \
    "$build/sanitizer-translated-execution-test" > \
    "$build/sanitizer-translated-execution.log"

for marker in \
    'WIN32 TRANSLATED EXECUTION TEST: PASS' \
    'TRANSLATED DIRECT TARGET CONDITIONAL RESOLUTION: PASS' \
    'TRANSLATED ORDINARY UD2 OUTCOME: PASS' \
    'TRANSLATED DBGT CONTINUE COORDINATION: PASS' \
    'TRANSLATED DBGT TERMINATION COORDINATION: PASS' \
    'TRANSLATED EXECUTION TRANSACTIONALITY: PASS' \
    'TRANSLATED NATIVE ENTRY ON THIS HOST: NOT RUN'
do
    count=$(grep -Fxc "$marker" \
        "$build/sanitizer-translated-execution.log" || true)
    [ "$count" -eq 1 ] || {
        printf 'SANITIZED TRANSLATED EXECUTION MARKER MISMATCH marker=[%s] count=%s\n' \
            "$marker" "$count" >&2
        exit 1
    }
done

gcc -Iinclude -Isrc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$run_test" $run_sources \
    -o "$build/sanitizer-translated-run-test"
ASAN_OPTIONS=detect_leaks=1 \
    "$build/sanitizer-translated-run-test" > \
    "$build/sanitizer-translated-run.log"

for marker in \
    'WIN32 TRANSLATED RUN TEST: PASS' \
    'CACHE HIT CONTINUE TERMINATE: PASS' \
    'CACHE HIT MISS COMMIT: PASS' \
    'GUEST TRAP PROPAGATION: PASS' \
    'BLOCK BUDGET EXHAUSTION: PASS' \
    'WHOLE RUN FAILURE TRANSACTIONALITY: PASS' \
    'PUBLIC REAL EXECUTOR NONAARCH64 ROLLBACK: PASS' \
    'NATIVE MULTI BLOCK ARM64 EXECUTION: NOT RUN' \
    'CACHE MISS TRANSLATION: NOT IMPLEMENTED'
do
    count=$(grep -Fxc "$marker" \
        "$build/sanitizer-translated-run.log" || true)
    [ "$count" -eq 1 ] || {
        printf 'SANITIZED TRANSLATED RUN MARKER MISMATCH marker=[%s] count=%s\n' \
            "$marker" "$count" >&2
        exit 1
    }
done

clang --analyze -Iinclude -Isrc -std=c11 -Wall -Wextra -Wpedantic -Werror \
    src/win32_provider.c src/win32_kernel32_gateway.c \
    src/win32_kernel32_invoke.c src/win32_kernel32.c \
    src/win32_runtime_dispatch.c \
    src/win32_translated_gateway_bridge.c \
    src/win32_translated_execution.c \
    src/win32_translated_run.c \
    src/arm64_block_cache.c \
    src/translated_terminal_exit.c \
    src/linux_arm64_translation_entry.c \
    src/gateway_decode.c src/gateway_lookup.c \
    src/gateway_dispatch.c src/gateway_frame.c \
    src/gateway_enter.c src/gateway_invoke.c src/gateway_emit.c \
    src/gateway_auth_complete.c src/gateway_result.c \
    src/gateway_complete.c src/gateway_return.c \
    src/gateway_return_apply.c tests/win32_kernel32_gateway_test.c \
    tests/win32_exit_process_boundary_test.c \
    tests/win32_runtime_dispatch_test.c \
    tests/win32_translated_gateway_bridge_test.c \
    tests/win32_translated_execution_test.c \
    tests/win32_translated_run_test.c

printf '%s\n' 'WIN32 KERNEL32 GATEWAY VERIFY: PASS'
printf '%s\n' 'TRANSLATED DBGT TRAP AUTHENTICATION: PASS'
printf '%s\n' 'ORDINARY UD2 PRESERVATION VERIFY: PASS'
printf '%s\n' 'TRANSLATED WIN32 CONTINUE OUTCOME: PASS'
printf '%s\n' 'TRANSLATED WIN32 TERMINATION OUTCOME: PASS'
printf '%s\n' 'TRANSLATED WIN32 BRIDGE TRANSACTIONALITY: PASS'
printf '%s\n' 'TRANSLATED WIN32 GATEWAY BRIDGE VERIFY: PASS'
printf '%s\n' 'PRODUCTION TRANSLATED EXECUTION LINK CONTRACT: PASS'
printf '%s\n' 'TRANSLATED TERMINAL OUTCOME REDUCER: PASS'
printf '%s\n' 'TRANSLATED ONE BLOCK COORDINATOR VERIFY: PASS'
printf '%s\n' 'PRODUCTION BLOCK CACHE RUN LOOP LINK CONTRACT: PASS'
printf '%s\n' 'TRANSLATED MULTI BLOCK CACHE LOOP VERIFY: PASS'
printf '%s\n' 'CACHE MISS EXPLICIT STOP: PASS'
printf '%s\n' 'GUEST TRAP RUN STOP: PASS'
printf '%s\n' 'GUEST TERMINATION RUN STOP: PASS'
printf '%s\n' 'BLOCK BUDGET RUN STOP: PASS'
printf '%s\n' 'WHOLE RUN TRANSACTIONALITY VERIFY: PASS'
printf '%s\n' 'NATIVE ONE BLOCK ARM64 EXECUTION: NOT RUN'
printf '%s\n' 'NATIVE MULTI BLOCK ARM64 EXECUTION: NOT RUN'
printf '%s\n' 'CACHE MISS TRANSLATION: NOT IMPLEMENTED'
printf '%s\n' 'NATIVE TRANSLATED DBGT END TO END: NOT RUN'
printf '%s\n' 'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN'
printf '%s\n' 'WINLATOR COMPARISON: NOT RUN'
