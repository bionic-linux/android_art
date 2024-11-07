/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.server.art;

import android.annotation.FlaggedApi;
import android.annotation.NonNull;
import android.annotation.SystemApi;
import android.os.Build;

import androidx.annotation.RequiresApi;

import com.android.art.flags.Flags;

import java.util.List;

/**
 * Helper class for <i>ART-managed install files</i> (files installed by Package Manager
 * and managed by ART).
 *
 * @hide
 */
@FlaggedApi(Flags.FLAG_ART_SERVICE_V3)
@SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public final class ArtManagedInstallFileHelper {
    private static final List<String> FILE_TYPES = List.of(ArtConstants.DEX_METADATA_FILE_EXT,
            ArtConstants.PROFILE_FILE_EXT, ArtConstants.SECURE_DEX_METADATA_FILE_EXT);

    private ArtManagedInstallFileHelper() {}

    /** Returns whether the file at the given path is an <i>ART-managed install file</i>. */
    @FlaggedApi(Flags.FLAG_ART_SERVICE_V3)
    public static boolean isArtManaged(@NonNull String path) {
        return FILE_TYPES.stream().anyMatch(ext -> path.endsWith(ext));
    }

    /**
     * Returns the subset of the given paths that are paths to the <i>ART-managed install files</i>
     * corresponding to the given APK file.
     */
    @FlaggedApi(Flags.FLAG_ART_SERVICE_V3)
    public static @NonNull List<String> filterPathsForApk(
            @NonNull List<String> paths, @NonNull String apkPath) {
        return paths.stream()
                .filter(path
                        -> FILE_TYPES.stream().anyMatch(
                                ext -> Utils.replaceFileExtension(apkPath, ext).equals(path)))
                .toList();
    }

    /**
     * Returns the rewritten path of the <i>ART-managed install file</i> for the given APK file.
     *
     * @throws IllegalArgumentException if {@code originalPath} does not represent an <i>ART-managed
     *         install file</i>
     */
    @FlaggedApi(Flags.FLAG_ART_SERVICE_V3)
    public static @NonNull String getTargetPathForApk(
            @NonNull String originalPath, @NonNull String apkPath) {
        for (String ext : FILE_TYPES) {
            if (originalPath.endsWith(ext)) {
                return Utils.replaceFileExtension(apkPath, ext);
            }
        }
        throw new IllegalArgumentException(
                "Illegal ART managed install file path '" + originalPath + "'");
    }
}
