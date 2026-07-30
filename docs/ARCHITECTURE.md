DBLYATEM ARCHITECTURE
====================

```mermaid
flowchart TB
    EXE[Windows x86-64 PE executable]

    subgraph GUEST[Guest image layer]
        VERIFY[Bounded PE32+ verifier]
        MAP[Image mapper]
        META[TLS / relocations / unwind metadata]
        IMPORTS[Import and export resolver]
    end

    subgraph ENGINE[Execution engine]
        DECODE[x86-64 decoder]
        REF[Reference interpreter]
        IR[Owned SSA-like IR]
        OPT[Verified local optimization passes]
        EMIT[ARM64 code generator]
        CACHE[W xor X code cache]
    end

    subgraph WIN[Windows personality]
        NTDLL[NTDLL contract]
        OBJECTS[Handles and objects]
        SYNC[Events / mutexes / semaphores / waits]
        THREADS[Threads / TLS / SEH]
        VM[Virtual memory]
        FILES[Files / paths / registry]
    end

    subgraph API[User-mode Windows APIs]
        KERNEL32[KERNEL32]
        USER32[USER32]
        INPUT[XInput / raw input]
        AUDIO[WASAPI / XAudio2]
        DXGI[DXGI]
        D3D11[D3D11]
    end

    subgraph HOST[Android native host]
        BIONIC[bionic and Linux syscalls]
        WINDOW[ANativeWindow]
        AINPUT[Android input]
        AAUDIO[AAudio]
        VULKAN[Vulkan]
        FS[App-sandbox filesystem]
    end

    EXE --> VERIFY --> MAP
    MAP --> META
    MAP --> IMPORTS
    MAP --> DECODE
    DECODE --> REF
    DECODE --> IR --> OPT --> EMIT --> CACHE

    IMPORTS --> NTDLL
    NTDLL --> OBJECTS
    NTDLL --> SYNC
    NTDLL --> THREADS
    NTDLL --> VM
    NTDLL --> FILES

    KERNEL32 --> NTDLL
    USER32 --> WINDOW
    INPUT --> AINPUT
    AUDIO --> AAUDIO
    DXGI --> VULKAN
    D3D11 --> VULKAN

    OBJECTS --> BIONIC
    SYNC --> BIONIC
    THREADS --> BIONIC
    VM --> BIONIC
    FILES --> FS
    CACHE --> BIONIC
```

INVARIANT
---------

No guest byte becomes executable before structural verification, bounded
mapping, decode validation, and a write-to-execute permission transition.
