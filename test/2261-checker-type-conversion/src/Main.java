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

import java.lang.reflect.Method;

public class Main {
    public static void main(String[] args) throws Throwable {
        assertEquals(11L, $noinline$testIntToLong(0, 1));
        assertEquals(12L, $noinline$testIntToLong(1, 0));

        assertEquals(11f, $noinline$testIntToFloat(0, 1));
        assertEquals(12f, $noinline$testIntToFloat(1, 0));

        assertEquals(11, $noinline$testIntToByte(0, 1));
        assertEquals(12, $noinline$testIntToByte(1, 0));

        assertEquals(11, $noinline$testLongToInt(0, 1));
        assertEquals(12, $noinline$testLongToInt(1, 0));
    }

    /// CHECK-START: long Main.$noinline$testIntToLong(int, int) select_generator (after)
    /// CHECK:     <<Const1:i\d+>> IntConstant 1
    /// CHECK:     <<Const2:i\d+>> IntConstant 2
    /// CHECK:     <<Sel:i\d+>>    Select [<<Const1>>,<<Const2>>,<<Condition:z\d+>>]
    /// CHECK:                     TypeConversion [<<Sel>>]

    /// CHECK-START: long Main.$noinline$testIntToLong(int, int) constant_folding$after_gvn (after)
    /// CHECK:     <<Const1:j\d+>> LongConstant 1
    /// CHECK:     <<Const2:j\d+>> LongConstant 2
    /// CHECK:     <<Sel:j\d+>>    Select [<<Const1>>,<<Const2>>,<<Condition:z\d+>>]

    /// CHECK-START: long Main.$noinline$testIntToLong(int, int) constant_folding$after_gvn (after)
    /// CHECK-NOT:                 TypeConversion
    private static long $noinline$testIntToLong(int a, int b) {
        long result = 10;
        int c = 1;
        int d = 2;
        return result + (a < b ? c : d);
    }

    /// CHECK-START: float Main.$noinline$testIntToFloat(int, int) select_generator (after)
    /// CHECK:     <<Const1:i\d+>> IntConstant 1
    /// CHECK:     <<Const2:i\d+>> IntConstant 2
    /// CHECK:     <<Sel:i\d+>>    Select [<<Const1>>,<<Const2>>,<<Condition:z\d+>>]
    /// CHECK:                     TypeConversion [<<Sel>>]

    /// CHECK-START: float Main.$noinline$testIntToFloat(int, int) constant_folding$after_gvn (after)
    /// CHECK:     <<Const1:f\d+>> FloatConstant 1
    /// CHECK:     <<Const2:f\d+>> FloatConstant 2
    /// CHECK:     <<Sel:f\d+>>    Select [<<Const1>>,<<Const2>>,<<Condition:z\d+>>]

    /// CHECK-START: float Main.$noinline$testIntToFloat(int, int) constant_folding$after_gvn (after)
    /// CHECK-NOT:                 TypeConversion
    private static float $noinline$testIntToFloat(int a, int b) {
        float result = 10;
        int c = 1;
        int d = 2;
        return result + (a < b ? c : d);
    }

    /// CHECK-START: byte Main.$noinline$testIntToByte(int, int) select_generator (after)
    /// CHECK:     <<Const257:i\d+>> IntConstant 257
    /// CHECK:     <<Const258:i\d+>> IntConstant 258
    /// CHECK:     <<Sel:i\d+>>      Select [<<Const257>>,<<Const258>>,<<Condition:z\d+>>]
    /// CHECK:                       TypeConversion [<<Sel>>]

    /// CHECK-START: byte Main.$noinline$testIntToByte(int, int) constant_folding$after_gvn (after)
    /// CHECK:     <<Const1:i\d+>>   IntConstant 1
    /// CHECK:     <<Const2:i\d+>>   IntConstant 2
    /// CHECK:     <<Sel:i\d+>>      Select [<<Const1>>,<<Const2>>,<<Condition:z\d+>>]

    /// CHECK-START: byte Main.$noinline$testIntToByte(int, int) constant_folding$after_gvn (after)
    /// CHECK:                       TypeConversion
    /// CHECK-NOT:                   TypeConversion
    private static byte $noinline$testIntToByte(int a, int b) {
        byte result = 10;
        int c = 257; // equal to 1 in byte
        int d = 258; // equal to 2 in byte
        // Due to `+` operating in ints, we need to do an extra cast. We can't eliminate that second
        // type conversion since we have an Add in the middle.
        return (byte)(result + (byte)(a < b ? c : d));
    }

    /// CHECK-START: int Main.$noinline$testLongToInt(int, int) select_generator (after)
    /// CHECK:     <<Const1:j\d+>> LongConstant 4294967297
    /// CHECK:     <<Const2:j\d+>> LongConstant 4294967298
    /// CHECK:     <<Sel:j\d+>>    Select [<<Const1>>,<<Const2>>,<<Condition:z\d+>>]
    /// CHECK:                     TypeConversion [<<Sel>>]

    /// CHECK-START: int Main.$noinline$testLongToInt(int, int) constant_folding$after_gvn (after)
    /// CHECK:     <<Const1:i\d+>> IntConstant 1
    /// CHECK:     <<Const2:i\d+>> IntConstant 2
    /// CHECK:     <<Sel:i\d+>>    Select [<<Const1>>,<<Const2>>,<<Condition:z\d+>>]

    /// CHECK-START: int Main.$noinline$testLongToInt(int, int) constant_folding$after_gvn (after)
    /// CHECK-NOT:                 TypeConversion
    private static int $noinline$testLongToInt(int a, int b) {
        int result = 10;
        long c = (1L << 32) + 1L; // Will be 1, casted to int
        long d = (1L << 32) + 2L; // Will be 2, casted to int
        return result + (int)(a < b ? c : d);
    }

    private static void assertEquals(int expected, int actual) {
        if (expected != actual) {
            throw new AssertionError("Expected " + expected + " got " + actual);
        }
    }

    private static void assertEquals(long expected, long actual) {
        if (expected != actual) {
            throw new AssertionError("Expected " + expected + " got " + actual);
        }
    }

    private static void assertEquals(float expected, float actual) {
        if (expected != actual) {
            throw new AssertionError("Expected " + expected + " got " + actual);
        }
    }
}
