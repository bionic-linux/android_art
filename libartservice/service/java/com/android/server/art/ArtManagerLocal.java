/*
 * Copyright (C) 2021 The Android Open Source Project
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

import static com.android.server.art.DexUseManagerLocal.SecondaryDexInfo;
import static com.android.server.art.PrimaryDexUtils.DetailedPrimaryDexInfo;
import static com.android.server.art.PrimaryDexUtils.PrimaryDexInfo;
import static com.android.server.art.ReasonMapping.BatchDexoptReason;
import static com.android.server.art.ReasonMapping.BootReason;
import static com.android.server.art.Utils.Abi;
import static com.android.server.art.model.ArtFlags.GetStatusFlags;
import static com.android.server.art.model.ArtFlags.ScheduleStatus;
import static com.android.server.art.model.Config.Callback;
import static com.android.server.art.model.DexoptStatus.DexContainerFileDexoptStatus;

import android.R;
import android.annotation.CallbackExecutor;
import android.annotation.NonNull;
import android.annotation.Nullable;
import android.annotation.SystemApi;
import android.annotation.SystemService;
import android.app.job.JobInfo;
import android.apphibernation.AppHibernationManager;
import android.content.Context;
import android.os.Binder;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import android.os.RemoteException;
import android.os.ServiceSpecificException;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.os.UserManager;
import android.os.storage.StorageManager;
import android.text.TextUtils;
import android.util.Log;
import android.util.Pair;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.LocalManagerRegistry;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.BatchDexoptParams;
import com.android.server.art.model.Config;
import com.android.server.art.model.DeleteResult;
import com.android.server.art.model.DexoptParams;
import com.android.server.art.model.DexoptResult;
import com.android.server.art.model.DexoptStatus;
import com.android.server.art.model.OperationProgress;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.AndroidPackageSplit;
import com.android.server.pm.pkg.PackageState;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * This class provides a system API for functionality provided by the ART module.
 *
 * Note: Although this class is the entry point of ART services, this class is not a {@link
 * SystemService}, and it does not publish a binder. Instead, it is a module loaded by the
 * system_server process, registered in {@link LocalManagerRegistry}. {@link LocalManagerRegistry}
 * specifies that in-process module interfaces should be named with the suffix {@code ManagerLocal}
 * for consistency.
 *
 * @hide
 */
@SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
public final class ArtManagerLocal {
    private static final String TAG = "ArtService";
    private static final String[] CLASSPATHS_FOR_BOOT_IMAGE_PROFILE = {
            "BOOTCLASSPATH", "SYSTEMSERVERCLASSPATH", "STANDALONE_SYSTEMSERVER_JARS"};

    /** @hide */
    @VisibleForTesting public static final long DOWNGRADE_THRESHOLD_ABOVE_LOW_BYTES = 500_000_000;

    @NonNull private final Injector mInjector;

    @Deprecated
    public ArtManagerLocal() {
        mInjector = new Injector(this, null /* context */);
    }

    /**
     * Creates an instance.
     *
     * Only {@code SystemServer} should create an instance and register it in {@link
     * LocalManagerRegistry}. Other API users should obtain the instance from {@link
     * LocalManagerRegistry}.
     *
     * @param context the system server context
     * @throws NullPointerException if required dependencies are missing
     */
    public ArtManagerLocal(@NonNull Context context) {
        mInjector = new Injector(this, context);
    }

    /** @hide */
    @VisibleForTesting
    public ArtManagerLocal(@NonNull Injector injector) {
        mInjector = injector;
    }

    /**
     * Handles ART Service commands, which is a subset of `cmd package` commands.
     *
     * Note: This method is not an override of {@link Binder#handleShellCommand} because ART
     * services does not publish a binder. Instead, it handles the commands forwarded by the
     * `package` service. The semantics of the parameters are the same as {@link
     * Binder#handleShellCommand}.
     *
     * @return zero on success, non-zero on internal error (e.g., I/O error)
     * @throws SecurityException if the caller is not root
     * @throws IllegalArgumentException if the arguments are illegal
     * @see ArtShellCommand#printHelp(PrintWriter)
     */
    public int handleShellCommand(@NonNull Binder target, @NonNull ParcelFileDescriptor in,
            @NonNull ParcelFileDescriptor out, @NonNull ParcelFileDescriptor err,
            @NonNull String[] args) {
        return new ArtShellCommand(
                this, mInjector.getPackageManagerLocal(), mInjector.getDexUseManager())
                .exec(target, in.getFileDescriptor(), out.getFileDescriptor(),
                        err.getFileDescriptor(), args);
    }

    /** Prints ART Service shell command help. */
    public void printShellCommandHelp(@NonNull PrintWriter pw) {
        ArtShellCommand.printHelp(pw);
    }

    /**
     * Deletes dexopt artifacts of a package, including the artifacts for primary dex files and the
     * ones for secondary dex files. This includes VDEX, ODEX, and ART files.
     *
     * @throws IllegalArgumentException if the package is not found or the flags are illegal
     * @throws IllegalStateException if the operation encounters an error that should never happen
     *         (e.g., an internal logic error).
     */
    @NonNull
    public DeleteResult deleteDexoptArtifacts(
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName) {
        PackageState pkgState = Utils.getPackageStateOrThrow(snapshot, packageName);
        AndroidPackage pkg = Utils.getPackageOrThrow(pkgState);

        try {
            long freedBytes = 0;

            boolean isInDalvikCache = Utils.isInDalvikCache(pkgState);
            for (PrimaryDexInfo dexInfo : PrimaryDexUtils.getDexInfo(pkg)) {
                if (!dexInfo.hasCode()) {
                    continue;
                }
                for (Abi abi : Utils.getAllAbis(pkgState)) {
                    freedBytes += mInjector.getArtd().deleteArtifacts(AidlUtils.buildArtifactsPath(
                            dexInfo.dexPath(), abi.isa(), isInDalvikCache));
                }
            }

            for (SecondaryDexInfo dexInfo :
                    mInjector.getDexUseManager().getSecondaryDexInfo(packageName)) {
                for (Abi abi : Utils.getAllAbisForNames(dexInfo.abiNames(), pkgState)) {
                    freedBytes += mInjector.getArtd().deleteArtifacts(AidlUtils.buildArtifactsPath(
                            dexInfo.dexPath(), abi.isa(), false /* isInDalvikCache */));
                }
            }

            return DeleteResult.create(freedBytes);
        } catch (RemoteException e) {
            throw new IllegalStateException("An error occurred when calling artd", e);
        }
    }

    /**
     * Returns the dexopt status of a package.
     *
     * Uses the default flags ({@link ArtFlags#defaultGetStatusFlags()}).
     *
     * @throws IllegalArgumentException if the package is not found or the flags are illegal
     * @throws IllegalStateException if the operation encounters an error that should never happen
     *         (e.g., an internal logic error).
     */
    @NonNull
    public DexoptStatus getDexoptStatus(
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName) {
        return getDexoptStatus(snapshot, packageName, ArtFlags.defaultGetStatusFlags());
    }

    /**
     * Same as above, but allows to specify flags.
     *
     * @see #getDexoptStatus(PackageManagerLocal.FilteredSnapshot, String)
     */
    @NonNull
    public DexoptStatus getDexoptStatus(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String packageName, @GetStatusFlags int flags) {
        if ((flags & ArtFlags.FLAG_FOR_PRIMARY_DEX) == 0
                && (flags & ArtFlags.FLAG_FOR_SECONDARY_DEX) == 0) {
            throw new IllegalArgumentException("Nothing to check");
        }

        PackageState pkgState = Utils.getPackageStateOrThrow(snapshot, packageName);
        AndroidPackage pkg = Utils.getPackageOrThrow(pkgState);

        try {
            List<DexContainerFileDexoptStatus> statuses = new ArrayList<>();

            if ((flags & ArtFlags.FLAG_FOR_PRIMARY_DEX) != 0) {
                for (DetailedPrimaryDexInfo dexInfo :
                        PrimaryDexUtils.getDetailedDexInfo(pkgState, pkg)) {
                    if (!dexInfo.hasCode()) {
                        continue;
                    }
                    for (Abi abi : Utils.getAllAbis(pkgState)) {
                        try {
                            GetDexoptStatusResult result = mInjector.getArtd().getDexoptStatus(
                                    dexInfo.dexPath(), abi.isa(), dexInfo.classLoaderContext());
                            statuses.add(DexContainerFileDexoptStatus.create(dexInfo.dexPath(),
                                    true /* isPrimaryDex */, abi.isPrimaryAbi(), abi.name(),
                                    result.compilerFilter, result.compilationReason,
                                    result.locationDebugString));
                        } catch (ServiceSpecificException e) {
                            statuses.add(DexContainerFileDexoptStatus.create(dexInfo.dexPath(),
                                    true /* isPrimaryDex */, abi.isPrimaryAbi(), abi.name(),
                                    "error", "error", e.getMessage()));
                        }
                    }
                }
            }

            if ((flags & ArtFlags.FLAG_FOR_SECONDARY_DEX) != 0) {
                for (SecondaryDexInfo dexInfo :
                        mInjector.getDexUseManager().getSecondaryDexInfo(packageName)) {
                    for (Abi abi : Utils.getAllAbisForNames(dexInfo.abiNames(), pkgState)) {
                        try {
                            GetDexoptStatusResult result = mInjector.getArtd().getDexoptStatus(
                                    dexInfo.dexPath(), abi.isa(), dexInfo.classLoaderContext());
                            statuses.add(DexContainerFileDexoptStatus.create(dexInfo.dexPath(),
                                    false /* isPrimaryDex */, abi.isPrimaryAbi(), abi.name(),
                                    result.compilerFilter, result.compilationReason,
                                    result.locationDebugString));
                        } catch (ServiceSpecificException e) {
                            statuses.add(DexContainerFileDexoptStatus.create(dexInfo.dexPath(),
                                    false /* isPrimaryDex */, abi.isPrimaryAbi(), abi.name(),
                                    "error", "error", e.getMessage()));
                        }
                    }
                }
            }

            return DexoptStatus.create(statuses);
        } catch (RemoteException e) {
            throw new IllegalStateException("An error occurred when calling artd", e);
        }
    }

    /**
     * Clears the profiles of the given app that are collected locally, including the profiles for
     * primary dex files and the ones for secondary dex files. More specifically, it clears
     * reference profiles and current profiles. External profiles (e.g., cloud profiles) will be
     * kept.
     *
     * @throws IllegalArgumentException if the package is not found or the flags are illegal
     * @throws IllegalStateException if the operation encounters an error that should never happen
     *         (e.g., an internal logic error).
     */
    @NonNull
    public void clearAppProfiles(
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName) {
        PackageState pkgState = Utils.getPackageStateOrThrow(snapshot, packageName);
        AndroidPackage pkg = Utils.getPackageOrThrow(pkgState);

        try {
            for (PrimaryDexInfo dexInfo : PrimaryDexUtils.getDexInfo(pkg)) {
                if (!dexInfo.hasCode()) {
                    continue;
                }
                mInjector.getArtd().deleteProfile(
                        PrimaryDexUtils.buildRefProfilePath(pkgState, dexInfo));
                for (ProfilePath profile : PrimaryDexUtils.getCurProfiles(
                             mInjector.getUserManager(), pkgState, dexInfo)) {
                    mInjector.getArtd().deleteProfile(profile);
                }
            }

            // This only deletes the profiles of known secondary dex files. If there are unknown
            // secondary dex files, their profiles will be deleted by `cleanup`.
            for (SecondaryDexInfo dexInfo :
                    mInjector.getDexUseManager().getSecondaryDexInfo(packageName)) {
                mInjector.getArtd().deleteProfile(
                        AidlUtils.buildProfilePathForSecondaryRef(dexInfo.dexPath()));
                mInjector.getArtd().deleteProfile(
                        AidlUtils.buildProfilePathForSecondaryCur(dexInfo.dexPath()));
            }
        } catch (RemoteException e) {
            throw new IllegalStateException("An error occurred when calling artd", e);
        }
    }

    /**
     * Dexopts a package. The time this operation takes ranges from a few milliseconds to several
     * minutes, depending on the params and the code size of the package.
     *
     * When this operation ends (either completed or cancelled), callbacks added by {@link
     * #addDexoptDoneCallback(Executor, DexoptDoneCallback)} are called.
     *
     * @throws IllegalArgumentException if the package is not found or the params are illegal
     * @throws IllegalStateException if the operation encounters an error that should never happen
     *         (e.g., an internal logic error).
     * @throws RuntimeException if called during boot before the app hibernation manager has
     *         started.
     */
    @NonNull
    public DexoptResult dexoptPackage(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String packageName, @NonNull DexoptParams params) {
        var cancellationSignal = new CancellationSignal();
        return dexoptPackage(snapshot, packageName, params, cancellationSignal);
    }

    /**
     * Same as above, but supports cancellation.
     *
     * @see #dexoptPackage(PackageManagerLocal.FilteredSnapshot, String, DexoptParams)
     */
    @NonNull
    public DexoptResult dexoptPackage(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String packageName, @NonNull DexoptParams params,
            @NonNull CancellationSignal cancellationSignal) {
        return mInjector.getDexoptHelper(mInjector.getAppHibernationManager())
                .dexopt(snapshot, List.of(packageName), params, cancellationSignal, Runnable::run);
    }

    /**
     * Resets the dexopt state of the package as if the package is newly installed.
     *
     * More specifically, it clears reference profiles, current profiles, and any code compiled from
     * those local profiles. If there is an external profile (e.g., a cloud profile), the code
     * compiled from that profile will be kept.
     *
     * For secondary dex files, it also clears all dexopt artifacts.
     *
     * @hide
     */
    @NonNull
    public DexoptResult resetDexoptStatus(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String packageName, @NonNull CancellationSignal cancellationSignal) {
        // We must delete the artifacts for primary dex files beforehand rather than relying on
        // `dexoptPackage` to replace them because:
        // - If dexopt is not needed after the deletion, then we shouldn't run dexopt at all. For
        //   example, when we have a DM file that contains a VDEX file but doesn't contain a cloud
        //   profile, this happens. Note that this is more about correctness rather than
        //   performance.
        // - We don't want the existing artifacts to affect dexopt. For example, the existing VDEX
        //   file should not be an input VDEX.
        //
        // We delete the artifacts for secondary dex files and `dexoptPackage` won't re-generate
        // them because `dexoptPackage` for `REASON_INSTALL` is for primary dex only. This is
        // intentional because secondary dex files are supposed to be unknown at install time.
        deleteDexoptArtifacts(snapshot, packageName);
        clearAppProfiles(snapshot, packageName);

        // Re-generate artifacts for primary dex files if needed.
        return dexoptPackage(snapshot, packageName,
                new DexoptParams.Builder(ReasonMapping.REASON_INSTALL).build(), cancellationSignal);
    }

    /**
     * Runs batch dexopt for the given reason.
     *
     * This is called by ART Service automatically during boot / background dexopt.
     *
     * The list of packages and options are determined by {@code reason}, and can be overridden by
     * {@link #setBatchDexoptStartCallback(Executor, BatchDexoptStartCallback)}.
     *
     * The dexopt is done in a thread pool. The number of packages being dexopted
     * simultaneously can be configured by system property {@code pm.dexopt.<reason>.concurrency}
     * (e.g., {@code pm.dexopt.bg-dexopt.concurrency=4}), and the number of threads for each {@code
     * dex2oat} invocation can be configured by system property {@code dalvik.vm.*dex2oat-threads}
     * (e.g., {@code dalvik.vm.background-dex2oat-threads=4}). I.e., the maximum number of
     * concurrent threads is the product of the two system properties. Note that the physical core
     * usage is always bound by {@code dalvik.vm.*dex2oat-cpu-set} regardless of the number of
     * threads.
     *
     * When this operation ends (either completed or cancelled), callbacks added by {@link
     * #addDexoptDoneCallback(Executor, DexoptDoneCallback)} are called.
     *
     * If the storage is nearly low, and {@code reason} is {@link ReasonMapping#REASON_BG_DEXOPT},
     * it may also downgrade some inactive packages to a less optimized compiler filter, specified
     * by the system property {@code pm.dexopt.inactive} (typically "verify"), to free up some
     * space. This feature is only enabled when the system property {@code
     * pm.dexopt.downgrade_after_inactive_days} is set. The space threshold to trigger this feature
     * is the Storage Manager's low space threshold plus {@link
     * #DOWNGRADE_THRESHOLD_ABOVE_LOW_BYTES}. The concurrency can be configured by system property
     * {@code pm.dexopt.bg-dexopt.concurrency}. The packages in the list provided by
     * {@link BatchDexoptStartCallback} for {@link ReasonMapping#REASON_BG_DEXOPT} are never
     * downgraded.
     *
     * @param snapshot the snapshot from {@link PackageManagerLocal} to operate on
     * @param reason determines the default list of packages and options
     * @param cancellationSignal provides the ability to cancel this operation
     * @param processCallbackExecutor the executor to call {@code progressCallback}
     * @param progressCallback called repeatedly whenever there is an update on the progress
     * @throws IllegalStateException if the operation encounters an error that should never happen
     *         (e.g., an internal logic error), or the callback set by {@link
     *         #setBatchDexoptStartCallback(Executor, BatchDexoptStartCallback)} provides invalid
     *         params.
     *
     * @hide
     */
    @NonNull
    public DexoptResult dexoptPackages(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull @BatchDexoptReason String reason,
            @NonNull CancellationSignal cancellationSignal,
            @Nullable @CallbackExecutor Executor progressCallbackExecutor,
            @Nullable Consumer<OperationProgress> progressCallback) {
        // We cannot assume the app hibernation manager has been initialized yet in the boot time
        // compilation, because ArtManagerLocal.onBoot needs to run early to ensure apps are
        // compiled before the system server fires them up. This means the boot time compilation
        // will ignore the hibernation states of the packages.
        //
        // TODO(b/265782156): When hibernated packages get compiled this way, the file GC will
        // delete them again in the next background dexopt run. That means they are likely to get
        // recreated again in the next boot dexopt (i.e. for OTA or Mainline update).
        AppHibernationManager appHibernationManager;
        switch (reason) {
            case ReasonMapping.REASON_FIRST_BOOT:
            case ReasonMapping.REASON_BOOT_AFTER_OTA:
            case ReasonMapping.REASON_BOOT_AFTER_MAINLINE_UPDATE:
                appHibernationManager = null;
                break;
            default:
                appHibernationManager = mInjector.getAppHibernationManager();
                break;
        }

        List<String> defaultPackages = Collections.unmodifiableList(
                getDefaultPackages(snapshot, reason, appHibernationManager));
        DexoptParams defaultDexoptParams = new DexoptParams.Builder(reason).build();
        var builder = new BatchDexoptParams.Builder(defaultPackages, defaultDexoptParams);
        Callback<BatchDexoptStartCallback, Void> callback =
                mInjector.getConfig().getBatchDexoptStartCallback();
        if (callback != null) {
            Utils.executeAndWait(callback.executor(), () -> {
                callback.get().onBatchDexoptStart(
                        snapshot, reason, defaultPackages, builder, cancellationSignal);
            });
        }
        BatchDexoptParams params = builder.build();
        Utils.check(params.getDexoptParams().getReason().equals(reason));

        ExecutorService dexoptExecutor =
                Executors.newFixedThreadPool(ReasonMapping.getConcurrencyForReason(reason));
        try {
            if (reason.equals(ReasonMapping.REASON_BG_DEXOPT)) {
                maybeDowngradePackages(snapshot,
                        new HashSet<>(params.getPackages()) /* excludedPackages */,
                        cancellationSignal, dexoptExecutor);
            }
            Log.i(TAG, "Dexopting packages");
            return mInjector.getDexoptHelper(appHibernationManager)
                    .dexopt(snapshot, params.getPackages(), params.getDexoptParams(),
                            cancellationSignal, dexoptExecutor, progressCallbackExecutor,
                            progressCallback);
        } finally {
            dexoptExecutor.shutdown();
        }
    }

    /**
     * Overrides the default params for {@link #dexoptPackages}. This method is thread-safe.
     *
     * This method gives users the opportunity to change the behavior of {@link #dexoptPackages},
     * which is called by ART Service automatically during boot / background dexopt.
     *
     * If this method is not called, the default list of packages and options determined by {@code
     * reason} will be used.
     */
    public void setBatchDexoptStartCallback(@NonNull @CallbackExecutor Executor executor,
            @NonNull BatchDexoptStartCallback callback) {
        mInjector.getConfig().setBatchDexoptStartCallback(executor, callback);
    }

    /**
     * Clears the callback set by {@link
     * #setBatchDexoptStartCallback(Executor, BatchDexoptStartCallback)}. This method is
     * thread-safe.
     */
    public void clearBatchDexoptStartCallback() {
        mInjector.getConfig().clearBatchDexoptStartCallback();
    }

    /**
     * Schedules a background dexopt job. Does nothing if the job is already scheduled.
     *
     * Use this method if you want the system to automatically determine the best time to run
     * dexopt.
     *
     * The job will be run by the job scheduler. The job scheduling configuration can be overridden
     * by {@link
     * #setScheduleBackgroundDexoptJobCallback(Executor, ScheduleBackgroundDexoptJobCallback)}. By
     * default, it runs periodically (at most once a day) when all the following constraints are
     * meet.
     *
     * <ul>
     *   <li>The device is idling. (see {@link JobInfo.Builder#setRequiresDeviceIdle(boolean)})
     *   <li>The device is charging. (see {@link JobInfo.Builder#setRequiresCharging(boolean)})
     *   <li>The battery level is not low.
     *     (see {@link JobInfo.Builder#setRequiresBatteryNotLow(boolean)})
     * </ul>
     *
     * When the job is running, it may be cancelled by the job scheduler immediately whenever one of
     * the constraints above is no longer met or cancelled by the {@link
     * #cancelBackgroundDexoptJob()} API. The job scheduler retries it in the next <i>maintenance
     * window</i>. For information about <i>maintenance window</i>, see
     * https://developer.android.com/training/monitoring-device-state/doze-standby.
     *
     * See {@link #dexoptPackages} for how to customize the behavior of the job.
     *
     * When the job ends (either completed or cancelled), the result is sent to the callbacks added
     * by {@link #addDexoptDoneCallback(Executor, DexoptDoneCallback)} with the
     * reason {@link ReasonMapping#REASON_BG_DEXOPT}.
     *
     * @throws RuntimeException if called during boot before the job scheduler service has started.
     */
    public @ScheduleStatus int scheduleBackgroundDexoptJob() {
        return mInjector.getBackgroundDexoptJob().schedule();
    }

    /**
     * Unschedules the background dexopt job scheduled by {@link #scheduleBackgroundDexoptJob()}.
     * Does nothing if the job is not scheduled.
     *
     * Use this method if you no longer want the system to automatically run dexopt.
     *
     * If the job is already started by the job scheduler and is running, it will be cancelled
     * immediately, and the result sent to the callbacks added by {@link
     * #addDexoptDoneCallback(Executor, DexoptDoneCallback)} will contain {@link
     * DexoptResult#DEXOPT_CANCELLED}. Note that a job started by {@link
     * #startBackgroundDexoptJob()} will not be cancelled by this method.
     */
    public void unscheduleBackgroundDexoptJob() {
        mInjector.getBackgroundDexoptJob().unschedule();
    }

    /**
     * Overrides the configuration of the background dexopt job. This method is thread-safe.
     */
    public void setScheduleBackgroundDexoptJobCallback(@NonNull @CallbackExecutor Executor executor,
            @NonNull ScheduleBackgroundDexoptJobCallback callback) {
        mInjector.getConfig().setScheduleBackgroundDexoptJobCallback(executor, callback);
    }

    /**
     * Clears the callback set by {@link
     * #setScheduleBackgroundDexoptJobCallback(Executor, ScheduleBackgroundDexoptJobCallback)}. This
     * method is thread-safe.
     */
    public void clearScheduleBackgroundDexoptJobCallback() {
        mInjector.getConfig().clearScheduleBackgroundDexoptJobCallback();
    }

    /**
     * Manually starts a background dexopt job. Does nothing if a job is already started by this
     * method or by the job scheduler. This method is not blocking.
     *
     * Unlike the job started by job scheduler, the job started by this method does not respect
     * constraints described in {@link #scheduleBackgroundDexoptJob()}, and hence will not be
     * cancelled when they aren't met.
     *
     * See {@link #dexoptPackages} for how to customize the behavior of the job.
     *
     * When the job ends (either completed or cancelled), the result is sent to the callbacks added
     * by {@link #addDexoptDoneCallback(Executor, DexoptDoneCallback)} with the
     * reason {@link ReasonMapping#REASON_BG_DEXOPT}.
     */
    public void startBackgroundDexoptJob() {
        mInjector.getBackgroundDexoptJob().start();
    }

    /**
     * Cancels the running background dexopt job started by the job scheduler or by {@link
     * #startBackgroundDexoptJob()}. Does nothing if the job is not running. This method is not
     * blocking.
     *
     * The result sent to the callbacks added by {@link
     * #addDexoptDoneCallback(Executor, DexoptDoneCallback)} will contain {@link
     * DexoptResult#DEXOPT_CANCELLED}.
     */
    public void cancelBackgroundDexoptJob() {
        mInjector.getBackgroundDexoptJob().cancel();
    }

    /**
     * Adds a global listener that listens to any result of dexopting package(s), no matter run
     * manually or automatically. Calling this method multiple times with different callbacks is
     * allowed. Callbacks are executed in the same order as the one in which they were added. This
     * method is thread-safe.
     *
     * @param onlyIncludeUpdates if true, the results passed to the callback will only contain
     *         packages that have any update, and the callback won't be called with results that
     *         don't have any update.
     * @throws IllegalStateException if the same callback instance is already added
     */
    public void addDexoptDoneCallback(boolean onlyIncludeUpdates,
            @NonNull @CallbackExecutor Executor executor, @NonNull DexoptDoneCallback callback) {
        mInjector.getConfig().addDexoptDoneCallback(onlyIncludeUpdates, executor, callback);
    }

    /**
     * Removes the listener added by {@link
     * #addDexoptDoneCallback(Executor, DexoptDoneCallback)}. Does nothing if the
     * callback was not added. This method is thread-safe.
     */
    public void removeDexoptDoneCallback(@NonNull DexoptDoneCallback callback) {
        mInjector.getConfig().removeDexoptDoneCallback(callback);
    }

    /**
     * Snapshots the profile of the given app split. The profile snapshot is the aggregation of all
     * existing profiles of the app split (all current user profiles and the reference profile).
     *
     * @param snapshot the snapshot from {@link PackageManagerLocal} to operate on
     * @param packageName the name of the app that owns the profile
     * @param splitName see {@link AndroidPackageSplit#getName()}
     * @return the file descriptor of the snapshot. It doesn't have any path associated with it. The
     *         caller is responsible for closing it. Note that the content may be empty.
     * @throws IllegalArgumentException if the package or the split is not found
     * @throws IllegalStateException if the operation encounters an error that should never happen
     *         (e.g., an internal logic error).
     * @throws SnapshotProfileException if the operation encounters an error that the caller should
     *         handle (e.g., an I/O error, a sub-process crash).
     */
    @NonNull
    public ParcelFileDescriptor snapshotAppProfile(
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName,
            @Nullable String splitName) throws SnapshotProfileException {
        var options = new MergeProfileOptions();
        options.forceMerge = true;
        return snapshotOrDumpAppProfile(snapshot, packageName, splitName, options);
    }

    /**
     * Same as above, but outputs in text format.
     *
     * @hide
     */
    @NonNull
    public ParcelFileDescriptor dumpAppProfile(
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName,
            @Nullable String splitName, boolean dumpClassesAndMethods)
            throws SnapshotProfileException {
        var options = new MergeProfileOptions();
        options.dumpOnly = !dumpClassesAndMethods;
        options.dumpClassesAndMethods = dumpClassesAndMethods;
        return snapshotOrDumpAppProfile(snapshot, packageName, splitName, options);
    }

    @NonNull
    private ParcelFileDescriptor snapshotOrDumpAppProfile(
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName,
            @Nullable String splitName, @NonNull MergeProfileOptions options)
            throws SnapshotProfileException {
        PackageState pkgState = Utils.getPackageStateOrThrow(snapshot, packageName);
        AndroidPackage pkg = Utils.getPackageOrThrow(pkgState);
        PrimaryDexInfo dexInfo = PrimaryDexUtils.getDexInfoBySplitName(pkg, splitName);

        List<ProfilePath> profiles = new ArrayList<>();
        profiles.add(PrimaryDexUtils.buildRefProfilePath(pkgState, dexInfo));
        profiles.addAll(
                PrimaryDexUtils.getCurProfiles(mInjector.getUserManager(), pkgState, dexInfo));

        OutputProfile output = PrimaryDexUtils.buildOutputProfile(
                pkgState, dexInfo, Process.SYSTEM_UID, Process.SYSTEM_UID, false /* isPublic */);

        return mergeProfilesAndGetFd(profiles, output, List.of(dexInfo.dexPath()), options);
    }

    /**
     * Snapshots the boot image profile
     * (https://source.android.com/docs/core/bootloader/boot-image-profiles). The profile snapshot
     * is the aggregation of all existing profiles on the device (all current user profiles and
     * reference profiles) of all apps and the system server filtered by applicable classpaths.
     *
     * @param snapshot the snapshot from {@link PackageManagerLocal} to operate on
     * @return the file descriptor of the snapshot. It doesn't have any path associated with it. The
     *         caller is responsible for closing it. Note that the content may be empty.
     * @throws IllegalStateException if the operation encounters an error that should never happen
     *         (e.g., an internal logic error).
     * @throws SnapshotProfileException if the operation encounters an error that the caller should
     *         handle (e.g., an I/O error, a sub-process crash).
     */
    @NonNull
    public ParcelFileDescriptor snapshotBootImageProfile(
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot)
            throws SnapshotProfileException {
        List<ProfilePath> profiles = new ArrayList<>();

        // System server profiles.
        profiles.add(AidlUtils.buildProfilePathForPrimaryRef(
                Utils.PLATFORM_PACKAGE_NAME, PrimaryDexUtils.PROFILE_PRIMARY));
        for (UserHandle handle :
                mInjector.getUserManager().getUserHandles(true /* excludeDying */)) {
            profiles.add(AidlUtils.buildProfilePathForPrimaryCur(handle.getIdentifier(),
                    Utils.PLATFORM_PACKAGE_NAME, PrimaryDexUtils.PROFILE_PRIMARY));
        }

        // App profiles.
        snapshot.getPackageStates().forEach((packageName, appPkgState) -> {
            // Hibernating apps can still provide useful profile contents, so skip the hibernation
            // check.
            if (Utils.canDexoptPackage(appPkgState, null /* appHibernationManager */)) {
                AndroidPackage appPkg = Utils.getPackageOrThrow(appPkgState);
                for (PrimaryDexInfo appDexInfo : PrimaryDexUtils.getDexInfo(appPkg)) {
                    if (!appDexInfo.hasCode()) {
                        continue;
                    }
                    profiles.add(PrimaryDexUtils.buildRefProfilePath(appPkgState, appDexInfo));
                    profiles.addAll(PrimaryDexUtils.getCurProfiles(
                            mInjector.getUserManager(), appPkgState, appDexInfo));
                }
            }
        });

        OutputProfile output = AidlUtils.buildOutputProfileForPrimary(Utils.PLATFORM_PACKAGE_NAME,
                PrimaryDexUtils.PROFILE_PRIMARY, Process.SYSTEM_UID, Process.SYSTEM_UID,
                false /* isPublic */);

        List<String> dexPaths = Arrays.stream(CLASSPATHS_FOR_BOOT_IMAGE_PROFILE)
                                        .map(envVar -> Constants.getenv(envVar))
                                        .filter(classpath -> !TextUtils.isEmpty(classpath))
                                        .flatMap(classpath -> Arrays.stream(classpath.split(":")))
                                        .collect(Collectors.toList());

        var options = new MergeProfileOptions();
        options.forceMerge = true;
        options.forBootImage = true;
        return mergeProfilesAndGetFd(profiles, output, dexPaths, options);
    }

    /**
     * Notifies ART Service that this is a boot that falls into one of the categories listed in
     * {@link BootReason}. The current behavior is that ART Service goes through all recently used
     * packages and dexopts those that are not dexopted. This might change in the future.
     *
     * This method is blocking. It takes about 30 seconds to a few minutes. During execution, {@code
     * progressCallback} is repeatedly called whenever there is an update on the progress.
     *
     * See {@link #dexoptPackages} for how to customize the behavior.
     */
    public void onBoot(@NonNull @BootReason String bootReason,
            @Nullable @CallbackExecutor Executor progressCallbackExecutor,
            @Nullable Consumer<OperationProgress> progressCallback) {
        try (var snapshot = mInjector.getPackageManagerLocal().withFilteredSnapshot()) {
            dexoptPackages(snapshot, bootReason, new CancellationSignal(), progressCallbackExecutor,
                    progressCallback);
        }
    }

    /**
     * Dumps the dexopt state of all packages in text format for debugging purposes.
     *
     * There are no stability guarantees for the output format.
     *
     * @throws IllegalStateException if the operation encounters an error that should never happen
     *         (e.g., an internal logic error).
     */
    public void dump(
            @NonNull PrintWriter pw, @NonNull PackageManagerLocal.FilteredSnapshot snapshot) {
        new DumpHelper(this).dump(pw, snapshot);
    }

    /**
     * Dumps the dexopt state of the given package in text format for debugging purposes.
     *
     * There are no stability guarantees for the output format.
     *
     * @throws IllegalArgumentException if the package is not found
     * @throws IllegalStateException if the operation encounters an error that should never happen
     *         (e.g., an internal logic error).
     */
    public void dumpPackage(@NonNull PrintWriter pw,
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName) {
        new DumpHelper(this).dumpPackage(
                pw, snapshot, Utils.getPackageStateOrThrow(snapshot, packageName));
    }

    /**
     * Should be used by {@link BackgroundDexoptJobService} ONLY.
     *
     * @hide
     */
    @NonNull
    BackgroundDexoptJob getBackgroundDexoptJob() {
        return mInjector.getBackgroundDexoptJob();
    }

    private void maybeDowngradePackages(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull Set<String> excludedPackages, @NonNull CancellationSignal cancellationSignal,
            @NonNull Executor executor) {
        if (shouldDowngrade()) {
            List<String> packages = getDefaultPackages(
                    snapshot, ReasonMapping.REASON_INACTIVE, mInjector.getAppHibernationManager())
                                            .stream()
                                            .filter(pkg -> !excludedPackages.contains(pkg))
                                            .collect(Collectors.toList());
            if (!packages.isEmpty()) {
                Log.i(TAG, "Storage is low. Downgrading inactive packages");
                DexoptParams params =
                        new DexoptParams.Builder(ReasonMapping.REASON_INACTIVE).build();
                mInjector.getDexoptHelper(mInjector.getAppHibernationManager())
                        .dexopt(snapshot, packages, params, cancellationSignal, executor,
                                null /* processCallbackExecutor */, null /* progressCallback */);
            } else {
                Log.i(TAG,
                        "Storage is low, but downgrading is disabled or there's nothing to "
                                + "downgrade");
            }
        }
    }

    private boolean shouldDowngrade() {
        try {
            return mInjector.getStorageManager().getAllocatableBytes(StorageManager.UUID_DEFAULT)
                    < DOWNGRADE_THRESHOLD_ABOVE_LOW_BYTES;
        } catch (IOException e) {
            Log.e(TAG, "Failed to check storage. Assuming storage not low", e);
            return false;
        }
    }

    /** Returns the list of packages to process for the given reason. */
    @NonNull
    private List<String> getDefaultPackages(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull /* @BatchDexoptReason|REASON_INACTIVE */ String reason,
            @Nullable AppHibernationManager appHibernationManager) {
        // Filter out hibernating packages even if the reason is REASON_INACTIVE. This is because
        // artifacts for hibernating packages are already deleted.
        Stream<PackageState> packages = snapshot.getPackageStates().values().stream().filter(
                pkgState -> Utils.canDexoptPackage(pkgState, appHibernationManager));

        switch (reason) {
            case ReasonMapping.REASON_BOOT_AFTER_MAINLINE_UPDATE:
                packages = packages.filter(
                        pkgState -> mInjector.isSystemUiPackage(pkgState.getPackageName()));
                break;
            case ReasonMapping.REASON_INACTIVE:
                packages = filterAndSortByLastActiveTime(
                        packages, false /* keepRecent */, false /* descending */);
                break;
            default:
                // Actually, the sorting is only needed for background dexopt, but we do it for all
                // cases for simplicity.
                packages = filterAndSortByLastActiveTime(
                        packages, true /* keepRecent */, true /* descending */);
        }

        return packages.map(PackageState::getPackageName).collect(Collectors.toList());
    }

    @NonNull
    private Stream<PackageState> filterAndSortByLastActiveTime(
            @NonNull Stream<PackageState> packages, boolean keepRecent, boolean descending) {
        // "pm.dexopt.downgrade_after_inactive_days" is repurposed to also determine whether to
        // dexopt a package.
        long inactiveMs = TimeUnit.DAYS.toMillis(SystemProperties.getInt(
                "pm.dexopt.downgrade_after_inactive_days", Integer.MAX_VALUE /* def */));
        long currentTimeMs = mInjector.getCurrentTimeMillis();
        long thresholdTimeMs = currentTimeMs - inactiveMs;
        return packages
                .map(pkgState
                        -> Pair.create(pkgState,
                                Utils.getPackageLastActiveTime(pkgState,
                                        mInjector.getDexUseManager(), mInjector.getUserManager())))
                .filter(keepRecent ? (pair -> pair.second > thresholdTimeMs)
                                   : (pair -> pair.second <= thresholdTimeMs))
                .sorted(descending ? Comparator.comparingLong(pair -> - pair.second)
                                   : Comparator.comparingLong(pair -> pair.second))
                .map(pair -> pair.first);
    }

    @NonNull
    private ParcelFileDescriptor mergeProfilesAndGetFd(@NonNull List<ProfilePath> profiles,
            @NonNull OutputProfile output, @NonNull List<String> dexPaths,
            @NonNull MergeProfileOptions options) throws SnapshotProfileException {
        try {
            boolean hasContent = false;
            try {
                hasContent = mInjector.getArtd().mergeProfiles(
                        profiles, null /* referenceProfile */, output, dexPaths, options);
            } catch (ServiceSpecificException e) {
                throw new SnapshotProfileException(e);
            }

            String path = hasContent ? output.profilePath.tmpPath : "/dev/null";
            ParcelFileDescriptor fd;
            try {
                fd = ParcelFileDescriptor.open(new File(path), ParcelFileDescriptor.MODE_READ_ONLY);
            } catch (FileNotFoundException e) {
                throw new IllegalStateException(
                        String.format("Failed to open profile snapshot '%s'", path), e);
            }

            if (hasContent) {
                // This is done on the open file so that only the FD keeps a reference to its
                // contents.
                mInjector.getArtd().deleteProfile(ProfilePath.tmpProfilePath(output.profilePath));
            }

            return fd;
        } catch (RemoteException e) {
            throw new IllegalStateException("An error occurred when calling artd", e);
        }
    }

    /** @hide */
    @SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
    public interface BatchDexoptStartCallback {
        /**
         * Mutates {@code builder} to override the default params for {@link #dexoptPackages}. It
         * must ignore unknown reasons because more reasons may be added in the future.
         *
         * This is called before the start of any automatic package dexopt (i.e., not
         * including package dexopt initiated by the {@link #dexoptPackage} API call).
         *
         * If {@code builder.setPackages} is not called, {@code defaultPackages} will be used as the
         * list of packages to dexopt.
         *
         * If {@code builder.setDexoptParams} is not called, the default params built from {@code
         * new DexoptParams.Builder(reason)} will to used as the params for dexopting each
         * package.
         *
         * Additionally, {@code cancellationSignal.cancel()} can be called to cancel this operation.
         * If this operation is initiated by the job scheduler and the {@code reason} is {@link
         * ReasonMapping#REASON_BG_DEXOPT}, the job will be retried in the next <i>maintenance
         * window</i>. For information about <i>maintenance window</i>, see
         * https://developer.android.com/training/monitoring-device-state/doze-standby.
         *
         * Changing the reason is not allowed. Doing so will result in {@link IllegalStateException}
         * when {@link #dexoptPackages} is called.
         */
        void onBatchDexoptStart(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
                @NonNull @BatchDexoptReason String reason, @NonNull List<String> defaultPackages,
                @NonNull BatchDexoptParams.Builder builder,
                @NonNull CancellationSignal cancellationSignal);
    }

    /** @hide */
    @SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
    public interface ScheduleBackgroundDexoptJobCallback {
        /**
         * Mutates {@code builder} to override the configuration of the background dexopt job.
         *
         * The default configuration described in {@link
         * ArtManagerLocal#scheduleBackgroundDexoptJob()} is passed to the callback as the {@code
         * builder} argument.
         *
         * Setting {@link JobInfo.Builder#setRequiresStorageNotLow(boolean)} is not allowed. Doing
         * so will result in {@link IllegalStateException} when {@link
         * #scheduleBackgroundDexoptJob()} is called. ART Service has its own storage check, which
         * skips package dexopt when the storage is low. The storage check is enabled by
         * default for background dexopt jobs. {@link
         * #setBatchDexoptStartCallback(Executor, BatchDexoptStartCallback)} can be used to disable
         * the storage check by clearing the {@link ArtFlags#FLAG_SKIP_IF_STORAGE_LOW} flag.
         */
        void onOverrideJobInfo(@NonNull JobInfo.Builder builder);
    }

    /** @hide */
    @SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
    public interface DexoptDoneCallback {
        void onDexoptDone(@NonNull DexoptResult result);
    }

    /**
     * Represents an error that happens when snapshotting profiles.
     *
     * @hide
     */
    @SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
    public static class SnapshotProfileException extends Exception {
        public SnapshotProfileException(@NonNull Throwable cause) {
            super(cause);
        }
    }

    /**
     * Injector pattern for testing purpose.
     *
     * @hide
     */
    @VisibleForTesting
    public static class Injector {
        @Nullable private final ArtManagerLocal mArtManagerLocal;
        @Nullable private final Context mContext;
        @Nullable private final PackageManagerLocal mPackageManagerLocal;
        @Nullable private final Config mConfig;
        @Nullable private BackgroundDexoptJob mBgDexoptJob = null;

        Injector(@NonNull ArtManagerLocal artManagerLocal, @Nullable Context context) {
            mArtManagerLocal = artManagerLocal;
            mContext = context;
            if (context != null) {
                // We only need them on Android U and above, where a context is passed.
                mPackageManagerLocal = Objects.requireNonNull(
                        LocalManagerRegistry.getManager(PackageManagerLocal.class));
                mConfig = new Config();

                // Call the getters for the dependencies that aren't optional, to ensure correct
                // initialization order.
                getDexoptHelper(null);
                getUserManager();
                getDexUseManager();
                getStorageManager();
                ArtModuleServiceInitializer.getArtModuleServiceManager();
            } else {
                mPackageManagerLocal = null;
                mConfig = null;
            }
        }

        @NonNull
        public Context getContext() {
            return Objects.requireNonNull(mContext);
        }

        @NonNull
        public PackageManagerLocal getPackageManagerLocal() {
            return Objects.requireNonNull(mPackageManagerLocal);
        }

        @NonNull
        public IArtd getArtd() {
            return Utils.getArtd();
        }

        /**
         * Returns a new {@link DexoptHelper} instance.
         *
         * The {@link AppHibernationManager} reference may be null for boot time compilation runs,
         * when the app hibernation manager hasn't yet been initialized. It should not be null
         * otherwise. See comment in {@link ArtManagerLocal.dexoptPackages} for more details.
         */
        @NonNull
        public DexoptHelper getDexoptHelper(@Nullable AppHibernationManager appHibernationManager) {
            return new DexoptHelper(getContext(), getConfig(), appHibernationManager);
        }

        @NonNull
        public Config getConfig() {
            return mConfig;
        }

        /**
         * Returns the registered {@link AppHibernationManager} instance.
         *
         * @throws RuntimeException if called during boot before the app hibernation manager has
         *         started.
         */
        @NonNull
        public AppHibernationManager getAppHibernationManager() {
            return Objects.requireNonNull(mContext.getSystemService(AppHibernationManager.class));
        }

        /**
         * Returns the {@link BackgroundDexoptJob} instance.
         *
         * @throws RuntimeException if called during boot before the job scheduler service has
         *         started.
         */
        @NonNull
        public synchronized BackgroundDexoptJob getBackgroundDexoptJob() {
            if (mBgDexoptJob == null) {
                mBgDexoptJob = new BackgroundDexoptJob(mContext, mArtManagerLocal, mConfig);
            }
            return mBgDexoptJob;
        }

        @NonNull
        public UserManager getUserManager() {
            return Objects.requireNonNull(mContext.getSystemService(UserManager.class));
        }

        @NonNull
        public DexUseManagerLocal getDexUseManager() {
            return Objects.requireNonNull(
                    LocalManagerRegistry.getManager(DexUseManagerLocal.class));
        }

        @NonNull
        public boolean isSystemUiPackage(@NonNull String packageName) {
            return packageName.equals(mContext.getString(R.string.config_systemUi));
        }

        public long getCurrentTimeMillis() {
            return System.currentTimeMillis();
        }

        @NonNull
        public StorageManager getStorageManager() {
            return Objects.requireNonNull(mContext.getSystemService(StorageManager.class));
        }
    }
}
