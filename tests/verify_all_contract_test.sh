#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/dblyatem-verify-contract.XXXXXX")
trap 'rm -rf "$temporary"' 0 HUP INT TERM

output="$temporary/output.txt"
DB_VERIFY_MANIFEST="$root/tools/verify-manifest.txt" \
    "$root/tools/verify-all.sh" --contract > "$output"

grep -Fx 'VERIFY ALL CONTRACT: PASS' "$output" >/dev/null
grep -Fx 'FULL_TREE=GNUmakefile' "$output" >/dev/null

for verifier in \
    tools/verify-win32-provider.sh \
    tools/verify-win32-kernel32.sh \
    tools/verify-win32-kernel32-invoke.sh \
    tools/verify-win32-kernel32-gateway.sh \
    tools/verify-x64-decode.sh \
    tools/verify-x64-ir-boundary.sh \
    tools/verify-x64-ir-lower.sh
do
    grep -Fx "STANDALONE=$verifier" "$output" >/dev/null
done

if grep -Fx 'STANDALONE=tools/verify-all.sh' "$output" >/dev/null
then
    printf '%s\n' 'VERIFY ALL CONTRACT TEST: recursive verifier accepted' >&2
    exit 1
fi

sed '/STANDALONE=tools\/verify-x64-ir-lower.sh/d' \
    "$root/tools/verify-manifest.txt" > "$temporary/missing.txt"
if DB_VERIFY_MANIFEST="$temporary/missing.txt" \
    "$root/tools/verify-all.sh" --contract >/dev/null 2>&1
then
    printf '%s\n' 'VERIFY ALL CONTRACT TEST: missing verifier accepted' >&2
    exit 1
fi

cat "$root/tools/verify-manifest.txt" > "$temporary/extra.txt"
printf '%s\n' 'STANDALONE=tools/verify-all.sh' >> "$temporary/extra.txt"
if DB_VERIFY_MANIFEST="$temporary/extra.txt" \
    "$root/tools/verify-all.sh" --contract >/dev/null 2>&1
then
    printf '%s\n' 'VERIFY ALL CONTRACT TEST: extra verifier accepted' >&2
    exit 1
fi

cat "$root/tools/verify-manifest.txt" > "$temporary/duplicate.txt"
printf '%s\n' 'STANDALONE=tools/verify-x64-ir-lower.sh' >> \
    "$temporary/duplicate.txt"
if DB_VERIFY_MANIFEST="$temporary/duplicate.txt" \
    "$root/tools/verify-all.sh" --contract >/dev/null 2>&1
then
    printf '%s\n' 'VERIFY ALL CONTRACT TEST: duplicate verifier accepted' >&2
    exit 1
fi

sed 's/FULL_TREE_TARGET=all/FULL_TREE_TARGET=test/' \
    "$root/tools/verify-manifest.txt" > "$temporary/bad-targets.txt"
if DB_VERIFY_MANIFEST="$temporary/bad-targets.txt" \
    "$root/tools/verify-all.sh" --contract >/dev/null 2>&1
then
    printf '%s\n' 'VERIFY ALL CONTRACT TEST: bad target sequence accepted' >&2
    exit 1
fi

printf '%s\n' 'VERIFY ALL CONTRACT TEST: PASS'
