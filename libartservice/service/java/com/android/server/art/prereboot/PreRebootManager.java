/*
 * Copyright (C) 2023 The Android Open Source Project
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

package com.android.server.art.prereboot;

import android.annotation.NonNull;
import android.content.Context;
import android.os.ArtModuleServiceManager;
import android.os.Build;
import android.os.CancellationSignal;
import android.util.Log;

import androidx.annotation.RequiresApi;

import com.android.server.LocalManagerRegistry;
import com.android.server.art.ArtManagerLocal;
import com.android.server.art.ArtdRefCache;
import com.android.server.art.GlobalInjector;
import com.android.server.art.IArtd;
import com.android.server.art.ReasonMapping;
import com.android.server.pm.PackageManagerLocal;

import java.util.Objects;

/** @hide */
@RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
public class PreRebootManager {
    private static final String TAG = ArtManagerLocal.TAG;

    public static void run(@NonNull ArtModuleServiceManager artModuleServiceManager,
            @NonNull Context context, @NonNull CancellationSignal cancellationSignal) {
        PreRebootGlobalInjector.init(artModuleServiceManager, context);
        ArtManagerLocal artManagerLocal = new ArtManagerLocal(context);
        PackageManagerLocal packageManagerLocal =
                Objects.requireNonNull(LocalManagerRegistry.getManager(PackageManagerLocal.class));
        try (var snapshot = packageManagerLocal.withFilteredSnapshot()) {
            artManagerLocal.dexoptPackages(snapshot, ReasonMapping.REASON_PRE_REBOOT_DEXOPT,
                    cancellationSignal, null /* processCallbackExecutor */,
                    null /* processCallback */);
        }
        ArtdRefCache.getInstance().reset();
    }
}
