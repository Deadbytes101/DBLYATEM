ARM64 TRANSLATION-REUSE BENCHMARK RECORD V1
================================================

PURPOSE
-------

This directory defines the first raw result boundary for translation-reuse
timing. It is a schema and runner contract, not a competitive result.

FILES
-----

    manifest-v1.txt
    Required runtime, device, platform, driver, power and thermal fields.

    raw-result-v1.csv
    Exact CSV header. One accepted benchmark operation is written per row.

WORKLOAD
--------

Each iteration records four operations in this order:

    PUBLICATION_INSERT
    LOOKUP_HIT
    LOOKUP_MISS
    PUBLICATION_DUPLICATE

The runner measures only the call duration around the merged translation-reuse
API with CLOCK_MONOTONIC_RAW. It does not call host_entry_address, dispatch a
published block, change page permissions, package an APK or control Winlator.

ACCEPTANCE
----------

A DBLYATEM-versus-Winlator comparison requires two manifest and CSV pairs from
the same Android device, Android build, driver, power mode, thermal start,
workload and iteration count. Wins and losses must both remain in the raw data.

No comparison has been run by this seed.
