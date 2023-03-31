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

// Note that the empty string is present in the BootImage but ABCD is a BSS string. We are testing
// both AOT LoadString kinds.

public class Main {
    public static void main(String[] args) {
        $noinline$testLength();
        $noinline$testIsEmpty();
    }

    private static void $noinline$testLength() {
        assertEquals(0, $noinline$testLengthEmptyString());
        assertEquals(0, $noinline$testLengthEmptyStringWithInline());
        assertEquals(4, $noinline$testLengthABCDString());
        assertEquals(4, $noinline$testLengthABCDStringWithInline());
    }

    /// CHECK-START: int Main.$noinline$testLengthEmptyString() constant_folding (before)
    /// CHECK: LoadString load_kind:BootImageRelRo
    /// CHECK: <<Length:i\d+>> ArrayLength
    /// CHECK:                 Return [<<Length>>]

    /// CHECK-START: int Main.$noinline$testLengthEmptyString() constant_folding (after)
    /// CHECK: <<Const0:i\d+>> IntConstant 0
    /// CHECK:                 Return [<<Const0>>]

    /// CHECK-START: int Main.$noinline$testLengthEmptyString() dead_code_elimination$initial (after)
    /// CHECK-NOT: LoadString

    /// CHECK-START: int Main.$noinline$testLengthEmptyString() dead_code_elimination$initial (after)
    /// CHECK-NOT: ArrayLength
    private static int $noinline$testLengthEmptyString() {
        String str = "";
        return str.length();
    }

    /// CHECK-START: int Main.$noinline$testLengthEmptyStringWithInline() constant_folding$after_inlining (before)
    /// CHECK: LoadString load_kind:BootImageRelRo
    /// CHECK: <<Length:i\d+>> ArrayLength
    /// CHECK:                 Return [<<Length>>]

    /// CHECK-START: int Main.$noinline$testLengthEmptyStringWithInline() constant_folding$after_inlining (after)
    /// CHECK: <<Const0:i\d+>> IntConstant 0
    /// CHECK:                 Return [<<Const0>>]

    /// CHECK-START: int Main.$noinline$testLengthEmptyStringWithInline() dead_code_elimination$after_inlining (after)
    /// CHECK-NOT: LoadString

    /// CHECK-START: int Main.$noinline$testLengthEmptyStringWithInline() dead_code_elimination$after_inlining (after)
    /// CHECK-NOT: ArrayLength
    private static int $noinline$testLengthEmptyStringWithInline() {
        String str = "";
        return $inline$returnLength(str);
    }

    /// CHECK-START: int Main.$noinline$testLengthABCDString() constant_folding (before)
    /// CHECK: LoadString load_kind:BssEntry
    /// CHECK: <<Length:i\d+>> ArrayLength
    /// CHECK:                 Return [<<Length>>]

    /// CHECK-START: int Main.$noinline$testLengthABCDString() constant_folding (after)
    /// CHECK: <<Const4:i\d+>> IntConstant 4
    /// CHECK:                 Return [<<Const4>>]

    // We don't remove LoadString load_kind:BssEntry even if they have no uses, since IsRemovable()
    // returns true for them.
    /// CHECK-START: int Main.$noinline$testLengthABCDString() dead_code_elimination$initial (after)
    /// CHECK: LoadString load_kind:BssEntry

    /// CHECK-START: int Main.$noinline$testLengthABCDString() dead_code_elimination$initial (after)
    /// CHECK-NOT: ArrayLength
    private static int $noinline$testLengthABCDString() {
        String str = "ABCD";
        return str.length();
    }

    /// CHECK-START: int Main.$noinline$testLengthABCDStringWithInline() constant_folding$after_inlining (before)
    /// CHECK: LoadString load_kind:BssEntry
    /// CHECK: <<Length:i\d+>> ArrayLength
    /// CHECK:                 Return [<<Length>>]

    /// CHECK-START: int Main.$noinline$testLengthABCDStringWithInline() constant_folding$after_inlining (after)
    /// CHECK: <<Const4:i\d+>> IntConstant 4
    /// CHECK:                 Return [<<Const4>>]

    // We don't remove LoadString load_kind:BssEntry even if they have no uses, since IsRemovable()
    // returns true for them.
    /// CHECK-START: int Main.$noinline$testLengthABCDStringWithInline() dead_code_elimination$after_inlining (after)
    /// CHECK: LoadString load_kind:BssEntry

    /// CHECK-START: int Main.$noinline$testLengthABCDStringWithInline() dead_code_elimination$after_inlining (after)
    /// CHECK-NOT: ArrayLength
    private static int $noinline$testLengthABCDStringWithInline() {
        String str = "ABCD";
        return $inline$returnLength(str);
    }

    private static int $inline$returnLength(String str) {
        return str.length();
    }

    private static void $noinline$testIsEmpty() {
        assertEquals(true, $noinline$testIsEmptyEmptyString());
        assertEquals(true, $noinline$testIsEmptyEmptyStringWithInline());
        assertEquals(false, $noinline$testIsEmptyABCDString());
        assertEquals(false, $noinline$testIsEmptyABCDStringWithInline());
    }

    /// CHECK-START: boolean Main.$noinline$testIsEmptyEmptyString() constant_folding (before)
    /// CHECK: <<Const0:i\d+>> IntConstant 0
    /// CHECK: LoadString load_kind:BootImageRelRo
    /// CHECK: <<Length:i\d+>> ArrayLength
    /// CHECK: <<Eq:z\d+>>     Equal [<<Length>>,<<Const0>>]
    /// CHECK:                 Return [<<Eq>>]

    /// CHECK-START: boolean Main.$noinline$testIsEmptyEmptyString() constant_folding (after)
    /// CHECK: <<Const1:i\d+>> IntConstant 1
    /// CHECK:                 Return [<<Const1>>]

    /// CHECK-START: boolean Main.$noinline$testIsEmptyEmptyString() dead_code_elimination$initial (after)
    /// CHECK-NOT: LoadString

    /// CHECK-START: boolean Main.$noinline$testIsEmptyEmptyString() dead_code_elimination$initial (after)
    /// CHECK-NOT: ArrayLength
    private static boolean $noinline$testIsEmptyEmptyString() {
        String str = "";
        return str.isEmpty();
    }

    /// CHECK-START: boolean Main.$noinline$testIsEmptyEmptyStringWithInline() constant_folding$after_inlining (before)
    /// CHECK: <<Const0:i\d+>> IntConstant 0
    /// CHECK: LoadString load_kind:BootImageRelRo
    /// CHECK: <<Length:i\d+>> ArrayLength
    /// CHECK: <<Eq:z\d+>>     Equal [<<Length>>,<<Const0>>]
    /// CHECK:                 Return [<<Eq>>]

    /// CHECK-START: boolean Main.$noinline$testIsEmptyEmptyStringWithInline() constant_folding$after_inlining (after)
    /// CHECK: <<Const1:i\d+>> IntConstant 1
    /// CHECK:                 Return [<<Const1>>]

    /// CHECK-START: boolean Main.$noinline$testIsEmptyEmptyStringWithInline() dead_code_elimination$after_inlining (after)
    /// CHECK-NOT: LoadString

    /// CHECK-START: boolean Main.$noinline$testIsEmptyEmptyStringWithInline() dead_code_elimination$after_inlining (after)
    /// CHECK-NOT: ArrayLength
    private static boolean $noinline$testIsEmptyEmptyStringWithInline() {
        String str = "";
        return $inline$returnIsEmpty(str);
    }

    /// CHECK-START: boolean Main.$noinline$testIsEmptyABCDString() constant_folding (before)
    /// CHECK: <<Const0:i\d+>> IntConstant 0
    /// CHECK: LoadString load_kind:BssEntry
    /// CHECK: <<Length:i\d+>> ArrayLength
    /// CHECK: <<Eq:z\d+>>     Equal [<<Length>>,<<Const0>>]
    /// CHECK:                 Return [<<Eq>>]

    /// CHECK-START: boolean Main.$noinline$testIsEmptyABCDString() constant_folding (after)
    /// CHECK: <<Const0:i\d+>> IntConstant 0
    /// CHECK:                 Return [<<Const0>>]

    // We don't remove LoadString load_kind:BssEntry even if they have no uses, since IsRemovable()
    // returns true for them.
    /// CHECK-START: boolean Main.$noinline$testIsEmptyABCDString() dead_code_elimination$initial (after)
    /// CHECK: LoadString load_kind:BssEntry

    /// CHECK-START: boolean Main.$noinline$testIsEmptyABCDString() dead_code_elimination$initial (after)
    /// CHECK-NOT: ArrayLength
    private static boolean $noinline$testIsEmptyABCDString() {
        String str = "ABCD";
        return str.isEmpty();
    }

    /// CHECK-START: boolean Main.$noinline$testIsEmptyABCDStringWithInline() constant_folding$after_inlining (before)
    /// CHECK: <<Const0:i\d+>> IntConstant 0
    /// CHECK: LoadString load_kind:BssEntry
    /// CHECK: <<Length:i\d+>> ArrayLength
    /// CHECK: <<Eq:z\d+>>     Equal [<<Length>>,<<Const0>>]
    /// CHECK:                 Return [<<Eq>>]

    /// CHECK-START: boolean Main.$noinline$testIsEmptyABCDStringWithInline() constant_folding$after_inlining (after)
    /// CHECK: <<Const0:i\d+>> IntConstant 0
    /// CHECK:                 Return [<<Const0>>]

    // We don't remove LoadString load_kind:BssEntry even if they have no uses, since IsRemovable()
    // returns true for them.
    /// CHECK-START: boolean Main.$noinline$testIsEmptyABCDStringWithInline() dead_code_elimination$after_inlining (after)
    /// CHECK: LoadString load_kind:BssEntry

    /// CHECK-START: boolean Main.$noinline$testIsEmptyABCDStringWithInline() dead_code_elimination$after_inlining (after)
    /// CHECK-NOT: ArrayLength
    private static boolean $noinline$testIsEmptyABCDStringWithInline() {
        String str = "ABCD";
        return $inline$returnIsEmpty(str);
    }

    private static boolean $inline$returnIsEmpty(String str) {
        return str.isEmpty();
    }

    static void assertEquals(int expected, int actual) {
        if (expected != actual) {
            throw new AssertionError("Expected " + expected + " got " + actual);
        }
    }

    static void assertEquals(boolean expected, boolean actual) {
        if (expected != actual) {
            throw new AssertionError("Expected " + expected + " got " + actual);
        }
    }
}
