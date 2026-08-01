#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_script="$script_dir/build-local-wsl.sh"

for command_name in sudo apt-get apt-cache sed awk
 do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'MISSING BOOTSTRAP COMMAND: %s\n' "$command_name" >&2
        exit 64
    }
 done

printf '%s\n' 'BOOTSTRAPPING REQUIRED WSL BUILD PACKAGES'
sudo apt-get update

jdk_package='default-jdk-headless'

if ! apt-cache show "$jdk_package" >/dev/null 2>&1; then
    printf 'JDK PACKAGE NOT AVAILABLE: %s\n' "$jdk_package" >&2
    exit 65
fi

sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    git \
    "$jdk_package" \
    unzip \
    zip \
    binutils

for command_name in \
    git \
    curl \
    unzip \
    sha256sum \
    sed \
    grep \
    awk \
    strings \
    find \
    sort \
    xargs \
    javac \
    java \
    readlink
 do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'BOOTSTRAP COMMAND STILL MISSING: %s\n' "$command_name" >&2
        exit 66
    }
 done

javac_version=$(javac -version 2>&1 | sed -n '1p')
java_major=$(printf '%s\n' "$javac_version" |
    awk '{print $2}' |
    sed 's/^1\.//' |
    sed 's/[^0-9].*$//')

case "$java_major" in
    ''|*[!0-9]*)
        printf 'UNPARSEABLE JAVAC VERSION: %s\n' "$javac_version" >&2
        exit 67
        ;;
esac

if [ "$java_major" -lt 17 ]; then
    printf 'JDK TOO OLD: major=%s required_minimum=17\n' \
        "$java_major" >&2
    exit 68
fi

if [ ! -f "$build_script" ]; then
    printf 'LOCAL BUILD SCRIPT MISSING: %s\n' "$build_script" >&2
    exit 69
fi

printf 'WSL_JDK_PACKAGE=%s\n' "$jdk_package"
printf 'WSL_JAVA_MAJOR=%s\n' "$java_major"
printf 'WSL_JAVAC=%s\n' "$javac_version"
printf '%s\n' 'WSL BUILD PACKAGE BOOTSTRAP: PASS'
exec /bin/sh "$build_script" "$@"
