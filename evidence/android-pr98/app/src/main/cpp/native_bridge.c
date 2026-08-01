#include <jni.h>

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifndef DBLYATEM_EXPECTED_HEAD
#error DBLYATEM_EXPECTED_HEAD must be defined
#endif

int dblyatem_provider_test_main(void);
int dblyatem_posix_test_main(void);
int dblyatem_execute_test_main(void);

JNIEXPORT jstring JNICALL
Java_com_deadbyte_dblyatem_pr98evidence_NativeBridge_runEvidence(
    JNIEnv *environment,
    jclass bridge_class);

JNIEXPORT jstring JNICALL
Java_com_deadbyte_dblyatem_pr98evidence_NativeBridge_runEvidence(
    JNIEnv *environment,
    jclass bridge_class)
{
    char report[2048];
    struct utsname host;
    const long page_size = sysconf(_SC_PAGESIZE);
    const int provider_status = dblyatem_provider_test_main();
    const int posix_status = dblyatem_posix_test_main();
    const int execute_status = dblyatem_execute_test_main();
    const char *compile_arch;
    int runtime_arm64;
    int native_execution_pass;
    int android_runtime_pass;
    int length;

    (void)bridge_class;
    (void)memset(&host, 0, sizeof(host));

    if (uname(&host) != 0) {
        (void)snprintf(
            host.machine,
            sizeof(host.machine),
            "%s",
            "uname-failed"
        );
    }

#if defined(__aarch64__)
    compile_arch = "AARCH64";
#else
    compile_arch = "NOT_AARCH64";
#endif

    runtime_arm64 =
        strcmp(host.machine, "aarch64") == 0 ||
        strcmp(host.machine, "arm64") == 0;

    native_execution_pass =
        execute_status == 0 &&
        runtime_arm64 != 0 &&
        strcmp(compile_arch, "AARCH64") == 0;

    android_runtime_pass =
        provider_status == 0 &&
        posix_status == 0 &&
        native_execution_pass != 0 &&
        page_size == 4096;

    length = snprintf(
        report,
        sizeof(report),
        "DBLYATEM_PR98_ANDROID_EVIDENCE_V1\n"
        "COMMIT_SHA=%s\n"
        "COMPILE_ARCH=%s\n"
        "UNAME_MACHINE=%s\n"
        "RUNTIME_PAGE_SIZE=%ld\n"
        "PROVIDER_TEST_EXIT=%d\n"
        "POSIX_TEST_EXIT=%d\n"
        "EXECUTE_TEST_EXIT=%d\n"
        "NATIVE_AARCH64_EXECUTION=%s\n"
        "ANDROID_ARM64_RUNTIME_EVIDENCE=%s\n"
        "NO_RWX_TRANSITION_VERIFY=%s\n",
        DBLYATEM_EXPECTED_HEAD,
        compile_arch,
        host.machine,
        page_size,
        provider_status,
        posix_status,
        execute_status,
        native_execution_pass != 0 ? "PASS" : "FAIL",
        android_runtime_pass != 0 ? "PASS" : "FAIL",
        provider_status == 0 && posix_status == 0
            ? "PASS"
            : "FAIL"
    );

    if (length < 0 || (size_t)length >= sizeof(report)) {
        return (*environment)->NewStringUTF(
            environment,
            "DBLYATEM_PR98_ANDROID_EVIDENCE_FORMAT_FAILURE"
        );
    }

    return (*environment)->NewStringUTF(environment, report);
}
