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

//
// Test on loop optimizations, in particular with try catches.
//
public class Main {
  /// CHECK-START: int Main.geo1(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Mul loop:<<Loop>>
  //
  /// CHECK-START: int Main.geo1(int) loop_optimization (after)
  /// CHECK-DAG: <<Par:i\d+>> ParameterValue         loop:none
  /// CHECK-DAG: <<Zer:i\d+>> IntConstant 0          loop:none
  /// CHECK-DAG: <<Int:i\d+>> IntConstant 1410065408 loop:none
  /// CHECK-DAG: <<Mul:i\d+>> Mul [<<Par>>,<<Int>>]  loop:none
  /// CHECK-DAG: <<Add:i\d+>> Add [<<Mul>>,<<Zer>>]  loop:none
  /// CHECK-DAG:              Return [<<Add>>]       loop:none
  //
  /// CHECK-START: int Main.geo1(int) loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.geo1(int) loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int geo1(int a) {
    for (int i = 0; i < 10; i++) {
      a *= 10;
    }

    // Outer try catch does not block loop optimizations.
    try {
      if (doThrow) {
        $noinline$unreachable();
      }
    } catch (Error e) {
      System.out.println("Not expected");
    }
    return a;
  }

  /// CHECK-START: int Main.geo1_Blocking(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Mul loop:<<Loop>>
  //
  /// CHECK-START: int Main.geo1_Blocking(int) loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Mul loop:<<Loop>>
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.geo1_Blocking(int) loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int geo1_Blocking(int a) {
    for (int i = 0; i < 10; i++) {
      a *= 10;

      // Try catch blocks optimizations.
      try {
        if (doThrow) {
          $noinline$unreachable();
        }
      } catch (Error e) {
        System.out.println("Not expected");
      }
    }
    return a;
  }

  /// CHECK-START: int Main.geo2(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Shl loop:<<Loop>>
  //
  /// CHECK-START: int Main.geo2(int) loop_optimization (after)
  /// CHECK-DAG: <<Par:i\d+>> ParameterValue        loop:none
  /// CHECK-DAG: <<Zer:i\d+>> IntConstant 0         loop:none
  /// CHECK-DAG: <<Int:i\d+>> IntConstant 1024      loop:none
  /// CHECK-DAG: <<Mul:i\d+>> Mul [<<Par>>,<<Int>>] loop:none
  /// CHECK-DAG: <<Add:i\d+>> Add [<<Mul>>,<<Zer>>] loop:none
  /// CHECK-DAG:              Return [<<Add>>]      loop:none
  //
  /// CHECK-START: int Main.geo2(int) loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.geo2(int) loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int geo2(int a) {
    for (int i = 0; i < 10; i++) {
      a <<= 1;
    }

    // Outer try catch does not block loop optimizations.
    try {
      if (doThrow) {
        $noinline$unreachable();
      }
    } catch (Error e) {
      System.out.println("Not expected");
    }
    return a;
  }

  /// CHECK-START: int Main.geo2_Blocking(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Shl loop:<<Loop>>
  //
  /// CHECK-START: int Main.geo2_Blocking(int) loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Shl loop:<<Loop>>
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.geo2_Blocking(int) loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int geo2_Blocking(int a) {
    for (int i = 0; i < 10; i++) {
      a <<= 1;

      // Try catch blocks optimizations.
      try {
        if (doThrow) {
          $noinline$unreachable();
        }
      } catch (Error e) {
        System.out.println("Not expected");
      }
    }
    return a;
  }

  /// CHECK-START: int Main.geo3(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Div loop:<<Loop>>
  //
  /// CHECK-START: int Main.geo3(int) loop_optimization (after)
  /// CHECK-DAG: <<Par:i\d+>> ParameterValue        loop:none
  /// CHECK-DAG: <<Zer:i\d+>> IntConstant 0         loop:none
  /// CHECK-DAG: <<Int:i\d+>> IntConstant 59049     loop:none
  /// CHECK-DAG: <<Div:i\d+>> Div [<<Par>>,<<Int>>] loop:none
  /// CHECK-DAG: <<Add:i\d+>> Add [<<Div>>,<<Zer>>] loop:none
  /// CHECK-DAG:              Return [<<Add>>]      loop:none
  //
  /// CHECK-START: int Main.geo3(int) loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.geo3(int) loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int geo3(int a) {
    for (int i = 0; i < 10; i++) {
      a /= 3;
    }

    // Outer try catch does not block loop optimizations.
    try {
      if (doThrow) {
        $noinline$unreachable();
      }
    } catch (Error e) {
      System.out.println("Not expected");
    }
    return a;
  }

  /// CHECK-START: int Main.geo3_Blocking(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Div loop:<<Loop>>
  //
  /// CHECK-START: int Main.geo3_Blocking(int) loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Div loop:<<Loop>>
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.geo3_Blocking(int) loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int geo3_Blocking(int a) {
    for (int i = 0; i < 10; i++) {
      a /= 3;

      // Try catch blocks optimizations.
      try {
        if (doThrow) {
          $noinline$unreachable();
        }
      } catch (Error e) {
        System.out.println("Not expected");
      }
    }
    return a;
  }

  /// CHECK-START: int Main.geo4(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Rem loop:<<Loop>>
  //
  /// CHECK-START: int Main.geo4(int) loop_optimization (after)
  /// CHECK-DAG: <<Par:i\d+>> ParameterValue        loop:none
  /// CHECK-DAG: <<Int:i\d+>> IntConstant 7         loop:none
  /// CHECK-DAG: <<Rem:i\d+>> Rem [<<Par>>,<<Int>>] loop:none
  /// CHECK-DAG:              Return [<<Rem>>]      loop:none
  //
  /// CHECK-START: int Main.geo4(int) loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.geo4(int) loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int geo4(int a) {
    for (int i = 0; i < 10; i++) {
      a %= 7; // a wrap-around induction
    }

    // Outer try catch does not block loop optimizations.
    try {
      if (doThrow) {
        $noinline$unreachable();
      }
    } catch (Error e) {
      System.out.println("Not expected");
    }
    return a;
  }

  /// CHECK-START: int Main.geo4_Blocking(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Rem loop:<<Loop>>
  //
  /// CHECK-START: int Main.geo4_Blocking(int) loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Rem loop:<<Loop>>
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.geo4_Blocking(int) loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int geo4_Blocking(int a) {
    for (int i = 0; i < 10; i++) {
      a %= 7; // a wrap-around induction

      // Try catch blocks optimizations.
      try {
        if (doThrow) {
          $noinline$unreachable();
        }
      } catch (Error e) {
        System.out.println("Not expected");
      }
    }
    return a;
  }

  /// CHECK-START: int Main.geo5() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Shr loop:<<Loop>>
  //
  /// CHECK-START: int Main.geo5() loop_optimization (after)
  /// CHECK-DAG: <<Zero:i\d+>> IntConstant 0          loop:none
  /// CHECK-DAG: <<Int1:i\d+>> IntConstant 2147483647 loop:none
  /// CHECK-DAG: <<Int2:i\d+>> IntConstant 1024       loop:none
  /// CHECK-DAG: <<Div:i\d+>>  Div [<<Int1>>,<<Int2>>] loop:none
  /// CHECK-DAG: <<Add:i\d+>>  Add [<<Div>>,<<Zero>>]  loop:none
  /// CHECK-DAG:               Return [<<Add>>]        loop:none
  //
  /// CHECK-START: int Main.geo5() loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.geo5() loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int geo5() {
    int a = 0x7fffffff;
    for (int i = 0; i < 10; i++) {
      a >>= 1;
    }

    // Outer try catch does not block loop optimizations.
    try {
      if (doThrow) {
        $noinline$unreachable();
      }
    } catch (Error e) {
      System.out.println("Not expected");
    }
    return a;
  }

  /// CHECK-START: int Main.geo5_Blocking() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Shr loop:<<Loop>>
  //
  /// CHECK-START: int Main.geo5_Blocking() loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Shr loop:<<Loop>>
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.geo5_Blocking() loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int geo5_Blocking() {
    int a = 0x7fffffff;
    for (int i = 0; i < 10; i++) {
      a >>= 1;

      // Try catch blocks optimizations.
      try {
        if (doThrow) {
          $noinline$unreachable();
        }
      } catch (Error e) {
        System.out.println("Not expected");
      }
    }
    return a;
  }

  // Tests taken from 530-checker-loops4
  private static void $noinline$loops4Tests() {
    int m = 1410065408;
    for (int i = -100; i <= 100; i++) {
      expectEquals(m * i, geo1(i));
      expectEquals(m * i, geo1_Blocking(i));
    }
    for (int i = 1; i <= 1000000000; i *= 10) {
      expectEquals(m * i, geo1(i));
      expectEquals(m * i, geo1_Blocking(i));
      expectEquals(-m * i, geo1(-i));
      expectEquals(-m * i, geo1_Blocking(-i));
    }

    for (int i = -100; i <= 100; i++) {
      expectEquals(i << 10, geo2(i));
      expectEquals(i << 10, geo2_Blocking(i));
    }
    for (int i = 0; i < 22; i++) {
      expectEquals(1 << (i + 10), geo2(1 << i));
      expectEquals(1 << (i + 10), geo2_Blocking(1 << i));
    }
    expectEquals(0x80000400, geo2(0x00200001));
    expectEquals(0x80000400, geo2_Blocking(0x00200001));
    expectEquals(0x00000000, geo2(0x00400000));
    expectEquals(0x00000000, geo2_Blocking(0x00400000));
    expectEquals(0x00000400, geo2(0x00400001));
    expectEquals(0x00000400, geo2_Blocking(0x00400001));

    int d = 59049;
    for (int i = -100; i <= 100; i++) {
      expectEquals(0, geo3(i));
      expectEquals(0, geo3_Blocking(i));
    }
    for (int i = 1; i <= 100; i++) {
      expectEquals(i, geo3(i * d));
      expectEquals(i, geo3_Blocking(i * d));
      expectEquals(i, geo3(i * d + 1));
      expectEquals(i, geo3_Blocking(i * d + 1));
      expectEquals(-i, geo3(-i * d));
      expectEquals(-i, geo3_Blocking(-i * d));
      expectEquals(-i, geo3(-i * d - 1));
      expectEquals(-i, geo3_Blocking(-i * d - 1));
    }

    for (int i = -100; i <= 100; i++) {
      expectEquals(i % 7, geo4(i));
      expectEquals(i % 7, geo4_Blocking(i));
    }

    expectEquals(0x1fffff, geo5());
    expectEquals(0x1fffff, geo5_Blocking());
  }

  /// CHECK-START: int Main.poly1() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  //
  /// CHECK-START: int Main.poly1() loop_optimization (after)
  /// CHECK-DAG: <<Zer:i\d+>> IntConstant 0         loop:none
  /// CHECK-DAG: <<Int:i\d+>> IntConstant 55        loop:none
  /// CHECK-DAG: <<Add:i\d+>> Add [<<Int>>,<<Zer>>] loop:none
  /// CHECK-DAG:              Return [<<Add>>]      loop:none
  //
  /// CHECK-START: int Main.poly1() instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Int:i\d+>>  IntConstant 55 loop:none
  /// CHECK-DAG:               Return [<<Int>>]  loop:none
  //
  /// CHECK-START: int Main.poly1() loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.poly1() loop_optimization (before)
  /// CHECK:     TryBoundary
  private static int poly1() {
    int a = 0;
    for (int i = 0; i <= 10; i++) {
      a += i;
    }

    // Outer try catch does not block loop optimizations.
    try {
      if (doThrow) {
        $noinline$unreachable();
      }
    } catch (Error e) {
      System.out.println("Not expected");
    }

    return a;
  }

  /// CHECK-START: int Main.poly1_Blocking() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  //
  /// CHECK-START: int Main.poly1_Blocking() loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.poly1_Blocking() loop_optimization (before)
  /// CHECK:     TryBoundary
  private static int poly1_Blocking() {
    int a = 0;
    for (int i = 0; i <= 10; i++) {
      a += i;

      // Try catch blocks optimizations.
      try {
        if (doThrow) {
          $noinline$unreachable();
        }
      } catch (Error e) {
        System.out.println("Not expected");
      }
    }

    return a;
  }

  // Multiplication in linear induction has been optimized earlier,
  // but that does not stop the induction variable recognition
  // and loop optimizer.
  //
  /// CHECK-START: int Main.poly2(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Shl loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  //
  /// CHECK-START: int Main.poly2(int) loop_optimization (after)
  /// CHECK-DAG: <<Par:i\d+>> ParameterValue        loop:none
  /// CHECK-DAG: <<Int:i\d+>> IntConstant 185       loop:none
  /// CHECK-DAG: <<Add:i\d+>> Add [<<Int>>,<<Par>>] loop:none
  /// CHECK-DAG:              Return [<<Add>>]      loop:none
  //
  /// CHECK-START: int Main.poly2(int) loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.poly2(int) loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int poly2(int a) {
    for (int i = 0; i < 10; i++) {
      int k = 3 * i + 5;
      a += k;
    }

    // Outer try catch does not block loop optimizations.
    try {
      if (doThrow) {
        $noinline$unreachable();
      }
    } catch (Error e) {
      System.out.println("Not expected");
    }
    return a;
  }

  /// CHECK-START: int Main.poly2_Blocking(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Shl loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  //
  /// CHECK-START: int Main.poly2_Blocking(int) loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Shl loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.poly2_Blocking(int) loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int poly2_Blocking(int a) {
    for (int i = 0; i < 10; i++) {
      int k = 3 * i + 5;
      a += k;

      // Try catch blocks optimizations.
      try {
        if (doThrow) {
          $noinline$unreachable();
        }
      } catch (Error e) {
        System.out.println("Not expected");
      }
    }

    return a;
  }

  /// CHECK-START: int Main.poly3() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  //
  /// CHECK-START: int Main.poly3() loop_optimization (after)
  /// CHECK-DAG: <<Ini:i\d+>> IntConstant 12345       loop:none
  /// CHECK-DAG: <<Int:i\d+>> IntConstant -2146736968 loop:none
  /// CHECK-DAG: <<Add:i\d+>> Add [<<Int>>,<<Ini>>]   loop:none
  /// CHECK-DAG:              Return [<<Add>>]        loop:none
  //
  /// CHECK-START: int Main.poly3() instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Int:i\d+>>  IntConstant -2146724623 loop:none
  /// CHECK-DAG:               Return [<<Int>>]        loop:none
  //
  /// CHECK-START: int Main.poly3() loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.poly3() loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int poly3() {
    int a = 12345;
    for (int i = 0; i <= 10; i++) {
      a += (2147483646 * i + 67890);
    }

    // Outer try catch does not block loop optimizations.
    try {
      if (doThrow) {
        $noinline$unreachable();
      }
    } catch (Error e) {
      System.out.println("Not expected");
    }
    return a;
  }

  /// CHECK-START: int Main.poly3_Blocking() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  //
  /// CHECK-START: int Main.poly3_Blocking() loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>>
  /// CHECK-DAG: Add loop:<<Loop>>
  /// CHECK-DAG: Add loop:<<Loop>>
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: int Main.poly3_Blocking() loop_optimization (before)
  /// CHECK:     TryBoundary
  public static int poly3_Blocking() {
    int a = 12345;
    for (int i = 0; i <= 10; i++) {
      a += (2147483646 * i + 67890);

      // Try catch blocks optimizations.
      try {
        if (doThrow) {
          $noinline$unreachable();
        }
      } catch (Error e) {
        System.out.println("Not expected");
      }
    }
    return a;
  }

  // Tests taken from 530-checker-loops5
  private static void $noinline$loops5Tests() {
    expectEquals(55, poly1());
    expectEquals(55, poly1_Blocking());
    expectEquals(185, poly2(0));
    expectEquals(185, poly2_Blocking(0));
    expectEquals(192, poly2(7));
    expectEquals(192, poly2_Blocking(7));
    expectEquals(-2146724623, poly3());
    expectEquals(-2146724623, poly3_Blocking());
  }

  // Contants used for peel unroll tests.
  private static final int LENGTH = 4 * 1024;
  private static final int RESULT_POS = 4;

  private static final void initIntArray(int[] a) {
    for (int i = 0; i < a.length; i++) {
      a[i] = i % 4;
    }
  }

  /// CHECK-START: void Main.unrollingLoadStoreElimination(int[]) loop_optimization (before)
  /// CHECK-DAG: <<Array:l\d+>>   ParameterValue                            loop:none
  /// CHECK-DAG: <<Const1:i\d+>>  IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>   IntConstant 4094                          loop:none
  /// CHECK-DAG: <<Phi:i\d+>>     Phi                                       loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Check:z\d+>>   GreaterThanOrEqual [<<Phi>>,<<Limit>>]    loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<If:v\d+>>      If [<<Check>>]                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get0:i\d+>>    ArrayGet [<<Array>>,<<Phi>>]              loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<IndAdd:i\d+>>  Add [<<Phi>>,<<Const1>>]                  loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get1:i\d+>>    ArrayGet [<<Array>>,<<IndAdd>>]           loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Add:i\d+>>     Add [<<Get0>>,<<Get1>>]                   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:                  ArraySet [<<Array>>,<<Phi>>,<<Add>>]      loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-NOT:                  ArrayGet                                  loop:<<Loop>>      outer_loop:none
  /// CHECK-NOT:                  ArraySet                                  loop:<<Loop>>      outer_loop:none

  /// CHECK-START: void Main.unrollingLoadStoreElimination(int[]) loop_optimization (after)
  /// CHECK-DAG: <<Array:l\d+>>   ParameterValue                            loop:none
  /// CHECK-DAG: <<Const0:i\d+>>  IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>  IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>   IntConstant 4094                          loop:none
  /// CHECK-DAG: <<Phi:i\d+>>     Phi                                       loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Check:z\d+>>   GreaterThanOrEqual [<<Phi>>,<<Limit>>]    loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<If:v\d+>>      If [<<Check>>]                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get0:i\d+>>    ArrayGet [<<Array>>,<<Phi>>]              loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<IndAdd:i\d+>>  Add [<<Phi>>,<<Const1>>]                  loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get1:i\d+>>    ArrayGet [<<Array>>,<<IndAdd>>]           loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Add:i\d+>>     Add [<<Get0>>,<<Get1>>]                   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:                  ArraySet [<<Array>>,<<Phi>>,<<Add>>]      loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-DAG: <<CheckA:z\d+>>  GreaterThanOrEqual [<<IndAdd>>,<<Limit>>] loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<IfA:v\d+>>     If [<<Const0>>]                           loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get0A:i\d+>>   ArrayGet [<<Array>>,<<IndAdd>>]           loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<IndAddA:i\d+>> Add [<<IndAdd>>,<<Const1>>]               loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get1A:i\d+>>   ArrayGet [<<Array>>,<<IndAddA>>]          loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<AddA:i\d+>>    Add [<<Get0A>>,<<Get1A>>]                 loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:                  ArraySet [<<Array>>,<<IndAdd>>,<<AddA>>]  loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-NOT:                  ArrayGet                                  loop:<<Loop>>      outer_loop:none
  /// CHECK-NOT:                  ArraySet                                  loop:<<Loop>>      outer_loop:none
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: void Main.unrollingLoadStoreElimination(int[]) loop_optimization (after)
  /// CHECK:     TryBoundary
  private static final void unrollingLoadStoreElimination(int[] a) {
    for (int i = 0; i < LENGTH - 2; i++) {
      a[i] += a[i + 1];
    }

    // Outer try catch does not block loop optimizations.
    try {
      if (doThrow) {
        $noinline$unreachable();
      }
    } catch (Error e) {
      System.out.println("Not expected");
    }
  }

  /// CHECK-START: void Main.unrollingLoadStoreElimination_Blocking(int[]) loop_optimization (before)
  /// CHECK-DAG: <<Array:l\d+>>   ParameterValue                            loop:none
  /// CHECK-DAG: <<Const1:i\d+>>  IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>   IntConstant 4094                          loop:none
  /// CHECK-DAG: <<Phi:i\d+>>     Phi                                       loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Check:z\d+>>   GreaterThanOrEqual [<<Phi>>,<<Limit>>]    loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<If:v\d+>>      If [<<Check>>]                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get0:i\d+>>    ArrayGet [<<Array>>,<<Phi>>]              loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<IndAdd:i\d+>>  Add [<<Phi>>,<<Const1>>]                  loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get1:i\d+>>    ArrayGet [<<Array>>,<<IndAdd>>]           loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Add:i\d+>>     Add [<<Get0>>,<<Get1>>]                   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:                  ArraySet [<<Array>>,<<Phi>>,<<Add>>]      loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-NOT:                  ArrayGet                                  loop:<<Loop>>      outer_loop:none
  /// CHECK-NOT:                  ArraySet                                  loop:<<Loop>>      outer_loop:none

  /// CHECK-START: void Main.unrollingLoadStoreElimination_Blocking(int[]) loop_optimization (after)
  /// CHECK-DAG: <<Array:l\d+>>   ParameterValue                            loop:none
  /// CHECK-DAG: <<Const1:i\d+>>  IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>   IntConstant 4094                          loop:none
  /// CHECK-DAG: <<Phi:i\d+>>     Phi                                       loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Check:z\d+>>   GreaterThanOrEqual [<<Phi>>,<<Limit>>]    loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<If:v\d+>>      If [<<Check>>]                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get0:i\d+>>    ArrayGet [<<Array>>,<<Phi>>]              loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<IndAdd:i\d+>>  Add [<<Phi>>,<<Const1>>]                  loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get1:i\d+>>    ArrayGet [<<Array>>,<<IndAdd>>]           loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Add:i\d+>>     Add [<<Get0>>,<<Get1>>]                   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:                  ArraySet [<<Array>>,<<Phi>>,<<Add>>]      loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-NOT:                  ArrayGet                                  loop:<<Loop>>      outer_loop:none
  /// CHECK-NOT:                  ArraySet                                  loop:<<Loop>>      outer_loop:none
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: void Main.unrollingLoadStoreElimination_Blocking(int[]) loop_optimization (after)
  /// CHECK:     TryBoundary
  private static final void unrollingLoadStoreElimination_Blocking(int[] a) {
    for (int i = 0; i < LENGTH - 2; i++) {
      a[i] += a[i + 1];

      // Try catch blocks optimizations.
      try {
        if (doThrow) {
          $noinline$unreachable();
        }
      } catch (Error e) {
        System.out.println("Not expected");
      }
    }
  }

  /// CHECK-START: void Main.unrollingInTheNest(int[], int[], int) loop_optimization (before)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop2>>
  /// CHECK-DAG: <<Check:z\d+>>    GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   If [<<Check>>]                            loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop2>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get:z\d+>>      StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get>>]
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If

  /// CHECK-START: void Main.unrollingInTheNest(int[], int[], int) loop_optimization (after)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop2>>
  /// CHECK-DAG: <<Check:z\d+>>    GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   If [<<Check>>]                            loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG: <<AddI:i\d+>>     Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop2>>
  //
  /// CHECK-DAG:                   If [<<Const0>>]                           loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get:z\d+>>      StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get>>]
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: void Main.unrollingInTheNest(int[], int[], int) loop_optimization (after)
  /// CHECK:     TryBoundary
  private static final void unrollingInTheNest(int[] a, int[] b, int x) {
    for (int k = 0; k < 16; k++) {
      for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 128; i++) {
          b[x]++;
          a[i] = a[i] + 1;
        }
      }
    }

    // Outer try catch does not block loop optimizations.
    try {
      if (doThrow) {
        $noinline$unreachable();
      }
    } catch (Error e) {
      System.out.println("Not expected");
    }
  }

  /// CHECK-START: void Main.unrollingInTheNest_Blocking(int[], int[], int) loop_optimization (before)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop2>>
  /// CHECK-DAG: <<Check:z\d+>>    GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   If [<<Check>>]                            loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  // 
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get:z\d+>>      StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get>>]
  //
  /// CHECK-DAG:                   Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop2>>
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If

  /// CHECK-START: void Main.unrollingInTheNest_Blocking(int[], int[], int) loop_optimization (after)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop2>>
  /// CHECK-DAG: <<Check:z\d+>>    GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   If [<<Check>>]                            loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get:z\d+>>      StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get>>]
  //
  /// CHECK-DAG:                   Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop2>>
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: void Main.unrollingInTheNest_Blocking(int[], int[], int) loop_optimization (after)
  /// CHECK:     TryBoundary
  private static final void unrollingInTheNest_Blocking(int[] a, int[] b, int x) {
    for (int k = 0; k < 16; k++) {
      for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 128; i++) {
          b[x]++;
          a[i] = a[i] + 1;

          // Try catch blocks optimizations.
          try {
            if (doThrow) {
              $noinline$unreachable();
            }
          } catch (Error e) {
            System.out.println("Not expected");
          }
        }
      }
    }
  }

  /// CHECK-START: void Main.unrollingInTheNest_TryCatchNotBlocking(int[], int[], int) loop_optimization (before)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop2>>
  /// CHECK-DAG: <<Check:z\d+>>    GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   If [<<Check>>]                            loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  // 
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get:z\d+>>      StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get>>]
  //
  /// CHECK-DAG:                   Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop2>>
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If

  /// CHECK-START: void Main.unrollingInTheNest_TryCatchNotBlocking(int[], int[], int) loop_optimization (after)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop2>>
  /// CHECK-DAG: <<Check:z\d+>>    GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   If [<<Check>>]                            loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG: <<AddI:i\d+>>     Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop2>>
  //
  /// CHECK-DAG:                   If [<<Const0>>]                           loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop2>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get:z\d+>>      StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get>>]
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: void Main.unrollingInTheNest_TryCatchNotBlocking(int[], int[], int) loop_optimization (after)
  /// CHECK:     TryBoundary
  private static final void unrollingInTheNest_TryCatchNotBlocking(int[] a, int[] b, int x) {
    for (int k = 0; k < 16; k++) {
      for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 128; i++) {
          b[x]++;
          a[i] = a[i] + 1;
        }
        // Try catch does not block the optimization in the innermost loop.
        try {
          if (doThrow) {
            $noinline$unreachable();
          }
        } catch (Error e) {
          System.out.println("Not expected");
        }
      }
    }
  }

  /// CHECK-START: void Main.unrollingTwoLoopsInTheNest(int[], int[], int) loop_optimization (before)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<XThres:i\d+>>   IntConstant 100                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  //
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check2:z\d+>>   GreaterThanOrEqual [<<Phi2>>,<<Limit>>]   loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check2>>]                           loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG: <<AddI2:i\d+>>    Add [<<Phi2>>,<<Const1>>]                 loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check3:z\d+>>   GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check3>>]                           loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG: <<AddI3:i\d+>>    Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop1>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get:z\d+>>      StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get>>]
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If

  /// CHECK-START: void Main.unrollingTwoLoopsInTheNest(int[], int[], int) loop_optimization (after)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<XThres:i\d+>>   IntConstant 100                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  //
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check2:z\d+>>   GreaterThanOrEqual [<<Phi2>>,<<Limit>>]   loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check2>>]                           loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG: <<AddI2:i\d+>>    Add [<<Phi2>>,<<Const1>>]                 loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Const0>>]                           loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   Add [<<AddI2>>,<<Const1>>]                loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check3:z\d+>>   GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check3>>]                           loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG: <<AddI3:i\d+>>    Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Const0>>]                           loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   Add [<<AddI3>>,<<Const1>>]                loop:<<Loop3>>      outer_loop:<<Loop1>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get:z\d+>>      StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get>>]
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: void Main.unrollingTwoLoopsInTheNest(int[], int[], int) loop_optimization (after)
  /// CHECK:     TryBoundary
  private static final void unrollingTwoLoopsInTheNest(int[] a, int[] b, int x) {
    for (int k = 0; k < 128; k++) {
      if (x > 100) {
        for (int j = 0; j < 128; j++) {
          a[x]++;
        }
      } else {
        for (int i = 0; i < 128; i++) {
          b[x]++;
        }
      }
    }

    // Outer try catch does not block loop optimizations.
    try {
      if (doThrow) {
        $noinline$unreachable();
      }
    } catch (Error e) {
      System.out.println("Not expected");
    }
  }

  /// CHECK-START: void Main.unrollingTwoLoopsInTheNest_OneBlocking(int[], int[], int) loop_optimization (before)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<XThres:i\d+>>   IntConstant 100                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  //
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check2:z\d+>>   GreaterThanOrEqual [<<Phi2>>,<<Limit>>]   loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check2>>]                           loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get:z\d+>>      StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get>>]
  //
  /// CHECK-DAG: <<AddI2:i\d+>>    Add [<<Phi2>>,<<Const1>>]                 loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check3:z\d+>>   GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check3>>]                           loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG: <<AddI3:i\d+>>    Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop1>>
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If

  /// CHECK-START: void Main.unrollingTwoLoopsInTheNest_OneBlocking(int[], int[], int) loop_optimization (after)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<XThres:i\d+>>   IntConstant 100                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  //
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check2:z\d+>>   GreaterThanOrEqual [<<Phi2>>,<<Limit>>]   loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check2>>]                           loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get:z\d+>>      StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get>>]
  //
  /// CHECK-DAG: <<AddI2:i\d+>>    Add [<<Phi2>>,<<Const1>>]                 loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check3:z\d+>>   GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check3>>]                           loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG: <<AddI3:i\d+>>    Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Const0>>]                           loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   Add [<<AddI3>>,<<Const1>>]                loop:<<Loop3>>      outer_loop:<<Loop1>>
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: void Main.unrollingTwoLoopsInTheNest_OneBlocking(int[], int[], int) loop_optimization (after)
  /// CHECK:     TryBoundary
  private static final void unrollingTwoLoopsInTheNest_OneBlocking(int[] a, int[] b, int x) {
    for (int k = 0; k < 128; k++) {
      if (x > 100) {
        for (int j = 0; j < 128; j++) {
          a[x]++;
          // Try catch blocks optimizations.
          try {
            if (doThrow) {
              $noinline$unreachable();
            }
          } catch (Error e) {
            System.out.println("Not expected");
          }
        }
      } else {
        for (int i = 0; i < 128; i++) {
          b[x]++;
        }
      }
    }
  }

  /// CHECK-START: void Main.unrollingTwoLoopsInTheNest_OtherBlocking(int[], int[], int) loop_optimization (before)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<XThres:i\d+>>   IntConstant 100                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  //
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check2:z\d+>>   GreaterThanOrEqual [<<Phi2>>,<<Limit>>]   loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check2>>]                           loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG: <<AddI2:i\d+>>    Add [<<Phi2>>,<<Const1>>]                 loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check3:z\d+>>   GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check3>>]                           loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get:z\d+>>      StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get>>]
  //
  /// CHECK-DAG: <<AddI3:i\d+>>    Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop1>>
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If

  /// CHECK-START: void Main.unrollingTwoLoopsInTheNest_OtherBlocking(int[], int[], int) loop_optimization (after)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<XThres:i\d+>>   IntConstant 100                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  //
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check2:z\d+>>   GreaterThanOrEqual [<<Phi2>>,<<Limit>>]   loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check2>>]                           loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG: <<AddI2:i\d+>>    Add [<<Phi2>>,<<Const1>>]                 loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Const0>>]                           loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   Add [<<AddI2>>,<<Const1>>]                loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check3:z\d+>>   GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check3>>]                           loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get:z\d+>>      StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get>>]
  //
  /// CHECK-DAG: <<AddI3:i\d+>>    Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop1>>
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: void Main.unrollingTwoLoopsInTheNest_OtherBlocking(int[], int[], int) loop_optimization (after)
  /// CHECK:     TryBoundary
  private static final void unrollingTwoLoopsInTheNest_OtherBlocking(int[] a, int[] b, int x) {
    for (int k = 0; k < 128; k++) {
      if (x > 100) {
        for (int j = 0; j < 128; j++) {
          a[x]++;
        }
      } else {
        for (int i = 0; i < 128; i++) {
          b[x]++;
          // Try catch blocks optimizations.
          try {
            if (doThrow) {
              $noinline$unreachable();
            }
          } catch (Error e) {
            System.out.println("Not expected");
          }
        }
      }
    }
  }

  /// CHECK-START: void Main.unrollingTwoLoopsInTheNest_BothBlocking(int[], int[], int) loop_optimization (before)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<XThres:i\d+>>   IntConstant 100                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  //
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check2:z\d+>>   GreaterThanOrEqual [<<Phi2>>,<<Limit>>]   loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check2>>]                           loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get1:z\d+>>     StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get1>>]
  //
  /// CHECK-DAG: <<AddI2:i\d+>>    Add [<<Phi2>>,<<Const1>>]                 loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check3:z\d+>>   GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check3>>]                           loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get2:z\d+>>     StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get2>>]
  //
  /// CHECK-DAG: <<AddI3:i\d+>>    Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop1>>
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If

  /// CHECK-START: void Main.unrollingTwoLoopsInTheNest_BothBlocking(int[], int[], int) loop_optimization (after)
  /// CHECK-DAG: <<Const0:i\d+>>   IntConstant 0                             loop:none
  /// CHECK-DAG: <<Const1:i\d+>>   IntConstant 1                             loop:none
  /// CHECK-DAG: <<Limit:i\d+>>    IntConstant 128                           loop:none
  /// CHECK-DAG: <<XThres:i\d+>>   IntConstant 100                           loop:none
  /// CHECK-DAG: <<Phi1:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop1:B\d+>> outer_loop:none
  //
  /// CHECK-DAG: <<Phi2:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check2:z\d+>>   GreaterThanOrEqual [<<Phi2>>,<<Limit>>]   loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check2>>]                           loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get1:z\d+>>     StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get1>>]
  //
  /// CHECK-DAG: <<AddI2:i\d+>>    Add [<<Phi2>>,<<Const1>>]                 loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  /// CHECK-DAG: <<Phi3:i\d+>>     Phi [<<Const0>>,{{i\d+}}]                 loop:<<Loop3:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: <<Check3:z\d+>>   GreaterThanOrEqual [<<Phi3>>,<<Limit>>]   loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   If [<<Check3>>]                           loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArrayGet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  /// CHECK-DAG:                   ArraySet                                  loop:<<Loop3>>      outer_loop:<<Loop1>>
  //
  // Unrelated to the optimization itself, the try catch has an if.
  /// CHECK-DAG: <<Get2:z\d+>>     StaticFieldGet field_name:Main.doThrow
  /// CHECK-DAG:                   If [<<Get2>>]
  //
  /// CHECK-DAG: <<AddI3:i\d+>>    Add [<<Phi3>>,<<Const1>>]                 loop:<<Loop3>>      outer_loop:<<Loop1>>
  //
  /// CHECK-NOT:                   ArrayGet
  /// CHECK-NOT:                   ArraySet
  /// CHECK-NOT:                   If
  //
  // Consistency check to see we haven't eliminated the try/catch.
  /// CHECK-START: void Main.unrollingTwoLoopsInTheNest_BothBlocking(int[], int[], int) loop_optimization (after)
  /// CHECK:     TryBoundary
  private static final void unrollingTwoLoopsInTheNest_BothBlocking(int[] a, int[] b, int x) {
    for (int k = 0; k < 128; k++) {
      if (x > 100) {
        for (int j = 0; j < 128; j++) {
          a[x]++;
          // Try catch blocks optimizations.
          try {
            if (doThrow) {
              $noinline$unreachable();
            }
          } catch (Error e) {
            System.out.println("Not expected");
          }
        }
      } else {
        for (int i = 0; i < 128; i++) {
          b[x]++;
          // Try catch blocks optimizations.
          try {
            if (doThrow) {
              $noinline$unreachable();
            }
          } catch (Error e) {
            System.out.println("Not expected");
          }
        }
      }
    }
  }

  // Tests taken from 530-checker-peel-unroll
  private static void $noinline$peelUnrollTests() {
    int[] a = new int[LENGTH];
    int[] b = new int[LENGTH];
    initIntArray(a);
    initIntArray(b);

    unrollingLoadStoreElimination(a);
    unrollingLoadStoreElimination_Blocking(a);
    unrollingInTheNest(a, b, RESULT_POS);
    unrollingInTheNest_Blocking(a, b, RESULT_POS);
    unrollingInTheNest_TryCatchNotBlocking(a, b, RESULT_POS);
    unrollingTwoLoopsInTheNest(a, b, RESULT_POS);
    unrollingTwoLoopsInTheNest_OneBlocking(a, b, RESULT_POS);
    unrollingTwoLoopsInTheNest_OtherBlocking(a, b, RESULT_POS);
    unrollingTwoLoopsInTheNest_BothBlocking(a, b, RESULT_POS);
  }

  public static void main(String[] args) {
    // Use existing tests to show that the difference between having a try catch inside or outside
    // the loop.
    $noinline$loops4Tests();
    $noinline$loops5Tests();
    $noinline$peelUnrollTests();

    System.out.println("passed");
  }

  private static void expectEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  private static void $noinline$unreachable() {
    throw new Error("Unreachable");
  }

  private static boolean doThrow = false;
}
