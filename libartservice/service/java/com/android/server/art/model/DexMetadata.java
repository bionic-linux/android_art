package com.android.server.art.model;

import android.annotation.IntDef;
import android.annotation.SuppressLint;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/** @hide */
@SuppressLint("UnflaggedApi") // Flag support for mainline is not available.
public class DexMetadata {
    /** An explicit private class to avoid exposing constructor.*/
    private DexMetadata() {}

    @SuppressLint("UnflaggedApi") // Flag support for mainline is not available.
    public static final int TYPE_UNKNOWN = 0;
    @SuppressLint("UnflaggedApi") // Flag support for mainline is not available.
    public static final int TYPE_PROFILE = 1;
    @SuppressLint("UnflaggedApi") // Flag support for mainline is not available.
    public static final int TYPE_VDEX = 2;
    @SuppressLint("UnflaggedApi") // Flag support for mainline is not available.
    public static final int TYPE_PROFILE_AND_VDEX = 3;
    @SuppressLint("UnflaggedApi") // Flag support for mainline is not available.
    public static final int TYPE_NONE = 4;
    @SuppressLint("UnflaggedApi") // Flag support for mainline is not available.
    public static final int TYPE_ERROR = 5;

    /** @hide */
    // clang-format off
    @IntDef(prefix = "TYPE_", value = {
        TYPE_UNKNOWN,
        TYPE_PROFILE,
        TYPE_VDEX,
        TYPE_PROFILE_AND_VDEX,
        TYPE_NONE,
        TYPE_ERROR,
    })
    // clang-format on
    @Retention(RetentionPolicy.SOURCE)
    public @interface Type {}
}
