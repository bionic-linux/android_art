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
        assertEquals(0, $noinline$returnSecondIfEqualElseFirst(0, 0));
        assertEquals(0, $noinline$returnSecondIfEqualElseFirst(0, 1));

        assertEquals(0, $noinline$returnSecondIfNotEqualElseFirst(0, 0));
        assertEquals(1, $noinline$returnSecondIfNotEqualElseFirst(0, 1));
    }

    // If a == b returns b (which is equal to a) else returns a. This can be simplified to just
    // return a.

    /// CHECK-START: int Main.$noinline$returnSecondIfEqualElseFirst(int, int) instruction_simplifier$after_gvn (before)
    /// CHECK:     <<Param1:i\d+>> ParameterValue
    /// CHECK:     <<Param2:i\d+>> ParameterValue
    /// CHECK:     <<Select:i\d+>> Select [<<Param2>>,<<Param1>>,<<Cond:z\d+>>]
    /// CHECK:     <<Return:v\d+>> Return [<<Select>>]

    /// CHECK-START: int Main.$noinline$returnSecondIfEqualElseFirst(int, int) instruction_simplifier$after_gvn (after)
    /// CHECK:     <<Param1:i\d+>> ParameterValue
    /// CHECK:     <<Param2:i\d+>> ParameterValue
    /// CHECK:     <<Return:v\d+>> Return [<<Param1>>]

    /// CHECK-START: int Main.$noinline$returnSecondIfEqualElseFirst(int, int) instruction_simplifier$after_gvn (after)
    /// CHECK-NOT: Select
    private static int $noinline$returnSecondIfEqualElseFirst(int a, int b) {
        return a == b ? b : a;
    }

    // If a != b returns b else returns a (which is equal to b). This can be simplified to just
    // return b.

    /// CHECK-START: int Main.$noinline$returnSecondIfNotEqualElseFirst(int, int) instruction_simplifier$after_gvn (before)
    /// CHECK:     <<Param1:i\d+>> ParameterValue
    /// CHECK:     <<Param2:i\d+>> ParameterValue
    /// CHECK:     <<Select:i\d+>> Select [<<Param2>>,<<Param1>>,<<Cond:z\d+>>]
    /// CHECK:     <<Return:v\d+>> Return [<<Select>>]

    /// CHECK-START: int Main.$noinline$returnSecondIfNotEqualElseFirst(int, int) instruction_simplifier$after_gvn (after)
    /// CHECK:     <<Param1:i\d+>> ParameterValue
    /// CHECK:     <<Param2:i\d+>> ParameterValue
    /// CHECK:     <<Return:v\d+>> Return [<<Param2>>]

    /// CHECK-START: int Main.$noinline$returnSecondIfNotEqualElseFirst(int, int) instruction_simplifier$after_gvn (after)
    /// CHECK-NOT: Select
    private static int $noinline$returnSecondIfNotEqualElseFirst(int a, int b) {
        return a != b ? b : a;
    }

    private static void assertEquals(int expected, int result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }
}
