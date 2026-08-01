#!/bin/sh
set -eu

expected_feature_head='5c219e8872895adcb7c3e9af17c32e9676420ea5'

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
harness_root=$(git -C "$script_dir" rev-parse --show-toplevel 2>/dev/null || true)

if [ -z "$harness_root" ]; then
    printf '%s\n' 'ORCHESTRATOR MUST RUN INSIDE THE PR100 HARNESS CHECKOUT' >&2
    exit 64
fi

harness_head=$(git -C "$harness_root" rev-parse HEAD)
harness_dirty=$(git -C "$harness_root" status \
    --porcelain \
    --untracked-files=no)

if [ -n "$harness_dirty" ]; then
    printf '%s\n' 'PR100 HARNESS CHECKOUT IS DIRTY' >&2
    printf '%s\n' "$harness_dirty" >&2
    exit 65
fi

bootstrap_script="$script_dir/bootstrap-local-wsl.sh"
build_script="$script_dir/build-local-wsl.sh"
source_runner="$script_dir/run-physical-device.sh"
cmake_file="$script_dir/app/src/main/cpp/CMakeLists.txt"
instrumentation_file="$script_dir/app/src/androidTest/java/com/deadbyte/dblyatem/pr98evidence/WxProviderInstrumentationTest.java"

for required_file in \
    "$bootstrap_script" \
    "$build_script" \
    "$source_runner" \
    "$cmake_file" \
    "$instrumentation_file"
do
    [ -f "$required_file" ] || {
        printf 'REQUIRED HARNESS FILE MISSING: %s\n' "$required_file" >&2
        exit 66
    }
done

grep -F "expected_feature_head='$expected_feature_head'" \
    "$build_script" >/dev/null || {
        printf '%s\n' 'LOCAL BUILD FEATURE PIN MISMATCH' >&2
        exit 67
    }

grep -F "expected_head='$expected_feature_head'" \
    "$source_runner" >/dev/null || {
        printf '%s\n' 'PHYSICAL RUNNER FEATURE PIN MISMATCH' >&2
        exit 68
    }

grep -F "$expected_feature_head" "$cmake_file" >/dev/null || {
    printf '%s\n' 'CMAKE FEATURE PIN MISMATCH' >&2
    exit 69
}

grep -F "$expected_feature_head" "$instrumentation_file" >/dev/null || {
    printf '%s\n' 'INSTRUMENTATION FEATURE PIN MISMATCH' >&2
    exit 70
}

output_root=${DBLYATEM_ANDROID_EVIDENCE_ROOT:-"$harness_root/build/pr100-current-head-android-evidence"}

case "$output_root" in
    /*) ;;
    *)
        printf 'EVIDENCE ROOT MUST BE ABSOLUTE: %s\n' "$output_root" >&2
        exit 71
        ;;
esac

if [ "$output_root" = / ] || [ -L "$output_root" ]; then
    printf 'UNSAFE EVIDENCE ROOT: %s\n' "$output_root" >&2
    exit 72
fi

mkdir -p "$output_root"

kit_dir="$output_root/pr100-android-kit"
physical_dir="$output_root/pr100-physical-android-evidence"
receipt="$output_root/receipt.txt"
top_manifest="$output_root/SHA256SUMS"

rm -f "$receipt" "$top_manifest"

printf '%s\n' 'BUILDING CURRENT-HEAD ANDROID KIT'
/bin/sh "$bootstrap_script" "$kit_dir"

app_apk="$kit_dir/android-build-evidence/apks/pr98-evidence-app.apk"
test_apk="$kit_dir/android-build-evidence/apks/pr98-evidence-test.apk"
runner="$kit_dir/run-physical-device.sh"
kit_manifest="$kit_dir/SHA256SUMS"

for required_artifact in \
    "$app_apk" \
    "$test_apk" \
    "$runner" \
    "$kit_manifest"
do
    [ -f "$required_artifact" ] || {
        printf 'BUILT KIT ARTIFACT MISSING: %s\n' "$required_artifact" >&2
        exit 73
    }
done

sha256sum -c "$kit_manifest"

unzip -p "$app_apk" lib/arm64-v8a/libpr98evidence.so |
    strings |
    grep -F "$expected_feature_head" >/dev/null || {
        printf '%s\n' 'BUILT APK FEATURE PIN VERIFY FAILED' >&2
        exit 74
    }

work_root=${DBLYATEM_ANDROID_WORK_ROOT:-"${HOME:?HOME_NOT_SET}/.cache/dblyatem-pr100-android"}
sdk_root=${ANDROID_SDK_ROOT:-"$work_root/android-sdk"}
export PATH="$sdk_root/platform-tools:$PATH"

command -v adb >/dev/null 2>&1 || {
    printf 'ADB NOT FOUND AFTER BUILD: %s\n' "$sdk_root/platform-tools/adb" >&2
    exit 75
}

printf '%s\n' 'RUNNING CURRENT-HEAD PHYSICAL ANDROID EVIDENCE'
/bin/sh "$runner" \
    "$app_apk" \
    "$test_apk" \
    "$physical_dir"

physical_manifest="$physical_dir/SHA256SUMS"
accepted="$physical_dir/accepted.txt"
runtime_report="$physical_dir/dblyatem-pr98-android-evidence.txt"

for required_evidence in \
    "$physical_manifest" \
    "$accepted" \
    "$runtime_report"
do
    [ -f "$required_evidence" ] || {
        printf 'PHYSICAL EVIDENCE FILE MISSING: %s\n' "$required_evidence" >&2
        exit 76
    }
done

sha256sum -c "$physical_manifest"

require_line()
{
    line=$1
    file=$2
    code=$3

    grep -Fx "$line" "$file" >/dev/null || {
        printf 'FINAL ORCHESTRATOR CONTRACT MISSING: %s\n' "$line" >&2
        exit "$code"
    }
}

require_line \
    "HEAD=$expected_feature_head" \
    "$accepted" \
    77

require_line \
    'DEVICE_CLASS=PHYSICAL' \
    "$accepted" \
    78

require_line \
    'INSTRUMENTATION_RESULT=PASS' \
    "$accepted" \
    79

require_line \
    'REPORT_RETRIEVAL=PASS' \
    "$accepted" \
    80

require_line \
    'ANDROID_RUNTIME=PASS' \
    "$accepted" \
    81

require_line \
    "COMMIT_SHA=$expected_feature_head" \
    "$runtime_report" \
    82

require_line \
    'PROVIDER_TEST_EXIT=0' \
    "$runtime_report" \
    83

require_line \
    'POSIX_TEST_EXIT=0' \
    "$runtime_report" \
    84

require_line \
    'EXECUTE_TEST_EXIT=0' \
    "$runtime_report" \
    85

require_line \
    'NATIVE_AARCH64_EXECUTION=PASS' \
    "$runtime_report" \
    86

require_line \
    'ANDROID_ARM64_RUNTIME_EVIDENCE=PASS' \
    "$runtime_report" \
    87

require_line \
    'NO_RWX_TRANSITION_VERIFY=PASS' \
    "$runtime_report" \
    88

if grep -F 'NOT CLAIMED' "$runtime_report" >/dev/null ||
   grep -E '=(FAIL|NOT_AARCH64)$' "$runtime_report" >/dev/null
then
    printf '%s\n' 'FINAL RUNTIME REPORT CONTAINS FAILURE MARKER' >&2
    exit 89
fi

serial_hash=$(sed -n 's/^DEVICE_SERIAL_SHA256=//p' "$accepted")

case "$serial_hash" in
    [0-9a-f][0-9a-f]*) ;;
    *)
        printf '%s\n' 'DEVICE SERIAL SHA256 RECORD MISSING OR INVALID' >&2
        exit 90
        ;;
esac

{
    printf '%s\n' \
        'DBLYATEM_PR98_CURRENT_HEAD_PHYSICAL_ANDROID_ORCHESTRATION_V1'
    printf 'HARNESS_HEAD=%s\n' "$harness_head"
    printf 'FEATURE_HEAD=%s\n' "$expected_feature_head"
    printf 'DEVICE_SERIAL_SHA256=%s\n' "$serial_hash"
    printf '%s\n' 'KIT_SHA256_VERIFY=PASS'
    printf '%s\n' 'APK_EXACT_HEAD_BINDING=PASS'
    printf '%s\n' 'PHYSICAL_EVIDENCE_SHA256_VERIFY=PASS'
    printf '%s\n' 'INSTRUMENTATION_RESULT=PASS'
    printf '%s\n' 'REPORT_RETRIEVAL=PASS'
    printf '%s\n' 'ANDROID_RUNTIME=PASS'
} > "$receipt"

(
    cd "$output_root"
    sha256sum \
        receipt.txt \
        pr100-android-kit/SHA256SUMS \
        pr100-physical-android-evidence/SHA256SUMS \
        pr100-physical-android-evidence/accepted.txt \
        pr100-physical-android-evidence/dblyatem-pr98-android-evidence.txt \
        > SHA256SUMS
)

cat "$receipt"
printf 'OUTPUT_ROOT=%s\n' "$output_root"
printf '%s\n' \
    'DBLYATEM PR100 CURRENT-HEAD PHYSICAL ANDROID ORCHESTRATION: PASS'
