#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
capture="$root/tools/capture-android-device-manifest.sh"
scratch=$(mktemp -d "${TMPDIR:-/tmp}/dblyatem-android-device-verify.XXXXXX")
trap 'rm -rf "$scratch"' 0 HUP INT TERM

fail()
{
    printf 'ANDROID DEVICE MANIFEST VERIFY: FAIL: %s\n' "$1" >&2
    exit 1
}

exact_line()
{
    file=$1
    expected=$2
    count=$(grep -Fxc "$expected" "$file" || true)
    [ "$count" -eq 1 ] ||
        fail "exact line mismatch file=$file line=[$expected] count=$count"
}

expect_failure()
{
    mode=$1
    output=$2
    shift 2
    rm -rf "$output"
    if FIXTURE_MODE="$mode" DBLYATEM_ADB="$fake_adb" \
        sh "$capture" "$output" "$@" >"$scratch/$mode.out" 2>"$scratch/$mode.err"
    then
        fail "fixture unexpectedly passed: $mode"
    fi
    [ ! -e "$output" ] || fail "failed fixture published output: $mode"
}

for command_name in awk cmp find grep mktemp sed sha256sum sort tr wc; do
    command -v "$command_name" >/dev/null 2>&1 ||
        fail "missing command: $command_name"
done

[ -f "$capture" ] || fail 'missing capture adapter'
sh -n "$capture"

if grep -E '\$adb.*[[:space:]](install|push|pull|root|remount|reboot)([[:space:]]|$)' \
    "$capture" >/dev/null
then
    fail 'forbidden mutating ADB command found'
fi

fake_adb="$scratch/adb-fixture"
cat > "$fake_adb" <<'FAKE'
#!/bin/sh
set -eu
mode=${FIXTURE_MODE:-valid}
serial='PHYSICAL-SERIAL-1234'

if [ "$#" -eq 1 ] && [ "$1" = devices ]; then
    printf 'List of devices attached\n'
    case "$mode" in
        zero) ;;
        multiple)
            printf '%s\tdevice\n' "$serial"
            printf 'SECOND-SERIAL\tdevice\n'
            ;;
        unauthorized) printf '%s\tunauthorized\n' "$serial" ;;
        emulator) printf 'emulator-5554\tdevice\n' ;;
        *) printf '%s\tdevice\n' "$serial" ;;
    esac
    exit 0
fi

[ "$1" = -s ]
selected=$2
shift 2
[ "$1" = shell ]
shift

if [ "$mode" = emulator ]; then
    selected='emulator-5554'
fi

case "$1" in
    getprop)
        if [ "$#" -eq 1 ]; then
            cat <<PROPS
[ro.boot.serialno]: [$selected]
[ro.build.fingerprint]: [Deadbyte/test_device/test:14/UP1A.240101.001/1234567:user/release-keys]
[ro.build.id]: [UP1A.240101.001]
[ro.build.version.release]: [14]
[ro.build.version.sdk]: [34]
[ro.build.version.security_patch]: [2026-07-05]
[ro.kernel.qemu]: [0]
[ro.product.cpu.abi]: [arm64-v8a]
[ro.product.device]: [test_device]
[ro.product.manufacturer]: [Deadbyte Labs]
[ro.product.model]: [DB Phone 1]
[ro.serialno]: [$selected]
PROPS
            exit 0
        fi
        property=$2
        if [ "$mode" = missing ] && [ "$property" = ro.product.model ]; then
            exit 0
        fi
        case "$property" in
            ro.kernel.qemu)
                [ "$mode" = qemu ] && printf '1\n' || printf '0\n'
                ;;
            ro.product.manufacturer) printf 'Deadbyte Labs\n' ;;
            ro.product.model) printf 'DB Phone 1\n' ;;
            ro.product.device) printf 'test_device\n' ;;
            ro.build.version.release) printf '14\n' ;;
            ro.build.version.sdk) printf '34\n' ;;
            ro.build.id) printf 'UP1A.240101.001\n' ;;
            ro.build.fingerprint)
                printf 'Deadbyte/test_device/test:14/UP1A.240101.001/1234567:user/release-keys\n'
                ;;
            ro.build.version.security_patch) printf '2026-07-05\n' ;;
            ro.product.cpu.abi)
                [ "$mode" = x86 ] && printf 'x86_64\n' || printf 'arm64-v8a\n'
                ;;
            *) exit 1 ;;
        esac
        ;;
    uname)
        [ "$2" = -r ]
        printf '6.1.99-android14-11-gdeadbyte+\n'
        ;;
    getconf)
        [ "$2" = PAGESIZE ]
        [ "$mode" = page8192 ] && printf '8192\n' || printf '4096\n'
        ;;
    dumpsys)
        case "$2" in
            thermalservice)
                printf 'Thermal Status: 0\nSkin: 31.5\nSerial marker: %s\n' "$selected"
                ;;
            battery)
                printf 'level: 88\ntemperature: 310\nserial=%s\n' "$selected"
                ;;
            SurfaceFlinger)
                printf 'GLES: Deadbyte, TestGPU, OpenGL ES 3.2 DB-1\nserial=%s\n' "$selected"
                ;;
            *) exit 1 ;;
        esac
        ;;
    *) exit 1 ;;
esac
FAKE
chmod +x "$fake_adb"

first="$scratch/package-one"
second="$scratch/package-two"
FIXTURE_MODE=valid DBLYATEM_ADB="$fake_adb" \
    sh "$capture" "$first" fixed_performance cool_30c > "$scratch/valid-one.out"
FIXTURE_MODE=valid DBLYATEM_ADB="$fake_adb" \
    sh "$capture" "$second" fixed_performance cool_30c > "$scratch/valid-two.out"

for file in \
    android-device-manifest-v1.txt \
    battery.txt \
    getprop.txt \
    surfaceflinger-gles.txt \
    thermalservice.txt \
    SHA256SUMS
do
    cmp -s "$first/$file" "$second/$file" ||
        fail "deterministic replay mismatch: $file"
done

file_count=$(find "$first" -type f | wc -l | tr -d ' ')
[ "$file_count" = 6 ] || fail "unexpected package file count: $file_count"

(
    cd "$first"
    sha256sum -c SHA256SUMS >/dev/null
)

manifest="$first/android-device-manifest-v1.txt"
manifest_lines=$(wc -l < "$manifest" | tr -d ' ')
[ "$manifest_lines" = 26 ] ||
    fail "unexpected manifest line count: $manifest_lines"

serial_hash=$(printf '%s' 'PHYSICAL-SERIAL-1234' | sha256sum | awk '{print $1}')
fingerprint_hash=$(
    printf '%s' \
        'Deadbyte/test_device/test:14/UP1A.240101.001/1234567:user/release-keys' |
        sha256sum | awk '{print $1}'
)
getprop_hash=$(sha256sum "$first/getprop.txt" | awk '{print $1}')
thermal_hash=$(sha256sum "$first/thermalservice.txt" | awk '{print $1}')
battery_hash=$(sha256sum "$first/battery.txt" | awk '{print $1}')
gles_hash=$(sha256sum "$first/surfaceflinger-gles.txt" | awk '{print $1}')

exact_line "$manifest" "DEVICE_SERIAL_SHA256=$serial_hash"
exact_line "$manifest" "BUILD_FINGERPRINT_SHA256=$fingerprint_hash"
exact_line "$manifest" "GETPROP_SHA256=$getprop_hash"
exact_line "$manifest" "THERMALSERVICE_SHA256=$thermal_hash"
exact_line "$manifest" "BATTERY_SHA256=$battery_hash"
exact_line "$manifest" "SURFACEFLINGER_GLES_SHA256=$gles_hash"
exact_line "$manifest" 'DBLYATEM_ANDROID_DEVICE_MANIFEST_V1'
exact_line "$manifest" 'FORMAT_VERSION=1'
exact_line "$manifest" 'CAPTURE_TRANSPORT=ADB_READ_ONLY'
exact_line "$manifest" 'MANUFACTURER=Deadbyte_Labs'
exact_line "$manifest" 'MODEL=DB_Phone_1'
exact_line "$manifest" 'DEVICE_CODENAME=test_device'
exact_line "$manifest" 'ANDROID_RELEASE=14'
exact_line "$manifest" 'ANDROID_SDK=34'
exact_line "$manifest" 'BUILD_ID=UP1A.240101.001'
exact_line "$manifest" 'SECURITY_PATCH=2026-07-05'
exact_line "$manifest" 'PRIMARY_ABI=arm64-v8a'
exact_line "$manifest" 'KERNEL_RELEASE=6.1.99-android14-11-gdeadbyte'
exact_line "$manifest" 'PAGE_SIZE=4096'
exact_line "$manifest" 'POWER_MODE=fixed_performance'
exact_line "$manifest" 'THERMAL_START=cool_30c'
exact_line "$manifest" 'RAW_SERIAL_PERSISTED=NO'
exact_line "$manifest" 'DEVICE_MUTATION=NONE'
exact_line "$manifest" 'DBLYATEM_EXECUTION=NOT_RUN'
exact_line "$manifest" 'WINLATOR_EXECUTION=NOT_RUN'
exact_line "$manifest" 'WINLATOR_COMPARISON=NOT_RUN'

if grep -R -F 'PHYSICAL-SERIAL-1234' "$first" >/dev/null; then
    fail 'raw serial leaked into published package'
fi
redacted_count=$(grep -R -F '[REDACTED_SERIAL]' "$first" | wc -l | tr -d ' ')
[ "$redacted_count" -ge 4 ] || fail 'serial redaction markers missing'

expect_failure zero "$scratch/fail-zero" fixed_performance cool_30c
expect_failure multiple "$scratch/fail-multiple" fixed_performance cool_30c
expect_failure unauthorized "$scratch/fail-unauthorized" fixed_performance cool_30c
expect_failure emulator "$scratch/fail-emulator" fixed_performance cool_30c
expect_failure qemu "$scratch/fail-qemu" fixed_performance cool_30c
expect_failure x86 "$scratch/fail-x86" fixed_performance cool_30c
expect_failure page8192 "$scratch/fail-page8192" fixed_performance cool_30c
expect_failure missing "$scratch/fail-missing" fixed_performance cool_30c
expect_failure valid "$scratch/fail-token" 'bad token' cool_30c
expect_failure valid "$scratch/fail-empty-token" '' cool_30c

existing="$scratch/existing"
mkdir "$existing"
if FIXTURE_MODE=valid DBLYATEM_ADB="$fake_adb" \
    sh "$capture" "$existing" fixed_performance cool_30c \
    > "$scratch/existing.out" 2> "$scratch/existing.err"
then
    fail 'pre-existing output unexpectedly accepted'
fi
[ -d "$existing" ] || fail 'pre-existing output was modified or removed'
[ "$(find "$existing" -mindepth 1 | wc -l | tr -d ' ')" = 0 ] ||
    fail 'pre-existing output contents were modified'

printf '%s\n' 'ANDROID DEVICE MANIFEST FIXTURE TEST: PASS'
printf '%s\n' 'ANDROID DEVICE MANIFEST REJECTION MATRIX: PASS'
printf '%s\n' 'ANDROID RAW SERIAL NON-PERSISTENCE: PASS'
printf '%s\n' 'ANDROID READ-ONLY ADB BOUNDARY: PASS'
printf '%s\n' 'ANDROID DEVICE MANIFEST VERIFY: PASS'
printf '%s\n' 'DBLYATEM EXECUTION: NOT RUN'
printf '%s\n' 'WINLATOR EXECUTION: NOT RUN'
printf '%s\n' 'WINLATOR COMPARISON: NOT RUN'
