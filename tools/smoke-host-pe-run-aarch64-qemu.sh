#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/host-pe-run-aarch64-qemu"
for tool in clang qemu-aarch64 aarch64-linux-gnu-readelf python3 timeout grep; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'AARCH64 QEMU HOST RUN: TOOL MISSING %s\n' "$tool" >&2
        exit 2
    }
done
test -f /usr/aarch64-linux-gnu/lib/libc.so.6 || {
    echo 'AARCH64 QEMU HOST RUN: CROSS GLIBC MISSING' >&2
    exit 2
}
mkdir -p "$BUILD"
for kind in exit0 exit42 exit42ecx exit42ecxclear loop; do
    python3 "$ROOT/tools/gen-host-pe-run-fixture.py" "$kind" "$BUILD/$kind.exe"
done

# Isolation: a separate BUILD directory avoids overwriting the normal x86 binary.
make -s -C "$ROOT" -f GNUmakefile \
    BUILD=build/host-pe-run-aarch64-qemu \
    HOST_MACHINE=aarch64 \
    CC='clang --target=aarch64-linux-gnu --sysroot=/ -B/usr/aarch64-linux-gnu/bin' \
    build/host-pe-run-aarch64-qemu/dblyatem
aarch64-linux-gnu-readelf -h "$BUILD/dblyatem" |
    grep -Fq 'Machine:                           AArch64'

set +e
timeout 20s qemu-aarch64 -L /usr/aarch64-linux-gnu \
    "$BUILD/dblyatem" run "$BUILD/exit0.exe" > "$BUILD/exit0.log" 2>&1
zero=$?
timeout 20s qemu-aarch64 -L /usr/aarch64-linux-gnu \
    "$BUILD/dblyatem" run "$BUILD/exit42.exe" > "$BUILD/exit42.log" 2>&1
forty_two=$?
timeout 20s qemu-aarch64 -L /usr/aarch64-linux-gnu \
    "$BUILD/dblyatem" run "$BUILD/exit42ecx.exe" > "$BUILD/exit42ecx.log" 2>&1
ecx=$?
timeout 20s qemu-aarch64 -L /usr/aarch64-linux-gnu \
    "$BUILD/dblyatem" run "$BUILD/exit42ecxclear.exe" > "$BUILD/exit42ecxclear.log" 2>&1
ecx_clear=$?
timeout 20s qemu-aarch64 -L /usr/aarch64-linux-gnu \
    "$BUILD/dblyatem" run "$BUILD/loop.exe" > "$BUILD/loop.log" 2>&1
loop=$?
set -e

test "$zero" -eq 0
test "$forty_two" -eq 42
test "$ecx" -eq 42
test "$ecx_clear" -eq 42
test "$loop" -eq 5
grep -Fxq 'PE RUN: EXIT code=0' "$BUILD/exit0.log"
grep -Fxq 'PE RUN: EXIT code=42' "$BUILD/exit42.log"
grep -Fxq 'PE RUN: EXIT code=42' "$BUILD/exit42ecx.log"
grep -Fxq 'PE RUN: EXIT code=42' "$BUILD/exit42ecxclear.log"
grep -Fxq 'PE RUN: BLOCK BUDGET EXHAUSTED' "$BUILD/loop.log"
printf '%s\n' 'AARCH64 QEMU EXITPROCESS 0: PASS' \
              'AARCH64 QEMU EXITPROCESS 42: PASS' \
              'AARCH64 QEMU MOV ECX ZERO EXTEND: PASS' \
              'AARCH64 QEMU LOOP BUDGET: PASS' \
              'AARCH64 QEMU HOST RUN SMOKE: PASS' \
              'PHYSICAL ARM64 HOST: NOT RUN'
