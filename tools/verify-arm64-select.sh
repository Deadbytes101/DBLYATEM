#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/arm64-select-verify"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
sources='src/arm64_select.c src/arm64_select_validate.c src/arm64_select_emit.c'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

if grep -E '#include[[:space:]]+"dblyat/x64_' \
    include/dblyat/arm64_select.h \
    src/arm64_select.c \
    src/arm64_select_internal.h \
    src/arm64_select_validate.c \
    src/arm64_select_emit.c >/dev/null
then
    printf '%s\n' 'ARM64 SELECTOR DEPENDS ON X86-64 HEADER' >&2
    exit 1
fi

gcc $common tests/arm64_select_test.c $sources \
    -o "$build/gcc-arm64-select-test"
"$build/gcc-arm64-select-test"

clang $common tests/arm64_select_test.c $sources \
    -o "$build/clang-arm64-select-test"
"$build/clang-arm64-select-test"

gcc -Iinclude -Isrc -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/arm64_select_test.c $sources \
    -o "$build/arm64-select-sanitizer-test"
"$build/arm64-select-sanitizer-test"

clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror \
    src/arm64_select.c src/arm64_select_validate.c \
    src/arm64_select_emit.c tests/arm64_select_test.c

printf '%s\n' 'ARM64 SELECT VERIFY: PASS'
printf '%s\n' 'ARM64 BACKEND PLAN VERIFY: PASS'
