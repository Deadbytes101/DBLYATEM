#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/x64-indirect-memory-control-check"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
test_source='tests/x64_indirect_memory_control_test.c'
sources='
src/x64_memory_instruction.c
src/x64_memory_decode.c
src/x64_block.c
src/x64_decode.c
src/x64_ir_lower.c
src/x64_ir_lower_validate.c
src/x64_ir_lower_emit.c
src/ir.c
src/arm64_select.c
src/arm64_select_validate.c
src/arm64_select_emit.c
src/x64_arm64_plan.c
src/x64_arm64_plan_validate.c
src/x64_arm64_plan_group.c
src/x64_arm64_plan_emit.c
src/arm64_published_block.c
src/translated_terminal_exit.c
'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

printf '%s\n' 'X64_INDIRECT_CONTROL_STAGE=files'
for required in \
    include/dblyat/x64_memory_instruction.h \
    include/dblyat/x64_block.h \
    include/dblyat/ir.h \
    include/dblyat/arm64_select.h \
    src/x64_memory_instruction.c \
    src/x64_block.c \
    src/x64_ir_lower_emit.c \
    src/x64_ir_lower_validate.c \
    src/arm64_select_emit.c \
    src/arm64_select_validate.c \
    src/x64_arm64_plan_group.c \
    src/x64_arm64_plan_emit.c \
    src/x64_arm64_plan_validate.c \
    "$test_source"
do
    [ -f "$required" ] || {
        printf 'X64_INDIRECT_CONTROL_REQUIRED_FILE_MISSING path=%s\n' \
            "$required" >&2
        exit 1
    }
done

printf '%s\n' 'X64_INDIRECT_CONTROL_STAGE=static-contract'
for requirement in \
    'DB_X64_MEMORY_INSTRUCTION_CALL_RM64' \
    'DB_X64_MEMORY_INSTRUCTION_JMP_RM64' \
    'opcode_extension' \
    'DB_X64_BLOCK_CONTROL_INDIRECT_CALL' \
    'DB_X64_BLOCK_CONTROL_INDIRECT_JUMP' \
    'DB_IR_OPCODE_CALL_INDIRECT' \
    'DB_IR_OPCODE_JUMP_INDIRECT' \
    'DB_ARM64_SELECT_LOAD_U64_TARGET' \
    'DB_X64_ARM64_HELPER_READ_GUEST_U64' \
    'DB_X64_ARM64_HELPER_PUSH_GUEST_U64' \
    'DB_X64_ARM64_HELPER_DISPATCH_TARGET'
do
    grep -R -F "$requirement" \
        include/dblyat/x64_memory_instruction.h \
        include/dblyat/x64_block.h \
        include/dblyat/ir.h \
        include/dblyat/arm64_select.h \
        src/x64_memory_instruction.c \
        src/x64_block.c \
        src/x64_ir_lower_emit.c \
        src/x64_ir_lower_validate.c \
        src/arm64_select_emit.c \
        src/arm64_select_validate.c \
        src/x64_arm64_plan_group.c \
        src/x64_arm64_plan_emit.c \
        src/x64_arm64_plan_validate.c >/dev/null || {
            printf 'X64_INDIRECT_CONTROL_STATIC_REQUIREMENT_MISSING token=%s\n' \
                "$requirement" >&2
            exit 1
        }
done

if grep -E 'win32|kernel32|gateway|iat_plan|iat_apply|db_pe_' \
    src/x64_memory_instruction.c \
    src/x64_block.c \
    src/x64_ir_lower_emit.c \
    src/x64_ir_lower_validate.c \
    src/arm64_select_emit.c \
    src/arm64_select_validate.c \
    src/x64_arm64_plan_group.c \
    src/x64_arm64_plan_emit.c \
    src/x64_arm64_plan_validate.c >/dev/null
then
    printf '%s\n' \
        'X64 INDIRECT CONTROL CONTAMINATED WITH PE/WIN32/GATEWAY SEMANTICS' >&2
    exit 1
fi

if grep -E '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|mprotect|munmap|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync)([^[:alnum:]_]|$)' \
    src/x64_memory_instruction.c \
    src/x64_block.c \
    src/x64_ir_lower_emit.c \
    src/x64_ir_lower_validate.c \
    src/arm64_select_emit.c \
    src/arm64_select_validate.c \
    src/x64_arm64_plan_group.c \
    src/x64_arm64_plan_emit.c \
    src/x64_arm64_plan_validate.c >/dev/null
then
    printf '%s\n' \
        'X64 INDIRECT CONTROL CROSSED OWNERSHIP OR MAPPING BOUNDARY' >&2
    exit 1
fi

for compiler in gcc clang
do
    binary="$build/$compiler-test"
    compile_log="$build/$compiler-compile.log"
    run_log="$build/$compiler.log"

    printf 'X64_INDIRECT_CONTROL_STAGE=compile-%s\n' "$compiler"
    set +e
    # shellcheck disable=SC2086
    "$compiler" $common "$test_source" $sources -o "$binary" \
        > "$compile_log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf 'X64_INDIRECT_CONTROL_COMPILE_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$status" >&2
        cat "$compile_log" >&2
        exit "$status"
    fi

    printf 'X64_INDIRECT_CONTROL_STAGE=run-%s\n' "$compiler"
    set +e
    "$binary" > "$run_log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf 'X64_INDIRECT_CONTROL_TEST_FAILED compiler=%s exit=%s\n' \
            "$compiler" "$status" >&2
        cat "$run_log" >&2
        exit "$status"
    fi
done

printf '%s\n' 'X64_INDIRECT_CONTROL_STAGE=marker-validation'
for log in "$build/gcc.log" "$build/clang.log"
do
    for marker in \
        'X64 INDIRECT MEMORY CONTROL TEST: PASS' \
        'FF /2 MEMORY CALL DECODE: PASS' \
        'FF /4 MEMORY JUMP DECODE: PASS' \
        'RIP RELATIVE IAT ADDRESSING: PASS' \
        'INDIRECT CALL TARGET DISPATCH: PASS' \
        'INDIRECT JUMP TARGET DISPATCH: PASS' \
        'INDIRECT CONTROL ADVERSARIAL REJECTION: PASS' \
        'TRANSLATED TARGET TERMINAL BINDING: PASS' \
        'REGISTER INDIRECT CONTROL: NOT IMPLEMENTED' \
        'PRODUCTION PE STARTUP RUN: NOT IMPLEMENTED' \
        'NATIVE ARM64 PE EXECUTION: NOT RUN'
    do
        count=$(grep -Fxc "$marker" "$log" || true)
        [ "$count" -eq 1 ] || {
            printf 'X64_INDIRECT_CONTROL_MARKER_MISMATCH log=%s marker=[%s] count=%s\n' \
                "$log" "$marker" "$count" >&2
            cat "$log" >&2
            exit 1
        }
    done
done

printf '%s\n' 'X64_INDIRECT_CONTROL_STAGE=compile-sanitize'
set +e
# shellcheck disable=SC2086
gcc -Iinclude -Isrc -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$test_source" $sources -o "$build/sanitize-test" \
    > "$build/sanitize-compile.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'X64_INDIRECT_CONTROL_SANITIZER_COMPILE_FAILED exit=%s\n' \
        "$status" >&2
    cat "$build/sanitize-compile.log" >&2
    exit "$status"
fi

printf '%s\n' 'X64_INDIRECT_CONTROL_STAGE=run-sanitize'
set +e
ASAN_OPTIONS=detect_leaks=1 "$build/sanitize-test" \
    > "$build/sanitize.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'X64_INDIRECT_CONTROL_SANITIZER_TEST_FAILED exit=%s\n' \
        "$status" >&2
    cat "$build/sanitize.log" >&2
    exit "$status"
fi

printf '%s\n' 'X64_INDIRECT_CONTROL_STAGE=clang-analyze'
set +e
# shellcheck disable=SC2086
clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    "$test_source" $sources \
    > "$build/clang-analyze.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'X64_INDIRECT_CONTROL_CLANG_ANALYZE_FAILED exit=%s\n' \
        "$status" >&2
    cat "$build/clang-analyze.log" >&2
    exit "$status"
fi

printf '%s\n' 'X64 INDIRECT MEMORY CONTROL FOCUSED VERIFY: PASS'
printf '%s\n' 'FF2 MEMORY CALL PIPELINE: PASS'
printf '%s\n' 'FF4 MEMORY JUMP PIPELINE: PASS'
printf '%s\n' 'RIP RELATIVE IAT CONTROL FLOW: PASS'
printf '%s\n' 'INDIRECT CALL RETURN PUSH: PASS'
printf '%s\n' 'INDIRECT TARGET TERMINAL EXIT: PASS'
printf '%s\n' 'DIRECT CONTROL FLOW REGRESSION: DEFERRED TO CANONICAL VERIFY ALL'
printf '%s\n' 'REGISTER INDIRECT CONTROL: NOT IMPLEMENTED'
printf '%s\n' 'PRODUCTION PE STARTUP RUN: NOT IMPLEMENTED'
printf '%s\n' 'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN'
printf '%s\n' 'WINLATOR COMPARISON: NOT RUN'
