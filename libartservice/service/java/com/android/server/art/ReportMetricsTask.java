package com.android.server.art;

import com.android.server.art.model.DetailedDexInfo;
import com.android.server.art.model.DexMetadata;

/**
 * A runnable class to report dex2oat metrics to StatsD asynchronously.
 *
 * @hide
 */
public class ReportMetricsTask implements Runnable {
    private final int mAppId;
    private final int mCompilerFilter;
    private final int mCompilationReason;
    private final int mDexMetadataType;
    private final int mApkType;
    private final int mIsa;
    private final int mResultStatus;
    private final int mResultExitCode;
    private final int mResultSignal;
    private final int mArtifactsSize;
    private final int mCompilationTime;

    public ReportMetricsTask(int appId, String compilerFilter, String compilationReason,
            @DexMetadata.Type int dexMetadataType, DetailedDexInfo dexInfo, String isa,
            int resultStatus, int resultExitCode, int resultSignal, long artifactsSize,
            long compilationTime) {
        mAppId = appId;
        mCompilerFilter = translateCompilerFilter(compilerFilter);
        mCompilationReason = translateCompilationReason(compilationReason);
        mDexMetadataType = translateDexMetadataType(dexMetadataType);
        mApkType = getApkType(dexInfo);
        mIsa = translateIsa(isa);
        mResultStatus = resultStatus;
        mResultExitCode = resultExitCode;
        mResultSignal = resultSignal;
        mArtifactsSize = (int) artifactsSize / 1024;
        mCompilationTime = (int) compilationTime;
    }

    @Override
    public void run() {
        ArtStatsLog.write(ArtStatsLog.ART_DEX2OAT_REPORTED, mAppId, mCompilerFilter,
                mCompilationReason, mDexMetadataType, mApkType, mIsa, mResultStatus,
                mResultExitCode, mResultSignal, mArtifactsSize, mCompilationTime);
    }

    private static int translateCompilerFilter(String compilerFilter) {
        return switch (compilerFilter) {
            case "error" ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_ERROR;
            case "assume-verified" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_ASSUMED_VERIFIED;
            case "extract" ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_EXTRACT;
            case "verify" ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_VERIFY;
            case "space-profile" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_SPACE_PROFILE;
            case "space" ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_SPACE;
            case "speed-profile" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_SPEED_PROFILE;
            case "speed" ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_SPEED;
            case "everything-profile" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_EVERYTHING_PROFILE;
            case "everything" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_EVERYTHING;
            case "run-from-apk" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_FAKE_RUN_FROM_APK;
            case "run-from-apk-fallback" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_FAKE_RUN_FROM_APK_FALLBACK;
            default ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_UNKNOWN;
        };
    }

    private static int translateCompilationReason(String compilationReason) {
        return switch (compilationReason) {
            case "error" ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_ERROR;
            case "first-boot" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_FIRST_BOOT;
            case "boot-after-ota" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_BOOT_AFTER_OTA;
            case "post-boot" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_POST_BOOT;
            case "install" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL;
            case "install-fast" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL_FAST;
            case "install-bulk" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL_BULK;
            case "install-bulk-secondary" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL_BULK_SECONDARY;
            case "install-bulk-downgraded" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL_BULK_DOWNGRADED;
            case "install-bulk-secondary-downgraded" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL_BULK_SECONDARY_DOWNGRADED;
            case "bg-dexopt" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_BG_DEXOPT;
            case "ab-ota" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_AB_OTA;
            case "inactive" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INACTIVE;
            case "shared" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_SHARED;
            case "install-with-dex-metadata" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL_WITH_DEX_METADATA;
            case "prebuilt" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_PREBUILT;
            case "cmdline" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_CMDLINE;
            case "vdex" ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_VDEX;
            case "boot-after-mainline-update" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_BOOT_AFTER_MAINLINE_UPDATE;
            default ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_UNKNOWN;
        };
    }

    private static int translateIsa(String isa) {
        return switch (isa) {
            case "arm" -> ArtStatsLog.ART_DEX2_OAT_REPORTED__ISA__ART_ISA_ARM;
            case "arm64" -> ArtStatsLog.ART_DEX2_OAT_REPORTED__ISA__ART_ISA_ARM64;
            case "riscv64" -> ArtStatsLog.ART_DEX2_OAT_REPORTED__ISA__ART_ISA_RISCV64;
            case "x86" -> ArtStatsLog.ART_DEX2_OAT_REPORTED__ISA__ART_ISA_X86;
            case "x86_64" -> ArtStatsLog.ART_DATUM_DELTA_REPORTED__ISA__ART_ISA_X86;
            default -> ArtStatsLog.ART_DEX2_OAT_REPORTED__ISA__ART_ISA_UNKNOWN;
        };
    }

    private static int translateDexMetadataType(@DexMetadata.Type int dexMetadataType) {
        return switch (dexMetadataType) {
            case DexMetadata.TYPE_PROFILE ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_PROFILE;
            case DexMetadata.TYPE_VDEX ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_VDEX;
            case DexMetadata.TYPE_PROFILE_AND_VDEX ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_PROFILE_AND_VDEX;
            case DexMetadata.TYPE_NONE ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_NONE;
            case DexMetadata.TYPE_ERROR ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_ERROR;
            case DexMetadata.TYPE_UNKNOWN ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_UNKNOWN;
            default ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_UNKNOWN;
        };
    }

    private static int getApkType(DetailedDexInfo dexInfo) {
        if (dexInfo instanceof PrimaryDexUtils.PrimaryDexInfo primaryDexInfo) {
            return primaryDexInfo.splitName() == null
                    ? ArtStatsLog.ART_DEX2_OAT_REPORTED__APK_TYPE__ART_APK_TYPE_BASE
                    : ArtStatsLog.ART_DEX2_OAT_REPORTED__APK_TYPE__ART_APK_TYPE_SPLIT;
        } else if (dexInfo instanceof DexUseManagerLocal.CheckedSecondaryDexInfo) {
            return ArtStatsLog.ART_DEX2_OAT_REPORTED__APK_TYPE__ART_APK_TYPE_SECONDARY;
        }
        return ArtStatsLog.ART_DEX2_OAT_REPORTED__APK_TYPE__ART_APK_TYPE_UNKNOWN;
    }
}
