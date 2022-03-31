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

class Point {
  int x;
  int y;
}

public class Main {

  public static void main(String[] args) {
    boolean boolean_throw = false;
    boolean boolean_other_throw = false;
    assertEquals(3, testDifferentFields(new Point(), new Point(), boolean_throw, boolean_other_throw));
    assertEquals(1, testRedundantStore(new Point(), boolean_throw, boolean_other_throw));
    assertEquals(1, testTryCatchBlocking(new Point(), boolean_throw));
    assertEquals(1, testTryCatchPhi(new Point(), boolean_throw));
    assertEquals(2, testTryCatchPhiWithTwoCatches(new Point(), new int[0]));
  }

  /// CHECK-START: int Main.testDifferentFields(Point, Point, boolean, boolean) load_store_elimination (before)
  /// CHECK: InstanceFieldSet field_name:Point.x
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK: InstanceFieldGet field_name:Point.x
  /// CHECK: InstanceFieldGet field_name:Point.y

  /// CHECK-START: int Main.testDifferentFields(Point, Point, boolean, boolean) load_store_elimination (after)
  /// CHECK: InstanceFieldSet field_name:Point.x
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK-NOT: NullCheck
  /// CHECK-NOT: InstanceFieldGet field_name:Point.x
  /// CHECK-NOT: InstanceFieldGet field_name:Point.y

  // Consistency check to make sure the try/catches weren't removed by an earlier pass.
  /// CHECK-START: int Main.testDifferentFields(Point, Point, boolean, boolean) load_store_elimination (after)
  /// CHECK: TryBoundary kind:entry
  /// CHECK: TryBoundary kind:entry

  // Different fields shouldn't alias.
  private static int testDifferentFields(Point obj1, Point obj2, boolean boolean_throw, boolean boolean_other_throw) {
    try {
      if (boolean_throw) {
        throw new Error();
      }
    } catch (Error e) {
      return 0;
    }
    obj1.x = 1;
    obj2.y = 2;
    int result = obj1.x + obj2.y;
    try {
      if (boolean_other_throw) {
        throw new Error();
      }
    } catch (Error e) {
      return 0;
    }
    return result;
  }

  /// CHECK-START: int Main.testRedundantStore(Point, boolean, boolean) load_store_elimination (before)
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK: InstanceFieldGet field_name:Point.y

  /// CHECK-START: int Main.testRedundantStore(Point, boolean, boolean) load_store_elimination (after)
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK-NOT: NullCheck
  /// CHECK-NOT: InstanceFieldSet field_name:Point.y
  /// CHECK-NOT: InstanceFieldGet field_name:Point.y

  // Consistency check to make sure the try/catches weren't removed by an earlier pass.
  /// CHECK-START: int Main.testRedundantStore(Point, boolean, boolean) load_store_elimination (after)
  /// CHECK: TryBoundary kind:entry
  /// CHECK: TryBoundary kind:entry

  // Redundant store of the same value.
  private static int testRedundantStore(Point obj, boolean boolean_throw, boolean boolean_other_throw) {
    try {
      if (boolean_throw) {
        throw new Error();
      }
    } catch (Error e) {
      return 0;
    }
    obj.y = 1;
    obj.y = 1;
    try {
      if (boolean_other_throw) {
        throw new Error();
      }
    } catch (Error e) {
      return 0;
    }
    return obj.y;
  }

  /// CHECK-START: int Main.testTryCatchBlocking(Point, boolean) load_store_elimination (before)
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK: InstanceFieldGet field_name:Point.y

  /// CHECK-START: int Main.testTryCatchBlocking(Point, boolean) load_store_elimination (after)
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK: InstanceFieldGet field_name:Point.y

  // Consistency check to make sure the try/catch wasn't removed by an earlier pass.
  /// CHECK-START: int Main.testTryCatchBlocking(Point, boolean) load_store_elimination (after)
  /// CHECK: TryBoundary kind:entry

  // We cannot remove the Get since we might have thrown.
  private static int testTryCatchBlocking(Point obj, boolean boolean_throw) {
    obj.y = 1;
    try {
      if (boolean_throw) {
        throw new Error();
      }
    } catch (Error e) {
    }
    return obj.y;
  }

  /// CHECK-START: int Main.testTryCatchPhi(Point, boolean) load_store_elimination (before)
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK: InstanceFieldGet field_name:Point.y

  /// CHECK-START: int Main.testTryCatchPhi(Point, boolean) load_store_elimination (after)
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK-NOT: InstanceFieldGet field_name:Point.y

  // Consistency check to make sure the try/catch wasn't removed by an earlier pass.
  /// CHECK-START: int Main.testTryCatchPhi(Point, boolean) load_store_elimination (after)
  /// CHECK: TryBoundary kind:entry

  // We either threw and we set the value in the catch, or we didn't throw and we set the value
  // before the catch. We can solve that with a Phi and skip the get.
  private static int testTryCatchPhi(Point obj, boolean boolean_throw) {
    obj.y = 1;
    try {
      if (boolean_throw) {
        throw new Error();
      }
    } catch (Error e) {
      obj.y = 2;
    }
    return obj.y;
  }


  /// CHECK-START: int Main.testTryCatchPhiWithTwoCatches(Point, int[]) load_store_elimination (before)
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK: InstanceFieldGet field_name:Point.y

  /// CHECK-START: int Main.testTryCatchPhiWithTwoCatches(Point, int[]) load_store_elimination (after)
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK: InstanceFieldSet field_name:Point.y
  /// CHECK-NOT: InstanceFieldGet field_name:Point.y

  // Consistency check to make sure the try/catch wasn't removed by an earlier pass.
  /// CHECK-START: int Main.testTryCatchPhiWithTwoCatches(Point, int[]) load_store_elimination (after)
  /// CHECK: TryBoundary kind:entry
  private static int testTryCatchPhiWithTwoCatches(Point obj, int[] numbers) {
    obj.y = 1;
    try {
      if (numbers[0] == 1) {
        throw new Error();
      }
    } catch (ArrayIndexOutOfBoundsException e) {
      obj.y = 2;
    } catch (Error e) {
      obj.y = 3;
    }
    return obj.y;
  }

  private static void assertEquals(int expected, int actual) {
    if (expected != actual) {
      throw new AssertionError("Expected: " + expected + ", Actual: " + actual);
    }
  }
}

