#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_script="$script_dir/build-local-wsl.sh"

for command_name in sudo apt-get
 do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'MISSING BOOTSTRAP COMMAND: %s\n' "$command_name" >&2
        exit 64
    }
 done

printf '%s\n' 'BOOTSTRAPPING REQUIRED WSL BUILD PACKAGES'
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    git \
    openjdk-17-jdk-headless \
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
    java
 do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'BOOTSTRAP COMMAND STILL MISSING: %s\n' "$command_name" >&2
        exit 65
    }
 done

if [ ! -f "$build_script" ]; then
    printf 'LOCAL BUILD SCRIPT MISSING: %s\n' "$build_script" >&2
    exit 66
fi

printf '%s\n' 'WSL BUILD PACKAGE BOOTSTRAP: PASS'
exec /bin/sh "$build_script" "$@"
