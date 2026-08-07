#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/win32-kernel32-gateway-verify"
common='-Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
sources='src/win32_provider.c src/win32_kernel32_gateway.c src/win32_kernel32_invoke.c src/win32_kernel32.c src/gateway_auth_complete.c src/gateway_result.c src/gateway_complete.c src/gateway_return.c src/gateway_return_apply.c'
runtime_sources="$sources src/win32_runtime_dispatch.c"

rm -rf "$build"
mkdir -p "$build"
cd "$root"

gcc $common tests/win32_kernel32_gateway_test.c $sources -o "$build/gcc-test"
"$build/gcc-test"

clang $common tests/win32_kernel32_gateway_test.c $sources -o "$build/clang-test"
"$build/clang-test"

gcc $common tests/win32_exit_process_boundary_test.c $sources \
    -o "$build/gcc-exit-process-test"
"$build/gcc-exit-process-test"

clang $common tests/win32_exit_process_boundary_test.c $sources \
    -o "$build/clang-exit-process-test"
"$build/clang-exit-process-test"

gcc $common tests/win32_runtime_dispatch_test.c $runtime_sources \
    -o "$build/gcc-runtime-dispatch-test"
"$build/gcc-runtime-dispatch-test"

clang $common tests/win32_runtime_dispatch_test.c $runtime_sources \
    -o "$build/clang-runtime-dispatch-test"
"$build/clang-runtime-dispatch-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/win32_kernel32_gateway_test.c $sources \
    -o "$build/sanitizer-test"
"$build/sanitizer-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/win32_exit_process_boundary_test.c $sources \
    -o "$build/sanitizer-exit-process-test"
"$build/sanitizer-exit-process-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/win32_runtime_dispatch_test.c $runtime_sources \
    -o "$build/sanitizer-runtime-dispatch-test"
"$build/sanitizer-runtime-dispatch-test"

clang --analyze -Iinclude -std=c11 -Wall -Wextra -Wpedantic -Werror \
    src/win32_provider.c src/win32_kernel32_gateway.c \
    src/win32_kernel32_invoke.c src/win32_kernel32.c \
    src/win32_runtime_dispatch.c \
    src/gateway_auth_complete.c src/gateway_result.c \
    src/gateway_complete.c src/gateway_return.c \
    src/gateway_return_apply.c tests/win32_kernel32_gateway_test.c \
    tests/win32_exit_process_boundary_test.c \
    tests/win32_runtime_dispatch_test.c

printf '%s\n' 'WIN32 KERNEL32 GATEWAY VERIFY: PASS'
