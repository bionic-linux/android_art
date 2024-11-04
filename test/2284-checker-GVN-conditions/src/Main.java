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
        $noinline$intTests();
        $noinline$floatTests();
    }

    private static void $noinline$intTests() {
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

    private static void $noinline$floatTests() {
        final float[] VALUES_LHS_FLOAT = {Float.NEGATIVE_INFINITY, -Float.MAX_VALUE, -1.0f,
                -Float.MIN_VALUE, -0.0f, +0.0f, Float.MIN_VALUE, +1.0f, Float.MAX_VALUE,
                Float.POSITIVE_INFINITY, Float.NaN};

        final float[] VALUES_RHS_FLOAT = {Float.NEGATIVE_INFINITY, -Float.MAX_VALUE, -1.0f,
                -Float.MIN_VALUE, -0.0f, +0.0f, Float.MIN_VALUE, +1.0f, Float.MAX_VALUE,
                Float.POSITIVE_INFINITY, Float.NaN};

        // Less or equal
        for (float lhs : VALUES_LHS_FLOAT) {
            for (float rhs : VALUES_RHS_FLOAT) {
                $noinline$assertBooleanEquals(
                        lhs <= rhs, $noinline$repeatedLessOrEqual_float(lhs, rhs));
            }
        }

        // Explicit if
        for (float lhs : VALUES_LHS_FLOAT) {
            for (float rhs : VALUES_RHS_FLOAT) {
                boolean result = (Float.isNaN(lhs) || Float.isNaN(rhs)) ? true : lhs <= rhs;
                $noinline$assertBooleanEquals(result, $noinline$lessOrEqualWithIf_float(lhs, rhs));
            }
        }

        // Greater or equal
        for (float lhs : VALUES_LHS_FLOAT) {
            for (float rhs : VALUES_RHS_FLOAT) {
                $noinline$assertBooleanEquals(
                        lhs >= rhs, $noinline$repeatedGreaterOrEqual_float(lhs, rhs));
            }
        }

        // Explicit if
        for (float lhs : VALUES_LHS_FLOAT) {
            for (float rhs : VALUES_RHS_FLOAT) {
                boolean result = (Float.isNaN(lhs) || Float.isNaN(rhs)) ? true : lhs >= rhs;
                $noinline$assertBooleanEquals(
                        result, $noinline$greaterOrEqualWithIf_float(lhs, rhs));
            }
        }
    }

    // Unsigned. We use helper methods to avoid calling Integer.compareUnsigned which will not get
    // inlined for host builds.
    /// CHECK-START: boolean Main.$noinline$alwaysTrue(int, int) GVN (before)
    /// CHECK: BelowOrEqual
    /// CHECK: Above
    //
    /// CHECK-START: boolean Main.$noinline$alwaysTrue(int, int) GVN (after)
    /// CHECK-NOT: Above
    //
    /// CHECK-START: boolean Main.$noinline$alwaysTrue(int, int) GVN (after)
    /// CHECK:     BelowOrEqual
    /// CHECK-NOT: Above
    private static boolean $noinline$alwaysTrue(int lhs, int rhs) {
        return $inline$AboveInteger(lhs, rhs) || $inline$BelowOrEqualInteger(lhs, rhs);
    }

    /// CHECK-START: boolean Main.$noinline$repeatedLessOrEqual(int, int) GVN (before)
    /// CHECK: Above
    /// CHECK: Above
    //
    /// CHECK-START: boolean Main.$noinline$repeatedLessOrEqual(int, int) GVN (after)
    /// CHECK:     Above
    /// CHECK-NOT: Above
    private static boolean $noinline$repeatedLessOrEqual(int lhs, int rhs) {
        return $inline$BelowOrEqualInteger(lhs, rhs) || $inline$BelowOrEqualInteger(lhs, rhs);
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

    private static boolean $inline$BelowOrEqualInteger(int x, int y) {
        return Integer.compare(x + Integer.MIN_VALUE, y + Integer.MIN_VALUE) <= 0;
    }

    private static boolean $inline$AboveInteger(int x, int y) {
        return Integer.compare(x + Integer.MIN_VALUE, y + Integer.MIN_VALUE) > 0;
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

    // Due to how the graph was constructed, the biases are the same before turning it opposite so
    // we cannot remove this one.

    /// CHECK-START: boolean Main.$noinline$repeatedLessOrEqual_float(float, float) GVN (before)
    /// CHECK: LessThanOrEqual bias:gt
    /// CHECK: GreaterThan bias:gt
    //
    /// CHECK-START: boolean Main.$noinline$repeatedLessOrEqual_float(float, float) GVN (before)
    /// CHECK: LessThanOrEqual bias:gt
    /// CHECK: GreaterThan bias:gt
    private static boolean $noinline$repeatedLessOrEqual_float(float lhs, float rhs) {
        return lhs <= rhs || lhs <= rhs;
    }

    /// CHECK-START: boolean Main.$noinline$lessOrEqualWithIf_float(float, float) GVN (before)
    /// CHECK: LessThanOrEqual bias:lt
    /// CHECK: GreaterThan bias:gt
    //
    /// CHECK-START: boolean Main.$noinline$lessOrEqualWithIf_float(float, float) GVN (after)
    /// CHECK-NOT: GreaterThan
    //
    /// CHECK-START: boolean Main.$noinline$lessOrEqualWithIf_float(float, float) GVN (after)
    /// CHECK:     LessThanOrEqual bias:lt
    /// CHECK-NOT: LessThanOrEqual
    private static boolean $noinline$lessOrEqualWithIf_float(float lhs, float rhs) {
        if (lhs > rhs) {
            return lhs <= rhs;
        } else {
            return true;
        }
    }

    // Due to how the graph was constructed, the biases are the same before turning it opposite so
    // we cannot remove this one.

    /// CHECK-START: boolean Main.$noinline$repeatedGreaterOrEqual_float(float, float) GVN (before)
    /// CHECK: GreaterThanOrEqual bias:lt
    /// CHECK: LessThan bias:lt
    //
    /// CHECK-START: boolean Main.$noinline$repeatedGreaterOrEqual_float(float, float) GVN (after)
    /// CHECK: GreaterThanOrEqual bias:lt
    /// CHECK: LessThan bias:lt
    private static boolean $noinline$repeatedGreaterOrEqual_float(float lhs, float rhs) {
        return lhs >= rhs || lhs >= rhs;
    }

    /// CHECK-START: boolean Main.$noinline$greaterOrEqualWithIf_float(float, float) GVN (before)
    /// CHECK: GreaterThanOrEqual bias:gt
    /// CHECK: LessThan bias:lt
    //
    /// CHECK-START: boolean Main.$noinline$greaterOrEqualWithIf_float(float, float) GVN (after)
    /// CHECK-NOT: LessThan
    //
    /// CHECK-START: boolean Main.$noinline$greaterOrEqualWithIf_float(float, float) GVN (after)
    /// CHECK:     GreaterThanOrEqual bias:gt
    /// CHECK-NOT: GreaterThanOrEqual
    private static boolean $noinline$greaterOrEqualWithIf_float(float lhs, float rhs) {
        if (lhs < rhs) {
            return lhs >= rhs;
        } else {
            return true;
        }
    }

    public static void $noinline$assertBooleanEquals(boolean expected, boolean result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }
}
