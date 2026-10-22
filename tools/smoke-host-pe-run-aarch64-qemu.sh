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
for kind in exit0 exit42 loop; do
    python3 "$ROOT/tools/gen-host-pe-run-fixture.py" "$kind" "$BUILD/$kind.exe"
done

# Adversarial inputs derived from the accepted ExitProcess(0) fixture.
# They exercise the native AArch64 host-run rejection path under QEMU,
# rather than relying on a compile-only branch check.
python3 - "$BUILD" <<'PY'
from pathlib import Path
import struct
import sys

out = Path(sys.argv[1])
valid = bytearray((out / "exit0.exe").read_bytes())
coff = 0x80 + 4
opt = coff + 20

def write_mutation(name, mutate):
    data = bytearray(valid)
    mutate(data)
    (out / (name + ".exe")).write_bytes(data)

write_mutation("bad-mz", lambda b: b.__setitem__(slice(0, 2), b"XX"))
write_mutation("bad-machine", lambda b: struct.pack_into("<H", b, coff, 0x14C))
write_mutation("image-oversize", lambda b: struct.pack_into("<I", b, opt + 56, 0x01001000))
write_mutation("image-gateway-overlap", lambda b: struct.pack_into("<Q", b, opt + 24, 0x180000000))
write_mutation("image-stack-overlap", lambda b: struct.pack_into("<Q", b, opt + 24, 0x70000000))
(out / "oversize-file.exe").write_bytes(
    valid + b"\\0" * (16 * 1024 * 1024 + 1 - len(valid))
)
PY

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
    "$BUILD/dblyatem" run "$BUILD/loop.exe" > "$BUILD/loop.log" 2>&1
loop=$?
set -e

test "$zero" -eq 0
test "$forty_two" -eq 42
test "$loop" -eq 5
grep -Fxq 'PE RUN: EXIT code=0' "$BUILD/exit0.log"
grep -Fxq 'PE RUN: EXIT code=42' "$BUILD/exit42.log"
grep -Fxq 'PE RUN: BLOCK BUDGET EXHAUSTED' "$BUILD/loop.log"

for kind in bad-mz bad-machine image-oversize \\
            image-gateway-overlap image-stack-overlap oversize-file; do
    set +e
    timeout 20s qemu-aarch64 -L /usr/aarch64-linux-gnu \\
        "$BUILD/dblyatem" run "$BUILD/$kind.exe" > "$BUILD/$kind.log" 2>&1
    rejected=$?
    set -e
    if [ "$rejected" -ne 2 ]; then
        printf 'AARCH64 QEMU BAD INPUT: %s exited %s instead of 2\\n' \\
            "$kind" "$rejected" >&2
        exit 1
    fi
done
grep -Fxq 'DblyatEM run: invalid PE: bad DOS signature' "$BUILD/bad-mz.log"
grep -Fxq 'DblyatEM run: invalid PE: unsupported machine' "$BUILD/bad-machine.log"
for kind in image-oversize image-gateway-overlap image-stack-overlap; do
    grep -Fxq 'DblyatEM run: image size/range unsupported' "$BUILD/$kind.log"
done
grep -Fxq 'DblyatEM run: invalid host input' "$BUILD/oversize-file.log"
printf '%s\n' 'AARCH64 QEMU EXITPROCESS 0: PASS' \
              'AARCH64 QEMU EXITPROCESS 42: PASS' \
              'AARCH64 QEMU LOOP BUDGET: PASS' \
              'AARCH64 QEMU BAD INPUT REJECTION: PASS' \
              'AARCH64 QEMU HOST RUN SMOKE: PASS' \
              'PHYSICAL ARM64 HOST: NOT RUN'
