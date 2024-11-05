/*
 * Copyright (C) 2023 The Android Open Source Project
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
 * Tests for autovectorization of loops with control flow.
 */
public class Main {

  public static final int ARRAY_LENGTH = 128;
  public static final int USED_ARRAY_LENGTH = ARRAY_LENGTH - 1;

  public static boolean[] booleanArray = new boolean[ARRAY_LENGTH];
  public static boolean[] booleanArray2 = new boolean[ARRAY_LENGTH];
  public static byte[] byteArray = new byte[ARRAY_LENGTH];
  public static short[] shortArray = new short[ARRAY_LENGTH];
  public static char[] charArray = new char[ARRAY_LENGTH];
  public static int[] intArray = new int[ARRAY_LENGTH];
  public static long[] longArray = new long[ARRAY_LENGTH];
  public static float[] floatArray = new float[ARRAY_LENGTH];
  public static double[] doubleArray = new double[ARRAY_LENGTH];

  public static final int MAGIC_VALUE_A = 2;
  public static final int MAGIC_VALUE_B = 10;
  public static final int MAGIC_VALUE_C = 100;

  public static final int MAGIC_ADD_CONST = 99;

  public static final float MAGIC_FLOAT_VALUE_A = 2.0f;
  public static final float MAGIC_FLOAT_VALUE_B = 10.0f;
  public static final float MAGIC_FLOAT_VALUE_C = 100.0f;

  public static final float MAGIC_FLOAT_ADD_CONST = 99.0f;

  /// CHECK-START-ARM64: int Main.$compile$noinline$FullDiamond(int[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-DAG: <<C0:i\d+>>      IntConstant 0                                         loop:none
  ///     CHECK-DAG: <<C4:i\d+>>      IntConstant 4                                         loop:none
  ///     CHECK-DAG: <<C99:i\d+>>     IntConstant 99                                        loop:none
  ///     CHECK-DAG: <<C100:i\d+>>    IntConstant 100                                       loop:none
  ///     CHECK-DAG: <<Vec4:d\d+>>    VecReplicateScalar [<<C4>>,{{j\d+}}]                  loop:none
  ///     CHECK-DAG: <<Vec99:d\d+>>   VecReplicateScalar [<<C99>>,{{j\d+}}]                 loop:none
  ///     CHECK-DAG: <<Vec100:d\d+>>  VecReplicateScalar [<<C100>>,{{j\d+}}]                loop:none
  //
  ///     CHECK-DAG: <<Phi:i\d+>>     Phi [<<C0>>,{{i\d+}}]                                 loop:<<Loop:B\d+>> outer_loop:none
  ///     CHECK-DAG: <<LoopP:j\d+>>   VecPredWhile [<<Phi>>,{{i\d+}}]                       loop:<<Loop>>      outer_loop:none
  //
  ///     CHECK-DAG: <<Load1:d\d+>>   VecLoad [<<Arr:l\d+>>,<<Phi>>,<<LoopP>>]              loop:<<Loop>>      outer_loop:none
  ///     CHECK-DAG: <<Cond:j\d+>>    VecEqual [<<Load1>>,<<Vec100>>,<<LoopP>>]             loop:<<Loop>>      outer_loop:none
  ///     CHECK-DAG: <<CondR:j\d+>>   VecPredNot [<<Cond>>,<<LoopP>>]                       loop:<<Loop>>      outer_loop:none
  ///     CHECK-DAG: <<AddT:d\d+>>    VecAdd [<<Load1>>,<<Vec99>>,<<CondR>>]                loop:<<Loop>>      outer_loop:none
  ///     CHECK-DAG: <<StT:d\d+>>     VecStore [<<Arr>>,<<Phi>>,<<AddT>>,<<CondR>>]         loop:<<Loop>>      outer_loop:none
  ///     CHECK-DAG: <<StF:d\d+>>     VecStore [<<Arr>>,<<Phi>>,{{d\d+}},<<Cond>>]          loop:<<Loop>>      outer_loop:none
  ///     CHECK-DAG: <<Ld2:d\d+>>     VecLoad [<<Arr>>,<<Phi>>,<<LoopP>>]                   loop:<<Loop>>      outer_loop:none
  ///     CHECK-DAG: <<Add2:d\d+>>    VecAdd [<<Ld2>>,<<Vec4>>,<<LoopP>>]                   loop:<<Loop>>      outer_loop:none
  ///     CHECK-DAG: <<St21:d\d+>>    VecStore [<<Arr>>,<<Phi>>,<<Add2>>,<<LoopP>>]         loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-ELSE:
  //
  ///     CHECK-NOT:                      VecLoad
  //
  /// CHECK-FI:
  public static int $compile$noinline$FullDiamond(int[] x) {
    int i = 0;
    for (; i < USED_ARRAY_LENGTH; i++) {
      int val = x[i];
      if (val != MAGIC_VALUE_C) {
        x[i] += MAGIC_ADD_CONST;
      } else {
        x[i] += 3;
      }
      x[i] += 4;
    }
    return i;
  }

  //
  // Test various types.
  //

  /// CHECK-START-ARM64: void Main.$compile$noinline$SimpleFloat(float[]) loop_optimization (before)
  //
  ///     CHECK-DAG: <<C0:i\d+>>      IntConstant 0                                                           loop:none
  ///     CHECK-DAG: <<C99:f\d+>>     FloatConstant 99                                                        loop:none
  ///     CHECK-DAG: <<C100:f\d+>>    FloatConstant 100                                                       loop:none
  //
  ///     CHECK-DAG: <<Phi:i\d+>>     Phi [<<C0>>,{{i\d+}}]                                                   loop:<<Loop:B\d+>>
  ///     CHECK-DAG: <<Load:f\d+>>    ArrayGet [<<Arr:l\d+>>,<<Phi>>]                                         loop:<<Loop>>
  ///     CHECK-DAG: <<Cond:z\d+>>    LessThanOrEqual [<<Load>>,<<C100>>]                                     loop:<<Loop>>
  ///     CHECK-DAG:                  If [<<Cond>>]                                                           loop:<<Loop>>
  //
  ///     CHECK-DAG: <<Add:f\d+>>     Add [<<Load>>,<<C99>>]                                                  loop:<<Loop>>
  ///     CHECK-DAG:                  ArraySet [<<Arr>>,<<Phi>>,<<Add>>]                                      loop:<<Loop>>
  //
  /// CHECK-START-ARM64: void Main.$compile$noinline$SimpleFloat(float[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-DAG: <<C0:i\d+>>      IntConstant 0                                                             loop:none
  ///     CHECK-DAG: <<C99:f\d+>>     FloatConstant 99                                                          loop:none
  ///     CHECK-DAG: <<C100:f\d+>>    FloatConstant 100                                                         loop:none
  //
  ///     CHECK-DAG: <<Vec99:d\d+>>   VecReplicateScalar [<<C99>>,{{j\d+}}]              packed_type:Float32    loop:none
  ///     CHECK-DAG: <<Vec100:d\d+>>  VecReplicateScalar [<<C100>>,{{j\d+}}]             packed_type:Float32    loop:none
  //
  ///     CHECK-DAG: <<Phi:i\d+>>     Phi [<<C0>>,{{i\d+}}]                                                     loop:<<Loop:B\d+>>
  ///     CHECK-DAG: <<LoopP:j\d+>>   VecPredWhile [<<Phi>>,{{i\d+}}]                                           loop:<<Loop>>
  //
  ///     CHECK-DAG: <<Load:d\d+>>    VecLoad [<<Arr:l\d+>>,<<Phi>>,<<LoopP>>]           packed_type:Float32    loop:<<Loop>>
  ///     CHECK-DAG: <<Cond:j\d+>>    VecLessThanOrEqual [<<Load>>,<<Vec100>>,<<LoopP>>] packed_type:Float32    loop:<<Loop>>
  ///     CHECK-DAG: <<CondR:j\d+>>   VecPredNot [<<Cond>>,<<LoopP>>]                    packed_type:Float32    loop:<<Loop>>
  ///     CHECK-DAG: <<Add:d\d+>>     VecAdd [<<Load>>,<<Vec99>>,<<CondR>>]
  ///     CHECK-DAG:                  VecStore [<<Arr>>,<<Phi>>,<<Add>>,<<CondR>>]       packed_type:Float32    loop:<<Loop>>
  //
  /// CHECK-FI:
  //
  // Example of a floating point type being vectorized. See loop_optimization_test.cc and
  // codegen_test.cc for full testing of vector types.
  public static void $compile$noinline$SimpleFloat(float[] x) {
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      float val = x[i];
      if (val > MAGIC_FLOAT_VALUE_C) {
        x[i] += MAGIC_FLOAT_ADD_CONST;
      }
    }
  }

  /// CHECK-START-ARM64: void Main.$compile$noinline$DifferentTypes(byte[], short[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-NOT: VecLoad
  //
  /// CHECK-FI:
  // Loops with different types cannot be implicitly widened during vectorization.
  public static void $compile$noinline$DifferentTypes(byte[] x, short[] y) {
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      byte val = x[i];
      if (val != y[i]) {
        x[i] += y[i];
      }
    }
  }

  //
  // Narrowing types.
  //

  /// CHECK-START-ARM64: void Main.$compile$noinline$ByteConv(byte[], byte[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-DAG: VecLoad
  //
  /// CHECK-FI:
  public static void $compile$noinline$ByteConv(byte[] x, byte[] y) {
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      byte val = (byte)(x[i] + 1);
      if (val != y[i]) {
        x[i] += MAGIC_ADD_CONST;
      }
    }
  }

  /// CHECK-START-ARM64: void Main.$compile$noinline$UByteAndWrongConst(byte[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-NOT: VecLoad
  //
  /// CHECK-FI:
  //
  // 'NarrowerOperands' not met: the constant is not a ubyte one.
  public static void $compile$noinline$UByteAndWrongConst(byte[] x) {
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      if ((x[i] & 0xFF) != (MAGIC_VALUE_C | 0x100)) {
        x[i] += MAGIC_ADD_CONST;
      }
    }
  }

  /// CHECK-START-ARM64: void Main.$compile$noinline$ByteNoHiBits(byte[], byte[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-NOT: VecLoad
  //
  /// CHECK-FI:
  //
  // Check kNoHiBits case when "wider" operations cannot bring in higher order bits.
  public static void $compile$noinline$ByteNoHiBits(byte[] x, byte[] y) {
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      byte val = x[i];
      if ((val >>> 3) != y[i]) {
        x[i] += MAGIC_ADD_CONST;
      }
    }
  }

  //
  // Test condition types.
  //

  /// CHECK-START-ARM64: void Main.$compile$noinline$SimpleCondition(int[]) loop_optimization (before)
  //
  ///     CHECK-DAG: <<C0:i\d+>>      IntConstant 0                                                           loop:none
  ///     CHECK-DAG: <<C100:i\d+>>    IntConstant 100                                                         loop:none
  ///     CHECK-DAG: <<C199:i\d+>>    IntConstant 199                                                         loop:none
  //
  ///     CHECK-DAG: <<Phi:i\d+>>     Phi [<<C0>>,{{i\d+}}]                                                   loop:<<Loop:B\d+>>
  ///     CHECK-DAG: <<Load:i\d+>>    ArrayGet [<<Arr:l\d+>>,<<Phi>>]                                         loop:<<Loop>>
  ///     CHECK-DAG: <<Cond:z\d+>>    NotEqual [<<Load>>,<<C100>>]                                            loop:<<Loop>>
  ///     CHECK-DAG:                  If [<<Cond>>]                                                           loop:<<Loop>>
  //
  ///     CHECK-DAG:                  ArraySet [<<Arr>>,<<Phi>>,<<C199>>]                                     loop:<<Loop>>
  //
  /// CHECK-START-ARM64: void Main.$compile$noinline$SimpleCondition(int[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-DAG: <<C0:i\d+>>      IntConstant 0                                                           loop:none
  ///     CHECK-DAG: <<C100:i\d+>>    IntConstant 100                                                         loop:none
  ///     CHECK-DAG: <<C199:i\d+>>    IntConstant 199                                                         loop:none
  //
  ///     CHECK-DAG: <<Vec100:d\d+>>  VecReplicateScalar [<<C100>>,{{j\d+}}]           packed_type:Int32      loop:none
  ///     CHECK-DAG: <<Vec199:d\d+>>  VecReplicateScalar [<<C199>>,{{j\d+}}]           packed_type:Int32      loop:none
  //
  ///     CHECK-DAG: <<Phi:i\d+>>     Phi [<<C0>>,{{i\d+}}]                                                   loop:<<Loop:B\d+>>
  ///     CHECK-DAG: <<LoopP:j\d+>>   VecPredWhile [<<Phi>>,{{i\d+}}]                                         loop:<<Loop>>
  //
  ///     CHECK-DAG: <<Load:d\d+>>    VecLoad [<<Arr:l\d+>>,<<Phi>>,<<LoopP>>]         packed_type:Int32      loop:<<Loop>>
  ///     CHECK-DAG: <<Cond:j\d+>>    VecNotEqual [<<Load>>,<<Vec100>>,<<LoopP>>]      packed_type:Int32      loop:<<Loop>>
  ///     CHECK-DAG: <<CondR:j\d+>>   VecPredNot [<<Cond>>,<<LoopP>>]                  packed_type:Int32      loop:<<Loop>>
  ///     CHECK-DAG:                  VecStore [<<Arr>>,<<Phi>>,<<Vec199>>,<<CondR>>]  packed_type:Int32      loop:<<Loop>>
  //
  /// CHECK-FI:
  //
  // Example of a condition being vectorized. See loop_optimization_test.cc and codegen_test.cc for
  // full testing of vector conditions.
  public static void $compile$noinline$SimpleCondition(int[] x) {
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      int val = x[i];
      if (val == MAGIC_VALUE_C) {
        x[i] += MAGIC_ADD_CONST;
      }
    }
  }

  //
  // Test vectorization idioms.
  //

  /// CHECK-START-ARM64: void Main.$compile$noinline$Select(int[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-NOT: VecLoad
  //
  /// CHECK-FI:
  //
  // TODO: vectorize loops with select in the body.
  public static void $compile$noinline$Select(int[] x) {
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      int val = x[i];
      if (val != MAGIC_VALUE_C) {
        val += MAGIC_ADD_CONST;
      }
      x[i] = val;
    }
  }

  /// CHECK-START-ARM64: void Main.$compile$noinline$Phi(int[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-NOT: VecLoad
  //
  /// CHECK-FI:
  //
  // TODO: vectorize loops with phis in the body.
  public static void $compile$noinline$Phi(int[] x) {
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      int val = x[i];
      if (val != MAGIC_VALUE_C) {
        val += MAGIC_ADD_CONST;
        x[i] += val;
      }
      x[i] += val;
    }
  }

  // TODO: when Phis are supported, test dotprod and sad idioms.

  /// CHECK-START-ARM64: int Main.$compile$noinline$Reduction(int[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-NOT: VecLoad
  //
  /// CHECK-FI:
  //
  // TODO: vectorize loops with phis and reductions in the body.
  private static int $compile$noinline$Reduction(int[] x) {
    int sum = 0;
    for (int i = 0; i < ARRAY_LENGTH; i++) {
      int val = x[i];
      if (val != MAGIC_VALUE_C) {
        sum += val + x[i];
      }
    }
    return sum;
  }

  /// CHECK-START-ARM64: int Main.$compile$noinline$ReductionBackEdge(int[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-DAG: VecLoad
  //
  /// CHECK-FI:
  //
  // Reduction in the back edge block, non-CF-dependent.
  public static int $compile$noinline$ReductionBackEdge(int[] x) {
    int sum = 0;
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      int val = x[i];
      if (val != MAGIC_VALUE_C) {
        x[i] += MAGIC_ADD_CONST;
      }
      sum += x[i];
    }
    return sum;
  }

  //
  // Negative compile tests.
  //

  public static final int STENCIL_ARRAY_SIZE = 130;

  /// CHECK-START-ARM64: void Main.$compile$noinline$stencilAlike(int[], int[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-NOT: VecLoad
  //
  /// CHECK-FI:
  //
  // This loop needs a runtime test for array references disambiguation and a scalar cleanup loop.
  // Currently we can't generate a scalar clean up loop with control flow.
  private static void $compile$noinline$stencilAlike(int[] a, int[] b) {
    for (int i = 1; i < STENCIL_ARRAY_SIZE - 1; i++) {
      int val0 = b[i - 1];
      int val1 = b[i];
      int val2 = b[i + 1];
      int un = a[i];
      if (val1 != MAGIC_VALUE_C) {
        a[i] = val0 + val1 + val2;
      }
    }
  }

  /// CHECK-START-ARM64: void Main.$compile$noinline$NotDiamondCf(int[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-NOT: VecLoad
  //
  /// CHECK-FI:
  //
  // Loops with complex CF are not supported.
  public static void $compile$noinline$NotDiamondCf(int[] x) {
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      int val = x[i];
      if (val != MAGIC_VALUE_C) {
        if (val != 1234) {
          x[i] += MAGIC_ADD_CONST;
        }
      }
    }
  }

  /// CHECK-START-ARM64: void Main.$compile$noinline$BrokenInduction(int[]) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-NOT: VecLoad
  //
  /// CHECK-FI:
  public static void $compile$noinline$BrokenInduction(int[] x) {
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      int val = x[i];
      if (val != MAGIC_VALUE_C) {
        x[i] += MAGIC_ADD_CONST;
        i++;
      }
    }
  }

  //
  // Non-condition if statements.
  //

  /// CHECK-START-ARM64: void Main.$compile$noinline$SingleBoolean(int[], boolean) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-NOT: VecLoad
  //
  /// CHECK-FI:
  //
  // Check that single boolean if statements are not vectorized because only binary condition if
  // statements are supported.
  public static void $compile$noinline$SingleBoolean(int[] x, boolean y) {
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      if (y) {
        x[i] += MAGIC_ADD_CONST;
      }
    }
  }

  /// CHECK-START-ARM64: void Main.$compile$noinline$InstanceOf(int[], java.lang.Object) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-NOT: VecLoad
  //
  /// CHECK-FI:
  //
  // Check that control flow without a condition is not vectorized because only binary condition if
  // statements are supported.
  public static void $compile$noinline$InstanceOf(int[] x, Object y) {
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      if (y instanceof Main) {
        x[i] += MAGIC_ADD_CONST;
      }
    }
  }

  /// CHECK-START-ARM64: void Main.$compile$noinline$LoopInvariantCondition(float[], float) loop_optimization (after)
  /// CHECK-IF:     hasIsaFeature("sve") and os.environ.get('ART_FORCE_TRY_PREDICATED_SIMD') == 'true'
  //
  ///     CHECK-NOT: VecLoad
  //
  /// CHECK-FI:
  //
  // TODO: vectorize loops with invariant conditions.
  public static void $compile$noinline$LoopInvariantCondition(float x[], float y) {
    for (int i = 0; i < USED_ARRAY_LENGTH; i++) {
      float val = x[i];
      if (y != MAGIC_FLOAT_VALUE_C) {
        x[i] += MAGIC_FLOAT_ADD_CONST + val;
      }
    }
  }

  //
  // Main driver.
  //

  public static void main(String[] args) {
    initIntArray(intArray);
    int final_ind_value = $compile$noinline$FullDiamond(intArray);
    expectIntEquals(23755, IntArraySum(intArray));
    expectIntEquals(USED_ARRAY_LENGTH, final_ind_value);

    // Types.
    initFloatArray(floatArray);
    $compile$noinline$SimpleFloat(floatArray);
    expectFloatEquals(14706.0f, FloatArraySum(floatArray));

    initByteArray(byteArray);
    initShortArray(shortArray);
    $compile$noinline$DifferentTypes(byteArray, shortArray);
    expectIntEquals(-31, ByteArraySum(byteArray));

    // Narrowing types.
    initByteArray(byteArray);
    $compile$noinline$ByteConv(byteArray, byteArray);
    expectIntEquals(-2, ByteArraySum(byteArray));

    initByteArray(byteArray);
    $compile$noinline$UByteAndWrongConst(byteArray);
    expectIntEquals(-2, ByteArraySum(byteArray));

    initByteArray(byteArray);
    $compile$noinline$ByteNoHiBits(byteArray, byteArray);
    expectIntEquals(-2, ByteArraySum(byteArray));

    // Conditions.
    initIntArray(intArray);
    $compile$noinline$SimpleCondition(intArray);
    expectIntEquals(18864, IntArraySum(intArray));

    // Idioms.
    initIntArray(intArray);
    $compile$noinline$Select(intArray);
    expectIntEquals(23121, IntArraySum(intArray));

    initIntArray(intArray);
    $compile$noinline$Phi(intArray);
    expectIntEquals(36748, IntArraySum(intArray));

    int reduction_result = 0;

    initIntArray(intArray);
    reduction_result = $compile$noinline$Reduction(intArray);
    expectIntEquals(14706, IntArraySum(intArray));
    expectIntEquals(21012, reduction_result);

    initIntArray(intArray);
    reduction_result = $compile$noinline$ReductionBackEdge(intArray);
    expectIntEquals(23121, IntArraySum(intArray));
    expectIntEquals(13121, reduction_result);

    int[] stencilArrayA = new int[STENCIL_ARRAY_SIZE];
    int[] stencilArrayB = new int[STENCIL_ARRAY_SIZE];
    initIntArray(stencilArrayA);
    initIntArray(stencilArrayB);
    $compile$noinline$stencilAlike(stencilArrayA, stencilArrayB);
    expectIntEquals(43602, IntArraySum(stencilArrayA));

    initIntArray(intArray);
    $compile$noinline$NotDiamondCf(intArray);
    expectIntEquals(23121, IntArraySum(intArray));

    initIntArray(intArray);
    $compile$noinline$BrokenInduction(intArray);
    expectIntEquals(18963, IntArraySum(intArray));

    // Non-condition if statements.
    initIntArray(intArray);
    $compile$noinline$SingleBoolean(intArray, true);
    expectIntEquals(27279, IntArraySum(intArray));

    initIntArray(intArray);
    Main instance = new Main();
    $compile$noinline$InstanceOf(intArray, instance);
    expectIntEquals(27279, IntArraySum(intArray));

    initFloatArray(floatArray);
    $compile$noinline$LoopInvariantCondition(floatArray, MAGIC_FLOAT_VALUE_A);
    expectFloatEquals(31985.0f, FloatArraySum(floatArray));

    System.out.println("passed");
  }

  public static void initBooleanArray(boolean[] a) {
    for (int i = 0; i < ARRAY_LENGTH; i++) {
      if (i % 3 != 0) {
        a[i] = true;
      }
    }
  }

  public static void initByteArray(byte[] a) {
    for (int i = 0; i < ARRAY_LENGTH; i++) {
      if (i % 3 == 0) {
        a[i] = (byte)MAGIC_VALUE_A;
      } else if (i % 3 == 1) {
        a[i] = (byte)MAGIC_VALUE_B;
      } else {
        a[i] = (byte)MAGIC_VALUE_C;
      }
    }
    a[USED_ARRAY_LENGTH] = 127;
  }

  public static void initShortArray(short[] a) {
    for (int i = 0; i < ARRAY_LENGTH; i++) {
      if (i % 3 == 0) {
        a[i] = (short)MAGIC_VALUE_A;
      } else if (i % 3 == 1) {
        a[i] = (short)MAGIC_VALUE_B;
      } else {
        a[i] = (short)MAGIC_VALUE_C;
      }
    }
    a[USED_ARRAY_LENGTH] = 10000;
  }

  public static void initCharArray(char[] a) {
    for (int i = 0; i < ARRAY_LENGTH; i++) {
      if (i % 3 == 0) {
        a[i] = (char)MAGIC_VALUE_A;
      } else if (i % 3 == 1) {
        a[i] = (char)MAGIC_VALUE_B;
      } else {
        a[i] = (char)MAGIC_VALUE_C;
      }
    }
    a[USED_ARRAY_LENGTH] = 10000;
  }

  public static void initIntArray(int[] a) {
    for (int i = 0; i < ARRAY_LENGTH; i++) {
      if (i % 3 == 0) {
        a[i] = MAGIC_VALUE_A;
      } else if (i % 3 == 1) {
        a[i] = MAGIC_VALUE_B;
      } else {
        a[i] = MAGIC_VALUE_C;
      }
    }
    a[USED_ARRAY_LENGTH] = 10000;
  }

  public static void initLongArray(long[] a) {
    for (int i = 0; i < ARRAY_LENGTH; i++) {
      if (i % 3 == 0) {
        a[i] = MAGIC_VALUE_A;
      } else if (i % 3 == 1) {
        a[i] = MAGIC_VALUE_B;
      } else {
        a[i] = MAGIC_VALUE_C;
      }
    }
    a[USED_ARRAY_LENGTH] = 10000;
  }

  public static void initFloatArray(float[] a) {
    for (int i = 0; i < ARRAY_LENGTH; i++) {
      if (i % 3 == 0) {
        a[i] = MAGIC_FLOAT_VALUE_A;
      } else if (i % 3 == 1) {
        a[i] = MAGIC_FLOAT_VALUE_B;
      } else {
        a[i] = MAGIC_FLOAT_VALUE_C;
      }
    }
    a[USED_ARRAY_LENGTH] = 10000.0f;
  }

  public static void initDoubleArray(double[] a) {
    for (int i = 0; i < ARRAY_LENGTH; i++) {
      if (i % 3 == 0) {
        a[i] = MAGIC_FLOAT_VALUE_A;
      } else if (i % 3 == 1) {
        a[i] = MAGIC_FLOAT_VALUE_B;
      } else {
        a[i] = MAGIC_FLOAT_VALUE_C;
      }
    }
    a[USED_ARRAY_LENGTH] = 10000.0f;
  }

  public static byte BooleanArraySum(boolean[] a) {
    byte sum = 0;
    for (int i = 0; i < a.length; i++) {
      sum += a[i] ? 1 : 0;
    }
    return sum;
  }

  public static byte ByteArraySum(byte[] a) {
    byte sum = 0;
    for (int i = 0; i < a.length; i++) {
      sum += a[i];
    }
    return sum;
  }

  public static short ShortArraySum(short[] a) {
    short sum = 0;
    for (int i = 0; i < a.length; i++) {
      sum += a[i];
    }
    return sum;
  }

  public static char CharArraySum(char[] a) {
    char sum = 0;
    for (int i = 0; i < a.length; i++) {
      sum += a[i];
    }
    return sum;
  }

  public static int IntArraySum(int[] a) {
    int sum = 0;
    for (int i = 0; i < a.length; i++) {
      sum += a[i];
    }
    return sum;
  }

  public static long LongArraySum(long[] a) {
    long sum = 0;
    for (int i = 0; i < a.length; i++) {
      sum += a[i];
    }
    return sum;
  }

  public static float FloatArraySum(float[] a) {
    float sum = 0.0f;
    for (int i = 0; i < a.length; i++) {
      sum += a[i];
    }
    return sum;
  }

  public static double DoubleArraySum(double[] a) {
    double sum = 0.0;
    for (int i = 0; i < a.length; i++) {
      sum += a[i];
    }
    return sum;
  }

  private static void expectIntEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  private static void expectLongEquals(long expected, long result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  private static void expectFloatEquals(float expected, float result) {
    final float THRESHOLD = .1f;
    if (Math.abs(expected - result) >= THRESHOLD) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  private static void expectDoubleEquals(double expected, double result) {
    final double THRESHOLD = .1;
    if (Math.abs(expected - result) >= THRESHOLD) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
