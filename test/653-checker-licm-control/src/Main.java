/*
 * Copyright (C) 2017 The Android Open Source Project
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

/**
 * Tests for LICM of control dependence.
 */
public class Main {

  /// CHECK-START: int Main.hoistOneControl(int) licm (before)
  /// CHECK-DAG: <<Phi:i\d+>> Phi  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG:              If   loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.hoistOneControl(int) licm (after)
  /// CHECK-DAG: <<Phi:i\d+>> Phi  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-NOT:              If   loop:<<Loop>>      outer_loop:none
  private static int hoistOneControl(int x) {
    int i = 0;
    while (true) {
      if (x == 0)
        return 1;
      i++;
    }
  }

  /// CHECK-START: int Main.hoistOneControl(int, int) licm (before)
  /// CHECK-DAG: <<Phi:i\d+>> Phi  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG:              If   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:              If   loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.hoistOneControl(int, int) licm (after)
  /// CHECK-DAG: <<Phi:i\d+>> Phi  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG:              If   loop:<<Loop>>      outer_loop:none
  /// CHECK-NOT:              If   loop:<<Loop>>      outer_loop:none
  private static int hoistOneControl(int x, int y) {
    while (true) {
      if (x == 0)
        return 1;
      if (y == 0)  // no longer invariant
        return 2;
      y--;
    }
  }

  /// CHECK-START: int Main.hoistTwoControl(int, int, int) licm (before)
  /// CHECK-DAG: <<Phi:i\d+>> Phi  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG:              If   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:              If   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:              If   loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.hoistTwoControl(int, int, int) licm (after)
  /// CHECK-DAG: <<Phi:i\d+>> Phi  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG:              If   loop:<<Loop>>      outer_loop:none
  /// CHECK-NOT:              If   loop:<<Loop>>      outer_loop:none
  private static int hoistTwoControl(int x, int y, int z) {
    while (true) {
      if (x == 0)
        return 1;
      if (y == 0)
        return 2;
      if (z == 0)  // no longer invariant
        return 3;
      z--;
    }
  }

  // Often used idiom that, when not hoisted, prevents BCE and vectorization.
  //
  /// CHECK-START: void Main.addInts(int[]) licm (before)
  /// CHECK-DAG: <<Phi:i\d+>> Phi         loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG:              If          loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:              If          loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:              BoundsCheck loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: void Main.addInts(int[]) licm (after)
  /// CHECK-DAG: <<Phi:i\d+>> Phi  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG:              If   loop:<<Loop>>      outer_loop:none
  /// CHECK-NOT:              If   loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: void Main.addInts(int[]) BCE (after)
  /// CHECK-NOT:              BoundsCheck
  //
  // TODO: fix strange BoundType issue
  /// CHECK-START-ARM64: void Main.addInts(int[]) loop_optimization (after)
  /// CHECK-NOT:              VecLoad
  /// CHECK-NOT:              VecAdd
  /// CHECK-NOT:              VecStore
  private static void addInts(int[] a) {
    for (int i = 0; a != null && i < a.length; i++) {
      a[i] += 1;
    }
  }

  public static void main(String[] args) {
    expectEquals(1, hoistOneControl(0));  // anything else loops
    expectEquals(1, hoistOneControl(0, 0));
    expectEquals(1, hoistOneControl(0, 1));
    expectEquals(2, hoistOneControl(1, 0));
    expectEquals(2, hoistOneControl(1, 1));
    expectEquals(1, hoistTwoControl(0, 0, 0));
    expectEquals(1, hoistTwoControl(0, 0, 1));
    expectEquals(1, hoistTwoControl(0, 1, 0));
    expectEquals(1, hoistTwoControl(0, 1, 1));
    expectEquals(2, hoistTwoControl(1, 0, 0));
    expectEquals(2, hoistTwoControl(1, 0, 1));
    expectEquals(3, hoistTwoControl(1, 1, 0));
    expectEquals(3, hoistTwoControl(1, 1, 1));

    int[] a = new int[256];
    addInts(a);
    for (int i = 0; i < a.length; i++) {
      expectEquals(1, a[i]);
    }
    addInts(null);  // okay

    System.out.println("passed");
  }

  private static void expectEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
