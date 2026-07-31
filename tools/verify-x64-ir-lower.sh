#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/x64-ir-lower-verify"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
sources='src/ir.c src/x64_ir_lower.c src/x64_ir_lower_validate.c src/x64_ir_lower_emit.c'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

if grep -E '#include[[:space:]]+"dblyat/x64_' \
    include/dblyat/ir.h src/ir.c >/dev/null
then
    printf '%s\n' 'GENERIC IR LAYER DEPENDS ON X86-64 HEADER' >&2
    exit 1
fi

gcc $common tests/x64_ir_lower_test.c $sources \
    -o "$build/gcc-ir-lower-test"
"$build/gcc-ir-lower-test"

clang $common tests/x64_ir_lower_test.c $sources \
    -o "$build/clang-ir-lower-test"
"$build/clang-ir-lower-test"

gcc -Iinclude -Isrc -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_ir_lower_test.c $sources \
    -o "$build/ir-lower-sanitizer-test"
"$build/ir-lower-sanitizer-test"

clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror \
    src/ir.c src/x64_ir_lower.c \
    src/x64_ir_lower_validate.c src/x64_ir_lower_emit.c \
    tests/x64_ir_lower_test.c

printf '%s\n' 'GENERIC IR ABI VERIFY: PASS'
printf '%s\n' 'X64 IR LOWER VERIFY: PASS'
printf '%s\n' 'ARCHITECTURE NEUTRAL IR VERIFY: PASS'
