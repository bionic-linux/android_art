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

package com.android.server.art;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.os.ArtModuleServiceManager;
import android.os.Build;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.GuardedBy;
import com.android.server.LocalManagerRegistry;

import dalvik.system.VMRuntime;

import java.util.Objects;

/**
 * Global injector.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public abstract class GlobalInjector {
    @GuardedBy("GlobalInjector.class") @Nullable private static GlobalInjector sInstance;

    @NonNull
    public static synchronized GlobalInjector getInstance() {
        if (sInstance == null) {
            sInstance = new DefaultGlobalInjector();
        }
        return sInstance;
    }

    public static synchronized void setInstance(@NonNull GlobalInjector instance) {
        if (sInstance != null) {
            throw new IllegalStateException("GlobalInjector already initialized");
        }
        sInstance = instance;
    }

    public abstract boolean isPreReboot();

    public abstract void checkArtd();

    @NonNull public abstract IArtd getArtd();

    @NonNull public abstract IArtdChroot getArtdChroot();

    @NonNull public abstract DexUseManagerLocal getDexUseManager();

    public static class DefaultGlobalInjector extends GlobalInjector {
        @Override
        public boolean isPreReboot() {
            return false;
        }

        @Override
        public void checkArtd() {
            Objects.requireNonNull(ArtModuleServiceInitializer.getArtModuleServiceManager());
        }

        @Override
        @NonNull
        public IArtd getArtd() {
            IArtd artd =
                    IArtd.Stub.asInterface(ArtModuleServiceInitializer.getArtModuleServiceManager()
                                                   .getArtdServiceRegisterer()
                                                   .waitForService());
            if (artd == null) {
                throw new IllegalStateException("Unable to connect to artd");
            }
            return artd;
        }

        @Override
        @NonNull
        public IArtdChroot getArtdChroot() {
            IArtdChroot artdChroot = IArtdChroot.Stub.asInterface(
                    ArtModuleServiceInitializer.getArtModuleServiceManager()
                            .getArtdChrootServiceRegisterer()
                            .waitForService());
            if (artdChroot == null) {
                throw new IllegalStateException("Unable to connect to artd_chroot");
            }
            return artdChroot;
        }

        @Override
        @NonNull
        public DexUseManagerLocal getDexUseManager() {
            return Objects.requireNonNull(
                    LocalManagerRegistry.getManager(DexUseManagerLocal.class));
        }
    }
}
