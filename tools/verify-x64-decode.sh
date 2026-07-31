#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/x64-decode-verify"
common='-Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

rm -rf "$build"
mkdir -p "$build"
cd "$root"

gcc $common tests/x64_decode_test.c src/x64_decode.c \
    -o "$build/gcc-decode-test"
"$build/gcc-decode-test"

clang $common tests/x64_decode_test.c src/x64_decode.c \
    -o "$build/clang-decode-test"
"$build/clang-decode-test"

gcc $common tests/x64_interpret_test.c src/x64_interpret.c \
    -o "$build/gcc-interpret-test"
"$build/gcc-interpret-test"

clang $common tests/x64_interpret_test.c src/x64_interpret.c \
    -o "$build/clang-interpret-test"
"$build/clang-interpret-test"

gcc $common tests/x64_execute_test.c src/x64_execute.c \
    src/x64_decode.c src/x64_interpret.c \
    -o "$build/gcc-execute-test"
"$build/gcc-execute-test"

clang $common tests/x64_execute_test.c src/x64_execute.c \
    src/x64_decode.c src/x64_interpret.c \
    -o "$build/clang-execute-test"
"$build/clang-execute-test"

gcc $common tests/x64_address_test.c src/x64_address.c \
    -o "$build/gcc-address-test"
"$build/gcc-address-test"

clang $common tests/x64_address_test.c src/x64_address.c \
    -o "$build/clang-address-test"
"$build/clang-address-test"

gcc $common tests/x64_memory_decode_test.c src/x64_memory_decode.c \
    -o "$build/gcc-memory-decode-test"
"$build/gcc-memory-decode-test"

clang $common tests/x64_memory_decode_test.c src/x64_memory_decode.c \
    -o "$build/clang-memory-decode-test"
"$build/clang-memory-decode-test"

gcc $common tests/x64_memory_instruction_test.c \
    src/x64_memory_instruction.c src/x64_memory_decode.c \
    -o "$build/gcc-memory-instruction-test"
"$build/gcc-memory-instruction-test"

clang $common tests/x64_memory_instruction_test.c \
    src/x64_memory_instruction.c src/x64_memory_decode.c \
    -o "$build/clang-memory-instruction-test"
"$build/clang-memory-instruction-test"

gcc $common tests/x64_memory_view_test.c src/x64_memory_view.c \
    -o "$build/gcc-memory-view-test"
"$build/gcc-memory-view-test"

clang $common tests/x64_memory_view_test.c src/x64_memory_view.c \
    -o "$build/clang-memory-view-test"
"$build/clang-memory-view-test"

gcc $common tests/x64_memory_execute_test.c \
    src/x64_memory_execute.c src/x64_memory_instruction.c \
    src/x64_memory_decode.c src/x64_address.c src/x64_memory_view.c \
    -o "$build/gcc-memory-execute-test"
"$build/gcc-memory-execute-test"

clang $common tests/x64_memory_execute_test.c \
    src/x64_memory_execute.c src/x64_memory_instruction.c \
    src/x64_memory_decode.c src/x64_address.c src/x64_memory_view.c \
    -o "$build/clang-memory-execute-test"
"$build/clang-memory-execute-test"

gcc $common tests/x64_stack_execute_test.c \
    src/x64_stack_execute.c src/x64_decode.c src/x64_memory_view.c \
    -o "$build/gcc-stack-execute-test"
"$build/gcc-stack-execute-test"

clang $common tests/x64_stack_execute_test.c \
    src/x64_stack_execute.c src/x64_decode.c src/x64_memory_view.c \
    -o "$build/clang-stack-execute-test"
"$build/clang-stack-execute-test"

gcc $common tests/x64_run_test.c \
    src/x64_run.c src/x64_execute.c src/x64_decode.c \
    src/x64_interpret.c src/x64_memory_execute.c \
    src/x64_memory_instruction.c src/x64_memory_decode.c \
    src/x64_address.c src/x64_memory_view.c src/x64_stack_execute.c \
    -o "$build/gcc-run-test"
"$build/gcc-run-test"

clang $common tests/x64_run_test.c \
    src/x64_run.c src/x64_execute.c src/x64_decode.c \
    src/x64_interpret.c src/x64_memory_execute.c \
    src/x64_memory_instruction.c src/x64_memory_decode.c \
    src/x64_address.c src/x64_memory_view.c src/x64_stack_execute.c \
    -o "$build/clang-run-test"
"$build/clang-run-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_decode_test.c src/x64_decode.c \
    -o "$build/decode-sanitizer-test"
"$build/decode-sanitizer-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_interpret_test.c src/x64_interpret.c \
    -o "$build/interpret-sanitizer-test"
"$build/interpret-sanitizer-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_execute_test.c src/x64_execute.c \
    src/x64_decode.c src/x64_interpret.c \
    -o "$build/execute-sanitizer-test"
"$build/execute-sanitizer-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_address_test.c src/x64_address.c \
    -o "$build/address-sanitizer-test"
"$build/address-sanitizer-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_memory_decode_test.c src/x64_memory_decode.c \
    -o "$build/memory-decode-sanitizer-test"
"$build/memory-decode-sanitizer-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_memory_instruction_test.c \
    src/x64_memory_instruction.c src/x64_memory_decode.c \
    -o "$build/memory-instruction-sanitizer-test"
"$build/memory-instruction-sanitizer-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_memory_view_test.c src/x64_memory_view.c \
    -o "$build/memory-view-sanitizer-test"
"$build/memory-view-sanitizer-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_memory_execute_test.c \
    src/x64_memory_execute.c src/x64_memory_instruction.c \
    src/x64_memory_decode.c src/x64_address.c src/x64_memory_view.c \
    -o "$build/memory-execute-sanitizer-test"
"$build/memory-execute-sanitizer-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_stack_execute_test.c \
    src/x64_stack_execute.c src/x64_decode.c src/x64_memory_view.c \
    -o "$build/stack-execute-sanitizer-test"
"$build/stack-execute-sanitizer-test"

gcc -Iinclude -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/x64_run_test.c \
    src/x64_run.c src/x64_execute.c src/x64_decode.c \
    src/x64_interpret.c src/x64_memory_execute.c \
    src/x64_memory_instruction.c src/x64_memory_decode.c \
    src/x64_address.c src/x64_memory_view.c src/x64_stack_execute.c \
    -o "$build/run-sanitizer-test"
"$build/run-sanitizer-test"

clang --analyze -Iinclude -std=c11 -Wall -Wextra -Wpedantic -Werror \
    src/x64_decode.c tests/x64_decode_test.c \
    src/x64_interpret.c tests/x64_interpret_test.c \
    src/x64_execute.c tests/x64_execute_test.c \
    src/x64_address.c tests/x64_address_test.c \
    src/x64_memory_decode.c tests/x64_memory_decode_test.c \
    src/x64_memory_instruction.c tests/x64_memory_instruction_test.c \
    src/x64_memory_view.c tests/x64_memory_view_test.c \
    src/x64_memory_execute.c tests/x64_memory_execute_test.c \
    src/x64_stack_execute.c tests/x64_stack_execute_test.c \
    src/x64_run.c tests/x64_run_test.c

printf '%s\n' 'X64 DECODE + INTERPRET VERIFY: PASS'
printf '%s\n' 'X64 DECODE + INTERPRET + EXECUTE VERIFY: PASS'
printf '%s\n' 'X64 ADDRESS VERIFY: PASS'
printf '%s\n' 'X64 MEMORY DECODE VERIFY: PASS'
printf '%s\n' 'X64 MEMORY INSTRUCTION VERIFY: PASS'
printf '%s\n' 'X64 MEMORY VIEW VERIFY: PASS'
printf '%s\n' 'X64 MEMORY EXECUTE VERIFY: PASS'
printf '%s\n' 'X64 STACK EXECUTE VERIFY: PASS'
printf '%s\n' 'X64 RUN VERIFY: PASS'
printf '%s\n' 'X64 CORE VERIFY: PASS'
