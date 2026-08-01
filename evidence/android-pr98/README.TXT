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
    ONE-PAGE AND TWO-PAGE /proc/self/maps TEST EXIT 0
    NATIVE AARCH64 CODE EXECUTION TEST EXIT 0
    COMPILE ARCH AARCH64
    NO NOT-CLAIMED MARKER
    EVIDENCE FILE WRITTEN TO APP EXTERNAL FILES DIRECTORY

FIREBASE TEST LAB

The Firebase job is disabled until repository credentials and variables are
configured. It targets an explicit catalog model and Android version, validates
the selected model as physical and arm64-v8a capable, runs an instrumentation
matrix through gcloud, and stores Test Lab results in a caller-owned bucket.

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
