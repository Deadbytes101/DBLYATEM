#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/arm64-helper-operand-abi-check"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
sources='
src/x64_arm64_plan.c
src/x64_arm64_plan_validate.c
src/x64_arm64_plan_group.c
src/x64_arm64_plan_emit.c
src/x64_arm64_encode.c
'
test='tests/x64_arm64_helper_operand_abi_check.c'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

printf '%s\n' 'ARM64_HELPER_OPERAND_ABI_STAGE=static-contract'
for token in \
    DB_X64_ARM64_HELPER_OPERAND_ABI_VERSION \
    DB_X64_ARM64_HELPER_ARG0 \
    DB_X64_ARM64_HELPER_ARG1 \
    DB_X64_ARM64_HELPER_ARG2 \
    DB_X64_ARM64_HELPER_ARG3 \
    DB_X64_ARM64_HELPER_ARG4 \
    DB_X64_ARM64_HELPER_ARG5 \
    DB_X64_ARM64_ENCODE_BAD_HELPER_ABI
do
    grep -R -F "$token" \
        include/dblyat/x64_arm64_plan.h \
        include/dblyat/x64_arm64_encode.h \
        src/x64_arm64_plan_emit.c \
        src/x64_arm64_encode.c >/dev/null || {
            printf 'ARM64_HELPER_OPERAND_ABI_TOKEN_MISSING token=%s\n' \
                "$token" >&2
            exit 1
        }
done

for token in \
    DB_X64_ARM64_HELPER_FORM_GUEST_ADDRESS \
    DB_X64_ARM64_HELPER_UPDATE_LOGIC_FLAGS \
    DB_X64_ARM64_HELPER_TEST_GUEST_CONDITION
do
    grep -F "$token" src/x64_arm64_plan_emit.c >/dev/null || {
        printf 'ARM64_HELPER_OPERAND_ABI_HELPER_MISSING token=%s\n' \
            "$token" >&2
        exit 1
    }
done

if grep -E '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|mprotect|munmap|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync)([^[:alnum:]_]|$)' \
    include/dblyat/x64_arm64_plan.h \
    include/dblyat/x64_arm64_encode.h \
    src/x64_arm64_plan_group.c \
    src/x64_arm64_plan_emit.c \
    src/x64_arm64_encode.c >/dev/null
then
    printf '%s\n' 'ARM64 HELPER OPERAND ABI CROSSED OWNERSHIP OR MEMORY BACKEND BOUNDARY' >&2
    exit 1
fi

for compiler in gcc clang
do
    printf 'ARM64_HELPER_OPERAND_ABI_STAGE=compile-%s\n' "$compiler"
    set +e
    # shellcheck disable=SC2086
    "$compiler" $common "$test" $sources \
        -o "$build/$compiler-helper-abi" \
        > "$build/$compiler-compile.log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf 'ARM64_HELPER_OPERAND_ABI_COMPILE_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$status" >&2
        cat "$build/$compiler-compile.log" >&2
        exit "$status"
    fi

    printf 'ARM64_HELPER_OPERAND_ABI_STAGE=run-%s\n' "$compiler"
    set +e
    "$build/$compiler-helper-abi" \
        > "$build/$compiler.log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf 'ARM64_HELPER_OPERAND_ABI_TEST_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$status" >&2
        cat "$build/$compiler.log" >&2
        exit "$status"
    fi
done

printf '%s\n' 'ARM64_HELPER_OPERAND_ABI_STAGE=marker-validation'
for log in "$build/gcc.log" "$build/clang.log"
do
    for marker in \
        'ARM64 HELPER OPERAND ABI CHECK: PASS' \
        'FORM ADDRESS ARGUMENT ABI: PASS' \
        'LOGIC FLAGS ARGUMENT ABI: PASS' \
        'CONDITION ARGUMENT ABI: PASS' \
        'STALE HELPER ABI TRANSACTIONAL REJECTION: PASS' \
        'MEMORY HELPER BACKEND: NOT IMPLEMENTED' \
        'NATIVE ARM64 HELPER EXECUTION: NOT RUN'
    do
        count=$(grep -Fxc "$marker" "$log" || true)
        [ "$count" -eq 1 ] || {
            printf 'ARM64_HELPER_OPERAND_ABI_MARKER_MISMATCH log=%s marker=[%s] count=%s\n' \
                "$log" "$marker" "$count" >&2
            cat "$log" >&2
            exit 1
        }
    done
done

printf '%s\n' 'ARM64_HELPER_OPERAND_ABI_STAGE=compile-sanitize'
set +e
# shellcheck disable=SC2086
gcc -Iinclude -Isrc -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$test" $sources \
    -o "$build/helper-abi-sanitize" \
    > "$build/sanitize-compile.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_HELPER_OPERAND_ABI_SANITIZER_COMPILE_FAILED exit=%s\n' \
        "$status" >&2
    cat "$build/sanitize-compile.log" >&2
    exit "$status"
fi

printf '%s\n' 'ARM64_HELPER_OPERAND_ABI_STAGE=run-sanitize'
set +e
ASAN_OPTIONS=detect_leaks=1 "$build/helper-abi-sanitize" \
    > "$build/sanitize.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_HELPER_OPERAND_ABI_SANITIZER_TEST_FAILED exit=%s\n' \
        "$status" >&2
    cat "$build/sanitize.log" >&2
    exit "$status"
fi

printf '%s\n' 'ARM64_HELPER_OPERAND_ABI_STAGE=clang-analyze'
set +e
# shellcheck disable=SC2086
clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    "$test" $sources \
    > "$build/clang-analyze.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_HELPER_OPERAND_ABI_CLANG_ANALYZE_FAILED exit=%s\n' \
        "$status" >&2
    cat "$build/clang-analyze.log" >&2
    exit "$status"
fi

printf '%s\n' 'ARM64_HELPER_OPERAND_ABI_STAGE=canonical-plan'
sh tools/verify-x64-arm64-plan.sh > "$build/plan.log" 2>&1 || {
    cat "$build/plan.log" >&2
    exit 1
}
grep -Fxc 'NONTERMINAL HELPER OPERAND PLAN ABI: PASS' \
    "$build/plan.log" >/dev/null || {
        cat "$build/plan.log" >&2
        exit 1
    }
grep -Fxc 'X64 INDIRECT MEMORY CONTROL VERIFY: PASS' \
    "$build/plan.log" >/dev/null || {
        cat "$build/plan.log" >&2
        exit 1
    }

printf '%s\n' 'ARM64_HELPER_OPERAND_ABI_STAGE=canonical-encode'
sh tools/verify-x64-arm64-encode.sh > "$build/encode.log" 2>&1 || {
    cat "$build/encode.log" >&2
    exit 1
}
grep -Fxc 'NONTERMINAL HELPER OPERAND ABI WORDS: PASS' \
    "$build/encode.log" >/dev/null || {
        cat "$build/encode.log" >&2
        exit 1
    }
grep -Fxc 'STALE HELPER OPERAND ABI REJECTION: PASS' \
    "$build/encode.log" >/dev/null || {
        cat "$build/encode.log" >&2
        exit 1
    }

printf '%s\n' 'ARM64 HELPER OPERAND ABI FOCUSED VERIFY: PASS'
printf '%s\n' 'HELPER OPERAND ABI VERSION 1: PASS'
printf '%s\n' 'FORM ADDRESS ABI MATERIALIZATION: PASS'
printf '%s\n' 'LOGIC FLAGS ABI MATERIALIZATION: PASS'
printf '%s\n' 'CONDITION ABI MATERIALIZATION: PASS'
printf '%s\n' 'STALE HELPER ABI REJECTION: PASS'
printf '%s\n' 'DIRECT INDIRECT CONTROL REGRESSION: PASS'
printf '%s\n' 'MEMORY HELPER BACKEND: NOT IMPLEMENTED'
printf '%s\n' 'NATIVE ARM64 HELPER EXECUTION: NOT RUN'
printf '%s\n' 'NATIVE ARM64 PE EXECUTION: NOT RUN'
printf '%s\n' 'HOST RUN CLI: NOT IMPLEMENTED'
printf '%s\n' 'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN'
printf '%s\n' 'WINLATOR COMPARISON: NOT RUN'
