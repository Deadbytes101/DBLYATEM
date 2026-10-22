#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/host-pe-run-aarch64-native"
ARCH=$(uname -m)

if [ "$(uname -s)" != Linux ] || [ "$ARCH" != aarch64 ]; then
    printf 'NATIVE ARM64 PE: REQUIRES LINUX AARCH64; observed %s\n' "$ARCH" >&2
    exit 3
fi

PAGE_SIZE=$(getconf PAGESIZE)
case "$PAGE_SIZE" in
    4096|16384|65536) ;;
    *)
        printf 'NATIVE ARM64 PE: UNSUPPORTED PAGE SIZE %s\n' "$PAGE_SIZE" >&2
        exit 2
        ;;
esac

for tool in gcc make readelf python3 timeout sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'NATIVE ARM64 PE: TOOL MISSING %s\n' "$tool" >&2
        exit 2
    }
done

printf 'NATIVE ARM64 HOST: arch=%s page_size=%s\n' "$ARCH" "$PAGE_SIZE"
printf 'NATIVE ARM64 SOURCE: %s\n' "$(git -C "$ROOT" rev-parse HEAD)"
mkdir -p "$BUILD"

for kind in exit0 exit42 loop; do
    python3 "$ROOT/tools/gen-host-pe-run-fixture.py" "$kind" "$BUILD/$kind.exe"
done

python3 - "$BUILD" <<'PY'
from pathlib import Path
import struct
import sys

out = Path(sys.argv[1])
valid = bytearray((out / "exit0.exe").read_bytes())
coff = 0x80 + 4
opt = coff + 20

def mutation(name, edit):
    data = bytearray(valid)
    edit(data)
    (out / (name + ".exe")).write_bytes(data)

mutation("bad-mz", lambda b: b.__setitem__(slice(0, 2), b"XX"))
mutation("bad-machine", lambda b: struct.pack_into("<H", b, coff, 0x14C))
mutation("image-oversize", lambda b: struct.pack_into("<I", b, opt + 56, 0x01001000))
mutation("image-gateway-overlap", lambda b: struct.pack_into("<Q", b, opt + 24, 0x180000000))
mutation("image-stack-overlap", lambda b: struct.pack_into("<Q", b, opt + 24, 0x70000000))
(out / "oversize-file.exe").write_bytes(
    valid + b"\0" * (16 * 1024 * 1024 + 1 - len(valid))
)
PY

make -s -C "$ROOT" -f GNUmakefile \
    BUILD=build/host-pe-run-aarch64-native HOST_MACHINE=aarch64 CC=gcc \
    build/host-pe-run-aarch64-native/dblyatem

readelf -h "$BUILD/dblyatem" | grep -Fq 'Machine:                           AArch64'
sha256sum "$BUILD/dblyatem" "$BUILD/exit0.exe" "$BUILD/exit42.exe" "$BUILD/loop.exe"

check_exit() {
    kind=$1
    expected=$2
    set +e
    timeout 20s "$BUILD/dblyatem" run "$BUILD/$kind.exe" > "$BUILD/$kind.log" 2>&1
    actual=$?
    set -e
    if [ "$actual" -ne "$expected" ]; then
        printf 'NATIVE ARM64 PE: %s got status %s, expected %s\n' \
            "$kind" "$actual" "$expected" >&2
        cat "$BUILD/$kind.log" >&2
        exit 1
    fi
    printf 'NATIVE ARM64 PE: %s process_status=%s\n' "$kind" "$actual"
}

check_exit exit0 0
check_exit exit42 42
check_exit loop 5
grep -Fxq 'PE RUN: EXIT code=0' "$BUILD/exit0.log"
grep -Fxq 'PE RUN: EXIT code=42' "$BUILD/exit42.log"
grep -Fxq 'PE RUN: BLOCK BUDGET EXHAUSTED' "$BUILD/loop.log"

for kind in bad-mz bad-machine image-oversize \
            image-gateway-overlap image-stack-overlap oversize-file; do
    check_exit "$kind" 2
done
grep -Fxq 'DblyatEM run: invalid PE: bad DOS signature' "$BUILD/bad-mz.log"
grep -Fxq 'DblyatEM run: invalid PE: unsupported machine' "$BUILD/bad-machine.log"
for kind in image-oversize image-gateway-overlap image-stack-overlap; do
    grep -Fxq 'DblyatEM run: image size/range unsupported' "$BUILD/$kind.log"
done
grep -Fxq 'DblyatEM run: invalid host input' "$BUILD/oversize-file.log"

printf '%s\n' 'NATIVE AARCH64 PE EXITPROCESS 0: PASS' \
              'NATIVE AARCH64 PE EXITPROCESS 42: PASS' \
              'NATIVE AARCH64 PE LOOP BUDGET: PASS' \
              'NATIVE AARCH64 PE BAD INPUT: PASS'
