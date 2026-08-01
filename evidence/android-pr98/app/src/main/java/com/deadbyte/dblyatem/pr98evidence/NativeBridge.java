package com.deadbyte.dblyatem.pr98evidence;

public final class NativeBridge {
    static {
        System.loadLibrary("pr98evidence");
    }

    private NativeBridge() {
    }

    public static native String runEvidence();
}
