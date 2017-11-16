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
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    return byteVal;
  }

  static short getShort() {
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    return shortVal;
  }

  static char getChar() {
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    return charVal;
  }

  static int getInt() {
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    System.out.print("");
    return intVal;
  }

  static boolean sFlag = true;

  /// CHECK-START: void Main.byteToShort() instruction_simplifier$before_codegen (after)
  /// CHECK-NOT: TypeConversion
  private static void byteToShort() {
    shortArr[0] = 0;
    if (sFlag) {
      shortArr[0] = getByte();
    }
  }

  /// CHECK-START: void Main.byteToChar() instruction_simplifier$before_codegen (after)
  /// CHECK: TypeConversion
  private static void byteToChar() {
    charArr[0] = 0;
    if (sFlag) {
      charArr[0] = (char)getByte();
    }
  }

  /// CHECK-START: void Main.byteToInt() instruction_simplifier$before_codegen (after)
  /// CHECK-NOT: TypeConversion
  private static void byteToInt() {
    intArr[0] = 0;
    if (sFlag) {
      intArr[0] = getByte();
    }
  }

  /// CHECK-START: void Main.charToByte() instruction_simplifier$before_codegen (after)
  /// CHECK-NOT: TypeConversion
  private static void charToByte() {
    byteArr[0] = 0;
    if (sFlag) {
      byteArr[0] = (byte)getChar();
    }
  }

  /// CHECK-START: void Main.charToShort() instruction_simplifier$before_codegen (after)
  /// CHECK-NOT: TypeConversion
  private static void charToShort() {
    shortArr[0] = 0;
    if (sFlag) {
      shortArr[0] = (short)getChar();
    }
  }

  /// CHECK-START: void Main.charToInt() instruction_simplifier$before_codegen (after)
  /// CHECK-NOT: TypeConversion
  private static void charToInt() {
    intArr[0] = 0;
    if (sFlag) {
      intArr[0] = getChar();
    }
  }

  /// CHECK-START: void Main.shortToByte() instruction_simplifier$before_codegen (after)
  /// CHECK-NOT: TypeConversion
  private static void shortToByte() {
    byteArr[0] = 0;
    if (sFlag) {
      byteArr[0] = (byte)getShort();
    }
  }

  /// CHECK-START: void Main.shortToChar() instruction_simplifier$before_codegen (after)
  /// CHECK-NOT: TypeConversion
  private static void shortToChar() {
    charArr[0] = 0;
    if (sFlag) {
      charArr[0] = (char)getShort();
    }
  }

  /// CHECK-START: void Main.shortToInt() instruction_simplifier$before_codegen (after)
  /// CHECK-NOT: TypeConversion
  private static void shortToInt() {
    intArr[0] = 0;
    if (sFlag) {
      intArr[0] = getShort();
    }
  }

  /// CHECK-START: void Main.intToByte() instruction_simplifier$before_codegen (after)
  /// CHECK-NOT: TypeConversion
  private static void intToByte() {
    byteArr[0] = 0;
    if (sFlag) {
      byteArr[0] = (byte)getInt();
    }
  }

  /// CHECK-START: void Main.intToShort() instruction_simplifier$before_codegen (after)
  /// CHECK-NOT: TypeConversion
  private static void intToShort() {
    shortArr[0] = 0;
    if (sFlag) {
      shortArr[0] = (short)getInt();
    }
  }

  /// CHECK-START: void Main.intToChar() instruction_simplifier$before_codegen (after)
  /// CHECK-NOT: TypeConversion
  private static void intToChar() {
    charArr[0] = 0;
    if (sFlag) {
      charArr[0] = (char)getInt();
    }
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
