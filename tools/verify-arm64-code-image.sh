#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/arm64-code-image-verify"
common='-Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

for command_name in gcc clang grep; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'ARM64 CODE IMAGE VERIFY: missing command: %s\n' \
            "$command_name" >&2
        exit 1
    }
done

if grep -E '#include[[:space:]]+<(sys/mman.h|windows.h)>|malloc[[:space:]]*\(|calloc[[:space:]]*\(|realloc[[:space:]]*\(|free[[:space:]]*\(|mmap[[:space:]]*\(|mprotect[[:space:]]*\(|VirtualAlloc[[:space:]]*\(|VirtualProtect[[:space:]]*\(|__builtin___clear_cache[[:space:]]*\(|__clear_cache[[:space:]]*\(|PROT_EXEC|MAP_JIT' \
    include/dblyat/arm64_code_image.h \
    src/arm64_code_image.c >/dev/null
then
    printf '%s\n' 'ARM64 CODE IMAGE CROSSED PLATFORM PUBLICATION BOUNDARY' >&2
    exit 1
fi

gcc $common tests/arm64_code_image_test.c \
    src/arm64_code_image.c \
    -o "$build/gcc-arm64-code-image-test"
"$build/gcc-arm64-code-image-test"

clang $common tests/arm64_code_image_test.c \
    src/arm64_code_image.c \
    -o "$build/clang-arm64-code-image-test"
"$build/clang-arm64-code-image-test"

gcc -Iinclude -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/arm64_code_image_test.c src/arm64_code_image.c \
    -o "$build/arm64-code-image-sanitizer-test"
"$build/arm64-code-image-sanitizer-test"

clang --analyze -Iinclude -std=c11 \
    -Wall -Wextra -Wpedantic -Werror \
    src/arm64_code_image.c tests/arm64_code_image_test.c

printf '%s\n' 'ARM64 LITTLE ENDIAN IMAGE VERIFY: PASS'
printf '%s\n' 'ARM64 CODE IMAGE VERIFY: PASS'
printf '%s\n' 'ARM64 W^X REQUEST PLAN VERIFY: PASS'
