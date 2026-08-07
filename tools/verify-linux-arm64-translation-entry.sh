#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/linux-arm64-translation-entry-verify"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
entry_source='src/linux_arm64_translation_entry.c'
entry_asm='src/linux_arm64_translation_entry_aarch64.S'
terminal_source='src/translated_terminal_exit.c'
published_source='src/arm64_published_block.c'
cache_source='src/arm64_block_cache.c'
test_source='tests/linux_arm64_translation_entry_test.c'
validation_sources="
$entry_source
$terminal_source
$published_source
$cache_source
"
full_sources='
src/linux_arm64_translation_entry.c
src/translated_terminal_exit.c
src/x64_block.c
src/x64_decode.c
src/x64_memory_instruction.c
src/x64_memory_decode.c
src/ir.c
src/x64_ir_lower.c
src/x64_ir_lower_validate.c
src/x64_ir_lower_emit.c
src/arm64_select.c
src/arm64_select_validate.c
src/arm64_select_emit.c
src/x64_arm64_plan.c
src/x64_arm64_plan_validate.c
src/x64_arm64_plan_group.c
src/x64_arm64_plan_emit.c
src/x64_arm64_encode.c
src/x64_arm64_relocate.c
src/x64_arm64_relocate_validate.c
src/x64_arm64_relocate_emit.c
src/arm64_code_image.c
src/linux_arm64_wx_provider.c
src/linux_arm64_wx_posix.c
src/arm64_published_block.c
src/arm64_block_cache.c
'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

for command_name in gcc clang readelf awk grep uname; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'TRANSLATED ENTRY VERIFY: missing command: %s\n' \
            "$command_name" >&2
        exit 1
    }
done

grep -F 'DB_LINUX_ARM64_TRANSLATION_ENTRY_FORMAT_VERSION' \
    include/dblyat/linux_arm64_translation_entry.h >/dev/null
grep -F 'db_translated_terminal_exit planned_exit' \
    include/dblyat/linux_arm64_translation_entry.h >/dev/null
grep -F 'db_translated_terminal_exit observed_exit' \
    include/dblyat/linux_arm64_translation_entry.h >/dev/null
grep -F 'db_linux_arm64_translation_context_bind_terminal' \
    include/dblyat/linux_arm64_translation_entry.h >/dev/null
grep -F 'context->terminal_return_count != UINT64_MAX' \
    "$entry_source" >/dev/null
grep -F 'planned_state = *state' "$entry_source" >/dev/null
grep -F 'planned_context = *context' "$entry_source" >/dev/null
grep -F 'db_translated_terminal_exit_match' "$entry_source" >/dev/null
grep -F '*state = planned_state' "$entry_source" >/dev/null
grep -F '*context = planned_context' "$entry_source" >/dev/null
grep -F 'mov x19, x1' "$entry_asm" >/dev/null
grep -F 'mov x20, x2' "$entry_asm" >/dev/null
grep -F 'blr x16' "$entry_asm" >/dev/null
grep -F 'db_linux_arm64_translation_target_helper' "$entry_asm" >/dev/null
grep -F 'db_linux_arm64_translation_conditional_helper' "$entry_asm" >/dev/null
grep -F 'db_linux_arm64_translation_trap_helper' "$entry_asm" >/dev/null
grep -F 'str x15, [x20, #96]' "$entry_asm" >/dev/null
grep -F 'str x15, [x20, #104]' "$entry_asm" >/dev/null
grep -F '.note.GNU-stack' "$entry_asm" >/dev/null
grep -F 'db_x64_block_build' "$test_source" >/dev/null
grep -F 'db_x64_ir_lower_block' "$test_source" >/dev/null
grep -F 'db_arm64_select_block' "$test_source" >/dev/null
grep -F 'db_x64_arm64_plan_block' "$test_source" >/dev/null
grep -F 'db_x64_arm64_encode_block' "$test_source" >/dev/null
grep -F 'db_x64_arm64_resolve_helpers' "$test_source" >/dev/null
grep -F 'db_arm64_code_image_build' "$test_source" >/dev/null
grep -F 'db_linux_arm64_wx_publish' "$test_source" >/dev/null
grep -F 'db_arm64_published_block_bind_terminal' "$test_source" >/dev/null
grep -F 'db_arm64_block_cache_insert' "$test_source" >/dev/null
grep -F 'db_arm64_block_cache_lookup' "$test_source" >/dev/null
grep -F 'DB_IR_TRAP_INVALID_OPCODE' "$test_source" >/dev/null
grep -F 'DB_LINUX_ARM64_TRANSLATION_ENTRY_TERMINAL_EXIT_MISMATCH' \
    "$test_source" >/dev/null
grep -F 'db_linux_arm64_translation_enter' "$test_source" >/dev/null
grep -F 'db_linux_arm64_wx_release' "$test_source" >/dev/null

if grep -E '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|mprotect|munmap|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync)([^[:alnum:]_]|$)' \
    include/dblyat/linux_arm64_translation_entry.h \
    include/dblyat/translated_terminal_exit.h \
    "$entry_source" "$terminal_source" "$entry_asm" >/dev/null
then
    printf '%s\n' \
        'TRANSLATED ENTRY CORE CROSSED OWNERSHIP, MAPPING, OR CONCURRENCY BOUNDARY' >&2
    exit 1
fi

clang --target=aarch64-linux-gnu \
    -c "$entry_asm" \
    -o "$build/entry-reference.o"

actual=$(readelf -x .text "$build/entry-reference.o" | awk '
/^  0x/ {
    for (field = 2; field <= NF; ++field) {
        if (length($field) == 8 && $field ~ /^[0-9a-fA-F]+$/) {
            printf "%s", tolower($field)
        } else {
            break
        }
    }
}
')

expected='fd7bbea9f35301a9fd030091f00300aaf30301aaf40302aa00023fd6f35341a9fd7bc2a8c0035fd689aa40a989aa04a989aa41a989aa05a989aa42a989aa06a989aa43a989aa07a929008052897200b9890240f929050091890200f9c0035fd689aa40a989aa04a989aa41a989aa05a989aa42a989aa06a989aa43a989aa07a949008052897200b98f3200f92900805289020239890240f929050091890200f9c0035fd689aa40a989aa04a989aa41a989aa05a989aa42a989aa06a989aa43a989aa07a969008052897200b98f3600f96f0000b48a0a40f9020000148a0e40f98a3200f92900805289020239890602398f0a0239890e0239890240f929050091890200f9c0035fd689aa40a989aa04a989aa41a989aa05a989aa42a989aa06a989aa43a989aa07a989008052897200b9890240f929050091890200f9c0035fd6'

if [ "$actual" != "$expected" ]; then
    printf '%s\n' 'TRANSLATED ENTRY ASSEMBLY WORDS CHANGED' >&2
    printf 'EXPECTED=%s\n' "$expected" >&2
    printf 'ACTUAL=%s\n' "$actual" >&2
    exit 1
fi

readelf -sW "$build/entry-reference.o" |
    grep -E '[[:space:]]40[[:space:]]+FUNC[[:space:]]+GLOBAL.*db_linux_arm64_translation_enter_asm$' >/dev/null
readelf -sW "$build/entry-reference.o" |
    grep -E '[[:space:]]56[[:space:]]+FUNC[[:space:]]+GLOBAL.*db_linux_arm64_translation_return_helper$' >/dev/null
readelf -sW "$build/entry-reference.o" |
    grep -E '[[:space:]]68[[:space:]]+FUNC[[:space:]]+GLOBAL.*db_linux_arm64_translation_target_helper$' >/dev/null
readelf -sW "$build/entry-reference.o" |
    grep -E '[[:space:]]100[[:space:]]+FUNC[[:space:]]+GLOBAL.*db_linux_arm64_translation_conditional_helper$' >/dev/null
readelf -sW "$build/entry-reference.o" |
    grep -E '[[:space:]]56[[:space:]]+FUNC[[:space:]]+GLOBAL.*db_linux_arm64_translation_trap_helper$' >/dev/null

machine=$(uname -m)
case "$machine" in
    aarch64|arm64)
        for compiler in gcc clang
        do
            asm_object="$build/entry-$compiler.o"
            binary="$build/translated-entry-$compiler"

            "$compiler" -c "$entry_asm" -o "$asm_object"
            # shellcheck disable=SC2086
            "$compiler" $common \
                "$test_source" $full_sources "$asm_object" \
                -o "$binary"
            "$binary"
        done

        gcc -c "$entry_asm" -o "$build/entry-sanitize.o"
        # shellcheck disable=SC2086
        gcc -Iinclude -Isrc -std=c11 -O1 -g \
            -Wall -Wextra -Wpedantic -Werror -Wshadow \
            -Wstrict-prototypes -Wmissing-prototypes \
            -fsanitize=address,undefined -fno-omit-frame-pointer \
            "$test_source" $full_sources \
            "$build/entry-sanitize.o" \
            -o "$build/translated-entry-sanitize"
        ASAN_OPTIONS=detect_leaks=1 \
            "$build/translated-entry-sanitize"

        printf '%s\n' 'X64 TO ARM64 TRANSLATED EXECUTION: PASS'
        printf '%s\n' 'TRANSLATED TERMINAL EXIT NATIVE IDENTITY: PASS'
        ;;
    *)
        for compiler in gcc clang
        do
            binary="$build/translated-entry-validation-$compiler"
            # shellcheck disable=SC2086
            "$compiler" $common \
                "$test_source" $validation_sources \
                -o "$binary"
            "$binary"
        done

        # shellcheck disable=SC2086
        gcc -Iinclude -Isrc -std=c11 -O1 -g \
            -Wall -Wextra -Wpedantic -Werror -Wshadow \
            -Wstrict-prototypes -Wmissing-prototypes \
            -fsanitize=address,undefined -fno-omit-frame-pointer \
            "$test_source" $validation_sources \
            -o "$build/translated-entry-validation-sanitize"
        ASAN_OPTIONS=detect_leaks=1 \
            "$build/translated-entry-validation-sanitize"

        printf '%s\n' 'X64 TO ARM64 TRANSLATED EXECUTION: NOT CLAIMED'
        printf '%s\n' 'TRANSLATED TERMINAL EXIT NATIVE IDENTITY: NOT CLAIMED'
        ;;
esac

# shellcheck disable=SC2086
clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    "$test_source" $validation_sources

printf '%s\n' 'TRANSLATED ENTRY ASSEMBLY ABI: PASS'
printf '%s\n' 'TRANSLATED TERMINAL EXIT RECORD VALIDATION: PASS'
printf '%s\n' 'TRANSLATED TERMINAL EXIT CACHE BINDING: PASS'
printf '%s\n' 'TRANSLATED ENTRY VALIDATION: PASS'
printf '%s\n' 'TRANSLATED ENTRY TRANSACTIONAL AUDIT: PASS'
printf '%s\n' 'TRANSLATED W^X PUBLISH ENTER RELEASE: PASS'
printf '%s\n' 'LINUX ARM64 TRANSLATED ENTRY VERIFY: PASS'
printf '%s\n' 'WINLATOR COMPARISON: NOT RUN'
