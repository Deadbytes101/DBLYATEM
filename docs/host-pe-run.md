# host PE run CLI (issue #153)

`dblyatem verify FILE.exe` keeps the original verification contract.

`dblyatem run FILE.exe` is a native AArch64 Linux entry point into the
existing translated PE session lifecycle. It is not a Wine or Winlator
compatibility implementation. On non-AArch64 builds it fails with exit code 3
and does not attempt to execute translated ARM64 machine code.

## resource boundary

- source: shared `db_host_pe_input` owner, read-only private file mapping
- input: maximum 16 MiB, and only a validated PE64 image
- mapped image: maximum 16 MiB, caller allocated image and staging buffers
- page plan: checked ceil(image bytes / actual host page size)
- page size: 4096, 16384, or 65536 bytes, power of two
- guest image: PE preferred image base only; relocation policy remains in startup
- guest stack: 64 KiB at 0x70000000
- DBGT gateway region: 64 KiB at 0x180000000, slots of 64 bytes
- native translated code address candidates: 0x1000000000, 64 KiB stride.
  These are **not** reservations. W^X POSIX publication must get the exact
  address safely through MAP_FIXED_NOREPLACE when supported, and refuse
  mismatched mappings. Existing mappings must not be replaced
- translation store: 32 blocks, translation 8 instructions/64 source bytes
  per block, 64 blocks per run, maximum 16 run calls per process
- provider catalog and import/reloc workspace have fixed documented capacities
  in `src/host_pe_run.c`; oversize input is rejected, not truncated
- no heap allocation within the startup, translation, or session layers
- far AArch64 helper veneers use volatile x9, preserving x16 guest address
  and x17 guest data across chained helpers; register clobber was verified
  against a real ExitProcess-import PE fixture under qemu-aarch64

The host executes bounded prepare/run/dispose and frees host allocations and
the borrowed file mapping on every exit. Failure to dispose W^X mappings is
reported; exiting the CLI process lets the OS recover process mappings.

Source mutation after a successful mmap is **not** prevented by MAP_PRIVATE;
a concurrently truncated file can still cause SIGBUS. Host run is for
trusted, stable files until an immutable source-copy input mode is proven.

## status and verification

`tools/verify-host-pe-run.sh` covers bounds, guest-region rejection, x86
unsupported behavior, verify CLI compatibility, GCC/Clang, sanitizers,
static analysis, and an AArch64 preprocessor branch syntax check. That
compile-only check is **not** native AArch64 execution proof.

Exact PE startup/runtime on physical AArch64 Linux or Android and comparison
with Winlator remain separately gated, not claimed by a WSL x86_64 result.

## upstream limitation

Only code paths supported by the current PE loader, IAT/DBGT and x64 ISA
translator can run. No broad Windows API compatibility, SEH, TLS callback,
exceptions, arbitrary import providers or unlimited translation cache.

## optional AArch64 QEMU execution check

`sh tools/smoke-host-pe-run-aarch64-qemu.sh` cross-builds a separate
AArch64 CLI without overwriting `build/dblyatem`. It generates small,
deterministic PE64 fixtures and checks that translated `ExitProcess(0)`
returns 0, `ExitProcess(42)` returns 42 and an infinite guest jump
exhausts its block budget (exit 5). It requires Clang, the AArch64
glibc cross sysroot, cross readelf, QEMU user mode, Python 3 and `timeout`.
This is optional on hosts without those tools.

The test exercises actual translated AArch64 machine code inside
QEMU. It is not evidence of execution on physical ARM64 hardware.
