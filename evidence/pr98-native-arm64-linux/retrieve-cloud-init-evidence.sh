#!/bin/sh
set -eu

expected_feature='8886fe231eaaefd9c28ed6fe9a7dc404739cc566'
expected_kit='b1688958f1993a5c885beb912f39fe7be5f64133'
expected_challenge='0f9d66bc95e62d3c5fa1ae695ed9917605db48c31e24427667c9dd8d08391328'
archive_name='pr98-native-arm64-linux-evidence.tar.gz'
archive_checksum_name="${archive_name}.sha256"
status_name='DBLYATEM-PR98-NATIVE-ARM64-STATUS.txt'

ssh_target=${1:-}
port=${2:-22}
identity_file=${3:-}
output_parent=${4:-"$PWD"}
known_hosts=${DBLYATEM_KNOWN_HOSTS:-"$HOME/.ssh/known_hosts"}

fail()
{
    printf 'PR98 CLOUD EVIDENCE RETRIEVAL: FAIL: %s\n' "$*" >&2
    exit 1
}

require_command()
{
    command -v "$1" >/dev/null 2>&1 ||
        fail "missing command: $1"
}

[ -n "$ssh_target" ] ||
    fail 'usage: retrieve-cloud-init-evidence.sh <user@host-or-alias> [port] [identity-file] [output-parent]'

printf '%s\n' "$ssh_target" |
    grep -Eq '^[A-Za-z0-9._-]+@[A-Za-z0-9._-]+$' ||
    fail 'SSH target must be user@host-or-ssh-alias without spaces or IPv6 literals'

printf '%s\n' "$port" |
    grep -Eq '^[0-9]+$' ||
    fail 'SSH port is not numeric'

[ "$port" -ge 1 ] 2>/dev/null &&
[ "$port" -le 65535 ] 2>/dev/null ||
    fail 'SSH port is outside 1..65535'

[ -d "$output_parent" ] ||
    fail "output parent is not a directory: $output_parent"

[ -f "$known_hosts" ] ||
    fail "known_hosts file missing: $known_hosts"

[ -s "$known_hosts" ] ||
    fail "known_hosts file is empty: $known_hosts"

if [ -n "$identity_file" ]; then
    [ -f "$identity_file" ] ||
        fail "identity file missing: $identity_file"
    [ ! -L "$identity_file" ] ||
        fail 'identity file must not be a symbolic link'
fi

for command_name in ssh scp grep awk wc sha256sum mktemp dirname basename; do
    require_command "$command_name"
done

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
verifier="$script_dir/verify-evidence-package.sh"
[ -f "$verifier" ] ||
    fail "offline verifier missing beside retrieval script: $verifier"

destination="$output_parent/pr99-native-arm64-linux-evidence-current"
[ ! -L "$destination" ] ||
    fail 'destination path is a symbolic link'

case "$destination" in
    /|.|..)
        fail "unsafe destination: $destination"
        ;;
esac

remote_status="/var/tmp/$status_name"
remote_archive="/var/tmp/$archive_name"
remote_archive_checksum="/var/tmp/$archive_checksum_name"

remote_check=$(cat <<REMOTE_EOF
set -eu
status='$remote_status'
archive='$remote_archive'
archive_checksum='$remote_archive_checksum'
test -f "\$status"
test -f "\$archive"
test -f "\$archive_checksum"
grep -Fqx 'DBLYATEM_PR98_NATIVE_ARM64_CLOUD_INIT_V1' "\$status"
grep -Fqx 'PORTABLE_KIT_HEAD=$expected_kit' "\$status"
grep -Fqx 'FEATURE_HEAD=$expected_feature' "\$status"
grep -Fqx 'EVIDENCE_CHALLENGE=$expected_challenge' "\$status"
grep -Fqx 'DIRECTORY_VERIFY=PASS' "\$status"
grep -Fqx 'ARCHIVE_VERIFY=PASS' "\$status"
grep -Fqx 'RESULT=PASS' "\$status"
cd /var/tmp
sha256sum -c '$archive_checksum_name'
REMOTE_EOF
)

run_ssh()
{
    if [ -n "$identity_file" ]; then
        ssh \
            -p "$port" \
            -i "$identity_file" \
            -o BatchMode=yes \
            -o IdentitiesOnly=yes \
            -o StrictHostKeyChecking=yes \
            -o UserKnownHostsFile="$known_hosts" \
            -o ConnectTimeout=15 \
            -o ServerAliveInterval=15 \
            -o ServerAliveCountMax=2 \
            "$ssh_target" \
            "$1"
    else
        ssh \
            -p "$port" \
            -o BatchMode=yes \
            -o StrictHostKeyChecking=yes \
            -o UserKnownHostsFile="$known_hosts" \
            -o ConnectTimeout=15 \
            -o ServerAliveInterval=15 \
            -o ServerAliveCountMax=2 \
            "$ssh_target" \
            "$1"
    fi
}

run_scp()
{
    remote_path=$1
    local_path=$2

    if [ -n "$identity_file" ]; then
        scp \
            -P "$port" \
            -i "$identity_file" \
            -o BatchMode=yes \
            -o IdentitiesOnly=yes \
            -o StrictHostKeyChecking=yes \
            -o UserKnownHostsFile="$known_hosts" \
            -o ConnectTimeout=15 \
            "$ssh_target:$remote_path" \
            "$local_path"
    else
        scp \
            -P "$port" \
            -o BatchMode=yes \
            -o StrictHostKeyChecking=yes \
            -o UserKnownHostsFile="$known_hosts" \
            -o ConnectTimeout=15 \
            "$ssh_target:$remote_path" \
            "$local_path"
    fi
}

temp_root=$(mktemp -d)
cleanup()
{
    rm -rf "$temp_root"
}
trap cleanup EXIT HUP INT TERM

ssh_error="$temp_root/ssh.err"
if ! run_ssh "$remote_check" > "$temp_root/ssh.out" 2> "$ssh_error"; then
    fail 'remote preflight failed; inspect the ARM64 host status and SSH trust configuration'
fi

for remote_file in \
    "$remote_status" \
    "$remote_archive" \
    "$remote_archive_checksum"
do
    local_name=$(basename "$remote_file")
    if ! run_scp "$remote_file" "$temp_root/$local_name" \
        > /dev/null 2> "$temp_root/scp.err"
    then
        fail "secure copy failed for expected artifact: $local_name"
    fi
done

status="$temp_root/$status_name"
archive="$temp_root/$archive_name"
archive_checksum="$temp_root/$archive_checksum_name"

for line in \
    'DBLYATEM_PR98_NATIVE_ARM64_CLOUD_INIT_V1' \
    "PORTABLE_KIT_HEAD=$expected_kit" \
    "FEATURE_HEAD=$expected_feature" \
    "EVIDENCE_CHALLENGE=$expected_challenge" \
    'DIRECTORY_VERIFY=PASS' \
    'ARCHIVE_VERIFY=PASS' \
    'RESULT=PASS'
do
    count=$(grep -Fxc "$line" "$status" || true)
    [ "$count" -eq 1 ] ||
        fail "downloaded status contract failed: line=$line actual=$count"
done

checksum_lines=$(wc -l < "$archive_checksum" | awk '{ print $1 }')
[ "$checksum_lines" -eq 1 ] ||
    fail "archive checksum file must contain exactly one line: actual=$checksum_lines"

checksum_count=$(
    grep -Ec \
        "^[0-9a-fA-F]{64}  ${archive_name}$" \
        "$archive_checksum" ||
    true
)
[ "$checksum_count" -eq 1 ] ||
    fail 'archive checksum line contract failed'

(
    cd "$temp_root"
    sha256sum -c "$archive_checksum_name"
)

/bin/sh "$verifier" "$archive"

rm -rf "$destination"
mkdir -p "$destination"

cp "$status" "$destination/$status_name"
cp "$archive" "$destination/$archive_name"
cp "$archive_checksum" "$destination/$archive_checksum_name"

remote_target_hash=$(
    printf '%s' "$ssh_target" |
        sha256sum |
        awk '{ print $1 }'
)
archive_hash=$(sha256sum "$archive" | awk '{ print $1 }')
status_hash=$(sha256sum "$status" | awk '{ print $1 }')
retrieval="$destination/retrieval.txt"

{
    printf '%s\n' 'DBLYATEM_PR98_NATIVE_ARM64_RETRIEVAL_V1'
    printf 'FEATURE_HEAD=%s\n' "$expected_feature"
    printf 'PORTABLE_KIT_HEAD=%s\n' "$expected_kit"
    printf 'EVIDENCE_CHALLENGE=%s\n' "$expected_challenge"
    printf 'REMOTE_TARGET_SHA256=%s\n' "$remote_target_hash"
    printf 'SSH_PORT=%s\n' "$port"
    printf 'REMOTE_STATUS_SHA256=%s\n' "$status_hash"
    printf 'ARCHIVE_SHA256=%s\n' "$archive_hash"
    printf '%s\n' 'STRICT_HOST_KEY_CHECKING=PASS'
    printf '%s\n' 'REMOTE_PRECHECK=PASS'
    printf '%s\n' 'REMOTE_ARCHIVE_SHA256=PASS'
    printf '%s\n' 'LOCAL_OFFLINE_PACKAGE_VERIFY=PASS'
    printf '%s\n' 'RETRIEVAL=PASS'
} > "$retrieval"

(
    cd "$destination"
    sha256sum \
        "$status_name" \
        "$archive_name" \
        "$archive_checksum_name" \
        retrieval.txt \
        > SHA256SUMS
)

printf '%s\n' 'DBLYATEM PR98 NATIVE ARM64 CLOUD EVIDENCE RETRIEVAL: PASS'
printf 'REMOTE_TARGET_SHA256=%s\n' "$remote_target_hash"
printf 'EVIDENCE=%s\n' "$destination"
printf 'SHA256SUMS=%s\n' "$destination/SHA256SUMS"
