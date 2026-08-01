package com.deadbyte.dblyatem.pr98evidence;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.os.Build;
import android.util.Log;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

@RunWith(AndroidJUnit4.class)
public final class WxProviderInstrumentationTest {
    private static final String TAG = "DBLYATEM_PR98";
    private static final String EXPECTED_HEAD =
        "8886fe231eaaefd9c28ed6fe9a7dc404739cc566";

    @Test
    public void exactHeadPublishesAndExecutesArm64Code() throws Exception {
        Context context =
            InstrumentationRegistry.getInstrumentation().getTargetContext();

        assertTrue(
            "device must expose arm64-v8a",
            Arrays.asList(Build.SUPPORTED_ABIS).contains("arm64-v8a")
        );

        String report = NativeBridge.runEvidence();
        Log.i(TAG, report);

        assertTrue(
            "exact feature head missing",
            report.contains("COMMIT_SHA=" + EXPECTED_HEAD)
        );
        assertTrue(
            "native build is not AArch64",
            report.contains("COMPILE_ARCH=AARCH64")
        );
        assertTrue(
            "runtime kernel is not AArch64",
            report.contains("UNAME_MACHINE=aarch64") ||
                report.contains("UNAME_MACHINE=arm64")
        );
        assertTrue(
            "runtime page size must be 4096 for the 4100-byte two-page proof",
            report.contains("RUNTIME_PAGE_SIZE=4096")
        );
        assertTrue(
            "mock failure and alias matrix failed",
            report.contains("PROVIDER_TEST_EXIT=0")
        );
        assertTrue(
            "Android RW->RX permission observation failed",
            report.contains("POSIX_TEST_EXIT=0")
        );
        assertTrue(
            "native ARM64 execution failed",
            report.contains("EXECUTE_TEST_EXIT=0")
        );
        assertTrue(
            "bounded ARM64 function did not return 42",
            report.contains("NATIVE_AARCH64_EXECUTION=PASS")
        );
        assertTrue(
            "Android runtime evidence marker missing",
            report.contains("ANDROID_ARM64_RUNTIME_EVIDENCE=PASS")
        );
        assertTrue(
            "no-RWX contract marker missing",
            report.contains("NO_RWX_TRANSITION_VERIFY=PASS")
        );
        assertFalse(
            "target evidence must not contain NOT CLAIMED",
            report.contains("NOT CLAIMED")
        );

        File evidenceDir = context.getExternalFilesDir(null);
        assertTrue(
            "external evidence directory unavailable",
            evidenceDir != null && evidenceDir.isDirectory()
        );

        File evidenceFile =
            new File(evidenceDir, "dblyatem-pr98-android-evidence.txt");

        try (FileOutputStream output =
                 new FileOutputStream(evidenceFile, false)) {
            output.write(report.getBytes(StandardCharsets.UTF_8));
            output.flush();
        }

        assertTrue("evidence file was not created", evidenceFile.isFile());
        assertTrue("evidence file is empty", evidenceFile.length() > 0L);
        assertEquals(
            "target package mismatch",
            "com.deadbyte.dblyatem.pr98evidence",
            context.getPackageName()
        );
    }
}
