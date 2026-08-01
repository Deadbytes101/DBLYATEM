#!/bin/sh
set -eu

repo_url='https://github.com/Deadbytes101/DBLYATEM.git'
expected_feature='8886fe231eaaefd9c28ed6fe9a7dc404739cc566'
expected_challenge='0f9d66bc95e62d3c5fa1ae695ed9917605db48c31e24427667c9dd8d08391328'
challenge=${DBLYATEM_EVIDENCE_CHALLENGE:-}
default_output="$PWD/pr98-native-arm64-linux-evidence"
output=${1:-"$default_output"}

[ "$challenge" = "$expected_challenge" ] || {
    printf '%s\n' \
        'PR98 NATIVE ARM64 LINUX EVIDENCE: FAIL: evidence challenge mismatch' >&2
    exit 1
}

[ -n "$output" ] || {
    printf '%s\n' 'PR98 NATIVE ARM64 LINUX EVIDENCE: FAIL: empty output path' >&2
    exit 1
}

case "$output" in
    /|.|..)
        printf 'PR98 NATIVE ARM64 LINUX EVIDENCE: FAIL: unsafe output path: %s\n' \
            "$output" >&2
        exit 1
        ;;
esac

output_name_guard=$(basename "$output")
[ "$output_name_guard" = pr98-native-arm64-linux-evidence ] || {
    printf 'PR98 NATIVE ARM64 LINUX EVIDENCE: FAIL: output basename must be %s\n' \
        'pr98-native-arm64-linux-evidence' >&2
    exit 1
}

[ ! -L "$output" ] || {
    printf '%s\n' \
        'PR98 NATIVE ARM64 LINUX EVIDENCE: FAIL: output path is a symlink' >&2
    exit 1
}

fail()
{
    printf 'PR98 NATIVE ARM64 LINUX EVIDENCE: FAIL: %s\n' "$*" >&2
    exit 1
}

require_command()
{
    command -v "$1" >/dev/null 2>&1 ||
        fail "missing command: $1"
}

require_count()
{
    file=$1
    line=$2
    expected_count=$3
    code=$4

    actual_count=$(grep -Fxc "$line" "$file" || true)
    if [ "$actual_count" -ne "$expected_count" ]; then
        printf 'LINE=%s\n' "$line" >&2
        printf 'EXPECTED_COUNT=%s\n' "$expected_count" >&2
        printf 'ACTUAL_COUNT=%s\n' "$actual_count" >&2
        exit "$code"
    fi
}

for command_name in \
    git uname getconf grep sed awk sha256sum tar \
    gcc clang lscpu file readlink mktemp
do
    require_command "$command_name"
done

kernel_name=$(uname -s)
machine=$(uname -m)
operating_system=$(uname -o 2>/dev/null || printf '%s' unknown)
page_size=$(getconf PAGESIZE)
shell_binary=$(readlink -f /bin/sh)
shell_file=$(file -b "$shell_binary")
virtualization=unknown

[ "$kernel_name" = Linux ] ||
    fail "kernel is not Linux: $kernel_name"

case "$machine" in
    aarch64|arm64) ;;
    *) fail "machine is not ARM64: $machine" ;;
esac

[ "$operating_system" != Android ] ||
    fail 'Android kernel/userspace class rejected'

[ ! -x /system/bin/getprop ] ||
    fail 'Android getprop detected'

[ "$page_size" = 4096 ] ||
    fail "page size must be 4096: actual=$page_size"

printf '%s\n' "$shell_file" |
    grep -Eiq '(ARM aarch64|ARM64|aarch64)' ||
    fail "host /bin/sh is not an AArch64 ELF: $shell_file"

if grep -Eiq '(QEMU Virtual CPU|TCG|Bochs)' /proc/cpuinfo; then
    fail 'software-emulated CPU marker detected'
fi

if command -v systemd-detect-virt >/dev/null 2>&1; then
    virtualization=$(
        systemd-detect-virt 2>/dev/null ||
        printf '%s' none
    )
fi

case "$virtualization" in
    qemu|bochs|uml)
        fail "software-emulated virtualization rejected: $virtualization"
        ;;
esac

rm -rf "$output"
mkdir -p "$output"

temp_root=$(mktemp -d)
cleanup()
{
    rm -rf "$temp_root"
}
trap cleanup EXIT HUP INT TERM

source_dir="$temp_root/feature-src"

git init -q "$source_dir"
git -C "$source_dir" remote add origin "$repo_url"
git -C "$source_dir" fetch -q --depth=1 origin "$expected_feature"
git -C "$source_dir" checkout -q --detach FETCH_HEAD

actual_feature=$(git -C "$source_dir" rev-parse HEAD)
dirty_before=$(git -C "$source_dir" status --porcelain --untracked-files=no)

[ "$actual_feature" = "$expected_feature" ] ||
    fail "wrong feature head: expected=$expected_feature actual=$actual_feature"

[ -z "$dirty_before" ] ||
    fail 'fresh exact-head checkout is dirty before execution'

cpu_model=$(
    lscpu |
        sed -n \
            -e 's/^Model name:[[:space:]]*//p' \
            -e 's/^Model:[[:space:]]*//p' |
        sed -n '1p'
)
[ -n "$cpu_model" ] || cpu_model=unknown

host="$output/host.txt"
log="$output/step18.log"
accepted="$output/accepted.txt"
checksums="$output/SHA256SUMS"

{
    printf '%s\n' 'DBLYATEM_PR98_NATIVE_ARM64_LINUX_HOST_V1'
    printf 'COMMIT_SHA=%s\n' "$actual_feature"
    printf 'EVIDENCE_CHALLENGE=%s\n' "$challenge"
    printf 'KERNEL_NAME=%s\n' "$kernel_name"
    printf 'MACHINE=%s\n' "$machine"
    printf '%s\n' 'HOST_ARCH=AARCH64'
    printf 'OPERATING_SYSTEM=%s\n' "$operating_system"
    printf 'RUNTIME_PAGE_SIZE=%s\n' "$page_size"
    printf 'SHELL_ELF=%s\n' "$shell_file"
    printf 'VIRTUALIZATION=%s\n' "$virtualization"
    printf 'CPU_MODEL=%s\n' "$cpu_model"
    printf 'GCC_VERSION=%s\n' "$(gcc --version | sed -n '1p')"
    printf 'CLANG_VERSION=%s\n' "$(clang --version | sed -n '1p')"
    printf '%s\n' 'ANDROID=NO'
    printf '%s\n' 'QEMU_TCG=NO'
    printf '%s\n' 'FRESH_CHECKOUT_DIRTY=NO'
} > "$host"

lscpu > "$output/lscpu.txt"
cat /proc/cpuinfo > "$output/cpuinfo.txt"

set +e
/bin/sh "$source_dir/tools/verify-linux-arm64-wx-provider.sh" \
    > "$log" 2>&1
step18_status=$?
set -e

cat "$log"

if [ "$step18_status" -ne 0 ]; then
    printf 'ARM64 LINUX STEP18 EXIT=%s\n' "$step18_status" >&2
    exit "$step18_status"
fi

require_count \
    "$log" \
    'LINUX ARM64 W^X MOCK TEST: PASS' \
    3 \
    101

require_count \
    "$log" \
    'LINUX W^X PERMISSION OBSERVATION: PASS' \
    2 \
    102

require_count \
    "$log" \
    'NATIVE AARCH64 EXECUTION: PASS' \
    1 \
    103

require_count \
    "$log" \
    'ANDROID ARM64 RUNTIME EVIDENCE: NOT CLAIMED' \
    1 \
    104

require_count \
    "$log" \
    'NO RWX TRANSITION VERIFY: PASS' \
    1 \
    105

require_count \
    "$log" \
    'LINUX ANDROID ARM64 W^X PROVIDER VERIFY: PASS' \
    1 \
    106

if grep -Fx \
    'NATIVE AARCH64 EXECUTION: NOT CLAIMED' \
    "$log" >/dev/null
then
    fail 'ARM64 execution was falsely left unclaimed'
fi

if grep -Fx \
    'ANDROID ARM64 RUNTIME EVIDENCE: PASS' \
    "$log" >/dev/null
then
    fail 'Linux host falsely claimed Android evidence'
fi

if grep -Eiq '(^|[^A-Z])(FAIL|FAILED|NOT_AARCH64)([^A-Z]|$)' "$log"; then
    fail 'failure marker found in step18 log'
fi

dirty_after=$(git -C "$source_dir" status --porcelain --untracked-files=no)
[ -z "$dirty_after" ] ||
    fail 'exact-head checkout became dirty during verification'

{
    printf '%s\n' 'DBLYATEM_PR98_NATIVE_ARM64_LINUX_EVIDENCE_ACCEPTED'
    printf 'HEAD=%s\n' "$actual_feature"
    printf 'EVIDENCE_CHALLENGE=%s\n' "$challenge"
    printf 'MACHINE=%s\n' "$machine"
    printf '%s\n' 'HOST_ARCH=AARCH64'
    printf 'RUNTIME_PAGE_SIZE=%s\n' "$page_size"
    printf 'VIRTUALIZATION=%s\n' "$virtualization"
    printf '%s\n' 'ANDROID=NO'
    printf '%s\n' 'QEMU_TCG=NO'
    printf '%s\n' 'FRESH_CHECKOUT_DIRTY=NO'
    printf '%s\n' 'LINUX_ARM64_WX_MOCK_TEST_COUNT=3'
    printf '%s\n' 'LINUX_WX_PERMISSION_OBSERVATION_COUNT=2'
    printf '%s\n' 'NATIVE_AARCH64_EXECUTION=PASS'
    printf '%s\n' 'ANDROID_ARM64_RUNTIME_EVIDENCE=NOT_CLAIMED'
    printf '%s\n' 'NO_RWX_TRANSITION_VERIFY=PASS'
    printf '%s\n' 'LINUX_ANDROID_ARM64_WX_PROVIDER_VERIFY=PASS'
    printf '%s\n' 'NATIVE_LINUX_ARM64_EVIDENCE=PASS'
} > "$accepted"

(
    cd "$output"
    sha256sum \
        host.txt \
        step18.log \
        lscpu.txt \
        cpuinfo.txt \
        accepted.txt \
        > SHA256SUMS
)

output_parent=$(dirname "$output")
output_name=$(basename "$output")
archive="${output}.tar.gz"
archive_checksum="${archive}.sha256"
archive_name=$(basename "$archive")

rm -f "$archive" "$archive_checksum"

tar -C "$output_parent" \
    -czf "$archive" \
    "$output_name"

(
    cd "$output_parent"
    sha256sum "$archive_name" > "$(basename "$archive_checksum")"
)

printf '\n%s\n' \
    'DBLYATEM PR98 NATIVE ARM64 LINUX EVIDENCE ACCEPTED'
printf 'HEAD=%s\n' "$actual_feature"
printf 'EVIDENCE_CHALLENGE=%s\n' "$challenge"
printf 'MACHINE=%s\n' "$machine"
printf 'PAGE_SIZE=%s\n' "$page_size"
printf 'VIRTUALIZATION=%s\n' "$virtualization"
printf 'EVIDENCE=%s\n' "$output"
printf 'SHA256SUMS=%s\n' "$checksums"
printf 'ARCHIVE=%s\n' "$archive"
printf 'ARCHIVE_SHA256=%s\n' "$archive_checksum"
