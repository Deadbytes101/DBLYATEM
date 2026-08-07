#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/win32-translated-execution-verify"
common='-Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
sources='src/win32_translated_execution.c src/win32_translated_gateway_bridge.c src/translated_terminal_exit.c src/linux_arm64_translation_entry.c src/win32_runtime_dispatch.c src/win32_provider.c src/win32_kernel32_gateway.c src/win32_kernel32_invoke.c src/win32_kernel32.c src/gateway_auth_complete.c src/gateway_result.c src/gateway_complete.c src/gateway_return.c src/gateway_return_apply.c src/gateway_decode.c src/gateway_lookup.c src/gateway_dispatch.c src/gateway_frame.c src/gateway_enter.c src/gateway_invoke.c src/gateway_emit.c'
test='tests/win32_translated_execution_test.c'
source='src/win32_translated_execution.c'
header='include/dblyat/win32_translated_execution.h'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

[ -f "$source" ]
[ -f "$header" ]
[ -f "$test" ]

grep -F 'db_linux_arm64_translation_enter' "$source" >/dev/null
grep -F 'db_win32_translated_gateway_bridge_run' "$source" >/dev/null
grep -F 'DB_TRANSLATED_TERMINAL_EXIT_DIRECT' "$source" >/dev/null
grep -F 'DB_TRANSLATED_TERMINAL_EXIT_TARGET' "$source" >/dev/null
grep -F 'DB_TRANSLATED_TERMINAL_EXIT_CONDITIONAL' "$source" >/dev/null
grep -F 'DB_TRANSLATED_TERMINAL_EXIT_TRAP' "$source" >/dev/null
grep -F 'DB_WIN32_TRANSLATED_EXECUTION_OUTCOME_GUEST_TRAP' "$source" >/dev/null

grep -F 'src/win32_translated_gateway_bridge.c' GNUmakefile >/dev/null
grep -F 'src/win32_translated_execution.c' GNUmakefile >/dev/null
grep -F 'src/translated_terminal_exit.c' GNUmakefile >/dev/null
grep -F 'src/linux_arm64_translation_entry.c' GNUmakefile >/dev/null
grep -F 'include/dblyat/win32_translated_execution.h' GNUmakefile >/dev/null

if grep -F 'db_arm64_block_cache_lookup' "$source" "$header" >/dev/null
then
    printf '%s\n' 'TRANSLATED EXECUTION STEP CROSSED CACHE-LOOP BOUNDARY' >&2
    exit 1
fi

if grep -F 'db_x64_block_build' "$source" "$header" >/dev/null
then
    printf '%s\n' 'TRANSLATED EXECUTION STEP CROSSED TRANSLATION/PUBLICATION BOUNDARY' >&2
    exit 1
fi

gcc $common "$test" $sources -o "$build/gcc-test"
"$build/gcc-test" > "$build/gcc.log"

clang $common "$test" $sources -o "$build/clang-test"
"$build/clang-test" > "$build/clang.log"

for log in "$build/gcc.log" "$build/clang.log"
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

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$test" $sources -o "$build/sanitizer-test"
ASAN_OPTIONS=detect_leaks=1 "$build/sanitizer-test" > "$build/sanitizer.log"

for marker in \
    'WIN32 TRANSLATED EXECUTION TEST: PASS' \
    'TRANSLATED DIRECT TARGET CONDITIONAL RESOLUTION: PASS' \
    'TRANSLATED ORDINARY UD2 OUTCOME: PASS' \
    'TRANSLATED DBGT CONTINUE COORDINATION: PASS' \
    'TRANSLATED DBGT TERMINATION COORDINATION: PASS' \
    'TRANSLATED EXECUTION TRANSACTIONALITY: PASS' \
    'TRANSLATED NATIVE ENTRY ON THIS HOST: NOT RUN'
do
    count=$(grep -Fxc "$marker" "$build/sanitizer.log" || true)
    [ "$count" -eq 1 ] || {
        printf 'SANITIZED TRANSLATED EXECUTION MARKER MISMATCH marker=[%s] count=%s\n' \
            "$marker" "$count" >&2
        exit 1
    }
done

clang --analyze -Iinclude -std=c11 -Wall -Wextra -Wpedantic -Werror \
    src/win32_translated_execution.c \
    src/win32_translated_gateway_bridge.c \
    src/translated_terminal_exit.c \
    src/linux_arm64_translation_entry.c \
    src/win32_runtime_dispatch.c src/win32_provider.c \
    src/win32_kernel32_gateway.c src/win32_kernel32_invoke.c \
    src/win32_kernel32.c src/gateway_auth_complete.c \
    src/gateway_result.c src/gateway_complete.c src/gateway_return.c \
    src/gateway_return_apply.c src/gateway_decode.c \
    src/gateway_lookup.c src/gateway_dispatch.c src/gateway_frame.c \
    src/gateway_enter.c src/gateway_invoke.c src/gateway_emit.c \
    tests/win32_translated_execution_test.c

printf '%s\n' 'WIN32 TRANSLATED EXECUTION VERIFY: PASS'
printf '%s\n' 'PRODUCTION TRANSLATED EXECUTION LINK CONTRACT: PASS'
printf '%s\n' 'TRANSLATED TERMINAL OUTCOME REDUCER: PASS'
printf '%s\n' 'ORDINARY UD2 TO GUEST TRAP: PASS'
printf '%s\n' 'AUTHENTICATED DBGT COORDINATION: PASS'
printf '%s\n' 'TRANSLATED EXECUTION TRANSACTIONALITY VERIFY: PASS'
printf '%s\n' 'NATIVE ONE-BLOCK ARM64 EXECUTION: NOT RUN'
printf '%s\n' 'BLOCK CACHE RUN LOOP: NOT IMPLEMENTED'
printf '%s\n' 'CACHE MISS TRANSLATION: NOT IMPLEMENTED'
printf '%s\n' 'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN'
printf '%s\n' 'WINLATOR COMPARISON: NOT RUN'
