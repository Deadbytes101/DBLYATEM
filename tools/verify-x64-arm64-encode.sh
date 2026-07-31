#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/x64-arm64-encode-verify"
common='-Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

for command_name in clang readelf awk; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'X64 ARM64 ENCODE VERIFY: missing command: %s\n' \
            "$command_name" >&2
        exit 1
    }
done

if grep -E '#include[[:space:]]+<(sys/mman.h|windows.h)>|mmap[[:space:]]*\(|mprotect[[:space:]]*\(|VirtualAlloc[[:space:]]*\(|VirtualProtect[[:space:]]*\(|__builtin___clear_cache[[:space:]]*\(' \
    include/dblyat/x64_arm64_encode.h \
    src/x64_arm64_encode.c >/dev/null
then
    printf '%s\n' 'ARM64 ENCODER CROSSED EXECUTABLE-MEMORY BOUNDARY' >&2
    exit 1
fi

gcc $common tests/x64_arm64_encode_test.c \
    src/x64_arm64_encode.c \
    -o "$build/gcc-x64-arm64-encode-test"
"$build/gcc-x64-arm64-encode-test"

clang $common tests/x64_arm64_encode_test.c \
    src/x64_arm64_encode.c \
    -o "$build/clang-x64-arm64-encode-test"
"$build/clang-x64-arm64-encode-test"

gcc -Iinclude -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_arm64_encode_test.c src/x64_arm64_encode.c \
    -o "$build/x64-arm64-encode-sanitizer-test"
"$build/x64-arm64-encode-sanitizer-test"

clang --analyze -Iinclude -std=c11 \
    -Wall -Wextra -Wpedantic -Werror \
    src/x64_arm64_encode.c tests/x64_arm64_encode_test.c

cat > "$build/reference.s" <<'ASSEMBLY'
.text
.global dblyat_arm64_reference_words
dblyat_arm64_reference_words:
    nop
    ldr x16, [x19, #16]
    str x17, [x19, #24]
    movz x16, #0x7788
    movk x16, #0x5566, lsl #16
    movk x16, #0x3344, lsl #32
    movk x16, #0x1122, lsl #48
    eor x16, x16, x17
    eor w16, w16, w17
    bl .
    b .
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

expected='1f2003d5700a40f9710e00f910f18ed2d0acaaf29068c6f25024e2f2100211ca1002114a0000009400000014'

if [ "$actual" != "$expected" ]; then
    printf '%s\n' 'ARM64 ASSEMBLER REFERENCE WORDS CHANGED' >&2
    printf 'EXPECTED=%s\n' "$expected" >&2
    printf 'ACTUAL=%s\n' "$actual" >&2
    exit 1
fi

printf '%s\n' 'ARM64 ASSEMBLER CROSSCHECK: PASS'
printf '%s\n' 'X64 ARM64 ENCODE VERIFY: PASS'
printf '%s\n' 'ARM64 RELOCATION ABI VERIFY: PASS'
