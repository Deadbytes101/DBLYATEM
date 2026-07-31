#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/x64-decode-verify"
common='-Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

gcc $common tests/x64_decode_test.c src/x64_decode.c \
    -o "$build/gcc-test"
"$build/gcc-test"

clang $common tests/x64_decode_test.c src/x64_decode.c \
    -o "$build/clang-test"
"$build/clang-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_decode_test.c src/x64_decode.c \
    -o "$build/sanitizer-test"
"$build/sanitizer-test"

clang --analyze -Iinclude -std=c11 -Wall -Wextra -Wpedantic -Werror \
    src/x64_decode.c tests/x64_decode_test.c

printf '%s\n' 'X64 DECODE VERIFY: PASS'
