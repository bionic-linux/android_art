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

public class Main {
    public static void main(String[] args) {
        assertEquals(1, $noinline$testEqualBool(true));
        assertEquals(1, $noinline$testNotEqualBool(true));

        assertEquals(1, $noinline$testEqualInt(0));
        assertEquals(1, $noinline$testNotEqualInt(0));

        assertEquals(1, $noinline$testEqualLong(0L));
        assertEquals(1, $noinline$testNotEqualLong(0L));

        // We cannot perform the optimization on unknown float/doubles since equality for NaN
        // returns the opposite as for normal numbers.
        assertEquals(1, $noinline$testEqualFloat(0f));
        assertEquals(1, $noinline$testNotEqualFloat(0f));
        assertEquals(0, $noinline$testEqualFloat(Float.NaN));
        assertEquals(0, $noinline$testNotEqualFloat(Float.NaN));

        assertEquals(1, $noinline$testEqualDouble(0d));
        assertEquals(1, $noinline$testNotEqualDouble(0d));
        assertEquals(0, $noinline$testEqualDouble(Double.NaN));
        assertEquals(0, $noinline$testNotEqualDouble(Double.NaN));

        // We can fold the comparisons into constants for known NaNs.
        assertEquals(0, $noinline$testEqualKnownFloatNaN());
        assertEquals(0, $noinline$testNotEqualKnownFloatNaN());
        assertEquals(0, $noinline$testEqualKnownDoubleNaN());
        assertEquals(0, $noinline$testNotEqualKnownDoubleNaN());
    }

    /// CHECK-START: int Main.$noinline$testEqualBool(boolean) register (after)
    /// CHECK: <<Const1:i\d+>> IntConstant 1
    /// CHECK:                 Return [<<Const1>>]
    private static int $noinline$testEqualBool(boolean a) {
        if (a == $inline$returnValueBool(a)) {
            return 1;
        } else {
            return 0;
        }
    }

    /// CHECK-START: int Main.$noinline$testNotEqualBool(boolean) register (after)
    /// CHECK: <<Const1:i\d+>> IntConstant 1
    /// CHECK:                 Return [<<Const1>>]
    private static int $noinline$testNotEqualBool(boolean a) {
        if (a != $inline$returnValueBool(a)) {
            return 0;
        } else {
            return 1;
        }
    }

    private static boolean $inline$returnValueBool(boolean a) {
        return a;
    }

    /// CHECK-START: int Main.$noinline$testEqualInt(int) register (after)
    /// CHECK: <<Const1:i\d+>> IntConstant 1
    /// CHECK:                 Return [<<Const1>>]
    private static int $noinline$testEqualInt(int a) {
        if (a == $inline$returnValueInt(a)) {
            return 1;
        } else {
            return 0;
        }
    }

    /// CHECK-START: int Main.$noinline$testNotEqualInt(int) register (after)
    /// CHECK: <<Const1:i\d+>> IntConstant 1
    /// CHECK:                 Return [<<Const1>>]
    private static int $noinline$testNotEqualInt(int a) {
        if (a != $inline$returnValueInt(a)) {
            return 0;
        } else {
            return 1;
        }
    }

    private static int $inline$returnValueInt(int a) {
        return a;
    }

    /// CHECK-START: int Main.$noinline$testEqualLong(long) register (after)
    /// CHECK: <<Const1:i\d+>> IntConstant 1
    /// CHECK:                 Return [<<Const1>>]
    private static int $noinline$testEqualLong(long a) {
        if (a == $inline$returnValueLong(a)) {
            return 1;
        } else {
            return 0;
        }
    }

    /// CHECK-START: int Main.$noinline$testNotEqualLong(long) register (after)
    /// CHECK: <<Const1:i\d+>> IntConstant 1
    /// CHECK:                 Return [<<Const1>>]
    private static int $noinline$testNotEqualLong(long a) {
        if (a != $inline$returnValueLong(a)) {
            return 0;
        } else {
            return 1;
        }
    }

    private static long $inline$returnValueLong(long a) {
        return a;
    }

    /// CHECK-START: int Main.$noinline$testEqualFloat(float) register (after)
    /// CHECK: <<NotEqual:z\d+>> NotEqual
    /// CHECK: <<BNot:z\d+>>     BooleanNot [<<NotEqual>>]
    /// CHECK:                   Return [<<BNot>>]
    private static int $noinline$testEqualFloat(float a) {
        if (a == $inline$returnValueFloat(a)) {
            return 1;
        } else {
            return 0;
        }
    }

    /// CHECK-START: int Main.$noinline$testNotEqualFloat(float) register (after)
    /// CHECK: <<Equal:z\d+>>    Equal
    /// CHECK:                   Return [<<Equal>>]
    private static int $noinline$testNotEqualFloat(float a) {
        if (a != $inline$returnValueFloat(a)) {
            return 0;
        } else {
            return 1;
        }
    }

    /// CHECK-START: int Main.$noinline$testEqualKnownFloatNaN() register (after)
    /// CHECK: <<Const1:i\d+>> IntConstant 0
    /// CHECK:                 Return [<<Const1>>]
    private static int $noinline$testEqualKnownFloatNaN() {
        if (Float.NaN == $inline$returnValueFloat(Float.NaN)) {
            return 1;
        } else {
            return 0;
        }
    }

    /// CHECK-START: int Main.$noinline$testNotEqualKnownFloatNaN() register (after)
    /// CHECK: <<Const0:i\d+>> IntConstant 0
    /// CHECK:                 Return [<<Const0>>]
    private static int $noinline$testNotEqualKnownFloatNaN() {
        if (Float.NaN != $inline$returnValueFloat(Float.NaN)) {
            return 0;
        } else {
            return 1;
        }
    }

    private static float $inline$returnValueFloat(float a) {
        return a;
    }

    /// CHECK-START: int Main.$noinline$testEqualDouble(double) register (after)
    /// CHECK: <<NotEqual:z\d+>> NotEqual
    /// CHECK: <<BNot:z\d+>>     BooleanNot [<<NotEqual>>]
    /// CHECK:                   Return [<<BNot>>]
    private static int $noinline$testEqualDouble(double a) {
        if (a == $inline$returnValueDouble(a)) {
            return 1;
        } else {
            return 0;
        }
    }

    /// CHECK-START: int Main.$noinline$testNotEqualDouble(double) register (after)
    /// CHECK: <<Equal:z\d+>>    Equal
    /// CHECK:                   Return [<<Equal>>]
    private static int $noinline$testNotEqualDouble(double a) {
        if (a != $inline$returnValueDouble(a)) {
            return 0;
        } else {
            return 1;
        }
    }

    /// CHECK-START: int Main.$noinline$testEqualKnownDoubleNaN() register (after)
    /// CHECK: <<Const1:i\d+>> IntConstant 0
    /// CHECK:                 Return [<<Const1>>]
    private static int $noinline$testEqualKnownDoubleNaN() {
        if (Double.NaN == $inline$returnValueDouble(Double.NaN)) {
            return 1;
        } else {
            return 0;
        }
    }

    /// CHECK-START: int Main.$noinline$testNotEqualKnownDoubleNaN() register (after)
    /// CHECK: <<Const0:i\d+>> IntConstant 0
    /// CHECK:                 Return [<<Const0>>]
    private static int $noinline$testNotEqualKnownDoubleNaN() {
        if (Double.NaN != $inline$returnValueDouble(Double.NaN)) {
            return 0;
        } else {
            return 1;
        }
    }

    private static double $inline$returnValueDouble(double a) {
        return a;
    }

    static void assertEquals(int expected, int actual) {
        if (expected != actual) {
            throw new AssertionError("Expected " + expected + " got " + actual);
        }
    }
}
