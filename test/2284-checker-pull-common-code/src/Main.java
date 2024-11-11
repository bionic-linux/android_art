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

class TestClass {

    TestClass(int value) {
        this.i = value;
    }

    int i;
}

public class Main {
    public static void main(String[] args) {
        $noinline$testClinit(true);
        $noinline$assertIntEquals(4, $noinline$testArray(new int[] {1, 2, 3}, 0, 4, false));
        $noinline$assertIntEquals(0, $noinline$testArray(new int[] {1, 2, 3}, 0, 4, true));
        $noinline$assertIntEquals(12, $noinline$testInstanceFieldGet(new TestClass(10), false));
        $noinline$assertIntEquals(11, $noinline$testInstanceFieldGet(new TestClass(10), true));
    }

    static class ClassWithClinit {
        static {
            System.out.println("Main$ClassWithClinit's static initializer");
        }

        static void $noinline$foo() {}
        static void $noinline$bar() {}
    }

    // We can pull loading the class and clinit checks, even when the actual methods we are calling
    // are different.
    /// CHECK-START: void Main.$noinline$testClinit(boolean) code_pulling (before)
    /// CHECK: LoadClass
    /// CHECK: ClinitCheck
    /// CHECK: LoadClass
    /// CHECK: ClinitCheck

    /// CHECK-START: void Main.$noinline$testClinit(boolean) code_pulling (after)
    /// CHECK:     LoadClass
    /// CHECK-NOT: LoadClass

    /// CHECK-START: void Main.$noinline$testClinit(boolean) code_pulling (after)
    /// CHECK:     ClinitCheck
    /// CHECK-NOT: ClinitCheck
    private static void $noinline$testClinit(boolean b) {
        if (b) {
            ClassWithClinit.$noinline$foo();
        } else {
            ClassWithClinit.$noinline$bar();
        }
    }

    // We can pull null checks, array length and bounds checks. We have it thrice, but we can pull
    // and deduplicate two. This also lets GVN remove the third.
    /// CHECK-START: int Main.$noinline$testArray(int[], int, int, boolean) code_pulling (before)
    /// CHECK: NullCheck
    /// CHECK: ArrayLength
    /// CHECK: BoundsCheck
    /// CHECK: NullCheck
    /// CHECK: ArrayLength
    /// CHECK: BoundsCheck
    /// CHECK: NullCheck
    /// CHECK: ArrayLength
    /// CHECK: BoundsCheck

    /// CHECK-START: int Main.$noinline$testArray(int[], int, int, boolean) code_pulling (after)
    /// CHECK:     NullCheck
    /// CHECK:     NullCheck
    /// CHECK-NOT: NullCheck

    /// CHECK-START: int Main.$noinline$testArray(int[], int, int, boolean) code_pulling (after)
    /// CHECK:     ArrayLength
    /// CHECK:     ArrayLength
    /// CHECK-NOT: ArrayLength

    /// CHECK-START: int Main.$noinline$testArray(int[], int, int, boolean) code_pulling (after)
    /// CHECK:     BoundsCheck
    /// CHECK:     BoundsCheck
    /// CHECK-NOT: BoundsCheck

    /// CHECK-START: int Main.$noinline$testArray(int[], int, int, boolean) GVN (after)
    /// CHECK:     NullCheck
    /// CHECK-NOT: NullCheck

    /// CHECK-START: int Main.$noinline$testArray(int[], int, int, boolean) GVN (after)
    /// CHECK:     ArrayLength
    /// CHECK-NOT: ArrayLength

    /// CHECK-START: int Main.$noinline$testArray(int[], int, int, boolean) GVN (after)
    /// CHECK:     BoundsCheck
    /// CHECK-NOT: BoundsCheck
    private static int $noinline$testArray(int[] arr, int index, int value, boolean b) {
        if (b) {
            arr[index] = 0;
        } else {
            arr[index] = value;
        }
        return arr[index];
    }

    // We can pull both a NullCheck and an InstanceFieldGet. This also lets GVN take care of the
    // remaining NullCheck.

    /// CHECK-START: int Main.$noinline$testInstanceFieldGet(TestClass, boolean) code_pulling (before)
    /// CHECK: NullCheck
    /// CHECK: InstanceFieldGet
    /// CHECK: NullCheck
    /// CHECK: InstanceFieldSet
    /// CHECK: NullCheck
    /// CHECK: InstanceFieldGet
    /// CHECK: NullCheck
    /// CHECK: InstanceFieldSet
    /// CHECK: NullCheck
    /// CHECK: InstanceFieldGet

    /// CHECK-START: int Main.$noinline$testInstanceFieldGet(TestClass, boolean) code_pulling (after)
    /// CHECK:     InstanceFieldGet
    /// CHECK:     InstanceFieldGet
    /// CHECK-NOT: InstanceFieldGet

    /// CHECK-START: int Main.$noinline$testInstanceFieldGet(TestClass, boolean) code_pulling (after)
    /// CHECK:     NullCheck
    /// CHECK:     NullCheck
    /// CHECK:     NullCheck
    /// CHECK:     NullCheck
    /// CHECK-NOT: NullCheck

    /// CHECK-START: int Main.$noinline$testInstanceFieldGet(TestClass, boolean) GVN (after)
    /// CHECK:     NullCheck
    /// CHECK-NOT: NullCheck
    static int $noinline$testInstanceFieldGet(TestClass obj, boolean b) {
        if (b) {
            obj.i += 1;
        } else {
            obj.i += 2;
        }
        return obj.i;
    }

    public static void $noinline$assertIntEquals(int expected, int result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }
}
