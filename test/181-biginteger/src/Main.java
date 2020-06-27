/*
 * Copyright (C) 2020 The Android Open Source Project
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

import java.math.BigInteger;

// This tries to double as a basic BigInteger arithmetic test (PRINT_TIMES = false)
// and a benchmark (PRINT_TIMES = true).

public class Main {
  private static final boolean PRINT_TIMES = false;

  private static long getStartTime() {
    if (PRINT_TIMES) {
      return System.nanoTime();
    } else {
      return 0;
    }
  }

  private static void printTime(String s, long startTime, int reps) {
    if (PRINT_TIMES) {
      System.out.println(s
          + (double)(System.nanoTime() - startTime) / 1000.0 / reps + " usecs / iter");
    }
  }

  // A simple inner product computation, mostly so we can check timing in the
  // absence of any division. Assumes n < 2^30.
  // Note that we're actually squaring values in computing the product. That
  // affects the algorithm used by some implementations.
  private static void inner(int n, int prec) {
    BigInteger big = BigInteger.TEN.pow(prec).shiftLeft(30).add(BigInteger.ONE);
    BigInteger sum = BigInteger.ZERO;
    for (int i = 0; i < n; ++i) {
      sum = sum.add(big.multiply(big));
    }
    if (sum.and(BigInteger.valueOf(0x3fffffff)).intValue() != n) {
      System.out.println("inner() got " + sum.and(BigInteger.valueOf(0x3fffffff))
          + " instead of " + n);
    }
  }

  private static void repeatInner(int n, int prec, int rep) {
    long startTime = getStartTime();
    for (int i = 0; i < rep; ++i) {
      inner(n, prec);
    }
    printTime("inner(" + n + "," + prec + ") took ", startTime, rep);
  }

  // Approximate the sum of the first n terms of the harmonic series to about
  // prec digits of precision, and return the result as a string. The result has
  // an implicit decimal point prec digits from the right.
  private static BigInteger harmonic(int n, int prec) {
    BigInteger scaledOne = BigInteger.TEN.pow(prec);
    BigInteger sum = BigInteger.ZERO;
    for (int i = 1; i <= n; ++i) {
      sum = sum.add(scaledOne.divide(BigInteger.valueOf(i)));
    }
    return sum;
  }

  private static void repeatHarmonic(int n, int prec, int rep) {
    long startTime = getStartTime();
    BigInteger refRes = harmonic(n, prec);
    for (int i = 1; i < rep; ++i) {
      BigInteger newRes = harmonic(n, prec);
      if (!newRes.equals(refRes)) {
        System.out.println(newRes + " != " + refRes);
      }
    }
    printTime("harmonic(" + n + "," + prec + ") took ", startTime, rep);
    System.out.println(refRes);
  }

  // Allow timing of the base conversion from the last test. Assumes rep < 2^30 .
  private static void repeatToString(int n, int prec, int rep) {
    BigInteger refRes = harmonic(n, prec);
    long startTime = getStartTime();
    String refString = refRes.toString();
    for (int i = 1; i < rep; ++i) {
      // Disguise refRes to avoid compiler optimization issues.
      BigInteger newRes = refRes.shiftLeft(30).add(BigInteger.valueOf(i)).shiftRight(30);
      // The time-consuming part:
      String newString = newRes.toString();
      if (!newString.equals(refString)) {
        System.out.println(newString + " != " + refString);
      }
    }
    printTime("toString(" + n + "," + prec + ") took ", startTime, rep);
  }

  // Compute base^exp, where base and result are scaled/multiplied by scaleBy to make them
  // integers. exp >= 0 .
  private static BigInteger myPow(BigInteger base, int exp, BigInteger scaleBy) {
    if (exp == 0) {
      return scaleBy; // Return one.
    } else if ((exp & 1) != 0) {
      BigInteger tmp = myPow(base, exp - 1, scaleBy);
      return tmp.multiply(base).divide(scaleBy);
    } else {
      BigInteger tmp = myPow(base, exp / 2, scaleBy);
      return tmp.multiply(tmp).divide(scaleBy);
    }
  }

  // Approximate e by computing (1 + 1/n)^n to prec decimal digits.
  // This isn't necessarily a very good approximation to e.
  // Return the result as a string, with an implicit decimal point just after
  // the leading 2.
  private static BigInteger eApprox(int n, int prec) {
    BigInteger scaledOne = BigInteger.TEN.pow(prec);
    BigInteger base = scaledOne.add(scaledOne.divide(BigInteger.valueOf(n)));
    return myPow(base, n, scaledOne);
  }

  private static void repeatEApprox(int n, int prec, int rep) {
    long startTime = getStartTime();
    BigInteger refRes = eApprox(n, prec);
    for (int i = 1; i < rep; ++i) {
      BigInteger newRes = eApprox(n, prec);
      if (!newRes.equals(refRes)) {
        System.out.println(newRes + " != " + refRes);
      }
    }
    printTime("eApprox(" + n + "," + prec + ") took ", startTime, rep);
    System.out.println(refRes);
  }

  // Test / time modPow()
  private static void repeatModPow(int len, int rep) {
    BigInteger odd1 = BigInteger.TEN.pow(len / 2).add(BigInteger.ONE);
    BigInteger odd2 = BigInteger.TEN.pow(len / 2).add(BigInteger.valueOf(17));
    BigInteger product = odd1.multiply(odd2);
    BigInteger exponent = BigInteger.TEN.pow(len / 2 - 1);
    BigInteger base = BigInteger.TEN.pow(len / 4);
    long startTime = getStartTime();
    BigInteger lastRes = null;
    for (int i = 0; i < rep; ++i) {
      BigInteger newRes = base.modPow(exponent, product);
      if (i != 0 && !newRes.equals(lastRes)) {
        System.out.println(newRes + " != " + lastRes);
      }
      lastRes = newRes;
    }
    printTime("ModPow() at decimal length " + len + " took ", startTime, rep);
    if (!lastRes.mod(odd1).equals(base.modPow(exponent, odd1))) {
      System.out.println("ModPow() result incorrect mod odd1:" + odd1 + "; lastRes.mod(odd1)="
          + lastRes.mod(odd1) + " vs. " + "base.modPow(exponent, odd1)="
          + base.modPow(exponent, odd1) + " base=" + base + " exponent=" + exponent);
    }
    if (!lastRes.mod(odd2).equals(base.modPow(exponent, odd2))) {
      System.out.println("ModPow() result incorrect mod odd2");
    }
    System.out.println(lastRes.remainder(BigInteger.valueOf(1_000_000_000)));
  }

  // Test / time modInverse()
  private static void repeatModInverse(int len, int rep) {
    BigInteger odd1 = BigInteger.TEN.pow(len / 2).add(BigInteger.ONE);
    BigInteger odd2 = BigInteger.TEN.pow(len / 2).add(BigInteger.valueOf(17));
    BigInteger product = odd1.multiply(odd2);
    BigInteger arg = BigInteger.ONE.shiftLeft(len / 4);
    long startTime = getStartTime();
    BigInteger lastRes = null;
    for (int i = 0; i < rep; ++i) {
      BigInteger newRes = arg.modInverse(product);
      if (i != 0 && !newRes.equals(lastRes)) {
        System.out.println(newRes + " != " + lastRes);
      }
      lastRes = newRes;
    }
    printTime("ModInverse() at decimal length " + len + " took ", startTime, rep);
    if (!lastRes.mod(odd1).equals(arg.modInverse(odd1))) {
      System.out.println("ModInverse() result incorrect mod odd1");
    }
    if (!lastRes.mod(odd2).equals(arg.modInverse(odd2))) {
      System.out.println("ModInverse() result incorrect mod odd2");
    }
    System.out.println(lastRes.remainder(BigInteger.valueOf(1_000_000_000)));
  }

  public static void main(String[] args) throws Exception {
    for (int i = 10; i <= 10_000; i *= 10) {
      repeatInner(1000, i, PRINT_TIMES ? Math.min(20_000 / i, 3_000) : 2);
    }
    for (int i = 5; i <= 5_000; i *= 10) {
      repeatHarmonic(1000, i, PRINT_TIMES ? Math.min(20_000 / i, 3_000) : 2);
    }
    for (int i = 5; i <= 5_000; i *= 10) {
      repeatToString(1000, i, PRINT_TIMES ? Math.min(20_000 / i, 3_000) : 2);
    }
    for (int i = 10; i <= 10_000; i *= 10) {
      repeatEApprox(100_000, i, PRINT_TIMES ? 50_000 / i : 2);
    }
    for (int i = 5; i <= 5_000; i *= 10) {
      repeatModPow(i, PRINT_TIMES ? 10_000 / i : 2);
    }
    for (int i = 10; i <= 10_000; i *= 10) {
      repeatModInverse(i, PRINT_TIMES ? 20_000 / i : 2);
    }
  }
}
