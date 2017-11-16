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

// Note that $opt$ is a marker for the optimizing compiler to test
// it does compile the method.
public class Main {

  public static void assertByteEquals(byte expected, byte result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void assertShortEquals(short expected, short result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void assertIntEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void assertLongEquals(long expected, long result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void assertCharEquals(char expected, char result) {
    if (expected != result) {
      // Values are cast to int to display numeric values instead of
      // (UTF-16 encoded) characters.
      throw new Error("Expected: " + (int)expected + ", found: " + (int)result);
    }
  }

  public static void assertFloatEquals(float expected, float result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void assertDoubleEquals(double expected, double result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void assertFloatIsNaN(float result) {
    if (!Float.isNaN(result)) {
      throw new Error("Expected: NaN, found: " + result);
    }
  }

  public static void assertDoubleIsNaN(double result) {
    if (!Double.isNaN(result)) {
      throw new Error("Expected: NaN, found: " + result);
    }
  }

  static byte byteVal = -1;
  static short shortVal = -1;
  static char charVal = 0xffff;
  static int intVal = -1;

  static byte[] byteArr = { 0 };
  static short[] shortArr = { 0 };
  static char[] charArr = { 0 };
  static int[] intArr = { 0 };

  static byte getByte() {
    return byteVal;
  }

  static short getShort() {
    return shortVal;
  }

  static char getChar() {
    return charVal;
  }

  static int getInt() {
    return intVal;
  }

  /// CHECK-START: void Main.byteToShort() prepare_for_register_allocation (before)
  /// CHECK-NOT: TypeConversion

  /// CHECK-START: void Main.byteToShort() prepare_for_register_allocation (after)
  /// CHECK-NOT: TypeConversion
  private static void byteToShort() {
    byte b = getByte();
    shortArr[0] = b;
  }

  /// CHECK-START: void Main.byteToChar() prepare_for_register_allocation (before)
  /// CHECK: TypeConversion

  /// CHECK-START: void Main.byteToChar() prepare_for_register_allocation (after)
  /// CHECK: TypeConversion
  private static void byteToChar() {
    byte b = getByte();
    charArr[0] = (char)b;
  }

  /// CHECK-START: void Main.byteToInt() prepare_for_register_allocation (before)
  /// CHECK-NOT: TypeConversion

  /// CHECK-START: void Main.byteToInt() prepare_for_register_allocation (after)
  /// CHECK-NOT: TypeConversion
  private static void byteToInt() {
    byte b = getByte();
    intArr[0] = b;
  }

  /// CHECK-START: void Main.charToByte() prepare_for_register_allocation (before)
  /// CHECK: TypeConversion

  /// CHECK-START: void Main.charToByte() prepare_for_register_allocation (after)
  /// CHECK-NOT: TypeConversion
  private static void charToByte() {
    char c = getChar();
    byteArr[0] = (byte)c;
  }

  /// CHECK-START: void Main.charToShort() prepare_for_register_allocation (before)
  /// CHECK: TypeConversion

  /// CHECK-START: void Main.charToShort() prepare_for_register_allocation (after)
  /// CHECK: TypeConversion
  private static void charToShort() {
    char c = getChar();
    shortArr[0] = (short)c;
  }

  /// CHECK-START: void Main.charToInt() prepare_for_register_allocation (before)
  /// CHECK-NOT: TypeConversion

  /// CHECK-START: void Main.charToInt() prepare_for_register_allocation (after)
  /// CHECK-NOT: TypeConversion
  private static void charToInt() {
    char c = getChar();
    intArr[0] = c;
  }

  /// CHECK-START: void Main.shortToByte() prepare_for_register_allocation (before)
  /// CHECK: TypeConversion

  /// CHECK-START: void Main.shortToByte() prepare_for_register_allocation (after)
  /// CHECK-NOT: TypeConversion
  private static void shortToByte() {
    short s = getShort();
    byteArr[0] = (byte)s;
  }

  /// CHECK-START: void Main.shortToChar() prepare_for_register_allocation (before)
  /// CHECK: TypeConversion

  /// CHECK-START: void Main.shortToChar() prepare_for_register_allocation (after)
  /// CHECK: TypeConversion
  private static void shortToChar() {
    short s = getShort();
    charArr[0] = (char)s;
  }

  /// CHECK-START: void Main.shortToInt() prepare_for_register_allocation (before)
  /// CHECK-NOT: TypeConversion

  /// CHECK-START: void Main.shortToInt() prepare_for_register_allocation (after)
  /// CHECK-NOT: TypeConversion
  private static void shortToInt() {
    short s = getShort();
    intArr[0] = s;
  }

  /// CHECK-START: void Main.intToByte() prepare_for_register_allocation (before)
  /// CHECK: TypeConversion

  /// CHECK-START: void Main.intToByte() prepare_for_register_allocation (after)
  /// CHECK-NOT: TypeConversion
  private static void intToByte() {
    int i = getInt();
    byteArr[0] = (byte)i;
  }

  /// CHECK-START: void Main.intToShort() prepare_for_register_allocation (before)
  /// CHECK: TypeConversion

  /// CHECK-START: void Main.intToShort() prepare_for_register_allocation (after)
  /// CHECK-NOT: TypeConversion
  private static void intToShort() {
    int i = getInt();
    shortArr[0] = (short)i;
  }

  /// CHECK-START: void Main.intToChar() prepare_for_register_allocation (before)
  /// CHECK: TypeConversion

  /// CHECK-START: void Main.intToChar() prepare_for_register_allocation (after)
  /// CHECK-NOT: TypeConversion
  private static void intToChar() {
    int i = getInt();
    charArr[0] = (char)i;
  }

  public static void main(String[] args) {
    byteToShort();
    assertShortEquals(shortArr[0], (short)-1);
    byteToChar();
    assertCharEquals(charArr[0], (char)-1);
    byteToInt();
    assertIntEquals(intArr[0], -1);
    charToByte();
    assertByteEquals(byteArr[0], (byte)-1);
    charToShort();
    assertShortEquals(shortArr[0], (short)-1);
    charToInt();
    assertIntEquals(intArr[0], 0xffff);
    shortToByte();
    assertByteEquals(byteArr[0], (byte)-1);
    shortToChar();
    assertCharEquals(charArr[0], (char)-1);
    shortToInt();
    assertIntEquals(intArr[0], -1);
    intToByte();
    assertByteEquals(byteArr[0], (byte)-1);
    intToShort();
    assertShortEquals(shortArr[0], (short)-1);
    intToChar();
    assertCharEquals(charArr[0], (char)-1);
  }
}
