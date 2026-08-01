#!/bin/sh
set -eu

expected_feature='5c219e8872895adcb7c3e9af17c32e9676420ea5'
expected_challenge='4cb3c29e27b641ca5a034412405ce1e957702f3669110c1d37fef329b9e22950'
expected_branch='deadbyte/pr98-arm64-evidence'
package_name='pr98-native-arm64-linux-evidence'
archive_name="${package_name}.tar.gz"
archive_checksum_name="${archive_name}.sha256"
input=${1:-}

fail()
{
    printf 'PR98 CIRCLECI ARM64 PACKAGE VERIFY: FAIL: %s\n' "$*" >&2
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

    actual_count=$(grep -Fxc "$line" "$file" || true)
    [ "$actual_count" -eq "$expected_count" ] ||
        fail "line count mismatch: file=$(basename "$file") line=$line expected=$expected_count actual=$actual_count"
}

require_regex_count()
{
    file=$1
    expression=$2
    expected_count=$3

    actual_count=$(grep -Ec "$expression" "$file" || true)
    [ "$actual_count" -eq "$expected_count" ] ||
        fail "regex count mismatch: file=$(basename "$file") expression=$expression expected=$expected_count actual=$actual_count"
}

verify_evidence_directory()
{
    evidence=$1

    [ -d "$evidence" ] ||
        fail "evidence directory missing: $evidence"
    [ ! -L "$evidence" ] ||
        fail 'evidence directory must not be a symbolic link'

    expected_files='SHA256SUMS accepted.txt cpuinfo.txt host.txt lscpu.txt step18.log'
    actual_files=$(
        find "$evidence" -mindepth 1 -maxdepth 1 -type f -printf '%f\n' |
            LC_ALL=C sort |
            tr '\n' ' ' |
            sed 's/[[:space:]]*$//'
    )

    [ "$actual_files" = "$expected_files" ] ||
        fail "evidence file set mismatch: actual=$actual_files"

    unexpected_entry=$(
        find "$evidence" -mindepth 1 -maxdepth 1 ! -type f -print |
            sed -n '1p'
    )
    [ -z "$unexpected_entry" ] ||
        fail "unexpected non-file evidence entry: $unexpected_entry"

    for required_file in \
        host.txt step18.log lscpu.txt cpuinfo.txt accepted.txt SHA256SUMS
    do
        [ -f "$evidence/$required_file" ] ||
            fail "required file missing: $required_file"
        [ ! -L "$evidence/$required_file" ] ||
            fail "required file is a symbolic link: $required_file"
    done

    [ -s "$evidence/lscpu.txt" ] ||
        fail 'lscpu.txt is empty'
    [ -s "$evidence/cpuinfo.txt" ] ||
        fail 'cpuinfo.txt is empty'

    checksum_line_count=$(
        wc -l < "$evidence/SHA256SUMS" |
            awk '{ print $1 }'
    )
    [ "$checksum_line_count" -eq 5 ] ||
        fail "inner SHA256SUMS must contain exactly five lines: actual=$checksum_line_count"

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
            fail "inner checksum entry contract failed: file=$checksum_name actual=$checksum_count"
    done

    (
        cd "$evidence"
        sha256sum -c SHA256SUMS
    )

    host="$evidence/host.txt"
    log="$evidence/step18.log"
    accepted="$evidence/accepted.txt"

    for line in \
        'DBLYATEM_PR98_CIRCLECI_ARM64_LINUX_V1' \
        "FEATURE_HEAD=$expected_feature" \
        "EVIDENCE_CHALLENGE=$expected_challenge" \
        'EXECUTION_PROVIDER=CIRCLECI' \
        "CIRCLE_BRANCH=$expected_branch" \
        'RUNTIME_PAGE_SIZE=4096' \
        'ANDROID=NO' \
        'FRESH_CHECKOUT_DIRTY=NO'
    do
        require_count "$host" "$line" 1
    done

    require_regex_count "$host" '^MACHINE=(aarch64|arm64)$' 1
    require_regex_count "$host" '^USERSPACE_FILE=.*(AArch64|aarch64).*$' 1
    require_regex_count "$host" '^CIRCLE_WORKFLOW_ID=[^[:space:]]+$' 1
    require_regex_count "$host" '^CIRCLE_BUILD_NUM=[0-9]+$' 1
    require_regex_count "$host" '^VIRTUALIZATION=[^[:space:]]+$' 1
    require_regex_count "$host" '^UNAME=.+$' 1
    require_regex_count "$host" '^GCC_VERSION=.+$' 1
    require_regex_count "$host" '^CLANG_VERSION=.+$' 1

    if grep -E '^VIRTUALIZATION=(qemu|bochs|uml)$' "$host" >/dev/null; then
        fail 'software-emulation virtualization marker found in host report'
    fi

    if grep -Fx 'CIRCLE_WORKFLOW_ID=unknown' "$host" >/dev/null; then
        fail 'CircleCI workflow ID is unknown'
    fi

    for line in \
        'DBLYATEM_PR98_NATIVE_ARM64_LINUX_EVIDENCE_ACCEPTED' \
        "HEAD=$expected_feature" \
        "EVIDENCE_CHALLENGE=$expected_challenge" \
        'EXECUTION_PROVIDER=CIRCLECI' \
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

    require_regex_count "$accepted" '^MACHINE=(aarch64|arm64)$' 1

    require_count "$log" 'LINUX ARM64 W^X MOCK TEST: PASS' 3
    require_count "$log" 'LINUX W^X PERMISSION OBSERVATION: PASS' 2
    require_count "$log" 'NATIVE AARCH64 EXECUTION: PASS' 1
    require_count "$log" 'ANDROID ARM64 RUNTIME EVIDENCE: NOT CLAIMED' 1
    require_count "$log" 'NO RWX TRANSITION VERIFY: PASS' 1
    require_count "$log" 'LINUX ANDROID ARM64 W^X PROVIDER VERIFY: PASS' 1

    if grep -Fx 'NATIVE AARCH64 EXECUTION: NOT CLAIMED' "$log" >/dev/null; then
        fail 'package leaves native AArch64 execution unclaimed'
    fi

    if grep -Fx 'ANDROID ARM64 RUNTIME EVIDENCE: PASS' "$log" >/dev/null; then
        fail 'package falsely claims Android runtime evidence'
    fi

    if grep -Eiq '(^|[^A-Z])(FAIL|FAILED|NOT_AARCH64)([^A-Z]|$)' "$log"; then
        fail 'failure marker found in step18 log'
    fi
}

[ -n "$input" ] ||
    fail 'usage: verify-circleci-evidence-package.sh <artifact-root-or-tar.gz>'

for command_name in \
    awk basename cmp dirname find grep mktemp rm sed sha256sum sort tar tr wc
 do
    require_command "$command_name"
done

if [ -d "$input" ]; then
    [ ! -L "$input" ] ||
        fail 'artifact root must not be a symbolic link'
    artifact_root=$(CDPATH= cd -- "$input" && pwd)
    archive="$artifact_root/$archive_name"
elif [ -f "$input" ]; then
    [ ! -L "$input" ] ||
        fail 'archive must not be a symbolic link'
    input_name=$(basename "$input")
    [ "$input_name" = "$archive_name" ] ||
        fail "archive basename mismatch: expected=$archive_name actual=$input_name"
    artifact_root=$(CDPATH= cd -- "$(dirname "$input")" && pwd)
    archive="$artifact_root/$archive_name"
else
    fail "input not found: $input"
fi

evidence="$artifact_root/$package_name"
archive_checksum="$artifact_root/$archive_checksum_name"

[ -f "$archive" ] ||
    fail "archive missing: $archive_name"
[ ! -L "$archive" ] ||
    fail 'archive must not be a symbolic link'
[ -f "$archive_checksum" ] ||
    fail "archive checksum missing: $archive_checksum_name"
[ ! -L "$archive_checksum" ] ||
    fail 'archive checksum must not be a symbolic link'

outer_checksum_lines=$(
    wc -l < "$archive_checksum" |
        awk '{ print $1 }'
)
[ "$outer_checksum_lines" -eq 1 ] ||
    fail "archive checksum file must contain exactly one line: actual=$outer_checksum_lines"

outer_checksum_count=$(
    grep -Ec \
        "^[0-9a-fA-F]{64}  ${archive_name}$" \
        "$archive_checksum" ||
    true
)
[ "$outer_checksum_count" -eq 1 ] ||
    fail 'archive checksum line contract failed'

(
    cd "$artifact_root"
    sha256sum -c "$archive_checksum_name"
)

archive_entries=$(tar -tzf "$archive") ||
    fail 'archive table-of-contents read failed'

printf '%s\n' "$archive_entries" |
    awk '
        /^\// { exit 1 }
        /(^|\/)\.\.($|\/)/ { exit 1 }
        { next }
    ' ||
    fail 'archive contains absolute or parent-traversal path'

tar -tvzf "$archive" |
    awk '
        $1 ~ /^[lh]/ { exit 1 }
        { next }
    ' ||
    fail 'archive contains a symbolic or hard link'

temp_root=$(mktemp -d)
cleanup()
{
    rm -rf "$temp_root"
}
trap cleanup EXIT HUP INT TERM

tar --no-same-owner --no-same-permissions \
    -xzf "$archive" \
    -C "$temp_root"

extracted="$temp_root/$package_name"
verify_evidence_directory "$extracted"

published_directory_compare=NOT_PRESENT
if [ -d "$evidence" ]; then
    verify_evidence_directory "$evidence"

    for compared_file in \
        host.txt step18.log lscpu.txt cpuinfo.txt accepted.txt SHA256SUMS
    do
        cmp -s "$evidence/$compared_file" "$extracted/$compared_file" ||
            fail "published directory/archive content mismatch: $compared_file"
    done

    published_directory_compare=PASS
elif [ -e "$evidence" ]; then
    fail 'published evidence path exists but is not a directory'
fi

archive_sha=$(sha256sum "$archive" | awk '{ print $1 }')

printf '%s\n' 'DBLYATEM PR98 CIRCLECI ARM64 LINUX PACKAGE VERIFY: PASS'
printf 'FEATURE_HEAD=%s\n' "$expected_feature"
printf 'EVIDENCE_CHALLENGE=%s\n' "$expected_challenge"
printf 'ARCHIVE_SHA256=%s\n' "$archive_sha"
printf 'PUBLISHED_DIRECTORY_COMPARE=%s\n' "$published_directory_compare"
printf 'ARTIFACT_ROOT=%s\n' "$artifact_root"
