#!/bin/sh
set -eu

expected_feature='8886fe231eaaefd9c28ed6fe9a7dc404739cc566'
expected_challenge='0f9d66bc95e62d3c5fa1ae695ed9917605db48c31e24427667c9dd8d08391328'
input=${1:-}

fail()
{
    printf 'PR98 NATIVE ARM64 PACKAGE VERIFY: FAIL: %s\n' "$*" >&2
    exit 1
}

require_count()
{
    file=$1
    line=$2
    expected_count=$3

    actual_count=$(grep -Fxc "$line" "$file" || true)
    [ "$actual_count" -eq "$expected_count" ] ||
        fail "line count mismatch: line=$line expected=$expected_count actual=$actual_count"
}

[ -n "$input" ] ||
    fail 'usage: verify-evidence-package.sh <directory-or-tar.gz>'

for command_name in grep sha256sum tar mktemp find awk wc; do
    command -v "$command_name" >/dev/null 2>&1 ||
        fail "missing command: $command_name"
done

temp_root=
cleanup()
{
    if [ -n "$temp_root" ]; then
        rm -rf "$temp_root"
    fi
}
trap cleanup EXIT HUP INT TERM

if [ -d "$input" ]; then
    evidence=$input
elif [ -f "$input" ]; then
    case "$input" in
        *.tar.gz|*.tgz) ;;
        *) fail "unsupported package type: $input" ;;
    esac

    archive_entries=$(tar -tzf "$input") ||
        fail 'archive table-of-contents read failed'

    printf '%s\n' "$archive_entries" |
        awk '
            /^\// { exit 1 }
            /(^|\/)\.\.($|\/)/ { exit 1 }
            { next }
        ' ||
        fail 'archive contains absolute or parent-traversal path'

    tar -tvzf "$input" |
        awk '
            $1 ~ /^[lh]/ { exit 1 }
            { next }
        ' ||
        fail 'archive contains a symbolic or hard link'

    temp_root=$(mktemp -d)
    tar --no-same-owner --no-same-permissions \
        -xzf "$input" \
        -C "$temp_root"

    accepted_paths=$(
        find "$temp_root" -type f -name accepted.txt -print
    )
    accepted_count=$(
        printf '%s\n' "$accepted_paths" |
            grep -c . || true
    )

    [ "$accepted_count" -eq 1 ] ||
        fail "archive must contain exactly one accepted.txt: actual=$accepted_count"

    accepted_path=$accepted_paths
    evidence=$(dirname "$accepted_path")
else
    fail "input not found: $input"
fi

for required_file in \
    host.txt step18.log lscpu.txt cpuinfo.txt accepted.txt SHA256SUMS
do
    [ -f "$evidence/$required_file" ] ||
        fail "required file missing: $required_file"
done

checksum_line_count=$(
    wc -l < "$evidence/SHA256SUMS" |
        awk '{ print $1 }'
)
[ "$checksum_line_count" -eq 5 ] ||
    fail "SHA256SUMS must contain exactly five lines: actual=$checksum_line_count"

for checksum_name in \
    host.txt step18.log lscpu.txt cpuinfo.txt accepted.txt
do
    checksum_count=$(
        grep -Ec \
            "^[0-9a-fA-F]{64}  ${checksum_name}$" \
            "$evidence/SHA256SUMS" ||
        true
    )
    [ "$checksum_count" -eq 1 ] ||
        fail "SHA256SUMS entry contract failed: file=$checksum_name actual=$checksum_count"
done

(
    cd "$evidence"
    sha256sum -c SHA256SUMS
)

host="$evidence/host.txt"
log="$evidence/step18.log"
accepted="$evidence/accepted.txt"

for line in \
    'DBLYATEM_PR98_NATIVE_ARM64_LINUX_HOST_V1' \
    "COMMIT_SHA=$expected_feature" \
    "EVIDENCE_CHALLENGE=$expected_challenge" \
    'HOST_ARCH=AARCH64' \
    'RUNTIME_PAGE_SIZE=4096' \
    'ANDROID=NO' \
    'QEMU_TCG=NO' \
    'FRESH_CHECKOUT_DIRTY=NO'
do
    require_count "$host" "$line" 1
done

machine_count=$(
    grep -Ec '^MACHINE=(aarch64|arm64)$' "$host" || true
)
[ "$machine_count" -eq 1 ] ||
    fail "host machine contract failed: actual=$machine_count"

for line in \
    'DBLYATEM_PR98_NATIVE_ARM64_LINUX_EVIDENCE_ACCEPTED' \
    "HEAD=$expected_feature" \
    "EVIDENCE_CHALLENGE=$expected_challenge" \
    'HOST_ARCH=AARCH64' \
    'RUNTIME_PAGE_SIZE=4096' \
    'ANDROID=NO' \
    'QEMU_TCG=NO' \
    'FRESH_CHECKOUT_DIRTY=NO' \
    'LINUX_ARM64_WX_MOCK_TEST_COUNT=3' \
    'LINUX_WX_PERMISSION_OBSERVATION_COUNT=2' \
    'NATIVE_AARCH64_EXECUTION=PASS' \
    'ANDROID_ARM64_RUNTIME_EVIDENCE=NOT_CLAIMED' \
    'NO_RWX_TRANSITION_VERIFY=PASS' \
    'LINUX_ANDROID_ARM64_WX_PROVIDER_VERIFY=PASS' \
    'NATIVE_LINUX_ARM64_EVIDENCE=PASS'
do
    require_count "$accepted" "$line" 1
done

require_count \
    "$log" \
    'LINUX ARM64 W^X MOCK TEST: PASS' \
    3

require_count \
    "$log" \
    'LINUX W^X PERMISSION OBSERVATION: PASS' \
    2

require_count \
    "$log" \
    'NATIVE AARCH64 EXECUTION: PASS' \
    1

require_count \
    "$log" \
    'ANDROID ARM64 RUNTIME EVIDENCE: NOT CLAIMED' \
    1

require_count \
    "$log" \
    'NO RWX TRANSITION VERIFY: PASS' \
    1

require_count \
    "$log" \
    'LINUX ANDROID ARM64 W^X PROVIDER VERIFY: PASS' \
    1

if grep -Fx \
    'NATIVE AARCH64 EXECUTION: NOT CLAIMED' \
    "$log" >/dev/null
then
    fail 'package leaves native AArch64 execution unclaimed'
fi

if grep -Fx \
    'ANDROID ARM64 RUNTIME EVIDENCE: PASS' \
    "$log" >/dev/null
then
    fail 'package falsely claims Android evidence'
fi

if grep -Eiq '(^|[^A-Z])(FAIL|FAILED|NOT_AARCH64)([^A-Z]|$)' "$log"; then
    fail 'failure marker found in package log'
fi

printf '%s\n' \
    'DBLYATEM PR98 NATIVE ARM64 LINUX PACKAGE VERIFY: PASS'
printf 'HEAD=%s\n' "$expected_feature"
printf 'EVIDENCE_CHALLENGE=%s\n' "$expected_challenge"
printf 'EVIDENCE=%s\n' "$evidence"
