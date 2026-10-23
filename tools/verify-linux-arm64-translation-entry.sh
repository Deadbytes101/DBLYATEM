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
guest_check='tests/linux_arm64_guest_runtime_check.c'
validation_sources="
$entry_source
$terminal_source
$published_source
$cache_source
"
guest_sources="
$entry_source
$terminal_source
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

for command_name in gcc clang readelf grep uname; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'TRANSLATED ENTRY VERIFY: missing command: %s\n' \
            "$command_name" >&2
        exit 1
    }
done

for required in \
    include/dblyat/linux_arm64_guest_runtime.h \
    include/dblyat/linux_arm64_translation_entry.h \
    src/linux_arm64_guest_runtime_internal.h \
    "$entry_source" "$entry_asm" "$guest_check"
do
    [ -f "$required" ] || {
        printf 'TRANSLATED ENTRY VERIFY: missing file: %s\n' "$required" >&2
        exit 1
    }
done

grep -F 'DB_LINUX_ARM64_TRANSLATION_ENTRY_FORMAT_VERSION' \
    include/dblyat/linux_arm64_translation_entry.h >/dev/null
grep -F 'db_translated_terminal_exit planned_exit' \
    include/dblyat/linux_arm64_translation_entry.h >/dev/null
grep -F 'db_translated_terminal_exit observed_exit' \
    include/dblyat/linux_arm64_translation_entry.h >/dev/null
grep -F 'db_linux_arm64_guest_memory guest_memory' \
    include/dblyat/linux_arm64_translation_entry.h >/dev/null
grep -F 'db_linux_arm64_translation_context_bind_terminal' \
    include/dblyat/linux_arm64_translation_entry.h >/dev/null
grep -F 'db_linux_arm64_translation_context_bind_guest_memory' \
    include/dblyat/linux_arm64_translation_entry.h >/dev/null
grep -F 'offsetof(db_linux_arm64_translation_context,' "$entry_source" >/dev/null
grep -F 'guest_memory) == 144u' "$entry_source" >/dev/null
grep -F 'helper_fault_address) == 248u' "$entry_source" >/dev/null
grep -F 'planned_state = *state' "$entry_source" >/dev/null
grep -F 'planned_context = *context' "$entry_source" >/dev/null
grep -F 'DB_LINUX_ARM64_TRANSLATION_ENTRY_HELPER_FAILED' "$entry_source" >/dev/null
grep -F '*state = planned_state' "$entry_source" >/dev/null
grep -F '*context = planned_context' "$entry_source" >/dev/null

grep -F 'mov x19, x1' "$entry_asm" >/dev/null
grep -F 'mov x20, x2' "$entry_asm" >/dev/null
grep -F 'adr x21, .Ldb_linux_arm64_translation_block_return' "$entry_asm" >/dev/null
grep -F 'blr x16' "$entry_asm" >/dev/null
[ "$(grep -Fc 'br x21' "$entry_asm")" -eq 4 ] || {
    printf '%s\n' 'TRANSLATED ENTRY TERMINAL RETURN ANCHOR MISMATCH' >&2
    exit 1
}
for symbol in \
    db_linux_arm64_translation_target_helper \
    db_linux_arm64_translation_conditional_helper \
    db_linux_arm64_translation_trap_helper \
    db_linux_arm64_helper_form_guest_address \
    db_linux_arm64_helper_read_guest_u64 \
    db_linux_arm64_helper_write_guest_u64 \
    db_linux_arm64_helper_push_guest_u64 \
    db_linux_arm64_helper_pop_guest_u64 \
    db_linux_arm64_helper_update_logic_flags \
    db_linux_arm64_helper_test_guest_condition
do
    grep -F ".global $symbol" "$entry_asm" >/dev/null
 done

grep -F 'mov x15, x0' "$entry_asm" >/dev/null
grep -F 'mov x17, x0' "$entry_asm" >/dev/null
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
grep -F 'DB_LINUX_ARM64_TRANSLATION_ENTRY_TERMINAL_EXIT_MISMATCH' \
    "$test_source" >/dev/null
grep -F 'db_linux_arm64_translation_enter' "$test_source" >/dev/null
grep -F 'db_linux_arm64_wx_release' "$test_source" >/dev/null

grep -F 'DB_MAP_PAGE_RX' "$guest_check" >/dev/null
grep -F 'DB_MAP_PAGE_RW' "$guest_check" >/dev/null
grep -F 'db_linux_arm64_guest_memory_read_u64' "$guest_check" >/dev/null
grep -F 'db_linux_arm64_guest_memory_write_u64' "$guest_check" >/dev/null
grep -F 'db_linux_arm64_helper_form_guest_address_core' "$guest_check" >/dev/null
grep -F 'db_linux_arm64_helper_push_guest_u64_core' "$guest_check" >/dev/null
grep -F 'db_linux_arm64_helper_pop_guest_u64_core' "$guest_check" >/dev/null
grep -F 'db_linux_arm64_helper_update_logic_flags_core' "$guest_check" >/dev/null
grep -F 'db_linux_arm64_helper_test_guest_condition_core' "$guest_check" >/dev/null

if grep -E '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|mprotect|munmap|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync)([^[:alnum:]_]|$)' \
    include/dblyat/linux_arm64_guest_runtime.h \
    include/dblyat/linux_arm64_translation_entry.h \
    src/linux_arm64_guest_runtime_internal.h \
    "$entry_source" "$terminal_source" "$entry_asm" >/dev/null
then
    printf '%s\n' \
        'TRANSLATED ENTRY CORE CROSSED OWNERSHIP, MAPPING, OR CONCURRENCY BOUNDARY' >&2
    exit 1
fi

clang --target=aarch64-linux-gnu \
    -c "$entry_asm" \
    -o "$build/entry-reference.o"

for symbol in \
    db_linux_arm64_translation_enter_asm \
    db_linux_arm64_translation_return_helper \
    db_linux_arm64_translation_target_helper \
    db_linux_arm64_translation_conditional_helper \
    db_linux_arm64_translation_trap_helper \
    db_linux_arm64_helper_form_guest_address \
    db_linux_arm64_helper_read_guest_u64 \
    db_linux_arm64_helper_write_guest_u64 \
    db_linux_arm64_helper_push_guest_u64 \
    db_linux_arm64_helper_pop_guest_u64 \
    db_linux_arm64_helper_update_logic_flags \
    db_linux_arm64_helper_test_guest_condition
do
    count=$(readelf -sW "$build/entry-reference.o" |
        grep -Ec "FUNC[[:space:]]+GLOBAL.*${symbol}$" || true)
    [ "$count" -eq 1 ] || {
        printf 'TRANSLATED ENTRY SYMBOL MISMATCH symbol=%s count=%s\n' \
            "$symbol" "$count" >&2
        exit 1
    }
done

machine=$(uname -m)
case "$machine" in
    aarch64|arm64)
        # The guest-runtime test links the entry C owner as well. On native
        # Arm64 that owner references wrappers defined in the real asm file.
        gcc -c "$entry_asm" -o "$build/guest-native-entry.o"
        guest_sources="$guest_sources $build/guest-native-entry.o"
        ;;
esac

for compiler in gcc clang
do
    binary="$build/guest-runtime-$compiler"
    # shellcheck disable=SC2086
    "$compiler" $common "$guest_check" $guest_sources -o "$binary"
    "$binary" > "$build/guest-runtime-$compiler.log"

done

for log in "$build/guest-runtime-gcc.log" "$build/guest-runtime-clang.log"
do
    for marker in \
        'LINUX ARM64 GUEST RUNTIME TEST: PASS' \
        'GUEST ADDRESS SPACE PERMISSIONS: PASS' \
        'FORM ADDRESS HELPER CORE: PASS' \
        'READ WRITE U64 HELPER CORE: PASS' \
        'PUSH POP STACK HELPER CORE: PASS' \
        'LOGIC FLAGS HELPER CORE: PASS' \
        'CONDITION HELPER CORE 16 OF 16: PASS' \
        'STICKY HELPER FAULT: PASS' \
        'AARCH64 HELPER WRAPPER EXECUTION: NOT RUN' \
        'WIN32 DEMAND GUEST MEMORY BINDING: NOT IMPLEMENTED' \
        'MULTI REGION GATEWAY CODE FETCH: NOT IMPLEMENTED' \
        'NATIVE ARM64 PE EXECUTION: NOT RUN'
    do
        count=$(grep -Fxc "$marker" "$log" || true)
        [ "$count" -eq 1 ] || {
            printf 'GUEST RUNTIME MARKER MISMATCH log=%s marker=[%s] count=%s\n' \
                "$log" "$marker" "$count" >&2
            exit 1
        }
    done
done

# shellcheck disable=SC2086
gcc -Iinclude -Isrc -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$guest_check" $guest_sources \
    -o "$build/guest-runtime-sanitize"
ASAN_OPTIONS=detect_leaks=1 "$build/guest-runtime-sanitize"

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
    "$guest_check" "$entry_source" "$terminal_source"
# shellcheck disable=SC2086
clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    "$test_source" $validation_sources

printf '%s\n' 'TRANSLATED ENTRY ASSEMBLY ABI: PASS'
printf '%s\n' 'TRANSLATED ENTRY RETURN ANCHOR X21: PASS'
printf '%s\n' 'AARCH64 NONTERMINAL HELPER WRAPPERS: CROSS-COMPILE PASS'
printf '%s\n' 'ARM64 GUEST MEMORY CORE VERIFY: PASS'
printf '%s\n' 'TRANSLATED TERMINAL EXIT RECORD VALIDATION: PASS'
printf '%s\n' 'TRANSLATED TERMINAL EXIT CACHE BINDING: PASS'
printf '%s\n' 'TRANSLATED ENTRY VALIDATION: PASS'
printf '%s\n' 'TRANSLATED ENTRY TRANSACTIONAL AUDIT: PASS'
printf '%s\n' 'TRANSLATED W^X PUBLISH ENTER RELEASE: PASS'
printf '%s\n' 'LINUX ARM64 TRANSLATED ENTRY VERIFY: PASS'
printf '%s\n' 'WIN32 DEMAND GUEST MEMORY BINDING: NOT IMPLEMENTED'
printf '%s\n' 'MULTI REGION GATEWAY CODE FETCH: NOT IMPLEMENTED'
printf '%s\n' 'NATIVE ARM64 PE EXECUTION: NOT RUN'
printf '%s\n' 'WINLATOR COMPARISON: NOT RUN'
