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

import java.lang.invoke.MethodType;
import java.lang.reflect.Method;
import java.util.Objects;

public class Main {
    public static void main(String... args) throws Throwable {
        testEquality();
        testNonEquality();
    }

    public static void testEquality() throws Throwable {
        Class returnMethodType = Class.forName("ReturnMethodType");

        MethodType actual = (MethodType) returnMethodType
                              .getDeclaredMethod("takesEverythingReturnsVoid")
                              .invoke(null);

        MethodType expected = MethodType.methodType(
            void.class,
            boolean.class,
            byte.class,
            char.class,
            short.class,
            int.class,
            long.class,
            float.class,
            double.class,
            Object.class,
            int[].class);

        assertSame(expected, actual);
    }

    public static void testNonEquality() throws Throwable {
        Class returnMethodType = Class.forName("ReturnMethodType");

        MethodType actual = (MethodType) returnMethodType
                              .getDeclaredMethod("takesEverythingReturnsVoid")
                              .invoke(null);

        MethodType expected = MethodType.methodType(
            boolean.class,
            boolean.class,
            byte.class,
            char.class,
            short.class,
            int.class,
            long.class,
            float.class,
            double.class,
            Object.class,
            int[].class);

        assertNotEqual(expected, actual);
    }

    public static void assertNotEqual(Object expected, Object actual) {
        if (Objects.equals(expected, actual)) {
            String msg = "Expected to be non equal, but got: " + expected;
            throw new AssertionError(msg);
        }
    }

    public static void assertSame(Object expected, Object actual) {
        if (actual != expected) {
            String msg = String.format("Expected: %s(%X), got %s(%X)",
                                  expected,
                                  System.identityHashCode(expected),
                                  actual,
                                  System.identityHashCode(actual));
            throw new AssertionError(msg);
        }
    }
}