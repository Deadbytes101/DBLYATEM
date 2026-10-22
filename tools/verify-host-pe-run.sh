#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/host-pe-run-check"
common='-Iinclude -D_POSIX_C_SOURCE=200809L -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

mkdir -p "$build"
cd "$root"
for name in include/dblyat/host_pe_run.h src/host_pe_run.c \
    tests/host_pe_run_plan_test.c main.c
do
    test -f "$name"
done

for compiler in gcc clang
do
    # shellcheck disable=SC2086
    "$compiler" $common tests/host_pe_run_plan_test.c \
        src/host_pe_run.c -o "$build/$compiler-test"
    "$build/$compiler-test" > "$build/$compiler.log"
    grep -Fqx 'HOST PE RUN PLAN CHECK: PASS' "$build/$compiler.log"
    grep -Fqx 'HOST PE RUN PLAN GUEST RANGES: PASS' "$build/$compiler.log"
    grep -Fqx 'HOST PE RUN PLAN PAGE BOUNDS: PASS' "$build/$compiler.log"
done

gcc -Iinclude -D_POSIX_C_SOURCE=200809L -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/host_pe_run_plan_test.c src/host_pe_run.c \
    -o "$build/sanitize-test"
ASAN_OPTIONS=detect_leaks=1 "$build/sanitize-test" > "$build/sanitize.log"
grep -Fqx 'HOST PE RUN PLAN CHECK: PASS' "$build/sanitize.log"

clang $common --analyze src/host_pe_run.c -o "$build/analyze.plist"
# Exercise AArch64 compile-time host branch on x86, not native execution.
gcc $common -D__aarch64__ -fsyntax-only src/host_pe_run.c

make -s build/dblyatem
set +e
build/dblyatem run missing.exe > "$build/run-unsupported.log" 2>&1
native_status=$?
build/dblyatem > "$build/usage.log" 2>&1
usage_status=$?
build/dblyatem verify missing.exe > "$build/verify-missing.log" 2>&1
verify_status=$?
set -e
if [ "$(uname -m)" = x86_64 ]; then
    test "$native_status" -eq 3
    grep -Fqx 'DblyatEM run: native AArch64 Linux host required' \
        "$build/run-unsupported.log"
fi
test "$usage_status" -eq 2
test "$verify_status" -eq 2
grep -Fqx 'usage: dblyatem verify FILE.exe' "$build/usage.log"
grep -Fqx 'DblyatEM: open failed' "$build/verify-missing.log"

printf '%s\n' 'HOST PE RUN PLAN AND CLI FOCUSED VERIFY: PASS'
printf '%s\n' 'AARCH64 COMPILE-BRANCH SYNTAX: PASS'
printf '%s\n' 'NATIVE AARCH64 WINDOWS PE EXECUTION: NOT RUN'
