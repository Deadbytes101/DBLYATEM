#!/bin/sh
set -eu

expected_head='8886fe231eaaefd9c28ed6fe9a7dc404739cc566'
package='com.deadbyte.dblyatem.pr98evidence'
test_package='com.deadbyte.dblyatem.pr98evidence.test'
runner='androidx.test.runner.AndroidJUnitRunner'
test_class='com.deadbyte.dblyatem.pr98evidence.WxProviderInstrumentationTest'
remote_evidence="/sdcard/Android/data/${package}/files/dblyatem-pr98-android-evidence.txt"

app_apk=${1:-}
test_apk=${2:-}
evidence_dir=${3:-./pr98-physical-android-evidence}

if [ -z "$app_apk" ] || [ -z "$test_apk" ]; then
    printf '%s\n' \
        'USAGE: run-physical-device.sh APP_APK TEST_APK [EVIDENCE_DIR]' >&2
    exit 64
fi

for command_name in \
    adb grep sed sha256sum strings unzip wc tr sort xargs
 do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'MISSING COMMAND: %s\n' "$command_name" >&2
        exit 65
    }
 done

[ -f "$app_apk" ] || {
    printf 'APP APK NOT FOUND: %s\n' "$app_apk" >&2
    exit 66
}

[ -f "$test_apk" ] || {
    printf 'TEST APK NOT FOUND: %s\n' "$test_apk" >&2
    exit 67
}

rm -rf "$evidence_dir"
mkdir -p "$evidence_dir"

app_entries="$evidence_dir/app-apk-entries.txt"
test_entries="$evidence_dir/test-apk-entries.txt"
instrumentation_log="$evidence_dir/instrumentation.txt"
device_log="$evidence_dir/device.txt"
local_evidence="$evidence_dir/dblyatem-pr98-android-evidence.txt"
logcat_file="$evidence_dir/logcat.txt"

unzip -l "$app_apk" > "$app_entries"
unzip -l "$test_apk" > "$test_entries"

grep -F 'lib/arm64-v8a/libpr98evidence.so' \
    "$app_entries" >/dev/null || {
        printf '%s\n' 'APP APK LACKS ARM64 EVIDENCE LIBRARY' >&2
        exit 68
    }

if grep -E 'lib/(x86|x86_64|armeabi-v7a)/' \
        "$app_entries" >/dev/null
then
    printf '%s\n' 'APP APK CONTAINS FORBIDDEN NON-ARM64 ABI' >&2
    exit 69
fi

unzip -p "$app_apk" \
    lib/arm64-v8a/libpr98evidence.so |
    strings |
    grep -F "$expected_head" >/dev/null || {
        printf '%s\n' 'APK IS NOT BOUND TO EXACT PR98 HEAD' >&2
        exit 70
    }

if [ -n "${ADB_SERIAL:-}" ]; then
    serial=$ADB_SERIAL
else
    serial=$(adb devices |
        sed -n '2,$s/[[:space:]]device$//p' |
        sed '/^[[:space:]]*$/d')

    serial_count=$(printf '%s\n' "$serial" |
        sed '/^[[:space:]]*$/d' |
        wc -l |
        tr -d ' ')

    if [ "$serial_count" -ne 1 ]; then
        printf 'AUTHORIZED PHYSICAL DEVICE COUNT=%s\n' "$serial_count" >&2
        printf '%s\n' \
            'CONNECT EXACTLY ONE DEVICE OR SET ADB_SERIAL' >&2
        adb devices >&2
        exit 71
    fi
fi

adb_target()
{
    adb -s "$serial" "$@"
}

state=$(adb_target get-state 2>/dev/null || true)
[ "$state" = device ] || {
    printf 'ADB DEVICE STATE=%s\n' "$state" >&2
    exit 72
}

kernel_machine=$(adb_target shell uname -m |
    tr -d '\r')
abi=$(adb_target shell getprop ro.product.cpu.abi |
    tr -d '\r')
abi_list=$(adb_target shell getprop ro.product.cpu.abilist |
    tr -d '\r')
page_size=$(adb_target shell sh -c \
    'getconf PAGESIZE 2>/dev/null || getconf PAGE_SIZE 2>/dev/null || pagesize 2>/dev/null' |
    tr -d '\r' || true)
qemu=$(adb_target shell getprop ro.kernel.qemu |
    tr -d '\r')
boot_qemu=$(adb_target shell getprop ro.boot.qemu |
    tr -d '\r')
hardware=$(adb_target shell getprop ro.hardware |
    tr -d '\r')
manufacturer=$(adb_target shell getprop ro.product.manufacturer |
    tr -d '\r')
model=$(adb_target shell getprop ro.product.model |
    tr -d '\r')
android_release=$(adb_target shell getprop ro.build.version.release |
    tr -d '\r')
android_sdk=$(adb_target shell getprop ro.build.version.sdk |
    tr -d '\r')
fingerprint=$(adb_target shell getprop ro.build.fingerprint |
    tr -d '\r')

case "$kernel_machine" in
    aarch64|arm64) ;;
    *)
        printf 'DEVICE KERNEL IS NOT ARM64: %s\n' "$kernel_machine" >&2
        exit 73
        ;;
esac

case ",$abi_list," in
    *,arm64-v8a,*) ;;
    *)
        if [ "$abi" != arm64-v8a ]; then
            printf 'DEVICE ABI IS NOT ARM64: abi=%s list=%s\n' \
                "$abi" "$abi_list" >&2
            exit 74
        fi
        ;;
esac

[ "$page_size" = 4096 ] || {
    printf 'UNSUPPORTED KERNEL PAGE SIZE=%s; REQUIRED=4096\n' \
        "$page_size" >&2
    exit 75
}

if [ "$qemu" = 1 ] || [ "$boot_qemu" = 1 ]; then
    printf '%s\n' 'EMULATOR DETECTED THROUGH QEMU PROPERTY' >&2
    exit 76
fi

case "$hardware" in
    *ranchu*|*goldfish*|*cuttlefish*)
        printf 'EMULATOR HARDWARE DETECTED: %s\n' "$hardware" >&2
        exit 77
        ;;
esac

serial_hash=$(printf '%s' "$serial" |
    sha256sum |
    sed 's/[[:space:]].*$//')

{
    printf '%s\n' 'DBLYATEM_PR98_PHYSICAL_ANDROID_HOST_V1'
    printf 'COMMIT_SHA=%s\n' "$expected_head"
    printf 'DEVICE_SERIAL_SHA256=%s\n' "$serial_hash"
    printf 'UNAME_MACHINE=%s\n' "$kernel_machine"
    printf 'ANDROID_ABI=%s\n' "$abi"
    printf 'ANDROID_ABI_LIST=%s\n' "$abi_list"
    printf 'RUNTIME_PAGE_SIZE=%s\n' "$page_size"
    printf 'MANUFACTURER=%s\n' "$manufacturer"
    printf 'MODEL=%s\n' "$model"
    printf 'ANDROID_RELEASE=%s\n' "$android_release"
    printf 'ANDROID_SDK=%s\n' "$android_sdk"
    printf 'BUILD_FINGERPRINT=%s\n' "$fingerprint"
    printf 'HARDWARE=%s\n' "$hardware"
    printf '%s\n' 'DEVICE_CLASS=PHYSICAL'
    printf '%s\n' 'ADB_STATE=DEVICE'
} > "$device_log"

adb_target logcat -c
adb_target uninstall "$test_package" >/dev/null 2>&1 || true
adb_target uninstall "$package" >/dev/null 2>&1 || true

adb_target install -r -t "$app_apk"
adb_target install -r -t "$test_apk"
adb_target shell rm -f "$remote_evidence" || true

set +e
adb_target shell am instrument \
    -w \
    -r \
    -e class "$test_class" \
    "${test_package}/${runner}" \
    > "$instrumentation_log" 2>&1
instrumentation_status=$?
set -e

cat "$instrumentation_log"
adb_target logcat -d -v threadtime > "$logcat_file" || true

if [ "$instrumentation_status" -ne 0 ]; then
    printf 'ANDROID INSTRUMENTATION EXIT=%s\n' \
        "$instrumentation_status" >&2
    exit "$instrumentation_status"
fi

adb_target shell test -f "$remote_evidence" || {
    printf 'REMOTE EVIDENCE FILE MISSING: %s\n' "$remote_evidence" >&2
    exit 78
}

adb_target pull "$remote_evidence" "$local_evidence" >/dev/null

require_line()
{
    line=$1
    code=$2

    grep -Fx "$line" "$local_evidence" >/dev/null || {
        printf 'MISSING ANDROID EVIDENCE CONTRACT: %s\n' "$line" >&2
        cat "$local_evidence" >&2
        exit "$code"
    }
}

require_line "COMMIT_SHA=$expected_head" 80
require_line 'COMPILE_ARCH=AARCH64' 81
require_line "UNAME_MACHINE=$kernel_machine" 82
require_line 'RUNTIME_PAGE_SIZE=4096' 83
require_line 'PROVIDER_TEST_EXIT=0' 84
require_line 'POSIX_TEST_EXIT=0' 85
require_line 'EXECUTE_TEST_EXIT=0' 86
require_line 'NATIVE_AARCH64_EXECUTION=PASS' 87
require_line 'ANDROID_ARM64_RUNTIME_EVIDENCE=PASS' 88
require_line 'NO_RWX_TRANSITION_VERIFY=PASS' 89

if grep -F 'NOT CLAIMED' "$local_evidence" >/dev/null; then
    printf '%s\n' 'PHYSICAL DEVICE EVIDENCE CONTAINS NOT CLAIMED' >&2
    exit 90
fi

if grep -E '=(FAIL|NOT_AARCH64)$' "$local_evidence" >/dev/null; then
    printf '%s\n' 'PHYSICAL DEVICE EVIDENCE CONTAINS FAILURE' >&2
    cat "$local_evidence" >&2
    exit 91
fi

{
    printf '%s\n' 'DBLYATEM_PR98_PHYSICAL_ANDROID_EVIDENCE_ACCEPTED'
    printf 'HEAD=%s\n' "$expected_head"
    printf 'UNAME_MACHINE=%s\n' "$kernel_machine"
    printf 'ABI=%s\n' "$abi"
    printf 'PAGE_SIZE=%s\n' "$page_size"
    printf 'DEVICE_SERIAL_SHA256=%s\n' "$serial_hash"
} > "$evidence_dir/accepted.txt"

find "$evidence_dir" \
    -type f \
    ! -name SHA256SUMS \
    -print0 |
    sort -z |
    xargs -0 sha256sum > "$evidence_dir/SHA256SUMS"

cat "$device_log"
printf '\n'
cat "$local_evidence"
printf '\n'
cat "$evidence_dir/accepted.txt"
printf 'EVIDENCE_DIR=%s\n' "$evidence_dir"
