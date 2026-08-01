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

    cloud-init-ubuntu.yaml
        Provision and execute the evidence route automatically on a fresh
        Ubuntu ARM64 VM.

    retrieve-cloud-init-evidence.sh
        Retrieve cloud-init output through strict SSH from Windows WSL or any
        POSIX host, verify it locally, and create a privacy-preserving receipt.

HOST USAGE
----------

Install the required tools with the host package manager.

Debian / Ubuntu example:

    sudo apt-get update
    sudo apt-get install -y \
        git gcc clang file util-linux coreutils tar

Freshness challenge:

    0f9d66bc95e62d3c5fa1ae695ed9917605db48c31e24427667c9dd8d08391328

Run:

    chmod +x run-native-arm64-linux-evidence.sh

    export DBLYATEM_EVIDENCE_CHALLENGE=\
0f9d66bc95e62d3c5fa1ae695ed9917605db48c31e24427667c9dd8d08391328

    ./run-native-arm64-linux-evidence.sh

Optional output path:

    ./run-native-arm64-linux-evidence.sh \
        "$HOME/pr98-native-arm64-linux-evidence"

CLOUD-INIT USAGE
----------------

For a fresh Ubuntu ARM64 VM, provide this file as cloud-init user data:

    cloud-init-ubuntu.yaml

The cloud-init route pins the portable collector and verifier at:

    b1688958f1993a5c885beb912f39fe7be5f64133

It installs the required packages, executes the challenge-bound collector,
verifies both the evidence directory and tar.gz archive, and writes:

    /var/tmp/DBLYATEM-PR98-NATIVE-ARM64-STATUS.txt
    /var/tmp/pr98-native-arm64-linux-evidence/
    /var/tmp/pr98-native-arm64-linux-evidence.tar.gz
    /var/tmp/pr98-native-arm64-linux-evidence.tar.gz.sha256

The cloud-init file contains no private key, cloud credential, public IP,
account identifier, or billing data.

WINDOWS WSL RETRIEVAL
---------------------

The retrieval script requires OpenSSH `ssh` and `scp`, a non-empty trusted
known_hosts file, and the offline verifier beside the script.

It enforces:

    BATCH MODE
    STRICT HOST KEY CHECKING
    REMOTE STATUS EXACT MARKERS
    REMOTE ARCHIVE SHA-256 PASS
    LOCAL ARCHIVE SHA-256 PASS
    LOCAL OFFLINE PACKAGE VERIFY PASS
    FIXED LOCAL DESTINATION BASENAME
    NO SYMLINK DESTINATION

The script accepts `user@host-or-ssh-alias`. IPv6 literals and shell metacharacters
are rejected. Use an SSH config alias for an IPv6 target.

Example from WSL with an explicit identity file:

    chmod +x retrieve-cloud-init-evidence.sh

    export DBLYATEM_KNOWN_HOSTS="$HOME/.ssh/known_hosts"

    ./retrieve-cloud-init-evidence.sh \
        ubuntu@dblyatem-arm64 \
        22 \
        /mnt/c/Users/DEADBYTE/.ssh/dblyatem-arm64 \
        /mnt/d/TECHNICAL/DblyatEM/build

Example using an SSH agent or default configured identity:

    ./retrieve-cloud-init-evidence.sh \
        ubuntu@dblyatem-arm64 \
        22 \
        '' \
        /mnt/d/TECHNICAL/DblyatEM/build

Local retrieval output:

    pr99-native-arm64-linux-evidence-current/
        DBLYATEM-PR98-NATIVE-ARM64-STATUS.txt
        pr98-native-arm64-linux-evidence.tar.gz
        pr98-native-arm64-linux-evidence.tar.gz.sha256
        retrieval.txt
        SHA256SUMS

The retrieval receipt records only SHA-256 of `user@host-or-alias`; it does not
store the raw host, private-key path, key bytes, cloud account, or billing data.

EXPECTED FINAL OUTPUT
---------------------

Host execution:

    DBLYATEM PR98 NATIVE ARM64 LINUX EVIDENCE ACCEPTED
    HEAD=8886fe231eaaefd9c28ed6fe9a7dc404739cc566
    EVIDENCE_CHALLENGE=0f9d66bc95e62d3c5fa1ae695ed9917605db48c31e24427667c9dd8d08391328
    MACHINE=aarch64
    PAGE_SIZE=4096

Retrieval:

    DBLYATEM PR98 NATIVE ARM64 CLOUD EVIDENCE RETRIEVAL: PASS
    RETRIEVAL=PASS
    LOCAL_OFFLINE_PACKAGE_VERIFY=PASS

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
    EVIDENCE CHALLENGE 0f9d66bc95e62d3c5fa1ae695ed9917605db48c31e24427667c9dd8d08391328
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
    RETRIEVAL RECEIPT VERIFY

DISPOSITION
-----------

The generated package is evidence, not feature code.

Attach the validated result to PR #99, PR #98, and Issue #96. Keep PR #99
draft and never merge it. PR #98 must remain draft until all review and CI
policy gates are also satisfied.
