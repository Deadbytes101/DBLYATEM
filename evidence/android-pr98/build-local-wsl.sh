#!/bin/sh
set -eu

feature_remote='https://github.com/Deadbytes101/DBLYATEM.git'
feature_branch='deadbyte/linux-arm64-wx-provider'
expected_feature_head='8886fe231eaaefd9c28ed6fe9a7dc404739cc566'

command_tools_revision='15859902'
command_tools_sha256='4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583'
command_tools_url="https://dl.google.com/android/repository/commandlinetools-linux-${command_tools_revision}_latest.zip"

gradle_version='9.4.1'
gradle_url="https://services.gradle.org/distributions/gradle-${gradle_version}-bin.zip"
gradle_sha_url="${gradle_url}.sha256"

output_dir=${1:-/mnt/d/TECHNICAL/DblyatEM/build/pr100-android-kit}
work_root=${DBLYATEM_ANDROID_WORK_ROOT:-"${HOME:?HOME_NOT_SET}/.cache/dblyatem-pr100-android"}
sdk_root=${ANDROID_SDK_ROOT:-"$work_root/android-sdk"}
tools_root="$work_root/tools"
download_root="$work_root/downloads"
feature_root="$work_root/feature-src"
gradle_root="$tools_root/gradle-${gradle_version}"

harness_root=$(git rev-parse --show-toplevel 2>/dev/null || true)

if [ -z "$harness_root" ]; then
    printf '%s\n' 'BUILD SCRIPT MUST RUN INSIDE THE PR100 HARNESS CHECKOUT' >&2
    exit 64
fi

harness_head=$(git -C "$harness_root" rev-parse HEAD)
harness_dirty=$(git -C "$harness_root" status --porcelain --untracked-files=no)

if [ -n "$harness_dirty" ]; then
    printf '%s\n' 'PR100 HARNESS CHECKOUT IS DIRTY' >&2
    printf '%s\n' "$harness_dirty" >&2
    exit 65
fi

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
        printf 'MISSING BOOTSTRAPPED HOST COMMAND: %s\n' "$command_name" >&2
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

printf 'OBSERVED BOOTSTRAPPED JDK MAJOR=%s\n' "$java_major"

JAVA_HOME=$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")
export JAVA_HOME
export ANDROID_SDK_ROOT="$sdk_root"
export ANDROID_HOME="$sdk_root"

mkdir -p "$download_root" "$tools_root" "$sdk_root/cmdline-tools"

command_tools_zip="$download_root/commandlinetools-linux-${command_tools_revision}_latest.zip"
command_tools_marker="$sdk_root/cmdline-tools/.dblyatem-${command_tools_revision}"

if [ ! -f "$command_tools_marker" ]; then
    printf '%s\n' 'INSTALLING PINNED ANDROID COMMAND-LINE TOOLS'
    rm -rf "$sdk_root/cmdline-tools/latest" "$work_root/cmdline-tools-stage"
    curl -fL --retry 3 --retry-delay 2 \
        "$command_tools_url" \
        -o "$command_tools_zip"
    printf '%s  %s\n' \
        "$command_tools_sha256" \
        "$command_tools_zip" |
        sha256sum -c -
    mkdir -p "$work_root/cmdline-tools-stage"
    unzip -q "$command_tools_zip" -d "$work_root/cmdline-tools-stage"
    test -x "$work_root/cmdline-tools-stage/cmdline-tools/bin/sdkmanager"
    mv \
        "$work_root/cmdline-tools-stage/cmdline-tools" \
        "$sdk_root/cmdline-tools/latest"
    rm -rf "$work_root/cmdline-tools-stage"
    : > "$command_tools_marker"
fi

sdkmanager="$sdk_root/cmdline-tools/latest/bin/sdkmanager"

if [ ! -x "$sdkmanager" ]; then
    printf 'SDKMANAGER NOT FOUND: %s\n' "$sdkmanager" >&2
    exit 69
fi

export PATH="$sdk_root/cmdline-tools/latest/bin:$sdk_root/platform-tools:$PATH"

printf '%s\n' 'ACCEPTING ANDROID SDK LICENSES'
yes | "$sdkmanager" --sdk_root="$sdk_root" --licenses >/dev/null

printf '%s\n' 'INSTALLING PINNED ANDROID SDK PACKAGES'
"$sdkmanager" --sdk_root="$sdk_root" \
    'platform-tools' \
    'platforms;android-36' \
    'build-tools;36.0.0' \
    'ndk;28.2.13676358' \
    'cmake;3.22.1'

gradle_zip="$download_root/gradle-${gradle_version}-bin.zip"
gradle_sha_file="$download_root/gradle-${gradle_version}-bin.zip.sha256"

if [ ! -x "$gradle_root/bin/gradle" ]; then
    printf '%s\n' 'INSTALLING PINNED GRADLE DISTRIBUTION'
    rm -rf "$gradle_root"
    curl -fL --retry 3 --retry-delay 2 \
        "$gradle_url" \
        -o "$gradle_zip"
    curl -fL --retry 3 --retry-delay 2 \
        "$gradle_sha_url" \
        -o "$gradle_sha_file"
    gradle_sha=$(tr -d '[:space:]' < "$gradle_sha_file")
    case "$gradle_sha" in
        [0-9a-fA-F][0-9a-fA-F]*) ;;
        *)
            printf '%s\n' 'INVALID GRADLE SHA256 RESPONSE' >&2
            cat "$gradle_sha_file" >&2
            exit 70
            ;;
    esac
    printf '%s  %s\n' "$gradle_sha" "$gradle_zip" |
        sha256sum -c -
    unzip -q "$gradle_zip" -d "$tools_root"
fi

gradle="$gradle_root/bin/gradle"

if [ ! -x "$gradle" ]; then
    printf 'GRADLE NOT FOUND: %s\n' "$gradle" >&2
    exit 71
fi

printf '%s\n' 'CLONING EXACT PR98 FEATURE HEAD'
rm -rf "$feature_root"
git -c core.autocrlf=false clone \
    --no-tags \
    --single-branch \
    --branch "$feature_branch" \
    "$feature_remote" \
    "$feature_root"

feature_head=$(git -C "$feature_root" rev-parse HEAD)
feature_dirty=$(git -C "$feature_root" status --porcelain --untracked-files=no)

if [ "$feature_head" != "$expected_feature_head" ]; then
    printf '%s\n' 'WRONG PR98 FEATURE HEAD' >&2
    printf 'EXPECTED=%s\n' "$expected_feature_head" >&2
    printf 'ACTUAL=%s\n' "$feature_head" >&2
    exit 72
fi

if [ -n "$feature_dirty" ]; then
    printf '%s\n' 'FRESH PR98 FEATURE CHECKOUT IS DIRTY' >&2
    printf '%s\n' "$feature_dirty" >&2
    exit 73
fi

project_root="$harness_root/evidence/android-pr98"
app_source="$project_root/app/build/outputs/apk/debug/app-debug.apk"
test_source="$project_root/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk"

if [ ! -f "$project_root/settings.gradle" ]; then
    printf 'ANDROID PROJECT NOT FOUND: %s\n' "$project_root" >&2
    exit 74
fi

printf 'sdk.dir=%s\n' "$sdk_root" > "$project_root/local.properties"

rm -rf "$output_dir"
mkdir -p "$output_dir/android-build-evidence/apks"
build_log="$output_dir/gradle-build.log"

printf '%s\n' 'BUILDING EXACT-HEAD ARM64 APP AND INSTRUMENTATION APKS'
set +e
(
    cd "$project_root"
    "$gradle" \
        --no-daemon \
        --stacktrace \
        -PdblyatemFeatureDir="$feature_root" \
        :app:clean \
        :app:assembleDebug \
        :app:assembleDebugAndroidTest
) > "$build_log" 2>&1
build_status=$?
set -e

cat "$build_log"

if [ "$build_status" -ne 0 ]; then
    printf 'ANDROID GRADLE BUILD EXIT=%s\n' "$build_status" >&2
    exit "$build_status"
fi

for artifact in "$app_source" "$test_source"
 do
    [ -f "$artifact" ] || {
        printf 'BUILT APK MISSING: %s\n' "$artifact" >&2
        exit 75
    }
 done

app_apk="$output_dir/android-build-evidence/apks/pr98-evidence-app.apk"
test_apk="$output_dir/android-build-evidence/apks/pr98-evidence-test.apk"

cp "$app_source" "$app_apk"
cp "$test_source" "$test_apk"
cp "$project_root/run-physical-device.sh" "$output_dir/run-physical-device.sh"

chmod +x "$output_dir/run-physical-device.sh"

unzip -l "$app_apk" > "$output_dir/android-build-evidence/app-apk-entries.txt"
unzip -l "$test_apk" > "$output_dir/android-build-evidence/test-apk-entries.txt"

grep -F 'lib/arm64-v8a/libpr98evidence.so' \
    "$output_dir/android-build-evidence/app-apk-entries.txt" >/dev/null || {
        printf '%s\n' 'APP APK LACKS ARM64 EVIDENCE LIBRARY' >&2
        exit 76
    }

if grep -E 'lib/(x86|x86_64|armeabi-v7a)/' \
        "$output_dir/android-build-evidence/app-apk-entries.txt" >/dev/null
then
    printf '%s\n' 'APP APK CONTAINS FORBIDDEN NON-ARM64 ABI' >&2
    exit 77
fi

unzip -p "$app_apk" lib/arm64-v8a/libpr98evidence.so |
    strings |
    grep -F "$expected_feature_head" >/dev/null || {
        printf '%s\n' 'APK DOES NOT EMBED EXACT PR98 FEATURE HEAD' >&2
        exit 78
    }

if [ -f /mnt/d/TECHNICAL/DblyatEM/build/pr100-physical-device-preflight/device-preflight.txt ]; then
    cp \
        /mnt/d/TECHNICAL/DblyatEM/build/pr100-physical-device-preflight/device-preflight.txt \
        "$output_dir/device-preflight.txt"
fi

java_version=$(java -version 2>&1 | sed -n '1p')
gradle_version_line=$("$gradle" --version | sed -n '/^Gradle /p' | sed -n '1p')
sdkmanager_version=$("$sdkmanager" --version | sed -n '1p')

{
    printf '%s\n' 'DBLYATEM_PR98_ANDROID_LOCAL_BUILD_V1'
    printf 'HARNESS_HEAD=%s\n' "$harness_head"
    printf 'FEATURE_HEAD=%s\n' "$feature_head"
    printf '%s\n' 'FEATURE_CHECKOUT_DIRTY=NO'
    printf '%s\n' 'HARNESS_CHECKOUT_DIRTY=NO'
    printf '%s\n' 'APP_ABI=arm64-v8a'
    printf '%s\n' 'TEST_RUNNER=androidx.test.runner.AndroidJUnitRunner'
    printf 'JAVA=%s\n' "$java_version"
    printf 'JAVA_MAJOR=%s\n' "$java_major"
    printf 'GRADLE=%s\n' "$gradle_version_line"
    printf 'SDKMANAGER=%s\n' "$sdkmanager_version"
    printf '%s\n' 'COMPILE_SDK=36'
    printf '%s\n' 'BUILD_TOOLS=36.0.0'
    printf '%s\n' 'NDK=28.2.13676358'
    printf '%s\n' 'CMAKE=3.22.1'
    printf '%s\n' 'ANDROID_APK_BUILD=PASS'
} > "$output_dir/android-build-evidence/build.txt"

find "$output_dir" \
    -type f \
    ! -name SHA256SUMS \
    -print0 |
    sort -z |
    xargs -0 sha256sum > "$output_dir/SHA256SUMS"

cat "$output_dir/android-build-evidence/build.txt"
printf '\n%s\n' 'DBLYATEM PR100 LOCAL WSL ANDROID BUILD ACCEPTED'
printf 'HARNESS_HEAD=%s\n' "$harness_head"
printf 'FEATURE_HEAD=%s\n' "$feature_head"
printf 'APP_APK=%s\n' "$app_apk"
printf 'TEST_APK=%s\n' "$test_apk"
printf 'RUNNER=%s\n' "$output_dir/run-physical-device.sh"
printf 'OUTPUT=%s\n' "$output_dir"
