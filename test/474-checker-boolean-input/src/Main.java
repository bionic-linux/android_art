/*
 * Copyright (C) 2015 The Android Open Source Project
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

  public static void assertBoolEquals(boolean expected, boolean result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static boolean f1;
  public static boolean f2;
  
  public static boolean $inline$Phi(int x) {
    return (x == 42) ? f1 : f2;
  }
  
  /*
   * Test that integer Phis are accepted as Boolean inputs until
   * we implement a suitable type analysis.
   */

  // We check that there's a Phi that is used as an input to an HIf instruction
  // and the return instruction (which returns a boolean).
  // We cannot use a Select like in the other tests as we perform more
  // opitmizations which remove the need of having a Phi.

  /// CHECK-START: boolean Main.$noinline$TestPhiAsBoolean(int) inliner (after)
  /// CHECK:     <<Phi1:i\d+>>     Phi
  /// CHECK:                       Return [<<Phi1>>]
  /// CHECK:     <<Phi2:i\d+>>     Phi
  /// CHECK:                       If [<<Phi2>>]
  public static boolean $noinline$TestPhiAsBoolean(int x) {
    return $inline$Phi(x) != true ? true : false;
  }

  /*
   * Test that integer And is accepted as a Boolean input until
   * we implement a suitable type analysis.
   */

  /// CHECK-START: boolean Main.$noinline$TestAndAsBoolean(boolean, boolean) select_generator (after)
  /// CHECK-DAG:     <<And:i\d+>>     And
  /// CHECK-DAG:                      Select [{{i\d+}},{{i\d+}},<<And>>]

  public static boolean $inline$And(boolean x, boolean y) {
    return x & y;
  }

  public static boolean $noinline$TestAndAsBoolean(boolean x, boolean y) {
    return $inline$And(x, y) != true ? true : false;
  }

  /*
   * Test that integer Or is accepted as a Boolean input until
   * we implement a suitable type analysis.
   */

  /// CHECK-START: boolean Main.$noinline$TestOrAsBoolean(boolean, boolean) select_generator (after)
  /// CHECK-DAG:     <<Or:i\d+>>      Or
  /// CHECK-DAG:                      Select [{{i\d+}},{{i\d+}},<<Or>>]

  public static boolean $inline$Or(boolean x, boolean y) {
    return x | y;
  }

  public static boolean $noinline$TestOrAsBoolean(boolean x, boolean y) {
    return $inline$Or(x, y) != true ? true : false;
  }

  /*
   * Test that integer Xor is accepted as a Boolean input until
   * we implement a suitable type analysis.
   */

  /// CHECK-START: boolean Main.$noinline$TestXorAsBoolean(boolean, boolean) select_generator (after)
  /// CHECK-DAG:     <<Xor:i\d+>>     Xor
  /// CHECK-DAG:                      Select [{{i\d+}},{{i\d+}},<<Xor>>]

  public static boolean $inline$Xor(boolean x, boolean y) {
    return x ^ y;
  }

  public static boolean $noinline$TestXorAsBoolean(boolean x, boolean y) {
    return $inline$Xor(x, y) != true ? true : false;
  }

  public static void main(String[] args) {
    f1 = true;
    f2 = false;
    assertBoolEquals(true, $noinline$TestPhiAsBoolean(0));
    assertBoolEquals(false, $noinline$TestPhiAsBoolean(42));
    assertBoolEquals(true, $noinline$TestAndAsBoolean(true, false));
    assertBoolEquals(false, $noinline$TestAndAsBoolean(true, true));
    assertBoolEquals(true, $noinline$TestOrAsBoolean(false, false));
    assertBoolEquals(false, $noinline$TestOrAsBoolean(true, true));
    assertBoolEquals(true, $noinline$TestXorAsBoolean(true, true));
    assertBoolEquals(false, $noinline$TestXorAsBoolean(true, false));
  }
}
