DBLYATEM PR98 PHYSICAL ANDROID EVIDENCE
======================================

CLASSIFICATION

    EVIDENCE-ONLY ANDROID INSTRUMENTATION HARNESS.
    DO NOT MERGE THIS BRANCH INTO MAIN.
    DO NOT MODIFY PR #98 FEATURE HEAD THROUGH THIS PROJECT.
    NO FIREBASE PROJECT, BILLING, OR PAID DEVICE LAB IS REQUIRED.

EXACT FEATURE INPUT

    PR              #98
    COMMIT          8886fe231eaaefd9c28ed6fe9a7dc404739cc566
    BRANCH          deadbyte/linux-arm64-wx-provider

BUILD CONTRACT

    AGP             9.2.0
    GRADLE          9.4.1
    JDK             17 OR NEWER
    COMPILE SDK     36
    NDK             28.2.13676358
    CMAKE           3.22.1
    ABI             arm64-v8a ONLY
    MIN SDK         26

The local WSL gate checks out the harness and the exact PR #98 commit into
separate directories. CMake compiles the provider implementation and its three
native test translation units directly from that exact checkout.

The build produces the physical-device kit:

    pr98-evidence-app.apk
    pr98-evidence-test.apk
    run-physical-device.sh
    APK SHA256SUMS
    APK entry inventories

No cloud device matrix is started by the workflow.

PHYSICAL DEVICE REQUIREMENTS

    REAL ANDROID DEVICE CONNECTED THROUGH USB ADB
    USB DEBUGGING AUTHORIZED
    KERNEL MACHINE aarch64 OR arm64
    ABI arm64-v8a
    KERNEL PAGE SIZE 4096
    NOT QEMU / RANCHU / GOLDFISH / CUTTLEFISH
    NO ROOT REQUIRED

A borrowed device is acceptable. The runner hashes the ADB serial before
recording it and does not retain the raw serial in the evidence package.

The 4096-byte page requirement is intentional. The exact PR #98 permission test
uses a 4100-byte second image. On a 4096-byte page kernel that image spans two
pages and proves the complete two-page RW -> RX -> unmap transition. A 16K-page
device is rejected for this exact evidence class.

RUNTIME CONTRACT

    COMMIT_SHA=8886fe231eaaefd9c28ed6fe9a7dc404739cc566
    COMPILE_ARCH=AARCH64
    UNAME_MACHINE=aarch64 OR arm64
    RUNTIME_PAGE_SIZE=4096
    PROVIDER_TEST_EXIT=0
    POSIX_TEST_EXIT=0
    EXECUTE_TEST_EXIT=0
    NATIVE_AARCH64_EXECUTION=PASS
    ANDROID_ARM64_RUNTIME_EVIDENCE=PASS
    NO_RWX_TRANSITION_VERIFY=PASS

Any NOT CLAIMED, FAIL, NOT_AARCH64, emulator marker, wrong SHA, wrong ABI, or
wrong page size is a hard failure.

REPORT STORAGE CONTRACT

The instrumentation test writes its machine-readable report to the target
application's internal files directory:

    files/dblyatem-pr98-android-evidence.txt

The physical runner retrieves that exact file through the debuggable app UID:

    adb exec-out run-as com.deadbyte.dblyatem.pr98evidence \
        cat files/dblyatem-pr98-android-evidence.txt

The report does not depend on shell access to /sdcard/Android/data. This avoids
Android 15 and vendor scoped-storage restrictions while retaining app-UID
ownership and no-root operation.

RUNNER

    evidence/android-pr98/run-physical-device.sh

Usage from WSL after building the kit:

    /bin/sh run-physical-device.sh \
        android-build-evidence/apks/pr98-evidence-app.apk \
        android-build-evidence/apks/pr98-evidence-test.apk \
        pr98-physical-android-evidence

If more than one authorized device is attached, set:

    ADB_SERIAL=DEVICE_SERIAL

The runner performs these observed steps:

    VERIFY APK IS arm64-v8a ONLY
    VERIFY EXACT PR #98 SHA EMBEDDED IN JNI LIBRARY
    VERIFY ONE AUTHORIZED PHYSICAL DEVICE
    VERIFY ARM64 KERNEL AND ABI
    VERIFY 4096-BYTE KERNEL PAGES
    REJECT KNOWN EMULATOR PROPERTIES
    REMOVE OLD APP AND TEST PACKAGES
    INSTALL FRESH APK PAIR
    VERIFY RUN-AS APP UID ACCESS
    DELETE STALE INTERNAL REPORT
    RUN THE EXACT INSTRUMENTATION TEST
    REQUIRE OK (1 test) AND COMPLETION CODE
    RETRIEVE INTERNAL REPORT THROUGH RUN-AS
    VERIFY EVERY REQUIRED MARKER
    HASH ALL COLLECTED FILES

OUTPUT

    device.txt
    instrumentation.txt
    logcat.txt
    retrieval-error.txt
    dblyatem-pr98-android-evidence.txt
    accepted.txt
    SHA256SUMS

The evidence branch remains draft and must be closed after its APK build and
physical-device evidence are attached to PR #98. It must never merge into main.
