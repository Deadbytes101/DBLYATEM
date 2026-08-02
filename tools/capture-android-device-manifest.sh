#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

usage()
{
    printf '%s\n' \
        'usage: tools/capture-android-device-manifest.sh OUTPUT_DIR POWER_MODE THERMAL_START' >&2
    exit 2
}

fail()
{
    printf 'ANDROID DEVICE MANIFEST CAPTURE: FAIL: %s\n' "$1" >&2
    exit 1
}

token_valid()
{
    value=$1
    [ -n "$value" ] || return 1
    [ "${#value}" -le 128 ] || return 1
    case "$value" in
        *[!A-Za-z0-9._-]*) return 1 ;;
    esac
    return 0
}

canonical_token()
{
    printf '%s' "$1" |
        tr -c 'A-Za-z0-9._-' '_' |
        sed 's/__*/_/g; s/^_//; s/_$//'
}

redact_serial()
{
    awk -v needle="$1" '
        {
            line = $0
            while (needle != "" && (position = index(line, needle)) != 0) {
                line = substr(line, 1, position - 1) \
                       "[REDACTED_SERIAL]" \
                       substr(line, position + length(needle))
            }
            print line
        }
    '
}

capture_shell()
{
    destination=$1
    shift
    "$adb" -s "$serial" shell "$@" 2>/dev/null |
        tr -d '\r' |
        redact_serial "$serial" > "$destination"
    [ -s "$destination" ] || fail "empty snapshot: $(basename "$destination")"
}

get_property()
{
    property=$1
    value=$(
        "$adb" -s "$serial" shell getprop "$property" 2>/dev/null |
            tr -d '\r\n'
    )
    [ -n "$value" ] || fail "empty Android property: $property"
    printf '%s' "$value"
}

hash_file()
{
    sha256sum "$1" | awk '{print $1}'
}

hash_text()
{
    printf '%s' "$1" | sha256sum | awk '{print $1}'
}

[ "$#" -eq 3 ] || usage

output=$1
power_mode=$2
thermal_start=$3
adb=${DBLYATEM_ADB:-adb}

token_valid "$power_mode" || fail 'invalid POWER_MODE token'
token_valid "$thermal_start" || fail 'invalid THERMAL_START token'

command -v "$adb" >/dev/null 2>&1 || fail "ADB command not found: $adb"
command -v awk >/dev/null 2>&1 || fail 'awk not found'
command -v sha256sum >/dev/null 2>&1 || fail 'sha256sum not found'

[ ! -e "$output" ] || fail "output already exists: $output"
parent=$(dirname -- "$output")
name=$(basename -- "$output")
[ -d "$parent" ] || fail "output parent is not a directory: $parent"
[ -n "$name" ] && [ "$name" != '.' ] && [ "$name" != '..' ] ||
    fail 'invalid output directory name'

stage=$(mktemp -d "$parent/.dblyatem-android-device.XXXXXX")
published=0
cleanup()
{
    status=$?
    trap - 0 HUP INT TERM
    if [ "$published" -eq 0 ]; then
        rm -rf "$stage"
    fi
    exit "$status"
}
trap cleanup 0 HUP INT TERM

devices=$(
    "$adb" devices 2>/dev/null |
        tr -d '\r' |
        awk 'NR > 1 && NF > 0 { print $1 "\t" $2 }'
)
device_count=$(printf '%s\n' "$devices" | awk 'NF > 0 { count++ } END { print count + 0 }')
[ "$device_count" -eq 1 ] ||
    fail "exactly one connected ADB device required: observed=$device_count"

serial=$(printf '%s\n' "$devices" | awk 'NF > 0 { print $1 }')
state=$(printf '%s\n' "$devices" | awk 'NF > 0 { print $2 }')
[ "$state" = 'device' ] || fail "ADB device is not authorized: state=$state"
case "$serial" in
    emulator-*|localhost:*|127.0.0.1:*) fail 'emulator or loopback ADB target rejected' ;;
esac

qemu=$(
    "$adb" -s "$serial" shell getprop ro.kernel.qemu 2>/dev/null |
        tr -d '\r\n'
)
[ "$qemu" != '1' ] || fail 'Android emulator property rejected'

manufacturer_raw=$(get_property ro.product.manufacturer)
model_raw=$(get_property ro.product.model)
device_raw=$(get_property ro.product.device)
android_release_raw=$(get_property ro.build.version.release)
android_sdk=$(get_property ro.build.version.sdk)
build_id_raw=$(get_property ro.build.id)
build_fingerprint=$(get_property ro.build.fingerprint)
security_patch_raw=$(get_property ro.build.version.security_patch)
primary_abi=$(get_property ro.product.cpu.abi)

[ "$primary_abi" = 'arm64-v8a' ] || fail "primary ABI rejected: $primary_abi"
case "$android_sdk" in
    ''|*[!0-9]*) fail "Android SDK is not decimal: $android_sdk" ;;
esac

kernel_release_raw=$(
    "$adb" -s "$serial" shell uname -r 2>/dev/null |
        tr -d '\r\n'
)
[ -n "$kernel_release_raw" ] || fail 'empty kernel release'

page_size=$(
    "$adb" -s "$serial" shell getconf PAGESIZE 2>/dev/null |
        tr -d '\r\n'
)
[ "$page_size" = '4096' ] || fail "runtime page size rejected: $page_size"

manufacturer=$(canonical_token "$manufacturer_raw")
model=$(canonical_token "$model_raw")
device_codename=$(canonical_token "$device_raw")
android_release=$(canonical_token "$android_release_raw")
build_id=$(canonical_token "$build_id_raw")
security_patch=$(canonical_token "$security_patch_raw")
kernel_release=$(canonical_token "$kernel_release_raw")

for pair in \
    "MANUFACTURER:$manufacturer" \
    "MODEL:$model" \
    "DEVICE_CODENAME:$device_codename" \
    "ANDROID_RELEASE:$android_release" \
    "BUILD_ID:$build_id" \
    "SECURITY_PATCH:$security_patch" \
    "KERNEL_RELEASE:$kernel_release"
do
    field=${pair%%:*}
    value=${pair#*:}
    token_valid "$value" || fail "empty or invalid canonical field: $field"
done

getprop_file="$stage/getprop.txt"
thermal_file="$stage/thermalservice.txt"
battery_file="$stage/battery.txt"
gles_file="$stage/surfaceflinger-gles.txt"
manifest_file="$stage/android-device-manifest-v1.txt"
sums_file="$stage/SHA256SUMS"

capture_shell "$getprop_file" getprop
LC_ALL=C sort "$getprop_file" -o "$getprop_file"
capture_shell "$thermal_file" dumpsys thermalservice
capture_shell "$battery_file" dumpsys battery
"$adb" -s "$serial" shell dumpsys SurfaceFlinger 2>/dev/null |
    tr -d '\r' |
    grep -E 'GLES|OpenGL|EGL' |
    redact_serial "$serial" > "$gles_file"
[ -s "$gles_file" ] || fail 'empty SurfaceFlinger GLES snapshot'

serial_hash=$(hash_text "$serial")
fingerprint_hash=$(hash_text "$build_fingerprint")
getprop_hash=$(hash_file "$getprop_file")
thermal_hash=$(hash_file "$thermal_file")
battery_hash=$(hash_file "$battery_file")
gles_hash=$(hash_file "$gles_file")

{
    printf '%s\n' 'DBLYATEM_ANDROID_DEVICE_MANIFEST_V1'
    printf 'FORMAT_VERSION=1\n'
    printf 'CAPTURE_TRANSPORT=ADB_READ_ONLY\n'
    printf 'DEVICE_SERIAL_SHA256=%s\n' "$serial_hash"
    printf 'MANUFACTURER=%s\n' "$manufacturer"
    printf 'MODEL=%s\n' "$model"
    printf 'DEVICE_CODENAME=%s\n' "$device_codename"
    printf 'ANDROID_RELEASE=%s\n' "$android_release"
    printf 'ANDROID_SDK=%s\n' "$android_sdk"
    printf 'BUILD_ID=%s\n' "$build_id"
    printf 'BUILD_FINGERPRINT_SHA256=%s\n' "$fingerprint_hash"
    printf 'SECURITY_PATCH=%s\n' "$security_patch"
    printf 'PRIMARY_ABI=%s\n' "$primary_abi"
    printf 'KERNEL_RELEASE=%s\n' "$kernel_release"
    printf 'PAGE_SIZE=%s\n' "$page_size"
    printf 'POWER_MODE=%s\n' "$power_mode"
    printf 'THERMAL_START=%s\n' "$thermal_start"
    printf 'GETPROP_SHA256=%s\n' "$getprop_hash"
    printf 'THERMALSERVICE_SHA256=%s\n' "$thermal_hash"
    printf 'BATTERY_SHA256=%s\n' "$battery_hash"
    printf 'SURFACEFLINGER_GLES_SHA256=%s\n' "$gles_hash"
    printf 'RAW_SERIAL_PERSISTED=NO\n'
    printf 'DEVICE_MUTATION=NONE\n'
    printf 'DBLYATEM_EXECUTION=NOT_RUN\n'
    printf 'WINLATOR_EXECUTION=NOT_RUN\n'
    printf 'WINLATOR_COMPARISON=NOT_RUN\n'
} > "$manifest_file"

if grep -R -F "$serial" "$stage" >/dev/null 2>&1; then
    fail 'raw device serial persisted in staged evidence'
fi

(
    cd "$stage"
    for file in \
        android-device-manifest-v1.txt \
        battery.txt \
        getprop.txt \
        surfaceflinger-gles.txt \
        thermalservice.txt
    do
        sha256sum "$file"
    done > SHA256SUMS
)

mv -T "$stage" "$output"
published=1

printf '%s\n' 'ANDROID DEVICE MANIFEST CAPTURE: PASS'
printf 'OUTPUT=%s\n' "$output"
printf 'DEVICE_SERIAL_SHA256=%s\n' "$serial_hash"
printf 'BUILD_FINGERPRINT_SHA256=%s\n' "$fingerprint_hash"
printf 'PAGE_SIZE=%s\n' "$page_size"
printf 'DEVICE_MUTATION=NONE\n'
printf 'DBLYATEM_EXECUTION=NOT_RUN\n'
printf 'WINLATOR_EXECUTION=NOT_RUN\n'
printf 'WINLATOR_COMPARISON=NOT_RUN\n'
