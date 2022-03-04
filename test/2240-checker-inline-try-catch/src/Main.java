/*
 * Copyright (C) 2022 The Android Open Source Project
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
  // TODO(solanes): Clean up and add more tests.
  public static void main(String[] args) {
    int[] numbers = {};
    // $inline$testSingleBlockFromTry();

    // $inline$outerOuterTryCatch(numbers);
    // $inline$outerTryCatch(numbers);
    // // try {
    //   int num = numbers[0];
    // } catch (Exception e) {
    //   $inline$tryCatch(numbers);
    //   // System.out.println("Hello from $inline$tryCatch");
    // }

    $inline$tryCatch(numbers);
    $inline$tryCatch(numbers);
    $inline$otherTryCatch(numbers);
    // try {
    //   int num = numbers[0];
    // } catch (Exception e) {
    // }
    // try {
    //   int num = numbers[0];
    // } catch (Exception e) {
    // }
    // testBoundsCheckAndCatch();
  }

  // private static int $inline$SingleBlock(String str) throws NumberFormatException {
  //   return Integer.parseInt(str);
  // }

  // public static void $inline$testSingleBlockFromTry() {
  //   // int val = 0;

  //   try {
  //     int val = $inline$SingleBlock("42");
  //   } catch (NumberFormatException ex) {
  //     // unreachable();
  //   }
  //   // assertEquals(42, val);

  //   try {
  //     $inline$SingleBlock("xyz");
  //     // unreachable();
  //   } catch (NumberFormatException ex) {}
  // }

  // Make sure this actually has a try catch block and we didn't eliminate it.
  // /// CHECK-START: void Main.$inline$tryCatch(int[]) register (after)
  // /// CHECK:       TryBoundary
  public static void $inline$tryCatch(int[] array) {
    try {
      int num = array[0];
    } catch (Exception e) {
      System.out.println("Hello from $inline$tryCatch");
    }
  }

  // public static void $inline$outerOuterTryCatch(int[] array) {
  //   int i = 1;
  //   $inline$outerTryCatch(array);
  //   // $inline$tryCatch(array);
  //   // $inline$otherTryCatch(array);
  // }

  // public static void $inline$outerTryCatch(int[] array) {
  //   $inline$tryCatch(array);
  //   // $inline$tryCatch(array);
  //   // $inline$otherTryCatch(array);
  // }

  public static void $inline$otherTryCatch(int[] array) {
    try {
      int num = array[0];
      // num += 1;
    } catch (Exception e) {
      System.out.println("Hello from $inline$otherTryCatch");
    }
  }

  // public static void $inline$multipleTryCatch(int[] array) {
  //   try {
  //     int num = array[0];
  //     // num += 1;
  //   } catch (Exception e) {
  //   }
  //   try {
  //     int num = array[1];
  //     // num += 1;
  //   } catch (Exception e) {
  //   }
  // }

  // Try catch into try and fail
  // Try catch into catch and fail
  // try catch inlined before non-inlined try catch
  // try catch inlined after non-inlined try catch

  // public static void boundsCheckAndCatch(int x, int[] a, int[] b) {
  //   a[x] = 1;
  //   try {
  //     a[x] = 2;
  //     a[x + 1] = b[0] / x;
  //   } catch (Exception e) {
  //     a[x] = 1;
  //   }
  // }

  // private static void expectEquals(int expected, int result) {
  //   if (expected != result) {
  //     throw new Error("Expected: " + expected + ", found: " + result);
  //   }
  // }

  // public final static int ARRAY_SIZE = 128;

  // public static void testBoundsCheckAndCatch() {
  //   int[] a = new int[ARRAY_SIZE];
  //   int[] b = new int[ARRAY_SIZE];

  //   int index = ARRAY_SIZE - 2;
  //   boundsCheckAndCatch(index, a, b);
  //   expectEquals(2, a[index]);

  //   index = ARRAY_SIZE - 1;
  //   boundsCheckAndCatch(index, a, b);
  //   expectEquals(1, a[index]);
  // }

  // Cases that work:

  // Using e.g.
  // /// CHECK-START: void Main.main(java.lang.String[]) inliner (before)
  // /// CHECK:       InvokeStaticOrDirect method_name:Main.$inline$twoLayerTryCatch

  // /// CHECK-START: void Main.main(java.lang.String[]) inliner (after)
  // /// CHECK-NOT:       InvokeStaticOrDirect

  // public static void $inline$simpleTryCatch(int[] array) {
  //   try {
  //     int num = array[0];
  //   } catch (Exception e) {
  //     System.out.println("Hello from $inline$simpleTryCatch");
  //   }
  // }

  // public static void $inline$twoLayerTryCatch(int[] array) {
  //   $inline$simpleTryCatch(array);
  // }
}