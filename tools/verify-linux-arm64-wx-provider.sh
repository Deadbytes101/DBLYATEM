#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/linux-arm64-wx-provider-verify"
common='-Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
sources='src/linux_arm64_wx_provider.c'
posix_sources='src/linux_arm64_wx_provider.c src/linux_arm64_wx_posix.c'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

for command_name in grep uname; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'LINUX ARM64 W^X VERIFY: missing command: %s\n' \
            "$command_name" >&2
        exit 1
    }
done

grep -F 'PROT_READ | PROT_WRITE' src/linux_arm64_wx_posix.c >/dev/null
grep -F 'PROT_READ | PROT_EXEC' src/linux_arm64_wx_posix.c >/dev/null
grep -F 'MAP_FIXED_NOREPLACE' src/linux_arm64_wx_posix.c >/dev/null
grep -F '__builtin___clear_cache' src/linux_arm64_wx_posix.c >/dev/null

if grep -E 'PROT_WRITE[^\n]*PROT_EXEC|PROT_EXEC[^\n]*PROT_WRITE|MAP_JIT' \
    src/linux_arm64_wx_posix.c >/dev/null
then
    printf '%s\n' 'LINUX ARM64 W^X PROVIDER ALLOWS RWX OR MAP_JIT' >&2
    exit 1
fi

machine=$(uname -m)
is_android=0
if [ -x /system/bin/getprop ]; then
    is_android=1
else
    operating_system=$(uname -o 2>/dev/null || printf '%s' unknown)
    [ "$operating_system" = Android ] && is_android=1
fi

if [ "$is_android" -eq 1 ]; then
    case "$machine" in
        aarch64|arm64) ;;
        *)
            printf 'ANDROID ARM64 VERIFY: unsupported machine: %s\n' \
                "$machine" >&2
            exit 1
            ;;
    esac

    compiler=${CC:-clang}
    command -v "$compiler" >/dev/null 2>&1 || {
        printf 'ANDROID ARM64 VERIFY: missing compiler: %s\n' \
            "$compiler" >&2
        exit 1
    }

    "$compiler" $common tests/linux_arm64_wx_provider_test.c $sources \
        -o "$build/android-linux-arm64-wx-provider-test"
    "$build/android-linux-arm64-wx-provider-test"

    "$compiler" $common tests/linux_arm64_wx_posix_test.c $posix_sources \
        -o "$build/android-linux-arm64-wx-posix-test"
    "$build/android-linux-arm64-wx-posix-test"

    "$compiler" $common tests/linux_arm64_wx_execute_test.c $posix_sources \
        -o "$build/android-linux-arm64-wx-execute-test"
    "$build/android-linux-arm64-wx-execute-test"

    printf '%s\n' 'ANDROID ARM64 RUNTIME EVIDENCE: PASS'
else
    for command_name in gcc clang; do
        command -v "$command_name" >/dev/null 2>&1 || {
            printf 'LINUX ARM64 W^X VERIFY: missing command: %s\n' \
                "$command_name" >&2
            exit 1
        }
    done

    gcc $common tests/linux_arm64_wx_provider_test.c $sources \
        -o "$build/gcc-linux-arm64-wx-provider-test"
    "$build/gcc-linux-arm64-wx-provider-test"

    clang $common tests/linux_arm64_wx_provider_test.c $sources \
        -o "$build/clang-linux-arm64-wx-provider-test"
    "$build/clang-linux-arm64-wx-provider-test"

    gcc -Iinclude -std=c11 -O1 -g \
        -Wall -Wextra -Wpedantic -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        tests/linux_arm64_wx_provider_test.c $sources \
        -o "$build/linux-arm64-wx-provider-sanitizer-test"
    "$build/linux-arm64-wx-provider-sanitizer-test"

    clang --analyze -Iinclude -std=c11 \
        -Wall -Wextra -Wpedantic -Werror \
        $sources src/linux_arm64_wx_posix.c \
        tests/linux_arm64_wx_provider_test.c \
        tests/linux_arm64_wx_posix_test.c

    gcc $common tests/linux_arm64_wx_posix_test.c $posix_sources \
        -o "$build/gcc-linux-arm64-wx-posix-test"
    "$build/gcc-linux-arm64-wx-posix-test"

    clang $common tests/linux_arm64_wx_posix_test.c $posix_sources \
        -o "$build/clang-linux-arm64-wx-posix-test"
    "$build/clang-linux-arm64-wx-posix-test"

    case "$machine" in
        aarch64|arm64)
            gcc $common tests/linux_arm64_wx_execute_test.c $posix_sources \
                -o "$build/linux-arm64-wx-execute-test"
            "$build/linux-arm64-wx-execute-test"
            ;;
        *)
            printf '%s\n' 'NATIVE AARCH64 EXECUTION: NOT CLAIMED'
            ;;
    esac

    printf '%s\n' 'ANDROID ARM64 RUNTIME EVIDENCE: NOT CLAIMED'
fi

printf '%s\n' 'NO RWX TRANSITION VERIFY: PASS'
printf '%s\n' 'LINUX ANDROID ARM64 W^X PROVIDER VERIFY: PASS'
