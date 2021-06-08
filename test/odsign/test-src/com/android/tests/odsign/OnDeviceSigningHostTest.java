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

package com.android.tests.odsign;

import static com.google.common.truth.Truth.assertWithMessage;

import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeTrue;

import android.cts.install.lib.host.InstallUtilsHost;

import com.android.tradefed.device.ITestDevice.ApexInfo;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.testtype.junit4.DeviceTestRunOptions;
import com.android.tradefed.util.CommandResult;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.time.Duration;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

@RunWith(DeviceJUnit4ClassRunner.class)
public class OnDeviceSigningHostTest extends BaseHostJUnit4Test {

    private static final String APEX_FILENAME = "test_com.android.art.apex";
    private static final String ART_APEX_DALVIK_CACHE_DIRNAME =
            "/data/misc/apexdata/com.android.art/dalvik-cache";

    private static final String TEST_APP_PACKAGE_NAME = "com.android.tests.odsign";
    private static final String TEST_APP_APK = "odsign_e2e_test_app.apk";

    private final InstallUtilsHost mInstallUtils = new InstallUtilsHost(this);

    private static final Duration BOOT_COMPLETE_TIMEOUT = Duration.ofMinutes(2);

    @Before
    public void setUp() throws Exception {
        assumeTrue("Updating APEX is not supported", mInstallUtils.isApexUpdateSupported());
        installPackage(TEST_APP_APK);
        mInstallUtils.installApexes(APEX_FILENAME);
        reboot();
    }

    @After
    public void cleanup() throws Exception {
        ApexInfo apex = mInstallUtils.getApexInfo(mInstallUtils.getTestFile(APEX_FILENAME));
        getDevice().uninstallPackage(apex.name);
        reboot();
    }

    @Test
    public void verifyArtUpgradeSignsFiles() throws Exception {
        DeviceTestRunOptions options = new DeviceTestRunOptions(TEST_APP_PACKAGE_NAME);
        options.setTestClassName(TEST_APP_PACKAGE_NAME + ".ArtifactsSignedTest");
        options.setTestMethodName("testArtArtifactsHaveFsverity");
        runDeviceTests(options);
    }

    @Test
    public void verifyArtUpgradeGeneratesRequiredArtifacts() throws Exception {
        DeviceTestRunOptions options = new DeviceTestRunOptions(TEST_APP_PACKAGE_NAME);
        options.setTestClassName(TEST_APP_PACKAGE_NAME + ".ArtifactsSignedTest");
        options.setTestMethodName("testGeneratesRequiredArtArtifacts");
        runDeviceTests(options);
    }

    private String[] getSystemServerClasspath() throws Exception {
        String systemServerClasspath =
                getDevice().executeShellCommand("echo $SYSTEMSERVERCLASSPATH");
        return systemServerClasspath.split(":");
    }

    private Set<String> getMappedSystemServerArtifacts() throws Exception {
        String systemServicePid = getDevice().executeShellCommand("pgrep system_server");
        assertTrue(systemServicePid != null);
        // system_server artifacts are in the APEX data dalvik cache and names all contain
        // the word "@classes". Look for mapped files that match this pattern in the proc map for
        // system_server.
        final String grepPattern = ART_APEX_DALVIK_CACHE_DIRNAME + ".*@classes";
        final String grepCommand =
                String.format("grep \"%s\" /proc/%s/maps", grepPattern, systemServicePid);
        CommandResult result = getDevice().executeShellV2Command(grepCommand);
        assertTrue(result.toString(), result.getExitCode() == 0);
        Set<String> mappedFiles = new HashSet<>();
        for (String line : result.getStdout().split("\\R")) {
            int start = line.indexOf(ART_APEX_DALVIK_CACHE_DIRNAME);
            if (line.contains("[")) {
                continue;  // ignore anonymously mapped sections which are quoted in square braces.
            }
            mappedFiles.add(line.substring(start));
        }
        return mappedFiles;
    }

    private String getSystemServerIsa(String mappedArtifact) {
        // Artifact path for system server artifacts has the form:
        //    ART_APEX_DALVIK_CACHE_DIRNAME + "/<arch>/system@framework@some.jar@classes.odex"
        // `mappedArtifacts` may include other artifacts, such as boot-framework.oat that are not
        // prefixed by the architecture.
        String[] pathComponents = mappedArtifact.split("/");
        return pathComponents[pathComponents.length - 2];
    }

    @Test
    public void verifySystemServerLoadedArtifacts() throws Exception {
        final boolean adbEnabled = getDevice().enableAdbRoot();
        assertTrue("ADB root failed and required to get process maps", adbEnabled);

        String[] classpathElements = getSystemServerClasspath();
        assertTrue("SYSTEMSERVERCLASSPATH is empty", classpathElements.length > 0);

        Set<String> mappedArtifacts = getMappedSystemServerArtifacts();
        assertTrue(
                "No mapped artifacts under " + ART_APEX_DALVIK_CACHE_DIRNAME,
                mappedArtifacts.size() > 0);
        final String isa = getSystemServerIsa(mappedArtifacts.iterator().next());
        final String[] extensions = new String[] {".art", ".odex", ".vdex"};

        for (String element : classpathElements) {
            // Skip system_server classpath elements from APEXes as these are not currently
            // compiled.
            if (element.startsWith("/apex")) {
                continue;
            }
            String escapedPath = element.substring(1).replace('/', '@');
            for (String extension : extensions) {
                final String fullArtifactPath =
                        String.format(
                                "%s/%s/%s@classes%s",
                                ART_APEX_DALVIK_CACHE_DIRNAME, isa, escapedPath, extension);
                assertTrue(
                        "Missing " + fullArtifactPath, mappedArtifacts.contains(fullArtifactPath));
            }
        }

        for (String mappedArtifact : mappedArtifacts) {
            // Check no APEX JAR artifacts mapped for system_server, if so policy around not
            // compiling APEX jars for system_server has changed and this test needs updating here
            // and in the check that skips over "/apex" paths above.
            assertTrue("Unexpected APEX jar", mappedArtifact.contains("/apex"));

            // Check mapped artifact has a .art, .odex or vdex extension.
            boolean knownArtifactKind =
                    Arrays.stream(extensions).anyMatch(e -> mappedArtifact.endsWith(e));
            assertTrue("Unknown artifact kind: " + mappedArtifact, knownArtifactKind);
        }
    }

    private void reboot() throws Exception {
        getDevice().reboot();
        boolean success = getDevice().waitForBootComplete(BOOT_COMPLETE_TIMEOUT.toMillis());
        assertWithMessage("Device didn't boot in %s", BOOT_COMPLETE_TIMEOUT).that(success).isTrue();
    }
}
