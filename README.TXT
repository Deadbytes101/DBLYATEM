DBLYATEM
========

DBLYATEM is an x86-64 Windows guest runtime for an ARM64 Android host.

The project is being built as a native, bounded, testable runtime rather
than a packaging layer around an existing Windows compatibility stack.
The long-term execution engine is dynamic binary translation from x86-64
to ARM64.

ENGINEERING TARGET
------------------

Compete with Winlator through observed results:

    compatibility
    startup latency
    memory footprint
    stability
    frame pacing

Claims are not evidence.  Every comparison must use reproducible builds,
fixed workloads, recorded hardware, and published measurements.

CURRENT LANDED BOUNDARIES
-------------------------

    PE32+ AMD64 structural verifier
    bounded caller-owned image mapper
    exact DOS-header layout assertions
    read-only base-relocation planner
    transactional DIR64 relocation applier

The relocation applier validates the entire operation before the first
image write and leaves the image unchanged on failure.

BUILD
-----

    make clean
    make test
    make

EXPECTED TESTS
--------------

    PE TEST: PASS
    MAP TEST: PASS
    RELOC TEST: PASS
    RELOC APPLY TEST: PASS
    PE STRUCT TEST: PASS

COMMAND LINE
------------

    ./build/dblyatem verify program.exe

NEXT HARD BOUNDARY
------------------

Build a read-only bounded import-directory planner.

It may parse descriptors, DLL names, ILT/IAT thunk chains, imports by
name, ordinal imports, and terminators into caller-owned records.

It must not load DLLs, resolve host symbols, mutate the IAT, change page
permissions, execute TLS, emit JIT code, or run guest instructions.

RULE
----

NO MAGIC.  NO GUESSING.  RUN THE BYTES AND READ WHAT THE MACHINE SAYS.
