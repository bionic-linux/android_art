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
import android.app.job.JobParameters;
import android.content.Context;
import android.os.ArtModuleServiceManager;
import android.os.Build;
import android.os.CancellationSignal;
import android.os.RemoteException;
import android.util.Log;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.model.ArtServiceJobInterface;

import dalvik.system.DelegateLastClassLoader;
import dalvik.system.VMRuntime;

/** @hide */
@RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
public class PreRebootDexoptJob implements ArtServiceJobInterface {
    private static final String TAG = ArtManagerLocal.TAG;
    private static final String CHROOT_DIR = "/mnt/pre_reboot_dexopt";

    public static final int JOB_ID = 27873781;

    @NonNull private final Injector mInjector;

    public PreRebootDexoptJob(@NonNull Context context) {
        this(new Injector(context));
    }

    @VisibleForTesting
    public PreRebootDexoptJob(@NonNull Injector injector) {
        mInjector = injector;
    }

    @Override
    public boolean onStartJob(
            @NonNull BackgroundDexoptJobService jobService, @NonNull JobParameters params) {
        // "true" means the job will continue running until `jobFinished` is called.
        return false;
    }

    @Override
    public boolean onStopJob(@NonNull JobParameters params) {
        // "true" means to execute again in the same interval with the default retry policy.
        return true;
    }

    public void run() {
        try {
            mInjector.getArtdChroot().setUp();
        } catch (RemoteException e) {
            Utils.logArtdException(e);
            return;
        }
    }

    public void test() {
        try {
            var cancellationSignal = new CancellationSignal();
            runImpl(cancellationSignal);
        } catch (ReflectiveOperationException e) {
            Log.e(TAG, "Failed to run pre-reboot dexopt", e);
        }
    }

    public void testOnlyTearDown() {
        try {
            mInjector.getArtdChroot().tearDown();
        } catch (RemoteException e) {
            Utils.logArtdException(e);
        }
    }

    private void runImpl(@NonNull CancellationSignal cancellationSignal)
            throws ReflectiveOperationException {
        String chrootArtDir = CHROOT_DIR + "/apex/com.android.art";
        String dexPath = chrootArtDir + "/javalib/service-art.jar";
        String libraryPath = chrootArtDir + "/lib" + (VMRuntime.getRuntime().is64Bit() ? "64" : "");
        Class<?> classLoaderFactoryClass =
                Class.forName("com.android.internal.os.ClassLoaderFactory");
        var classLoader =
                (ClassLoader) classLoaderFactoryClass
                        .getMethod("createClassLoader", String.class, String.class, String.class,
                                ClassLoader.class, int.class, boolean.class, String.class)
                        .invoke(null, dexPath, libraryPath /* librarySearchPath */,
                                libraryPath /* libraryPermittedPath */,
                                this.getClass().getClassLoader() /* parent */,
                                VMRuntime.SDK_VERSION_CUR_DEVELOPMENT, true /* isNamespaceShared */,
                                DelegateLastClassLoader.class.getName());
        Class<?> preRebootManagerClass =
                classLoader.loadClass("com.android.server.art.prereboot.PreRebootManager");
        preRebootManagerClass
                .getMethod("run", ArtModuleServiceManager.class, Context.class,
                        CancellationSignal.class)
                .invoke(null, ArtModuleServiceInitializer.getArtModuleServiceManager(),
                        mInjector.getContext(), cancellationSignal);
    }

    /**
     * Injector pattern for testing purpose.
     *
     * @hide
     */
    @VisibleForTesting
    public static class Injector {
        @NonNull private final Context mContext;

        Injector(@NonNull Context context) {
            mContext = context;
        }

        @NonNull
        public Context getContext() {
            return mContext;
        }

        @NonNull
        public IArtdChroot getArtdChroot() {
            return GlobalInjector.getInstance().getArtdChroot();
        }
    }
}
