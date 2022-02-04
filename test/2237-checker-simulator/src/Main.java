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
// Tests various entrypoints and patterns in simulator mode.
//
public class Main {

  static final int LENGTH = 4 * 1024;
  int[][] a = new int[LENGTH][];

  public Main() {
  }

  public static void $compile$noline$plainLoop(int[] a) {
    for (int i = 0; i < LENGTH; i++) {
      a[i] += i;
    }
  }

  public int $noinline$simpleFunction() {
    return 42;
  }

  public int $compile$noinline$shellForSimpleFunction() {
    return $noinline$simpleFunction();
  }

  static native byte jniMethodWithManyParameters(byte b1,
                                                 char c2,
                                                 short s3,
                                                 int i4,
                                                 long l5,
                                                 Integer o6,
                                                 byte b7,
                                                 char c8,
                                                 short s9,
                                                 int i10,
                                                 long l11,
                                                 Integer o12,
                                                 byte b13,
                                                 char c14,
                                                 short s15,
                                                 int i16,
                                                 long l17,
                                                 Integer o18,
                                                 float f19,
                                                 double d20,
                                                 float f21,
                                                 double d22,
                                                 float f23,
                                                 double d24,
                                                 float f25,
                                                 double d26,
                                                 float f27,
                                                 double d28,
                                                 float f29,
                                                 double d30);

  // Tests a method with a large number of parameters, so they would end up on the stack
  // during JNI transition.
  private static void testjniMethodWithManyParameters() {
    jniMethodWithManyParameters((byte)123,
                                (char) 123,
                                (short) 123,
                                123,
                                123l,
                                null,
                                (byte)(Byte.MAX_VALUE - 1),
                                (char)(Character.MAX_VALUE - 1),
                                (short)(Short.MAX_VALUE - 1),
                                Integer.MAX_VALUE - 1,
                                Long.MAX_VALUE - 1,
                                Integer.valueOf(1),
                                (byte)(Byte.MIN_VALUE + 1),
                                (char)(Character.MIN_VALUE + 1),
                                (short)(Short.MIN_VALUE + 1),
                                Integer.MIN_VALUE + 1,
                                Long.MIN_VALUE + 1,
                                Integer.valueOf(-1),
                                0.0f,
                                0.0d,
                                -0.0f,
                                -0.0d,
                                123.456f,
                                123.456d,
                                Float.NaN,
                                Double.NaN,
                                Float.MAX_VALUE,
                                Double.MAX_VALUE,
                                -5.5f,
                                -5.5d);
  }

  native double jniNonStaticReturnsDouble(double val);

  // Tests a non-static JNI method that returns double.
  static void testJniNonStaticReturnsDouble() {
    Main obj = new Main();
    double result = obj.jniNonStaticReturnsDouble(-5.5);
    double expected = -4.5;
    if (result != expected) {
      System.out.println("Expected:" + expected + ", but found: " + result);
      throw new AssertionError();
    }
  };

  public static native void jniSimpleMethod();

  public static void testJNI() {
    jniSimpleMethod();
    testjniMethodWithManyParameters();
    testJniNonStaticReturnsDouble();
  }

  public static int $compile$simpleThrow(int value) {
    return 15 / value;
  }

  static native void jniNewException();

  public static void testExceptions() {
    try {
      $compile$simpleThrow(5);
      System.out.println("No exception thrown as expected");
      $compile$simpleThrow(0);
    } catch (ArithmeticException e) {
      System.out.println("ArithmeticException caught as expected");
    }

    // Test exceptions thrown from JNI.
    try {
      jniNewException();
    } catch (Exception e) {
      System.out.println("Exception caught: " + e.toString());
    }
  }

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);
    Main obj = new Main();

    int[] arr = new int[LENGTH];
    obj.$compile$noline$plainLoop(arr);

    int val = obj.$compile$noinline$shellForSimpleFunction();
    System.out.println("SimpleFunction value: " + val);

    testJNI();

    testExceptions();

    System.out.println("passed");
  }
}
