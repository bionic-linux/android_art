/*
 * Copyright (C) 2024 The Android Open Source Project
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

import java.lang.reflect.Method;

public class Main {
    public static int mI = 0;
    public static float mF = 0f;

    // Copy of testAllocationEliminationWithLoops from 530-checker-lse

    /// CHECK-START: float Main.$noinline$testAllocationEliminationWithLoops() load_store_elimination (before)
    /// CHECK: NewInstance
    /// CHECK: NewInstance
    /// CHECK: NewInstance

    /// CHECK-START: float Main.$noinline$testAllocationEliminationWithLoops() load_store_elimination (after)
    /// CHECK-NOT: NewInstance

    private static float $noinline$testAllocationEliminationWithLoops() {
        for (int i0 = 0; i0 < 5; i0++) {
            for (int i1 = 0; i1 < 5; i1++) {
                for (int i2 = 0; i2 < 5; i2++) {
                    int lI0 = ((int) new Integer(((int) new Integer(mI))));
                    if (((boolean) new Boolean(false))) {
                        for (int i3 = 576 - 1; i3 >= 0; i3--) {
                            mF -= 976981405.0f;
                        }
                    }
                }
            }
        }
        return 1.0f;
    }

    private static void $noinline$testCallToSmali(String name) throws Exception {
        Class<?> c = Class.forName("Smali");
        Method m = c.getMethod(name);
        m.invoke(null);
    }

    public static void main(String[] args) throws Exception {
        $noinline$testAllocationEliminationWithLoops();
        $noinline$assertFloatEquals(mF, 0f);

        $noinline$testCallToSmali("$noinline$testAllocationEliminationWithLoops_Smali");
        Class<?> smali_class = Class.forName("Smali");
        float smali_field = (float) smali_class.getField("mF").getFloat(smali_class);
        $noinline$assertFloatEquals(smali_field, 0f);

        $noinline$testCallToSmali("$noinline$testAllocationEliminationWithLoops_Smali_Range");
        smali_field = (float) smali_class.getField("mF").getFloat(smali_class);
        $noinline$assertFloatEquals(smali_field, 0f);
    }

    private static void $noinline$assertFloatEquals(float expected, float result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }
}
