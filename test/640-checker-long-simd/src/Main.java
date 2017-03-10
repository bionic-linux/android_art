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
 * Functional tests for SIMD vectorization.
 */
public class Main {

  static long[] a;

  //
  // Arithmetic operations.
  //

  /// CHECK-START: void Main.add(long) loop_optimization (before)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArrayGet loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.add(long) loop_optimization (after)
  //
  // TODO: fill in when supported
  static void add(long x) {
    for (int i = 0; i < 128; i++)
      a[i] += x;
  }

  /// CHECK-START: void Main.sub(long) loop_optimization (before)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArrayGet loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.sub(long) loop_optimization (after)
  //
  // TODO: fill in when supported
  static void sub(long x) {
    for (int i = 0; i < 128; i++)
      a[i] -= x;
  }

  /// CHECK-START: void Main.mul(long) loop_optimization (before)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArrayGet loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.mul(long) loop_optimization (after)
  //
  // TODO: fill in when supported
  static void mul(long x) {
    for (int i = 0; i < 128; i++)
      a[i] *= x;
  }

  /// CHECK-START: void Main.div(long) loop_optimization (before)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArrayGet loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.div(long) loop_optimization (after)
  //
  // TODO: fill in when supported
  static void div(long x) {
    for (int i = 0; i < 128; i++)
      a[i] /= x;
  }

  /// CHECK-START: void Main.neg() loop_optimization (before)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArrayGet loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.neg() loop_optimization (after)
  //
  // TODO: fill in when supported
  static void neg() {
    for (int i = 0; i < 128; i++)
      a[i] = -a[i];
  }

  /// CHECK-START: void Main.shl() loop_optimization (before)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArrayGet loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.shl() loop_optimization (after)
  //
  // TODO: fill in when supported
  static void shl() {
    for (int i = 0; i < 128; i++)
      a[i] <<= 4;
  }

  /// CHECK-START: void Main.sar() loop_optimization (before)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArrayGet loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.sar() loop_optimization (after)
  //
  // TODO: fill in when supported
  static void sar() {
    for (int i = 0; i < 128; i++)
      a[i] >>= 2;
  }

  /// CHECK-START: void Main.shr() loop_optimization (before)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArrayGet loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.shr() loop_optimization (after)
  //
  // TODO: fill in when supported
  static void shr() {
    for (int i = 0; i < 128; i++)
      a[i] >>>= 2;
  }

  //
  // Loop bounds.
  //

  static void add() {
    for (int i = 1; i < 127; i++)
      a[i] += 11;
  }

  //
  // Test Driver.
  //

  public static void main(String[] args) {
    // Set up.
    a = new long[128];
    for (int i = 0; i < 128; i++) {
      a[i] = i;
    }
    // Arithmetic operations.
    add(2L);
    for (int i = 0; i < 128; i++) {
      expectEquals(i + 2, a[i], "add");
    }
    sub(2L);
    for (int i = 0; i < 128; i++) {
      expectEquals(i, a[i], "sub");
    }
    mul(2L);
    for (int i = 0; i < 128; i++) {
      expectEquals(i + i, a[i], "mul");
    }
    div(2L);
    for (int i = 0; i < 128; i++) {
      expectEquals(i, a[i], "div");
    }
    neg();
    for (int i = 0; i < 128; i++) {
      expectEquals(-i, a[i], "neg");
    }
    // Loop bounds.
    add();
    expectEquals(0, a[0], "bounds0");
    for (int i = 1; i < 127; i++) {
      expectEquals(11 - i, a[i], "bounds");
    }
    expectEquals(-127, a[127], "bounds127");
    // Shifts.
    for (int i = 0; i < 128; i++) {
      a[i] = 0xffffffffffffffffL;
    }
    shl();
    for (int i = 0; i < 128; i++) {
      expectEquals(0xfffffffffffffff0L, a[i], "shl");
    }
    sar();
    for (int i = 0; i < 128; i++) {
      expectEquals(0xfffffffffffffffcL, a[i], "sar");
    }
    shr();
    for (int i = 0; i < 128; i++) {
      expectEquals(0x3fffffffffffffffL, a[i], "shr");
    }
    // Done.
    System.out.println("passed");
  }

  private static void expectEquals(long expected, long result, String action) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result + " for " + action);
    }
  }
}
