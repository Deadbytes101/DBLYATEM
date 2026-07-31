#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/x64-decode-verify"
common='-Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

gcc $common tests/x64_decode_test.c src/x64_decode.c \
    -o "$build/gcc-decode-test"
"$build/gcc-decode-test"

clang $common tests/x64_decode_test.c src/x64_decode.c \
    -o "$build/clang-decode-test"
"$build/clang-decode-test"

gcc $common tests/x64_interpret_test.c src/x64_interpret.c \
    -o "$build/gcc-interpret-test"
"$build/gcc-interpret-test"

clang $common tests/x64_interpret_test.c src/x64_interpret.c \
    -o "$build/clang-interpret-test"
"$build/clang-interpret-test"

gcc $common tests/x64_execute_test.c src/x64_execute.c \
    src/x64_decode.c src/x64_interpret.c \
    -o "$build/gcc-execute-test"
"$build/gcc-execute-test"

clang $common tests/x64_execute_test.c src/x64_execute.c \
    src/x64_decode.c src/x64_interpret.c \
    -o "$build/clang-execute-test"
"$build/clang-execute-test"

gcc $common tests/x64_address_test.c src/x64_address.c \
    -o "$build/gcc-address-test"
"$build/gcc-address-test"

clang $common tests/x64_address_test.c src/x64_address.c \
    -o "$build/clang-address-test"
"$build/clang-address-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_decode_test.c src/x64_decode.c \
    -o "$build/decode-sanitizer-test"
"$build/decode-sanitizer-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_interpret_test.c src/x64_interpret.c \
    -o "$build/interpret-sanitizer-test"
"$build/interpret-sanitizer-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_execute_test.c src/x64_execute.c \
    src/x64_decode.c src/x64_interpret.c \
    -o "$build/execute-sanitizer-test"
"$build/execute-sanitizer-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_address_test.c src/x64_address.c \
    -o "$build/address-sanitizer-test"
"$build/address-sanitizer-test"

clang --analyze -Iinclude -std=c11 -Wall -Wextra -Wpedantic -Werror \
    src/x64_decode.c tests/x64_decode_test.c \
    src/x64_interpret.c tests/x64_interpret_test.c \
    src/x64_execute.c tests/x64_execute_test.c \
    src/x64_address.c tests/x64_address_test.c

printf '%s\n' 'X64 DECODE + INTERPRET VERIFY: PASS'
printf '%s\n' 'X64 DECODE + INTERPRET + EXECUTE VERIFY: PASS'
printf '%s\n' 'X64 ADDRESS VERIFY: PASS'
printf '%s\n' 'X64 CORE VERIFY: PASS'
