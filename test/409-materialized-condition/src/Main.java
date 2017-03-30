/*
 * Copyright (C) 2014 The Android Open Source Project
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

  public static void doNothing(boolean b) {
    System.out.println("In do nothing.");
  }

  public static void inIf() {
    System.out.println("In if.");
  }

  public static int bar() {
    return 42;
  }

  public static int foo1() {
    int b = bar();
    doNothing(b == 42);
    // This second `b == 42` will be GVN'ed away.
    if (b == 42) {
      inIf();
      return b;
    }
    return 0;
  }

  public static int foo2() {
    int b = bar();
    doNothing(b == 41);
    // This second `b == 41` will be GVN'ed away.
    if (b == 41) {
      inIf();
      return 0;
    }
    return b;
  }

  public static boolean $noinline$intEq0(int x) {
    return x == 0;
  }

  public static boolean $noinline$intNe0(int x) {
    return x != 0;
  }

  public static boolean $noinline$longEq0(long x) {
    return x == 0;
  }

  public static boolean $noinline$longNe0(long x) {
    return x != 0;
  }

  public static boolean $noinline$longEqCst(long x) {
    return x == 0x0123456789ABCDEFL;
  }

  public static boolean $noinline$longNeCst(long x) {
    return x != 0x0123456789ABCDEFL;
  }

  public static void main(String[] args) {
    System.out.println("foo1");
    int res = foo1();
    if (res != 42) {
      throw new Error("Unexpected return value for foo1: " + res + ", expected 42.");
    }

    System.out.println("foo2");
    res = foo2();
    if (res != 42) {
      throw new Error("Unexpected return value for foo2: " + res + ", expected 42.");
    }

    if (!$noinline$intEq0(0)) {
      throw new Error("Unexpected return value for $noinline$intEq0: false, expected true.");
    }
    if ($noinline$intEq0(42)) {
      throw new Error("Unexpected return value for $noinline$intEq0: true, expected false.");
    }
    if ($noinline$intEq0(-9000)) {
      throw new Error("Unexpected return value for $noinline$intEq0: true, expected false.");
    }

    if ($noinline$intNe0(0)) {
      throw new Error("Unexpected return value for $noinline$intNe0: true, expected false.");
    }
    if (!$noinline$intNe0(42)) {
      throw new Error("Unexpected return value for $noinline$intNe0: false, expected true.");
    }
    if (!$noinline$intNe0(-9000)) {
      throw new Error("Unexpected return value for $noinline$intNe0: false, expected true.");
    }

    if (!$noinline$longEq0(0L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: false, expected true.");
    }
    if ($noinline$longEq0(1L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: true, expected false.");
    }
    if ($noinline$longEq0(0x100000000L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: true, expected false.");
    }
    if ($noinline$longEq0(0x100000001L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: true, expected false.");
    }
    if ($noinline$longEq0(-9000L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: true, expected false.");
    }

    if ($noinline$longNe0(0L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: true, expected false.");
    }
    if (!$noinline$longNe0(1L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: false, expected true.");
    }
    if (!$noinline$longNe0(0x100000000L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: false, expected true.");
    }
    if (!$noinline$longNe0(0x100000001L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: false, expected true.");
    }
    if (!$noinline$longNe0(-9000L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: false, expected true.");
    }

    if ($noinline$longEqCst(0L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: true, expected false.");
    }
    if ($noinline$longEqCst(-1L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: true, expected false.");
    }
    if (!$noinline$longEqCst(0x0123456789ABCDEFL)) {
      throw new Error("Unexpected return value for $noinline$intNe0: false, expected true.");
    }

    if (!$noinline$longNeCst(0L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: false, expected true.");
    }
    if (!$noinline$longNeCst(-1L)) {
      throw new Error("Unexpected return value for $noinline$intNe0: false, expected true.");
    }
    if ($noinline$longNeCst(0x0123456789ABCDEFL)) {
      throw new Error("Unexpected return value for $noinline$intNe0: true, expected false.");
    }
  }
}
