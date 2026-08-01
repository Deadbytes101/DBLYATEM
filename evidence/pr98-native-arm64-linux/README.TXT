DBLYATEM PR98 NATIVE ARM64 LINUX EVIDENCE KIT
================================================

CLASSIFICATION
--------------

PORTABLE EVIDENCE TRANSPORT FOR PR #98.

THIS DIRECTORY DOES NOT MODIFY PR #98 FEATURE SOURCE.
THIS EVIDENCE BRANCH MUST NEVER MERGE INTO MAIN.

EXACT FEATURE INPUT
-------------------

    PR              #98
    FEATURE HEAD    8886fe231eaaefd9c28ed6fe9a7dc404739cc566

PURPOSE
-------

Run the exact PR #98 native Linux ARM64 evidence class without depending on
GitHub Actions.

The runner creates a fresh detached checkout of the exact feature commit,
verifies the host contract, executes the canonical provider verifier, enforces
exact marker counts, and emits a hashed evidence package.

ACCEPTED HOST CLASS
-------------------

    LINUX KERNEL
    MACHINE aarch64 OR arm64
    NATIVE AARCH64 / ARM64 USERSPACE ELF
    PAGE SIZE 4096
    GCC PRESENT
    CLANG PRESENT
    NOT ANDROID
    NO QEMU TCG / BOCHS / UML SOFTWARE-EMULATION MARKER

A physical ARM64 board is accepted.

An ARM-native cloud VM is accepted when its guest architecture is AArch64 and
the recorded host data does not show software emulation. KVM or another hardware
virtualization layer is not rejected merely because the host is virtual.

REJECTED ROUTES
---------------

    X86-64 HOST WITH QEMU USER EMULATION
    QEMU TCG SYSTEM EMULATION
    ANDROID / TERMUX / PROOT-DISTRO
    DOCKER BUILDX ARM64 UNDER BINFMT EMULATION
    WRONG FEATURE COMMIT
    DIRTY EXACT-HEAD CHECKOUT
    PAGE SIZE OTHER THAN 4096
    MISSING GCC OR CLANG
    ANY FAIL / FAILED / NOT_AARCH64 MARKER

FILES
-----

    run-native-arm64-linux-evidence.sh
        Execute on the accepted Linux ARM64 host.

    verify-evidence-package.sh
        Verify the resulting directory or tar.gz package offline.

HOST USAGE
----------

Install the required tools with the host package manager.

Debian / Ubuntu example:

    sudo apt-get update
    sudo apt-get install -y \
        git gcc clang file util-linux coreutils tar

Run:

    chmod +x run-native-arm64-linux-evidence.sh
    ./run-native-arm64-linux-evidence.sh

Optional output path:

    ./run-native-arm64-linux-evidence.sh \
        "$HOME/pr98-native-arm64-linux-evidence"

EXPECTED FINAL OUTPUT
---------------------

    DBLYATEM PR98 NATIVE ARM64 LINUX EVIDENCE ACCEPTED
    HEAD=8886fe231eaaefd9c28ed6fe9a7dc404739cc566
    MACHINE=aarch64
    PAGE_SIZE=4096

PACKAGE OUTPUT
--------------

    pr98-native-arm64-linux-evidence/
        host.txt
        step18.log
        lscpu.txt
        cpuinfo.txt
        accepted.txt
        SHA256SUMS

    pr98-native-arm64-linux-evidence.tar.gz
    pr98-native-arm64-linux-evidence.tar.gz.sha256

OFFLINE VERIFY
--------------

    chmod +x verify-evidence-package.sh

Directory:

    ./verify-evidence-package.sh \
        ./pr98-native-arm64-linux-evidence

Archive:

    ./verify-evidence-package.sh \
        ./pr98-native-arm64-linux-evidence.tar.gz

ACCEPTANCE CONTRACT
-------------------

    COMMIT SHA EXACT
    MACHINE aarch64 OR arm64
    HOST ARCH AARCH64
    PAGE SIZE 4096
    ANDROID NO
    QEMU TCG NO
    CLEAN EXACT-HEAD CHECKOUT
    MOCK PASS COUNT 3
    PERMISSION PASS COUNT 2
    NATIVE AARCH64 EXECUTION PASS COUNT 1
    ANDROID EVIDENCE NOT CLAIMED COUNT 1
    NO RWX PASS COUNT 1
    FINAL PROVIDER PASS COUNT 1
    HASHES VERIFY

DISPOSITION
-----------

The generated package is evidence, not feature code.

Attach the validated result to PR #99, PR #98, and Issue #96. Keep PR #99
draft and never merge it. PR #98 must remain draft until all review and CI
policy gates are also satisfied.
