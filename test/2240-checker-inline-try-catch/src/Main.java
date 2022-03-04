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
  public static void main(String[] args) {
    testSingleTryCatch();
    testSingleTryCatchTwice();
    testDifferentTryCatches();
    testRecursiveTryCatch();
    testDoNotInlineInsideTryOrCatch();
    testBeforeAfterTryCatch();
    testDifferentTypes();
  }

  public static void assertEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  // Basic try catch inline.
  private static void testSingleTryCatch() {
    int[] numbers = {};
    assertEquals(1, $inline$OOBTryCatch(numbers));
  }

  // Two instances of the same method with a try catch.
  private static void testSingleTryCatchTwice() {
    int[] numbers = {};
    assertEquals(1, $inline$OOBTryCatch(numbers));
    assertEquals(1, $inline$OOBTryCatch(numbers));
  }

  // Two different try catches, with the same catch's dex_pc.
  private static void testDifferentTryCatches() {
    int[] numbers = {};
    assertEquals(1, $inline$OOBTryCatch(numbers));
    assertEquals(2, $inline$OtherOOBTryCatch(numbers));
  }

  // Test that we can inline even when the try catch is several levels deep. Note that we are
  // testing inlining for [0..`number_of_recursions`] levels.
  // `number_of_recursions` must not be larger than art::kMaximumNumberOfRecursiveCalls. Otherwise,
  // the inliner will not inline due to hitting the recursive limit.
  private static void testRecursiveTryCatch() {
    int[] numbers = {};
    assertEquals(1, $inline$RecursiveOOBTryCatchLevel3(numbers));
  }

  /// CHECK-START: void Main.testDoNotInlineInsideTryOrCatch() inliner (before)
  /// CHECK:       InvokeStaticOrDirect method_name:Main.DoNotInlineOOBTryCatch
  /// CHECK:       InvokeStaticOrDirect method_name:Main.DoNotInlineOOBTryCatch

  /// CHECK-START: void Main.testDoNotInlineInsideTryOrCatch() inliner (after)
  /// CHECK:       InvokeStaticOrDirect method_name:Main.DoNotInlineOOBTryCatch
  /// CHECK:       InvokeStaticOrDirect method_name:Main.DoNotInlineOOBTryCatch
  private static void testDoNotInlineInsideTryOrCatch() {
    int val = 0;
    try {
      int[] numbers = {};
      val = DoNotInlineOOBTryCatch(numbers);
    } catch (Exception ex) {
      unreachable();
      // This is unreachable but we will still compile it so it works for checker purposes
      int[] numbers = {};
      DoNotInlineOOBTryCatch(numbers);
    }
    assertEquals(1, val);
  }

  private static void testBeforeAfterTryCatch() {
    int[] numbers = {};
    assertEquals(1, $inline$OOBTryCatch(numbers));

    // Unrelated try catch does not block inlining outside of it. We fill it in to make sure it is
    // still there by the time the inliner runs.
    int val = 0;
    try {
      int[] other_array = {};
      val = other_array[0];
    } catch (Exception ex) {
      assertEquals(0, val);
      val = 1;
    }
    assertEquals(1, val);

    assertEquals(1, $inline$OOBTryCatch(numbers));
  }

  private static void testDifferentTypes() {
    int[] numbers = {};
    assertEquals(1, $inline$OOBTryCatch(numbers));
    assertEquals(2, $inline$OtherOOBTryCatch(numbers));
    assertEquals(123, $inline$ParseIntTryCatch("123"));
    assertEquals(-1, $inline$ParseIntTryCatch("abc"));
  }

  // Building blocks for the test functions.
  private static int $inline$OOBTryCatch(int[] array) {
    try {
      return array[0];
    } catch (Exception e) {
      return 1;
    }
  }

  private static int $inline$OtherOOBTryCatch(int[] array) {
    try {
      return array[0];
    } catch (Exception e) {
      return 2;
    }
  }

  // If we make the recursiveness a parameter, we wouldn't be able to mark as $inline$ and we would
  // need extra CHECKer statements.
  private static int $inline$RecursiveOOBTryCatchLevel3(int[] array) {
    return $inline$RecursiveOOBTryCatchLevel2(array);
  }

  private static int $inline$RecursiveOOBTryCatchLevel2(int[] array) {
    return $inline$RecursiveOOBTryCatchLevel1(array);
  }

  private static int $inline$RecursiveOOBTryCatchLevel1(int[] array) {
    return $inline$RecursiveOOBTryCatchLevel0(array);
  }

  private static int $inline$RecursiveOOBTryCatchLevel0(int[] array) {
    return $inline$OOBTryCatch(array);
  }

  private static int DoNotInlineOOBTryCatch(int[] array) {
    try {
      return array[0];
    } catch (Exception e) {
      return 1;
    }
  }

  private static void unreachable() {
    throw new Error("Unreachable");
  }

  private static int $inline$ParseIntTryCatch(String str) {
    try {
      return Integer.parseInt(str);
    } catch (NumberFormatException ex) {
      return -1;
    }
  }
}