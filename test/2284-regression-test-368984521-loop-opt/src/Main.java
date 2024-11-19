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

public class Main {
    int field;
    long checksum;

    void foo() {
        int to_add_to_checksum = 112;
        int[] iArr1 = new int[256];
        try {
            for (int i = 10; i < 187; i++) {
                try {
                    int i20 = field % iArr1[i];
                } catch (ArithmeticException ae) {
                }
                // This loop won't be fully unrolled, since `field` is used in the try above and
                // therefore in the implicit catch phi too.
                for (int j = 1; j < 2; j++) {
                    to_add_to_checksum += 209;
                    field -= field;
                }
            }
        } catch (Throwable e) {
        }
        checksum += to_add_to_checksum;
    }

    void mainTest() {
        foo();
        System.out.println(checksum);
    }

    public static void main(String[] strArr) {
        Main test = new Main();
        for (int i = 0; i < 10; ++i) {
            test.mainTest();
        }
    }
}
