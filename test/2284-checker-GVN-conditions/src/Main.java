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

public class Main {
    public static void main(String[] args) {
        final int[] VALUES_LHS = {Integer.MIN_VALUE, -1, 0, +1, Integer.MAX_VALUE};
        final int[] VALUES_RHS = {Integer.MIN_VALUE, -1, 0, +1, Integer.MAX_VALUE};

        // Unsigned
        for (int lhs : VALUES_LHS) {
            for (int rhs : VALUES_RHS) {
                $noinline$assertBooleanEquals(true, $noinline$alwaysTrue(lhs, rhs));
            }
        }

        for (int lhs : VALUES_LHS) {
            for (int rhs : VALUES_RHS) {
                $noinline$assertBooleanEquals(Integer.compareUnsigned(lhs, rhs) <= 0,
                        $noinline$repeatedLessOrEqual(lhs, rhs));
            }
        }

        // Signed
        for (int lhs : VALUES_LHS) {
            for (int rhs : VALUES_RHS) {
                $noinline$assertBooleanEquals(true, $noinline$alwaysTrue_signed(lhs, rhs));
            }
        }

        for (int lhs : VALUES_LHS) {
            for (int rhs : VALUES_RHS) {
                $noinline$assertBooleanEquals(
                        lhs <= rhs, $noinline$repeatedLessOrEqual_signed(lhs, rhs));
            }
        }

        // Explicit if
        for (int lhs : VALUES_LHS) {
            for (int rhs : VALUES_RHS) {
                $noinline$assertBooleanEquals(lhs <= rhs, $noinline$lessOrEqualWithIf(lhs, rhs));
            }
        }

        for (int lhs : VALUES_LHS) {
            for (int rhs : VALUES_RHS) {
                $noinline$assertBooleanEquals(lhs <= rhs, $noinline$lessOrEqualWithIf_v2(lhs, rhs));
            }
        }
    }

    // Unsigned. For target builds, compareUnsigned will get inlined and we can GVN. For host
    // builds, we will have a call and therefore won't be able to GVN the conditions.
    /// CHECK-START-{ARM,ARM64}: boolean Main.$noinline$alwaysTrue(int, int) GVN (before)
    /// CHECK: Above
    /// CHECK: Above
    //
    /// CHECK-START-{ARM,ARM64}: boolean Main.$noinline$alwaysTrue(int, int) GVN (after)
    /// CHECK:     Above
    /// CHECK-NOT: Above
    private static boolean $noinline$alwaysTrue(int lhs, int rhs) {
        return Integer.compareUnsigned(lhs, rhs) > 0 || Integer.compareUnsigned(lhs, rhs) <= 0;
    }

    /// CHECK-START-{ARM,ARM64}: boolean Main.$noinline$repeatedLessOrEqual(int, int) GVN (before)
    /// CHECK: BelowOrEqual
    /// CHECK: Above
    //
    /// CHECK-START-{ARM,ARM64}: boolean Main.$noinline$repeatedLessOrEqual(int, int) GVN (after)
    /// CHECK-NOT: Above
    //
    /// CHECK-START-{ARM,ARM64}: boolean Main.$noinline$repeatedLessOrEqual(int, int) GVN (after)
    /// CHECK:     BelowOrEqual
    /// CHECK-NOT: BelowOrEqual
    private static boolean $noinline$repeatedLessOrEqual(int lhs, int rhs) {
        return Integer.compareUnsigned(lhs, rhs) <= 0 || Integer.compareUnsigned(lhs, rhs) <= 0;
    }

    // Signed
    /// CHECK-START: boolean Main.$noinline$alwaysTrue_signed(int, int) GVN (before)
    /// CHECK: LessThanOrEqual
    /// CHECK: LessThanOrEqual
    //
    /// CHECK-START: boolean Main.$noinline$alwaysTrue_signed(int, int) GVN (after)
    /// CHECK:     LessThanOrEqual
    /// CHECK-NOT: LessThanOrEqual
    private static boolean $noinline$alwaysTrue_signed(int lhs, int rhs) {
        return lhs <= rhs || lhs > rhs;
    }

    /// CHECK-START: boolean Main.$noinline$repeatedLessOrEqual_signed(int, int) GVN (before)
    /// CHECK: LessThanOrEqual
    /// CHECK: GreaterThan
    //
    /// CHECK-START: boolean Main.$noinline$repeatedLessOrEqual_signed(int, int) GVN (after)
    /// CHECK-NOT: GreaterThan
    //
    /// CHECK-START: boolean Main.$noinline$repeatedLessOrEqual_signed(int, int) GVN (after)
    /// CHECK:     LessThanOrEqual
    /// CHECK-NOT: LessThanOrEqual
    private static boolean $noinline$repeatedLessOrEqual_signed(int lhs, int rhs) {
        return lhs <= rhs || lhs <= rhs;
    }

    // Explicit if
    /// CHECK-START: boolean Main.$noinline$lessOrEqualWithIf(int, int) GVN (before)
    /// CHECK: LessThanOrEqual
    /// CHECK: GreaterThan
    //
    /// CHECK-START: boolean Main.$noinline$lessOrEqualWithIf(int, int) GVN (after)
    /// CHECK-NOT: GreaterThan
    //
    /// CHECK-START: boolean Main.$noinline$lessOrEqualWithIf(int, int) GVN (after)
    /// CHECK:     LessThanOrEqual
    /// CHECK-NOT: LessThanOrEqual
    private static boolean $noinline$lessOrEqualWithIf(int lhs, int rhs) {
        if (lhs > rhs) {
            return lhs <= rhs;
        } else {
            return true;
        }
    }

    // TODO(solanes): SelectGenerator gets in the way and we end up with code that could be better
    // since it is essentially `return lhs <= rhs`.

    /// CHECK-START: boolean Main.$noinline$lessOrEqualWithIf_v2(int, int) GVN (before)
    /// CHECK: GreaterThan
    /// CHECK: GreaterThan
    //
    /// CHECK-START: boolean Main.$noinline$lessOrEqualWithIf_v2(int, int) GVN (after)
    /// CHECK:     GreaterThan
    /// CHECK-NOT: GreaterThan
    //
    /// CHECK-START: boolean Main.$noinline$lessOrEqualWithIf_v2(int, int) disassembly (after)
    /// CHECK: <<Const0:i\d+>> IntConstant 0
    /// CHECK: <<GT:z\d+>>     GreaterThan
    /// CHECK: <<BN:z\d+>>     BooleanNot [<<GT>>]
    /// CHECK:                 Select [<<BN>>,<<Const0>>,<<GT>>]
    private static boolean $noinline$lessOrEqualWithIf_v2(int lhs, int rhs) {
        if (lhs <= rhs) {
            return lhs <= rhs;
        } else {
            return false;
        }
    }

    public static void $noinline$assertBooleanEquals(boolean expected, boolean result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }
}
