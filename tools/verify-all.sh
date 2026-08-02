#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
manifest=${DB_VERIFY_MANIFEST:-"$root/tools/verify-manifest.txt"}
test_compiler_contract="$root/tools/test-compiler-contract.sh"
mode=--run
if [ "$#" -gt 1 ]; then
    printf '%s\n' 'usage: tools/verify-all.sh [--run|--contract]' >&2
    exit 2
fi
if [ "$#" -eq 1 ]; then
    mode=$1
fi

case "$mode" in
    --run|--contract) ;;
    *)
        printf '%s\n' 'usage: tools/verify-all.sh [--run|--contract]' >&2
        exit 2
        ;;
esac

scratch=$(mktemp -d "${TMPDIR:-/tmp}/dblyatem-verify-all.XXXXXX")
evidence="$scratch/evidence"
manifest_output="$evidence/manifest.txt"
finalized=0
failed_step=setup

fail()
{
    printf 'VERIFY ALL: FAIL: %s\n' "$1" >&2
    exit 1
}

publish_evidence()
{
    destination="$root/build/verify-all"
    rm -rf "$destination"
    mkdir -p "$destination"
    cp -R "$evidence/." "$destination/"
}

finish()
{
    status=$1
    trap - 0 HUP INT TERM

    if [ "$finalized" -eq 0 ] && [ -d "$evidence" ]; then
        end_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || printf '%s' unknown)
        {
            printf 'END_UTC=%s\n' "$end_utc"
            printf 'FAILED_STEP=%s\n' "$failed_step"
            printf 'OVERALL_STATUS=FAIL\n'
        } >> "$manifest_output"
        publish_evidence || true
    fi

    rm -rf "$scratch"
    exit "$status"
}
trap 'finish $?' 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

validate_manifest()
{
    [ -f "$manifest" ] || fail "missing manifest: $manifest"

    header=$(sed -n '1p' "$manifest")
    [ "$header" = 'DBLYATEM_VERIFY_MANIFEST_V1' ] ||
        fail 'bad manifest header'

    full_tree=
    targets=
    : > "$scratch/standalone.manifest"

    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in
            DBLYATEM_VERIFY_MANIFEST_V1|'')
                ;;
            FULL_TREE=*)
                [ -z "$full_tree" ] || fail 'duplicate FULL_TREE entry'
                full_tree=${line#FULL_TREE=}
                ;;
            FULL_TREE_TARGET=*)
                target=${line#FULL_TREE_TARGET=}
                [ -n "$target" ] || fail 'empty full-tree target'
                targets="$targets $target"
                ;;
            STANDALONE=*)
                verifier=${line#STANDALONE=}
                [ -n "$verifier" ] || fail 'empty standalone verifier path'
                printf '%s\n' "$verifier" >> "$scratch/standalone.manifest"
                ;;
            \#*)
                ;;
            *)
                fail "unknown manifest entry: $line"
                ;;
        esac
    done < "$manifest"

    [ "$full_tree" = 'GNUmakefile' ] || fail 'FULL_TREE must be GNUmakefile'
    [ "$targets" = ' clean all test' ] ||
        fail 'full-tree targets must be clean, all, test in order'
    [ -f "$root/$full_tree" ] || fail "missing full-tree makefile: $full_tree"
    [ -f "$test_compiler_contract" ] ||
        fail 'missing standalone test compiler contract'
    [ -s "$scratch/standalone.manifest" ] || fail 'no standalone verifiers listed'

    duplicate=$(LC_ALL=C sort "$scratch/standalone.manifest" | uniq -d | sed -n '1p')
    [ -z "$duplicate" ] || fail "duplicate standalone verifier: $duplicate"

    : > "$scratch/standalone.discovered.unsorted"
    for path in "$root"/tools/verify-*.sh; do
        [ -f "$path" ] || continue
        filename=$(basename -- "$path")
        [ "$filename" = 'verify-all.sh' ] && continue
        printf 'tools/%s\n' "$filename" >> \
            "$scratch/standalone.discovered.unsorted"
    done

    LC_ALL=C sort "$scratch/standalone.discovered.unsorted" > \
        "$scratch/standalone.discovered"

    [ -s "$scratch/standalone.discovered" ] ||
        fail 'no standalone verifier discovered'

    LC_ALL=C sort "$scratch/standalone.manifest" > \
        "$scratch/standalone.manifest.sorted"
    cmp -s "$scratch/standalone.manifest.sorted" \
           "$scratch/standalone.discovered" ||
        fail 'manifest standalone set does not match tools/verify-*.sh excluding verify-all.sh'

    while IFS= read -r verifier; do
        [ -f "$root/$verifier" ] || fail "missing verifier: $verifier"
        [ -x "$root/$verifier" ] || fail "verifier is not executable: $verifier"
    done < "$scratch/standalone.manifest"
}

validate_manifest

if [ "$mode" = '--contract' ]; then
    printf '%s\n' 'VERIFY ALL CONTRACT: PASS'
    printf '%s\n' 'FULL_TREE=GNUmakefile'
    printf '%s\n' 'TEST_COMPILER_CONTRACT=tools/test-compiler-contract.sh'
    while IFS= read -r verifier; do
        printf 'STANDALONE=%s\n' "$verifier"
    done < "$scratch/standalone.manifest"
    finalized=1
    exit 0
fi

for command_name in git make gcc clang date mkdir rm cp chmod sed sort uniq cmp; do
    command -v "$command_name" >/dev/null 2>&1 ||
        fail "required command not found: $command_name"
done

real_gcc=$(command -v gcc)
real_clang=$(command -v clang)

commit=$(git -C "$root" rev-parse --verify 'HEAD^{commit}') ||
    fail 'repository HEAD is unavailable'
tracked_dirty=$(git -C "$root" status --porcelain --untracked-files=no)
[ -z "$tracked_dirty" ] || fail 'tracked worktree changes would invalidate evidence'

mkdir -p "$evidence/logs"
start_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
gcc_version=$("$real_gcc" --version | sed -n '1p')
clang_version=$("$real_clang" --version | sed -n '1p')

{
    printf 'DBLYATEM_VERIFY_EVIDENCE_V1\n'
    printf 'COMMIT_SHA=%s\n' "$commit"
    printf 'START_UTC=%s\n' "$start_utc"
    printf 'FULL_TREE=GNUmakefile\n'
    printf 'FULL_TREE_TARGETS=clean all test\n'
    printf 'GCC_VERSION=%s\n' "$gcc_version"
    printf 'CLANG_VERSION=%s\n' "$clang_version"
    printf 'TEST_COMPILER_CONTRACT=tools/test-compiler-contract.sh\n'
    printf 'GITHUB_CI_CLAIM=NO\n'
} > "$manifest_output"

step_number=0
run_step()
{
    name=$1
    display=$2
    shift 2

    step_number=$((step_number + 1))
    step=$(printf '%02d_%s' "$step_number" "$name")
    log="$evidence/logs/$step.log"
    started=$(date +%s)

    set +e
    (cd "$root" && "$@") > "$log" 2>&1
    status=$?
    set -e

    ended=$(date +%s)
    duration=$((ended - started))
    {
        printf 'STEP_%02d_NAME=%s\n' "$step_number" "$name"
        printf 'STEP_%02d_COMMAND=%s\n' "$step_number" "$display"
        printf 'STEP_%02d_EXIT=%s\n' "$step_number" "$status"
        printf 'STEP_%02d_DURATION_SECONDS=%s\n' "$step_number" "$duration"
        printf 'STEP_%02d_LOG=logs/%s.log\n' "$step_number" "$step"
    } >> "$manifest_output"

    if [ "$status" -ne 0 ]; then
        failed_step=$name
        return "$status"
    fi

    return 0
}

run_step contract-test \
    'sh tests/verify_all_contract_test.sh' \
    sh tests/verify_all_contract_test.sh || exit $?
run_step gcc-clean \
    'make -f GNUmakefile clean' \
    make -f GNUmakefile clean || exit $?
run_step gcc-full-tree \
    'make -f GNUmakefile CC=gcc all test' \
    make -f GNUmakefile CC=gcc all test || exit $?
run_step clang-clean \
    'make -f GNUmakefile clean' \
    make -f GNUmakefile clean || exit $?
run_step clang-full-tree \
    'make -f GNUmakefile CC=clang all test' \
    make -f GNUmakefile CC=clang all test || exit $?

compiler_contract_bin="$scratch/test-compiler-contract-bin"
mkdir -p "$compiler_contract_bin"
cp "$test_compiler_contract" "$compiler_contract_bin/gcc"
cp "$test_compiler_contract" "$compiler_contract_bin/clang"
chmod 700 "$compiler_contract_bin/gcc" "$compiler_contract_bin/clang"

DB_TEST_REAL_GCC=$real_gcc
DB_TEST_REAL_CLANG=$real_clang
DB_TEST_ROOT=$root
DB_TEST_WRITE_CONTRACT="$root/tests/test_write_contract.h"
CC=clang
PATH="$compiler_contract_bin:$PATH"
export DB_TEST_REAL_GCC DB_TEST_REAL_CLANG DB_TEST_ROOT
export DB_TEST_WRITE_CONTRACT CC PATH

while IFS= read -r verifier; do
    name=$(basename -- "$verifier" .sh)
    run_step "$name" "sh $verifier" sh "$verifier" || exit $?
done < "$scratch/standalone.manifest"

end_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
{
    printf 'END_UTC=%s\n' "$end_utc"
    printf 'FAILED_STEP=NONE\n'
    printf 'OVERALL_STATUS=PASS\n'
} >> "$manifest_output"

publish_evidence
finalized=1
printf '%s\n' 'VERIFY ALL: PASS'
printf '%s\n' 'EVIDENCE=build/verify-all/manifest.txt'
exit 0
