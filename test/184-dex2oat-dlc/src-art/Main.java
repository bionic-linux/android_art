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

import dalvik.system.DelegateLastClassLoader;

public class Main {
    static final String TEST_NAME = "184-dex2oat-dlc";
    static final String EX_JAR_FILE = System.getenv("DEX_LOCATION") + "/" + TEST_NAME + "-ex.jar";

    public static void main(String[] args) throws Exception {
        System.loadLibrary(args[0]);

        assertTrue("Main app image is loaded", checkAppImageLoaded("184-dex2oat-dlc"));
        ClassLoader mainClassLoader = Main.class.getClassLoader();
        Class<?> dc_main = mainClassLoader.loadClass("DuplicateClass");
        assertTrue("App image contains Main", checkAppImageContains(Main.class));
        //assertTrue("App image contains DuplicateClass", checkAppImageContains(dc_main));

        assertFalse("Secondary app image is not loaded", checkAppImageLoaded("184-dex2oat-dlc-ex"));
        ClassLoader delegateLast = new DelegateLastClassLoader(EX_JAR_FILE, mainClassLoader);
        assertTrue("Secondary app image is loaded", checkAppImageLoaded("184-dex2oat-dlc-ex"));
        Class<?> dc_ex = delegateLast.loadClass("DuplicateClass");
        System.out.println("dc_main: " + dc_main + "; " + checkAppImageContains(dc_main));
        System.out.println("dc_ex: " + dc_ex + "; " + checkAppImageContains(dc_ex));
        System.out.println("(dc_main == dc_ex): " + (dc_main == dc_ex));
    }

    private static void assertTrue(String message, boolean flag) {
        if (flag) {
            return;
        }
        throw new AssertionError(message);
    }

    private static void assertFalse(String message, boolean flag) {
        if (!flag) {
            return;
        }
        throw new AssertionError(message);
    }

    public static native boolean checkAppImageLoaded(String name);
    public static native boolean checkAppImageContains(Class<?> klass);
}
