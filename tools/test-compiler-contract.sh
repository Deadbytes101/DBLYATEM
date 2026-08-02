#!/bin/sh
set -eu

case ${0##*/} in
    gcc)
        compiler=${DB_TEST_REAL_GCC:?missing DB_TEST_REAL_GCC}
        ;;
    clang)
        compiler=${DB_TEST_REAL_CLANG:?missing DB_TEST_REAL_CLANG}
        ;;
    *)
        printf 'TEST COMPILER CONTRACT: unsupported compiler name: %s\n' \
            "${0##*/}" >&2
        exit 2
        ;;
esac

root=${DB_TEST_ROOT:?missing DB_TEST_ROOT}
header=${DB_TEST_WRITE_CONTRACT:?missing DB_TEST_WRITE_CONTRACT}

[ -x "$compiler" ] || {
    printf 'TEST COMPILER CONTRACT: compiler is not executable: %s\n' \
        "$compiler" >&2
    exit 2
}

[ -f "$header" ] || {
    printf 'TEST COMPILER CONTRACT: header is missing: %s\n' \
        "$header" >&2
    exit 2
}

inject=0
for argument do
    case "$argument" in
        tests/*_test.c|./tests/*_test.c|"$root"/tests/*_test.c)
            inject=1
            ;;
    esac
done

if [ "$inject" -eq 1 ]; then
    exec "$compiler" -include "$header" "$@"
fi

exec "$compiler" "$@"
