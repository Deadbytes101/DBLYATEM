#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/x64-arm64-relocate-verify"
common='-Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

for command_name in gcc clang readelf awk grep; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'X64 ARM64 RELOCATE VERIFY: missing command: %s\n' \
            "$command_name" >&2
        exit 1
    }
done

if grep -E '#include[[:space:]]+<(sys/mman.h|windows.h)>|mmap[[:space:]]*\(|mprotect[[:space:]]*\(|VirtualAlloc[[:space:]]*\(|VirtualProtect[[:space:]]*\(|__builtin___clear_cache[[:space:]]*\(|__clear_cache[[:space:]]*\(' \
    include/dblyat/x64_arm64_relocate.h \
    src/x64_arm64_relocate.c \
    src/x64_arm64_relocate_validate.c \
    src/x64_arm64_relocate_emit.c >/dev/null
then
    printf '%s\n' 'ARM64 RELOCATOR CROSSED EXECUTABLE-MEMORY BOUNDARY' >&2
    exit 1
fi

sources='src/x64_arm64_relocate.c src/x64_arm64_relocate_validate.c src/x64_arm64_relocate_emit.c'

gcc $common tests/x64_arm64_relocate_test.c $sources \
    -o "$build/gcc-x64-arm64-relocate-test"
"$build/gcc-x64-arm64-relocate-test"

clang $common tests/x64_arm64_relocate_test.c $sources \
    -o "$build/clang-x64-arm64-relocate-test"
"$build/clang-x64-arm64-relocate-test"

gcc -Iinclude -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_arm64_relocate_test.c $sources \
    -o "$build/x64-arm64-relocate-sanitizer-test"
"$build/x64-arm64-relocate-sanitizer-test"

clang --analyze -Iinclude -std=c11 \
    -Wall -Wextra -Wpedantic -Werror \
    $sources tests/x64_arm64_relocate_test.c

cat > "$build/reference.s" <<'ASSEMBLY'
.text
.global dblyat_arm64_relocation_reference
dblyat_arm64_relocation_reference:
    movz x16, #0x5678
    movk x16, #0x1234, lsl #16
    movk x16, #0x7fff, lsl #32
    br x16
    movz x16, #0x1234, lsl #16
    movk x16, #0x8000, lsl #32
    movk x16, #0xffff, lsl #48
    br x16
    bl . + 0x100
    b . - 0x108
ASSEMBLY

clang --target=aarch64-linux-gnu \
    -c "$build/reference.s" \
    -o "$build/reference.o"

actual=$(readelf -x .text "$build/reference.o" | awk '
/^  0x/ {
    for (index = 2; index <= NF; ++index) {
        if (length($index) == 8 && $index ~ /^[0-9a-fA-F]+$/) {
            printf "%s", tolower($index)
        } else {
            break
        }
    }
}
')

expected='10cf8ad29046a2f2f0ffcff200021fd69046a2d21000d0f2f0fffff200021fd640000094beffff17'

if [ "$actual" != "$expected" ]; then
    printf '%s\n' 'ARM64 RELOCATION ASSEMBLER REFERENCE WORDS CHANGED' >&2
    printf 'EXPECTED=%s\n' "$expected" >&2
    printf 'ACTUAL=%s\n' "$actual" >&2
    exit 1
fi

printf '%s\n' 'ARM64 RELOCATION ASSEMBLER CROSSCHECK: PASS'
printf '%s\n' 'X64 ARM64 RELOCATE VERIFY: PASS'
printf '%s\n' 'ARM64 FAR THUNK VERIFY: PASS'
