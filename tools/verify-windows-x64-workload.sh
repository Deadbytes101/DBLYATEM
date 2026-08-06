#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
directory="$root/benchmarks/windows-x64-workload"
manifest="$directory/workload-manifest-v1.txt"
generator_source="$directory/generate-probe-v1.c"
structure_source="$directory/verify-probe-v1.c"
canonical_hex="$directory/probe-v1.hex"
import_set="$directory/import-set-v1.txt"
section_table="$directory/section-table-v1.txt"
readme="$directory/README.TXT"
self="$root/tools/verify-windows-x64-workload.sh"
scratch=$(mktemp -d "${TMPDIR:-/tmp}/dblyatem-windows-x64-workload.XXXXXX")
trap 'rm -rf "$scratch"' 0 HUP INT TERM

expected_executable_sha256='b32d2a090ce2fde1132f1daba6501a1bc6bf8f2c3e5b950b728eb30f8b211210'
expected_executable_bytes='1536'
expected_hex_sha256='12d42d73b87294fd3e4dc635d7f9c5404cb39f18bf07df4320028b65bdfaacac'
expected_hex_bytes='3073'
expected_generator_sha256='29b458b0af3b7cfa3081f60f9b23d56f3fc8d0e20e0264c5e19caecb7a6aa3ea'
expected_structure_sha256='3361fe60249cc8db1ee2178bcc126257f9cc0ee9ff576fe7eda0d946a0ad9949'
expected_readme_sha256='a6e2da8f22d1d2908e6f044397e87ec1d1a8e1eeb3629301a82de9827076b9b0'
expected_import_sha256='11257ccbd66a05eb70386075b99d692806ec448f86269e5b0158bd2b962e58c8'
expected_section_sha256='c8adedc908ea9411e47c3a199216bf56ba9322731f324feecabaef408bf96029'
expected_device_manifest_sha256='61921ce6b302c007d509e7b5a28982e2c9434706df2714a306cd228495573f01'

common_flags='-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

fail()
{
    printf 'WINDOWS X64 WORKLOAD VERIFY: FAIL: %s\n' "$1" >&2
    exit 1
}

hash_file()
{
    sha256sum "$1" | awk '{print $1}'
}

byte_count()
{
    wc -c < "$1" | tr -d '[:space:]'
}

exact_line()
{
    file=$1
    expected=$2
    count=$(grep -Fxc "$expected" "$file" || true)
    [ "$count" -eq 1 ] ||
        fail "exact line mismatch file=$file line=[$expected] count=$count"
}

manifest_value()
{
    key=$1
    value=$(sed -n "s/^${key}=//p" "$manifest")
    count=$(grep -c "^${key}=" "$manifest" || true)
    [ "$count" -eq 1 ] || fail "manifest key count mismatch key=$key count=$count"
    [ -n "$value" ] || fail "empty manifest value key=$key"
    printf '%s' "$value"
}

expect_failure()
{
    name=$1
    verifier=$2
    candidate=$3
    log="$scratch/$name.log"

    set +e
    "$verifier" "$candidate" > "$log" 2>&1
    status=$?
    set -e

    [ "$status" -ne 0 ] ||
        fail "negative fixture unexpectedly accepted: $name"
}

for command_name in \
    gcc clang sha256sum awk grep sed sort cmp wc tr cp dd printf mktemp rm mkdir basename
do
    command -v "$command_name" >/dev/null 2>&1 ||
        fail "required command not found: $command_name"
done

for required_file in \
    "$manifest" \
    "$generator_source" \
    "$structure_source" \
    "$canonical_hex" \
    "$import_set" \
    "$section_table" \
    "$readme" \
    "$self"
do
    [ -f "$required_file" ] || fail "required file missing: $required_file"
done

[ "$(hash_file "$generator_source")" = "$expected_generator_sha256" ] ||
    fail 'generator source hash mismatch'
[ "$(hash_file "$structure_source")" = "$expected_structure_sha256" ] ||
    fail 'structure verifier source hash mismatch'
[ "$(hash_file "$canonical_hex")" = "$expected_hex_sha256" ] ||
    fail 'canonical hex hash mismatch'
[ "$(byte_count "$canonical_hex")" = "$expected_hex_bytes" ] ||
    fail 'canonical hex byte count mismatch'
[ "$(hash_file "$readme")" = "$expected_readme_sha256" ] ||
    fail 'README hash mismatch'
[ "$(hash_file "$import_set")" = "$expected_import_sha256" ] ||
    fail 'import-set hash mismatch'
[ "$(hash_file "$section_table")" = "$expected_section_sha256" ] ||
    fail 'section-table hash mismatch'

self_sha256=$(hash_file "$self")
[ "$(manifest_value SHELL_VERIFIER_SHA256)" = "$self_sha256" ] ||
    fail 'shell verifier hash does not match manifest'

exact_line "$manifest" 'DBLYATEM_IDENTICAL_X64_WORKLOAD_MANIFEST_V1'
exact_line "$manifest" 'FORMAT_VERSION=1'
exact_line "$manifest" 'WORKLOAD_ID=DBLYATEM_IDENTICAL_X64_WORKLOAD_V1'
exact_line "$manifest" 'WORKLOAD=SET_LAST_ERROR_42_VERIFY_AND_EXIT'
exact_line "$manifest" 'EXECUTABLE=probe.exe'
exact_line "$manifest" "EXECUTABLE_SHA256=$expected_executable_sha256"
exact_line "$manifest" "EXECUTABLE_BYTES=$expected_executable_bytes"
exact_line "$manifest" 'CANONICAL_HEX=probe-v1.hex'
exact_line "$manifest" "CANONICAL_HEX_SHA256=$expected_hex_sha256"
exact_line "$manifest" "CANONICAL_HEX_BYTES=$expected_hex_bytes"
exact_line "$manifest" 'PE_MACHINE=x86_64'
exact_line "$manifest" 'PE_FORMAT=PE32_PLUS'
exact_line "$manifest" 'SUBSYSTEM=CONSOLE'
exact_line "$manifest" 'ENTRY_POINT_RVA=00001000'
exact_line "$manifest" 'IMAGE_BASE=0000000140000000'
exact_line "$manifest" 'SECTION_COUNT=2'
exact_line "$manifest" 'IMPORT_DLL_COUNT=1'
exact_line "$manifest" 'IMPORT_SYMBOL_COUNT=3'
exact_line "$manifest" "IMPORT_SET_SHA256=$expected_import_sha256"
exact_line "$manifest" "SECTION_TABLE_SHA256=$expected_section_sha256"
exact_line "$manifest" 'EXPECTED_STDOUT_SHA256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'
exact_line "$manifest" 'EXPECTED_STDERR_SHA256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'
exact_line "$manifest" 'EXPECTED_EXIT_CODE=0'
exact_line "$manifest" "GENERATOR_SOURCE_SHA256=$expected_generator_sha256"
exact_line "$manifest" "STRUCTURE_VERIFIER_SOURCE_SHA256=$expected_structure_sha256"
exact_line "$manifest" "SHELL_VERIFIER_SHA256=$self_sha256"
exact_line "$manifest" "README_SHA256=$expected_readme_sha256"
exact_line "$manifest" "IMPORT_SET_FILE_SHA256=$expected_import_sha256"
exact_line "$manifest" "SECTION_TABLE_FILE_SHA256=$expected_section_sha256"
exact_line "$manifest" 'BUILD_REPRODUCIBLE=YES'
exact_line "$manifest" "ACCEPTED_DEVICE_MANIFEST_SHA256=$expected_device_manifest_sha256"
exact_line "$manifest" 'DBLYATEM_EXIT_PROCESS_PROVIDER_AT_BASELINE=NOT_PRESENT'
exact_line "$manifest" 'NATIVE_WINDOWS_EXECUTION=NOT_RUN'
exact_line "$manifest" 'DBLYATEM_EXECUTION=NOT_RUN'
exact_line "$manifest" 'WINLATOR_EXECUTION=NOT_RUN'
exact_line "$manifest" 'WINLATOR_COMPARISON=NOT_RUN'
exact_line "$manifest" 'COMPATIBILITY_RESULT=NONE'
exact_line "$manifest" 'PERFORMANCE_RESULT=NONE'

mkdir "$scratch/build-a" "$scratch/build-b"

gcc $common_flags "$generator_source" -o "$scratch/build-a/generator"
clang $common_flags "$generator_source" -o "$scratch/build-b/generator"

(
    cd "$root"
    "$scratch/build-a/generator" "$scratch/build-a/probe.exe"
) > "$scratch/build-a/generator.log" 2>&1

(
    cd "$root"
    "$scratch/build-b/generator" "$scratch/build-b/probe.exe"
) > "$scratch/build-b/generator.log" 2>&1

cmp -s "$scratch/build-a/probe.exe" "$scratch/build-b/probe.exe" ||
    fail 'GCC and Clang generator outputs differ'

for probe in "$scratch/build-a/probe.exe" "$scratch/build-b/probe.exe"
do
    [ "$(byte_count "$probe")" = "$expected_executable_bytes" ] ||
        fail "generated executable byte count mismatch: $probe"
    [ "$(hash_file "$probe")" = "$expected_executable_sha256" ] ||
        fail "generated executable hash mismatch: $probe"
done

gcc $common_flags "$structure_source" -o "$scratch/verify-gcc"
clang $common_flags "$structure_source" -o "$scratch/verify-clang"

for verifier in "$scratch/verify-gcc" "$scratch/verify-clang"
do
    "$verifier" "$scratch/build-a/probe.exe" > "$scratch/structure.log" 2>&1 ||
        fail "structure verifier rejected build A: $verifier"
    "$verifier" "$scratch/build-b/probe.exe" > "$scratch/structure.log" 2>&1 ||
        fail "structure verifier rejected build B: $verifier"
done

cp "$scratch/build-a/probe.exe" "$scratch/mutated.exe"
printf '\001' |
    dd of="$scratch/mutated.exe" bs=1 seek=512 count=1 conv=notrunc 2>/dev/null

dd if="$scratch/build-a/probe.exe" \
   of="$scratch/truncated.exe" \
   bs=1 count=1535 2>/dev/null

cp "$scratch/build-a/probe.exe" "$scratch/appended.exe"
printf '\000' >> "$scratch/appended.exe"

for verifier in "$scratch/verify-gcc" "$scratch/verify-clang"
do
    verifier_name=$(basename "$verifier")
    expect_failure "${verifier_name}-mutated" "$verifier" "$scratch/mutated.exe"
    expect_failure "${verifier_name}-truncated" "$verifier" "$scratch/truncated.exe"
    expect_failure "${verifier_name}-appended" "$verifier" "$scratch/appended.exe"
done

printf '%s\n' 'WINDOWS X64 WORKLOAD VERIFY: PASS'
printf 'EXECUTABLE_SHA256=%s\n' "$expected_executable_sha256"
printf 'EXECUTABLE_BYTES=%s\n' "$expected_executable_bytes"
printf '%s\n' 'PE_MACHINE=x86_64'
printf '%s\n' 'PE_FORMAT=PE32_PLUS'
printf '%s\n' 'SUBSYSTEM=CONSOLE'
printf '%s\n' 'ENTRY_POINT_RVA=00001000'
printf '%s\n' 'IMPORT_DLL_COUNT=1'
printf '%s\n' 'IMPORT_SYMBOL_COUNT=3'
printf '%s\n' 'BUILD_REPRODUCIBLE=YES'
printf '%s\n' 'NEGATIVE_FIXTURES=PASS'
printf '%s\n' 'NATIVE_WINDOWS_EXECUTION=NOT_RUN'
printf '%s\n' 'DBLYATEM_EXECUTION=NOT_RUN'
printf '%s\n' 'WINLATOR_EXECUTION=NOT_RUN'
printf '%s\n' 'WINLATOR_COMPARISON=NOT_RUN'
printf '%s\n' 'COMPATIBILITY_RESULT=NONE'
printf '%s\n' 'PERFORMANCE_RESULT=NONE'
