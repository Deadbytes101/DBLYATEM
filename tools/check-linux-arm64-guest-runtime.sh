#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/linux-arm64-guest-runtime-check"
common='-Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
check='tests/linux_arm64_guest_runtime_check.c'
entry='src/linux_arm64_translation_entry.c'
terminal='src/translated_terminal_exit.c'
asm='src/linux_arm64_translation_entry_aarch64.S'
sources="$entry $terminal"

rm -rf "$build"
mkdir -p "$build"
cd "$root"

printf '%s\n' 'ARM64_GUEST_RUNTIME_STAGE=static-contract'
for command_name in gcc clang readelf grep; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'ARM64_GUEST_RUNTIME_MISSING_COMMAND=%s\n' "$command_name" >&2
        exit 1
    }
done

for file in \
    include/dblyat/linux_arm64_guest_runtime.h \
    include/dblyat/linux_arm64_translation_entry.h \
    src/linux_arm64_guest_runtime_internal.h \
    "$entry" "$asm" "$check" \
    tools/verify-linux-arm64-translation-entry.sh
do
    [ -f "$file" ] || {
        printf 'ARM64_GUEST_RUNTIME_MISSING_FILE=%s\n' "$file" >&2
        exit 1
    }
done

[ ! -f src/linux_arm64_guest_runtime.c ] || {
    printf '%s\n' 'ARM64_GUEST_RUNTIME_DUPLICATE_C_IMPLEMENTATION' >&2
    exit 1
}
[ ! -f src/linux_arm64_guest_runtime_aarch64.S ] || {
    printf '%s\n' 'ARM64_GUEST_RUNTIME_DUPLICATE_ASM_IMPLEMENTATION' >&2
    exit 1
}
[ ! -f tests/linux_arm64_guest_runtime_test.c ] || {
    printf '%s\n' 'ARM64_GUEST_RUNTIME_IMPLICIT_WILDCARD_TEST_PRESENT' >&2
    exit 1
}

grep -F 'DB_LINUX_ARM64_GUEST_MEMORY_FORMAT_VERSION' \
    include/dblyat/linux_arm64_guest_runtime.h >/dev/null
grep -F 'db_linux_arm64_guest_memory_read_u64' \
    include/dblyat/linux_arm64_guest_runtime.h >/dev/null
grep -F 'db_linux_arm64_guest_memory_write_u64' \
    include/dblyat/linux_arm64_guest_runtime.h >/dev/null
grep -F 'db_linux_arm64_translation_context_bind_guest_memory' \
    include/dblyat/linux_arm64_translation_entry.h >/dev/null
grep -F 'guest_memory) == 144u' "$entry" >/dev/null
grep -F 'helper_fault_address) == 248u' "$entry" >/dev/null
grep -F 'sizeof(db_linux_arm64_translation_context) == 272u' \
    "$entry" >/dev/null
grep -F 'DB_LINUX_ARM64_TRANSLATION_ENTRY_HELPER_FAILED' \
    "$entry" >/dev/null
grep -F 'db_linux_arm64_guest_runtime_helper_targets' "$entry" >/dev/null
grep -F 'adr x21, .Ldb_linux_arm64_translation_block_return' \
    "$asm" >/dev/null
[ "$(grep -Fc 'br x21' "$asm")" -eq 4 ] || {
    printf '%s\n' 'ARM64_GUEST_RUNTIME_RETURN_ANCHOR_MISMATCH' >&2
    exit 1
}

grep -F 'mov x16, x0' "$asm" >/dev/null
[ "$(grep -Fc 'mov x15, x0' "$asm")" -ge 3 ] || {
    printf '%s\n' 'ARM64_GUEST_RUNTIME_TARGET_RESULT_MIRROR_MISSING' >&2
    exit 1
}
[ "$(grep -Fc 'mov x17, x0' "$asm")" -eq 2 ] || {
    printf '%s\n' 'ARM64_GUEST_RUNTIME_SECONDARY_RESULT_MIRROR_MISSING' >&2
    exit 1
}

if grep -E '(^|[^[:alnum:]_])(malloc|calloc|realloc|free|mmap|mprotect|munmap|VirtualAlloc|VirtualProtect|pthread_|atomic_|__atomic|__sync)([^[:alnum:]_]|$)' \
    include/dblyat/linux_arm64_guest_runtime.h \
    include/dblyat/linux_arm64_translation_entry.h \
    src/linux_arm64_guest_runtime_internal.h \
    "$entry" "$asm" >/dev/null
then
    printf '%s\n' 'ARM64_GUEST_RUNTIME_CROSSED_OWNERSHIP_MAPPING_OR_CONCURRENCY_BOUNDARY' >&2
    exit 1
fi

printf '%s\n' 'ARM64_GUEST_RUNTIME_STAGE=compile-gcc'
set +e
gcc $common "$check" $sources -o "$build/gcc-check" \
    >"$build/gcc-compile.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_GUEST_RUNTIME_COMPILE_FAILED compiler=gcc exit=%s\n' "$status" >&2
    cat "$build/gcc-compile.log" >&2
    exit "$status"
fi

printf '%s\n' 'ARM64_GUEST_RUNTIME_STAGE=run-gcc'
set +e
"$build/gcc-check" >"$build/gcc.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_GUEST_RUNTIME_TEST_FAILED compiler=gcc exit=%s\n' "$status" >&2
    cat "$build/gcc.log" >&2
    exit "$status"
fi

printf '%s\n' 'ARM64_GUEST_RUNTIME_STAGE=compile-clang'
set +e
clang $common "$check" $sources -o "$build/clang-check" \
    >"$build/clang-compile.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_GUEST_RUNTIME_COMPILE_FAILED compiler=clang exit=%s\n' "$status" >&2
    cat "$build/clang-compile.log" >&2
    exit "$status"
fi

printf '%s\n' 'ARM64_GUEST_RUNTIME_STAGE=run-clang'
set +e
"$build/clang-check" >"$build/clang.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_GUEST_RUNTIME_TEST_FAILED compiler=clang exit=%s\n' "$status" >&2
    cat "$build/clang.log" >&2
    exit "$status"
fi

printf '%s\n' 'ARM64_GUEST_RUNTIME_STAGE=marker-validation'
for log in "$build/gcc.log" "$build/clang.log"; do
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
            printf 'ARM64_GUEST_RUNTIME_MARKER_MISMATCH log=%s marker=[%s] count=%s\n' \
                "$log" "$marker" "$count" >&2
            cat "$log" >&2
            exit 1
        }
    done
done

printf '%s\n' 'ARM64_GUEST_RUNTIME_STAGE=compile-sanitize'
set +e
gcc -Iinclude -Isrc -std=c11 -O1 -g \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$check" $sources -o "$build/sanitize-check" \
    >"$build/sanitize-compile.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_GUEST_RUNTIME_SANITIZER_COMPILE_FAILED exit=%s\n' "$status" >&2
    cat "$build/sanitize-compile.log" >&2
    exit "$status"
fi

printf '%s\n' 'ARM64_GUEST_RUNTIME_STAGE=run-sanitize'
set +e
ASAN_OPTIONS=detect_leaks=1 "$build/sanitize-check" \
    >"$build/sanitize.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_GUEST_RUNTIME_SANITIZER_TEST_FAILED exit=%s\n' "$status" >&2
    cat "$build/sanitize.log" >&2
    exit "$status"
fi

printf '%s\n' 'ARM64_GUEST_RUNTIME_STAGE=clang-analyze'
set +e
clang --analyze -Iinclude -Isrc -std=c11 \
    -Wall -Wextra -Wpedantic -Werror -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes \
    "$check" "$entry" "$terminal" \
    >"$build/analyze.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_GUEST_RUNTIME_CLANG_ANALYZE_FAILED exit=%s\n' "$status" >&2
    cat "$build/analyze.log" >&2
    exit "$status"
fi

printf '%s\n' 'ARM64_GUEST_RUNTIME_STAGE=aarch64-wrapper-cross-compile'
set +e
clang --target=aarch64-linux-gnu -c "$asm" -o "$build/wrappers.o" \
    >"$build/wrappers-compile.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_GUEST_RUNTIME_WRAPPER_COMPILE_FAILED exit=%s\n' "$status" >&2
    cat "$build/wrappers-compile.log" >&2
    exit "$status"
fi

for symbol in \
    db_linux_arm64_helper_form_guest_address \
    db_linux_arm64_helper_read_guest_u64 \
    db_linux_arm64_helper_write_guest_u64 \
    db_linux_arm64_helper_push_guest_u64 \
    db_linux_arm64_helper_pop_guest_u64 \
    db_linux_arm64_helper_update_logic_flags \
    db_linux_arm64_helper_test_guest_condition
do
    count=$(readelf -sW "$build/wrappers.o" |
        grep -Ec "FUNC[[:space:]]+GLOBAL.*${symbol}$" || true)
    [ "$count" -eq 1 ] || {
        printf 'ARM64_GUEST_RUNTIME_WRAPPER_SYMBOL_MISMATCH symbol=%s count=%s\n' \
            "$symbol" "$count" >&2
        exit 1
    }
done

printf '%s\n' 'ARM64_GUEST_RUNTIME_STAGE=canonical-entry-regression'
set +e
sh tools/verify-linux-arm64-translation-entry.sh \
    >"$build/canonical-entry.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'ARM64_GUEST_RUNTIME_CANONICAL_ENTRY_FAILED exit=%s\n' "$status" >&2
    tail -n 160 "$build/canonical-entry.log" >&2
    exit "$status"
fi
for marker in \
    'TRANSLATED ENTRY RETURN ANCHOR X21: PASS' \
    'AARCH64 NONTERMINAL HELPER WRAPPERS: CROSS-COMPILE PASS' \
    'ARM64 GUEST MEMORY CORE VERIFY: PASS' \
    'LINUX ARM64 TRANSLATED ENTRY VERIFY: PASS'
do
    count=$(grep -Fxc "$marker" "$build/canonical-entry.log" || true)
    [ "$count" -eq 1 ] || {
        printf 'ARM64_GUEST_RUNTIME_CANONICAL_MARKER_MISMATCH marker=[%s] count=%s\n' \
            "$marker" "$count" >&2
        exit 1
    }
done

printf '%s\n' 'LINUX ARM64 GUEST RUNTIME FOCUSED VERIFY: PASS'
printf '%s\n' 'GUEST ADDRESS SPACE BOUNDS: PASS'
printf '%s\n' 'IMAGE PAGE PERMISSIONS: PASS'
printf '%s\n' 'STACK GATEWAY REGION PERMISSIONS: PASS'
printf '%s\n' 'FORM READ WRITE PUSH POP HELPERS: PASS'
printf '%s\n' 'FLAGS CONDITIONS HELPERS: PASS'
printf '%s\n' 'STICKY HELPER FAULT: PASS'
printf '%s\n' 'TRANSLATED ENTRY RETURN ANCHOR X21: PASS'
printf '%s\n' 'AARCH64 HELPER WRAPPER ABI: CROSS COMPILE PASS'
printf '%s\n' 'COMPLETE NATIVE HELPER TARGET TABLE: STATIC CONTRACT PASS'
printf '%s\n' 'WIN32 DEMAND GUEST MEMORY BINDING: NOT IMPLEMENTED'
printf '%s\n' 'MULTI REGION GATEWAY CODE FETCH: NOT IMPLEMENTED'
printf '%s\n' 'NATIVE ARM64 HELPER EXECUTION: NOT RUN'
printf '%s\n' 'NATIVE ARM64 PE EXECUTION: NOT RUN'
printf '%s\n' 'HOST RUN CLI: NOT IMPLEMENTED'
printf '%s\n' 'DBLYATEM IDENTICAL PE EXECUTION: NOT RUN'
printf '%s\n' 'WINLATOR COMPARISON: NOT RUN'
