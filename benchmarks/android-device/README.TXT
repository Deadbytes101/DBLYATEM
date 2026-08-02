ANDROID SAME-DEVICE EVIDENCE MANIFEST V1
========================================

PURPOSE
-------

This directory defines the first physical-Android environment record required
before a DBLYATEM-versus-Winlator comparison can be accepted.

The capture adapter is read-only. It records a SHA-256 of the selected ADB
serial but never stores the raw serial. Exact Android properties, thermal state,
battery state and SurfaceFlinger GLES identity are retained as redacted snapshot
files and bound through SHA-256 values in the manifest.

CAPTURE
-------

Run from a host with exactly one authorized physical Android device:

    sh tools/capture-android-device-manifest.sh \
        OUTPUT_DIRECTORY POWER_MODE THERMAL_START

POWER_MODE and THERMAL_START are explicit operator declarations using only:

    A-Z a-z 0-9 . _ -

The first scoreboard route requires:

    PRIMARY_ABI=arm64-v8a
    PAGE_SIZE=4096

OUTPUT FILES
------------

    android-device-manifest-v1.txt
    battery.txt
    getprop.txt
    surfaceflinger-gles.txt
    thermalservice.txt
    SHA256SUMS

BOUNDARY
--------

The adapter does not install, copy, start, stop or modify software on the device.
It does not execute DBLYATEM, execute Winlator, package an APK, measure frame
time, report performance or claim compatibility.

A later benchmark package must bind both runtime result sets to the same accepted
device manifest hash and the same declared power and thermal start state.
