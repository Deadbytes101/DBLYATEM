IDENTICAL WINDOWS X86-64 WORKLOAD V1
====================================

PURPOSE
-------

This directory owns one deterministic PE32+ console executable used as the
byte-identical input for later DBLYATEM and Winlator execution packages.

The workload is intentionally smaller than a real application. It exists to
separate executable identity from runtime claims.

WORKLOAD
--------

The entry point performs exactly this sequence:

    SetLastError(42)
    observed = GetLastError()

    if observed == 42
        ExitProcess(0)
    else
        ExitProcess(1)

The executable writes no stdout or stderr. It has no CRT, GUI, filesystem,
network, registry, clock, random, environment or device dependency.

IMPORT SET
----------

    KERNEL32.dll!SetLastError
    KERNEL32.dll!GetLastError
    KERNEL32.dll!ExitProcess

SetLastError and GetLastError exercise the narrow KERNEL32 state semantics
already relevant to DBLYATEM. ExitProcess is the explicit deterministic process
termination boundary.

At the exact baseline used to create this workload, DBLYATEM does not yet expose
ExitProcess in its built-in KERNEL32 provider catalog. This is not hidden. Issue
#120 seals the workload input only. A later DBLYATEM execution must either report
the unsupported import honestly or first land a separately reviewed termination
boundary.

ARTIFACT
--------

The executable is materialized as:

    probe.exe

Its canonical repository representation is:

    probe-v1.hex

The executable generator is:

    generate-probe-v1.c

The generator writes every byte explicitly. It does not invoke a Windows
cross-linker and does not inherit timestamps, debug paths, build IDs or host
environment data.

The canonical verifier compiles the generator independently with GCC and Clang,
generates two isolated artifacts, requires them to be byte-identical, verifies
their fixed SHA-256 and requires their hexadecimal serialization to match
probe-v1.hex exactly.

STRUCTURE
---------

    PE machine          x86-64 / 0x8664
    format              PE32+
    subsystem           Windows console
    entry RVA           0x00001000
    image base          0x0000000140000000
    file bytes          1536
    sections            .text, .idata
    import DLLs         1
    import symbols      3
    stdout              empty
    stderr              empty
    expected exit       0

VERIFY
------

Portable structural and rebuild verification:

    sh tools/verify-windows-x64-workload.sh

Observed native Windows verification is performed by a separately hashed owner
harness published on Issue #120 after the exact feature head exists.

That harness performs three isolated native runs, requires exit code 0 and empty
stdout/stderr on every run, and publishes a create-new local evidence record. It
does not execute DBLYATEM or Winlator.

BOUNDARY
--------

This workload does not establish:

    DBLYATEM Android execution
    Winlator execution
    runtime compatibility
    performance
    parity
    displacement
    victory

Those claims remain blocked until separate retained runtime packages execute this
exact executable SHA-256 on the accepted physical-device manifest.
