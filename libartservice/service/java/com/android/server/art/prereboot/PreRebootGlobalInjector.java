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
import android.annotation.Nullable;
import android.content.Context;
import android.os.ArtModuleServiceManager;
import android.os.Build;

import androidx.annotation.RequiresApi;

import com.android.server.art.DexUseManagerLocal;
import com.android.server.art.GlobalInjector;
import com.android.server.art.IArtd;
import com.android.server.art.IArtdChroot;

import dalvik.system.VMRuntime;

import java.util.Objects;

/**
 * Global injector.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
public class PreRebootGlobalInjector extends GlobalInjector {
    @NonNull private final ArtModuleServiceManager mArtModuleServiceManager;
    @Nullable private DexUseManagerLocal mDexUseManager;

    private PreRebootGlobalInjector(@NonNull ArtModuleServiceManager artModuleServiceManager) {
        mArtModuleServiceManager = artModuleServiceManager;
    }

    public static void init(
            @NonNull ArtModuleServiceManager artModuleServiceManager, @NonNull Context context) {
        var instance = new PreRebootGlobalInjector(artModuleServiceManager);
        GlobalInjector.setInstance(instance);
        instance.mDexUseManager = DexUseManagerLocal.createInstance(context);
    }

    @Override
    public boolean isPreReboot() {
        return true;
    }

    @Override
    public void checkArtd() {}

    @Override
    @NonNull
    public IArtd getArtd() {
        IArtd artd = IArtd.Stub.asInterface(
                mArtModuleServiceManager.getArtdServiceRegisterer().waitForService());
        if (artd == null) {
            throw new IllegalStateException("Unable to connect to artd");
        }
        return artd;
    }

    @Override
    @NonNull
    public IArtdChroot getArtdChroot() {
        throw new UnsupportedOperationException();
    }

    @Override
    @NonNull
    public DexUseManagerLocal getDexUseManager() {
        return Objects.requireNonNull(mDexUseManager);
    }
}
