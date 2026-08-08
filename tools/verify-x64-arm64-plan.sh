#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/x64-arm64-plan-verify"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
sources='src/x64_arm64_plan.c src/x64_arm64_plan_validate.c src/x64_arm64_plan_group.c src/x64_arm64_plan_emit.c'
indirect_sources='
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

if grep -E '#include[[:space:]]+<(sys/mman.h|windows.h)>|mmap[[:space:]]*\(|mprotect[[:space:]]*\(|VirtualAlloc[[:space:]]*\(|VirtualProtect[[:space:]]*\(|__builtin___clear_cache[[:space:]]*\(' \
    include/dblyat/x64_arm64_plan.h \
    src/x64_arm64_plan.c \
    src/x64_arm64_plan_internal.h \
    src/x64_arm64_plan_validate.c \
    src/x64_arm64_plan_group.c \
    src/x64_arm64_plan_emit.c >/dev/null
then
    printf '%s\n' 'X64 ARM64 PLAN CROSSED EXECUTABLE-MEMORY BOUNDARY' >&2
    exit 1
fi

gcc $common tests/x64_arm64_plan_test.c $sources \
    -o "$build/gcc-x64-arm64-plan-test"
"$build/gcc-x64-arm64-plan-test"

clang $common tests/x64_arm64_plan_test.c $sources \
    -o "$build/clang-x64-arm64-plan-test"
"$build/clang-x64-arm64-plan-test"

gcc $common tests/x64_indirect_memory_control_test.c $indirect_sources \
    -o "$build/gcc-x64-indirect-memory-control-test"
"$build/gcc-x64-indirect-memory-control-test" > \
    "$build/gcc-x64-indirect-memory-control.log"

clang $common tests/x64_indirect_memory_control_test.c $indirect_sources \
    -o "$build/clang-x64-indirect-memory-control-test"
"$build/clang-x64-indirect-memory-control-test" > \
    "$build/clang-x64-indirect-memory-control.log"

for log in \
    "$build/gcc-x64-indirect-memory-control.log" \
    "$build/clang-x64-indirect-memory-control.log"
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
            printf 'X64 INDIRECT CONTROL MARKER MISMATCH log=%s marker=[%s] count=%s\n' \
                "$log" "$marker" "$count" >&2
            exit 1
        }
    done
done

gcc -Iinclude -Isrc -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_arm64_plan_test.c $sources \
    -o "$build/x64-arm64-plan-sanitizer-test"
"$build/x64-arm64-plan-sanitizer-test"

clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror \
    src/x64_arm64_plan.c src/x64_arm64_plan_validate.c \
    src/x64_arm64_plan_group.c src/x64_arm64_plan_emit.c \
    tests/x64_arm64_plan_test.c

printf '%s\n' 'X64 ARM64 PLAN VERIFY: PASS'
printf '%s\n' 'FIXED ARM64 RUNTIME ABI VERIFY: PASS'
printf '%s\n' 'X64 INDIRECT MEMORY CONTROL VERIFY: PASS'
printf '%s\n' 'RIP RELATIVE IAT CONTROL FLOW VERIFY: PASS'
printf '%s\n' 'REGISTER INDIRECT CONTROL: NOT IMPLEMENTED'
printf '%s\n' 'PRODUCTION PE STARTUP RUN: NOT IMPLEMENTED'
