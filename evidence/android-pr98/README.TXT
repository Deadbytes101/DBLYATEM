DBLYATEM PR98 FIREBASE ANDROID EVIDENCE
======================================

CLASSIFICATION

    EVIDENCE-ONLY ANDROID INSTRUMENTATION HARNESS.
    DO NOT MERGE THIS BRANCH INTO MAIN.
    DO NOT MODIFY PR #98 FEATURE HEAD THROUGH THIS PROJECT.

EXACT FEATURE INPUT

    PR              #98
    COMMIT          8886fe231eaaefd9c28ed6fe9a7dc404739cc566
    BRANCH          deadbyte/linux-arm64-wx-provider

BUILD CONTRACT

    AGP             9.2.0
    GRADLE          9.4.1
    JDK             17
    COMPILE SDK     36
    NDK             28.2.13676358
    CMAKE           3.22.1
    ABI             arm64-v8a ONLY
    MIN SDK         26

The GitHub workflow checks out the harness and the exact PR #98 commit into
separate directories. CMake compiles the provider implementation and its three
native test translation units directly from that exact checkout.

ANDROID TEST CONTRACT

    PROVIDER MOCK / FAILURE / ALIAS MATRIX EXIT 0
    ONE-PAGE AND 4100-BYTE /proc/self/maps TEST EXIT 0
    RUNTIME KERNEL aarch64 OR arm64
    RUNTIME PAGE SIZE 4096
    4100-BYTE IMAGE THEREFORE SPANS TWO PAGES
    NATIVE AARCH64 CODE EXECUTION TEST EXIT 0
    COMPILE ARCH AARCH64
    NO NOT-CLAIMED MARKER
    EVIDENCE FILE WRITTEN TO APP EXTERNAL FILES DIRECTORY

A 16K-page Android target is intentionally rejected for this evidence class.
The exact PR #98 POSIX test uses a 4100-byte second image; only a 4096-byte page
kernel makes that image cross a page boundary and therefore proves a complete
two-page RW -> RX -> unmap transition.

FIREBASE TEST LAB

The Firebase job is disabled until repository credentials and variables are
configured. It targets an explicit catalog model and Android version, validates
the selected model as physical and arm64-v8a capable, runs an instrumentation
matrix through gcloud, and stores Test Lab results in a caller-owned bucket.
Runtime instrumentation additionally rejects a device whose kernel page size is
not 4096.

WSL BOOTSTRAP

Run the bootstrap through /bin/sh from a trusted local WSL session:

    /bin/sh evidence/android-pr98/bootstrap-firebase-wif.sh

Required input environment variables:

    GCP_PROJECT_ID
    FIREBASE_TESTLAB_RESULTS_BUCKET

Optional inputs:

    GCP_BUCKET_LOCATION
    GCP_SERVICE_ACCOUNT_ID
    GCP_WORKLOAD_IDENTITY_POOL_ID
    GCP_WORKLOAD_IDENTITY_PROVIDER_ID
    FIREBASE_TESTLAB_DEVICE_MODEL
    FIREBASE_TESTLAB_ANDROID_VERSION

The bootstrap does not start a Test Lab matrix. It enables required APIs,
creates the dedicated service account and uniform-access result bucket, creates
a Workload Identity Federation provider, and configures the protected GitHub
Environment.

The OIDC provider is restricted by immutable repository ID, repository owner
ID, environment name, and exact workflow reference:

    REPOSITORY ID   1317635781
    OWNER ID        184736772
    ENVIRONMENT     pr98-firebase-testlab
    WORKFLOW        .github/workflows/pr98-firebase-android-evidence.yml
    BRANCH          deadbyte/pr98-firebase-android-evidence

REQUIRED GITHUB SECRETS

    GCP_WORKLOAD_IDENTITY_PROVIDER
    GCP_SERVICE_ACCOUNT

REQUIRED GITHUB VARIABLES

    GCP_PROJECT_ID
    FIREBASE_TESTLAB_DEVICE_MODEL
    FIREBASE_TESTLAB_ANDROID_VERSION
    FIREBASE_TESTLAB_RESULTS_BUCKET

REQUIRED GOOGLE CLOUD ROLES

    roles/cloudtestservice.testAdmin
    roles/firebase.analyticsViewer

Use a dedicated Firebase project and a dedicated results bucket. The workflow
uses Workload Identity Federation and does not require a long-lived JSON key.
