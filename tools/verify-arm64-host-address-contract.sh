#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/arm64-host-address-contract-verify"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
test_source='tests/arm64_host_address_contract_test.c'
sources='src/x64_arm64_relocate.c src/x64_arm64_relocate_validate.c src/x64_arm64_relocate_emit.c src/arm64_code_image.c'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

for command_name in gcc clang grep; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'ARM64 HOST ADDRESS CONTRACT VERIFY: missing command: %s\n' \
            "$command_name" >&2
        exit 1
    }
done

grep -F 'static bool x64_canonical_nonzero' \
    src/x64_arm64_relocate_validate.c >/dev/null
grep -F 'upper == UINT64_C(0) ||' \
    src/x64_arm64_relocate_validate.c >/dev/null
grep -F 'upper == UINT64_C(0) ||' \
    src/arm64_code_image.c >/dev/null
grep -F 'UINT64_C(0x0000ffff00000000)' \
    "$test_source" >/dev/null
grep -F 'UINT64_C(0x0000aaaa12345678)' \
    "$test_source" >/dev/null
grep -F 'UINT64_C(0x0001000000000000)' \
    "$test_source" >/dev/null
grep -F 'UINT64_C(0x0000800000000000)' \
    "$test_source" >/dev/null

# shellcheck disable=SC2086
gcc $common "$test_source" $sources \
    -o "$build/gcc-arm64-host-address-contract-test"
"$build/gcc-arm64-host-address-contract-test"

# shellcheck disable=SC2086
clang $common "$test_source" $sources \
    -o "$build/clang-arm64-host-address-contract-test"
"$build/clang-arm64-host-address-contract-test"

# shellcheck disable=SC2086
gcc -Iinclude -Isrc -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$test_source" $sources \
    -o "$build/arm64-host-address-contract-sanitizer"
ASAN_OPTIONS=detect_leaks=1 \
    "$build/arm64-host-address-contract-sanitizer"

# shellcheck disable=SC2086
clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    $sources "$test_source"

printf '%s\n' 'ARM64 HOST ADDRESS CONTRACT VERIFY: PASS'
printf '%s\n' 'ARM64 UNSIGNED 48-BIT HOST VA: PASS'
printf '%s\n' 'X64 GUEST CANONICALITY PRESERVED: PASS'
printf '%s\n' 'ARM64 HOST ADDRESS OVERFLOW BOUNDARY: PASS'
