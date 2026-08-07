#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/win32-kernel32-verify"
common='-Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

gcc $common tests/win32_kernel32_test.c src/win32_kernel32.c \
    -o "$build/gcc-test"
"$build/gcc-test"

clang $common tests/win32_kernel32_test.c src/win32_kernel32.c \
    -o "$build/clang-test"
"$build/clang-test"

clang -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/win32_kernel32_test.c src/win32_kernel32.c \
    -o "$build/sanitizer-test"
"$build/sanitizer-test"

clang --analyze -Iinclude -std=c11 -Wall -Wextra -Wpedantic -Werror \
    src/win32_kernel32.c tests/win32_kernel32_test.c

printf '%s\n' 'WIN32 KERNEL32 VERIFY: PASS'
