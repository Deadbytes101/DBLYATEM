#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/host-pe-input-check"
common='-Iinclude -D_POSIX_C_SOURCE=200809L -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

mkdir -p "$build"
cd "$root"

for source in include/dblyat/host_pe_input.h src/host_pe_input.c tests/host_pe_input_check.c
do
    test -f "$source"
done

for compiler in gcc clang
do
    # shellcheck disable=SC2086
    "$compiler" $common tests/host_pe_input_check.c src/host_pe_input.c \
        -o "$build/$compiler-check"
    "$build/$compiler-check" > "$build/$compiler.log"
    for marker in \
        'HOST PE INPUT BOUNDS: PASS' \
        'HOST PE INPUT OWNER LIFECYCLE: PASS' \
        'HOST PE INPUT SYMLINK COMPATIBILITY: PASS' \
        'HOST PE INPUT CHECK: PASS'
    do
        grep -Fqx "$marker" "$build/$compiler.log"
    done
done

gcc -Iinclude -D_POSIX_C_SOURCE=200809L -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/host_pe_input_check.c src/host_pe_input.c \
    -o "$build/sanitize-check"
ASAN_OPTIONS=detect_leaks=1 "$build/sanitize-check" > "$build/sanitize.log"
grep -Fqx 'HOST PE INPUT CHECK: PASS' "$build/sanitize.log"

clang $common --analyze src/host_pe_input.c \
    -o "$build/host-pe-input.plist"

make -s build/dblyatem

set +e
build/dblyatem > "$build/usage.out" 2>&1
usage_status=$?
build/dblyatem verify /path/that/does/not/exist \
    > "$build/missing.out" 2>&1
missing_status=$?
build/dblyatem verify tests/host_pe_input_check.c \
    > "$build/invalid-pe.out" 2>&1
invalid_status=$?
set -e

test "$usage_status" -eq 2
test "$missing_status" -eq 2
test "$invalid_status" -eq 1
grep -Fqx 'usage: dblyatem verify FILE.exe' "$build/usage.out"
grep -Fqx 'DblyatEM: open failed' "$build/missing.out"
grep -F 'PE VERIFY: FAIL:' "$build/invalid-pe.out" >/dev/null

printf '%s\n' 'HOST PE INPUT FOCUSED VERIFY: PASS'
printf '%s\n' 'HOST FILE RUN CLI: NOT IMPLEMENTED'
printf '%s\n' 'NATIVE ARM64 PE EXECUTION: NOT RUN'
