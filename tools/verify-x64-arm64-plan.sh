#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/x64-arm64-plan-verify"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
sources='src/x64_arm64_plan.c src/x64_arm64_plan_validate.c src/x64_arm64_plan_group.c src/x64_arm64_plan_emit.c'

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
