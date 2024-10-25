/*
 * Copyright (C) 2016 The Android Open Source Project
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

import java.io.File;
import java.io.IOException;
import java.lang.reflect.Method;

public class Main {

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);

    if (!hasJit()) {
      // Test requires JIT for creating profiling infos.
      return;
    }

    // Register `file2` with an empty jar. Even though `file2` is registered before `file`, the
    // runtime should not write bootclasspath methods to `file2`, and it should not even create
    // `file2`.
    File file2 = createTempFile();
    file2.deleteOnExit();
    String emptyJarPath =
            System.getenv("DEX_LOCATION") + "/res/art-gtest-jars-MainEmptyUncompressed.jar";
    VMRuntime.registerAppInfo("test.app", file2.getPath(), file2.getPath(),
            new String[] {emptyJarPath}, VMRuntime.CODE_PATH_TYPE_SPLIT_APK);

    File file = createTempFile();
    file.deleteOnExit();
    String codePath = System.getenv("DEX_LOCATION") + "/595-profile-saving.jar";
    VMRuntime.registerAppInfo("test.app", file.getPath(), file.getPath(), new String[] {codePath},
            VMRuntime.CODE_PATH_TYPE_PRIMARY_APK);

    File file3 = createTempFile();
    file3.deleteOnExit();
    String dexPath = System.getenv("DEX_LOCATION") + "/res/art-gtest-jars-Main.dex";
    VMRuntime.registerAppInfo("test.app", file3.getPath(), file3.getPath(), new String[] {dexPath},
            VMRuntime.CODE_PATH_TYPE_SPLIT_APK);

    // Delete the files so that we can check if the runtime creates them. The runtime should
    // create `file` and `file3` but not `file2`.
    file.delete();
    file2.delete();
    file3.delete();

    // Test that the runtime saves the profiling info of an app method in a .jar file.
    Method appMethod =
            Main.class.getDeclaredMethod("testAddMethodToProfile", File.class, Method.class);
    testAddMethodToProfile(file, appMethod);

    // Test that the runtime saves the profiling info of an app method in a .dex file.
    ClassLoader dexClassLoader = (ClassLoader) Class.forName("dalvik.system.PathClassLoader")
                                         .getDeclaredConstructor(String.class, ClassLoader.class)
                                         .newInstance(dexPath, null /* parent */);
    Class<?> c = Class.forName("Main", true /* initialize */, dexClassLoader);
    Method methodInDex = c.getMethod("main", (new String[0]).getClass());
    testAddMethodToProfile(file3, methodInDex);

    // Test that the runtime saves the profiling info of a bootclasspath method.
    Method bootMethod = File.class.getDeclaredMethod("exists");
    if (bootMethod.getDeclaringClass().getClassLoader() != Object.class.getClassLoader()) {
        System.out.println("Class loader does not match boot class");
    }
    testAddMethodToProfile(file, bootMethod);

    // We never expect System.console to be executed before Main.main gets invoked, and therefore
    // it should never be in a profile.
    Method bootNotInProfileMethod = System.class.getDeclaredMethod("console");
    testMethodNotInProfile(file, bootNotInProfileMethod);

    testProfileNotExist(file2);

    if (!isForBootImage(file.getPath())) {
        throw new Error("Expected profile to be for boot image");
    }

    // Test that the runtime deletes the notification file to notify other processes that a force
    // profile save is done.
    File notificationFile = new File(file.getParent() + "/pid_" + Os.getpid() + ".tmp");
    notificationFile.delete();
    if (!notificationFile.createNewFile()) {
        throw new Error("Failed to create notification file '" + notificationFile.getPath() + "'");
    }
    notificationFile.deleteOnExit();
    ensureProfileProcessing();
    if (notificationFile.exists()) {
        throw new Error("Expected the notification file '" + notificationFile.getPath()
                + "' to be deleted");
    }
  }

  static void testAddMethodToProfile(File file, Method m) {
    // Make sure we have a profile info for this method without the need to loop.
    ensureProfilingInfo(m);
    // Make sure the profile gets saved.
    ensureProfileProcessing();
    // Verify that the profile was saved and contains the method.
    if (!presentInProfile(file.getPath(), m)) {
      throw new RuntimeException("Expected method " + m + " to be in the profile");
    }
  }

  static void testMethodNotInProfile(File file, Method m) {
    // Make sure the profile gets saved.
    ensureProfileProcessing();
    // Verify that the profile was saved and contains the method.
    if (presentInProfile(file.getPath(), m)) {
      throw new RuntimeException("Did not expect method " + m + " to be in the profile");
    }
  }

  static void testProfileNotExist(File file) {
    // Make sure the profile saving has been attempted.
    ensureProfileProcessing();
    // Verify that the profile does not exist.
    if (file.exists()) {
      throw new RuntimeException("Did not expect " + file + " to exist");
    }
  }

  // Ensure a method has a profiling info.
  public static void ensureProfilingInfo(Method method) {
    ensureJitBaselineCompiled(method.getDeclaringClass(), method.getName());
  }
  public static native void ensureJitBaselineCompiled(Class<?> cls, String methodName);
  // Ensures the profile saver does its usual processing.
  public static native void ensureProfileProcessing();
  // Checks if the profiles saver knows about the method.
  public static native boolean presentInProfile(String profile, Method method);
  // Returns true if the profile is for the boot image.
  public static native boolean isForBootImage(String profile);
  public static native boolean hasJit();

  private static final String TEMP_FILE_NAME_PREFIX = "temp";
  private static final String TEMP_FILE_NAME_SUFFIX = "-file";

  static native String getProfileInfoDump(
      String filename);

  private static File createTempFile() throws Exception {
    try {
      return File.createTempFile(TEMP_FILE_NAME_PREFIX, TEMP_FILE_NAME_SUFFIX);
    } catch (IOException e) {
      System.setProperty("java.io.tmpdir", "/data/local/tmp");
      try {
        return File.createTempFile(TEMP_FILE_NAME_PREFIX, TEMP_FILE_NAME_SUFFIX);
      } catch (IOException e2) {
        System.setProperty("java.io.tmpdir", "/sdcard");
        return File.createTempFile(TEMP_FILE_NAME_PREFIX, TEMP_FILE_NAME_SUFFIX);
      }
    }
  }

  private static class VMRuntime {
    public static final int CODE_PATH_TYPE_PRIMARY_APK = 1 << 0;
    public static final int CODE_PATH_TYPE_SPLIT_APK = 1 << 1;
    private static final Method registerAppInfoMethod;

    static {
      try {
        Class<? extends Object> c = Class.forName("dalvik.system.VMRuntime");
        registerAppInfoMethod = c.getDeclaredMethod("registerAppInfo",
            String.class, String.class, String.class, String[].class, int.class);
      } catch (Exception e) {
        throw new RuntimeException(e);
      }
    }

    public static void registerAppInfo(
        String packageName,
        String curProfile,
        String refProfile,
        String[] codePaths,
        int codePathsType) throws Exception {
      registerAppInfoMethod.invoke(
          null,
          packageName,
          curProfile,
          refProfile,
          codePaths,
          codePathsType);
    }
  }

  private static class Os {
      public static int getpid() throws Exception {
          return (int) Class.forName("android.system.Os").getMethod("getpid").invoke(null);
      }
  }
}
