#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/x64-ir-boundary-verify"
common='-Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

gcc $common tests/x64_block_test.c \
    src/x64_block.c src/x64_decode.c \
    src/x64_memory_instruction.c src/x64_memory_decode.c \
    -o "$build/gcc-block-test"
"$build/gcc-block-test"

clang $common tests/x64_block_test.c \
    src/x64_block.c src/x64_decode.c \
    src/x64_memory_instruction.c src/x64_memory_decode.c \
    -o "$build/clang-block-test"
"$build/clang-block-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_block_test.c \
    src/x64_block.c src/x64_decode.c \
    src/x64_memory_instruction.c src/x64_memory_decode.c \
    -o "$build/block-sanitizer-test"
"$build/block-sanitizer-test"

clang --analyze -Iinclude -std=c11 -Wall -Wextra -Wpedantic -Werror \
    src/x64_block.c tests/x64_block_test.c \
    src/x64_decode.c src/x64_memory_instruction.c \
    src/x64_memory_decode.c

printf '%s\n' 'X64 BLOCK VERIFY: PASS'
printf '%s\n' 'X64 IR BOUNDARY VERIFY: PASS'
