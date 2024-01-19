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

class Main {

    private static class ByteArrayTests {
        public static byte[] init(byte[] arr) {
            for (byte i = 0; i < arr.length; i++) {
                arr[i] = (byte) i;
            }
            return arr;
        }

        public static void assertTrue(boolean condition) {
            if (!condition) {
                throw new Error();
            }
        }

        public static byte[] createArray0(int size) {
            byte[] arr = new byte[size];
            return arr;
        }

        public static byte[] createArray(int size) {
            byte[] arr = new byte[size];
            init(arr);
            return arr;
        }

        public static void checkIncrementedArray(byte[] dst) {
            for (byte i = 0; i < dst.length; i++) {
                assertTrue(dst[i] == i);
            }
        }

        public static void test1(byte[] src, byte[] dst) {
            System.arraycopy(src, 0, dst, 0, 15);
        }

        public static void test2(byte[] src, byte[] dst) {
            System.arraycopy(src, 0, dst, 0, 127);
        }

        public static byte[] test3() {
            byte size = 15;
            byte[] src = new byte[size];
            src = init(src);
            byte[] dst = new byte[size];

            System.arraycopy(src, 0, dst, 0, size);
            return dst;
        }


        public static byte[] test4() {
            int size = 127;
            byte[] src = new byte[size];
            src = init(src);
            byte[] dst = new byte[size];

            System.arraycopy(src, 0, dst, 0, size);
            return dst;
        }

        public static byte[] test5() {
            byte[] src = new byte[127];
            src = init(src);
            byte[] dst = new byte[127];

            System.arraycopy(src, 0, dst, 0, 127);
            return dst;
        }

        public static byte[] test6() {
            byte[] src = new byte[80];
            src = init(src);
            byte[] dst = new byte[80];

            System.arraycopy(src, 0, dst, 0, 80);
            return dst;
        }

        public static byte[] test7() {
            byte[] src = new byte[127];
            src = init(src);
            byte[] dst = new byte[127];

            System.arraycopy(src, 0, dst, 100, 27);
            return dst;
        }
        public static void check7(byte[] dst) {
            for (byte i = 0; i < 100; i++) {
                assertTrue(dst[i] == 0);
            }
            for (byte i = 100; i < 127; i++) {
                assertTrue(dst[i] == (i - 100));
            }
        }

        public static byte[] test8() {
            byte[] src = new byte[127];
            src = init(src);

            System.arraycopy(src, 0, src, 100, 27);
            return src;
        }
        public static void check8(byte[] dst) {
            for (byte i = 0; i < 100; i++) {
                assertTrue(dst[i] == i);
            }
            for (byte i = 100; i < 127; i++) {
                assertTrue(dst[i] == (i - 100));
            }
        }

        public static byte[] test9() {
            byte[] src = new byte[127];
            src = init(src);

            System.arraycopy(src, 100, src, 0, 27);
            return src;
        }
        public static void check9(byte[] dst) {
            for (byte i = 0; i < 27; i++) {
                assertTrue(dst[i] == (100 + i));
            }
            for (byte i = 27; i < 127; i++) {
                assertTrue(dst[i] == i);
            }
        }

        public static void test10(byte[] src, byte[] dst, byte i) {
            System.arraycopy(src, 0, dst, 0, i);
        }

        public static void runTests() {
            System.out.print("[ByteArrayTests]: ");

            byte[] src15 = createArray(15);
            byte[] dst15 = createArray0(15);
            test1(src15, dst15);
            checkIncrementedArray(dst15);

            byte[] src150 = createArray(127);
            byte[] dst150 = createArray0(127);
            test2(src150, dst150);
            checkIncrementedArray(dst150);

            checkIncrementedArray(test3());
            checkIncrementedArray(test4());
            checkIncrementedArray(test5());
            checkIncrementedArray(test6());

            check7(test7());
            check8(test8());
            check9(test9());

            for (byte i = 1; i < 127; i++) {
                byte[] src = createArray(i);
                byte[] dst = createArray0(i);
                test10(src, dst, i);
                checkIncrementedArray(dst);
            }
            System.out.println("passed");
        }
    }

    private static class CharArrayTests {

        public static char[] init(char[] arr) {
            for (int i = 0; i < arr.length; i++) {
                arr[i] = (char) i;
            }
            return arr;
        }

        public static void assertTrue(boolean condition) {
            if (!condition) {
                throw new Error();
            }
        }

        public static char[] createArray0(int size) {
            char[] arr = new char[size];
            return arr;
        }

        public static char[] createArray(int size) {
            char[] arr = new char[size];
            init(arr);
            return arr;
        }

        public static void checkIncrementedArray(char[] dst) {
            for (int i = 0; i < dst.length; i++) {
                assertTrue(dst[i] == i);
            }
        }

        public static void test1(char[] src, char[] dst) {
            System.arraycopy(src, 0, dst, 0, 15);
        }

        public static void test2(char[] src, char[] dst) {
            System.arraycopy(src, 0, dst, 0, 150);
        }

        public static char[] test3() {
            int size = 15;
            char[] src = new char[size];
            src = init(src);
            char[] dst = new char[size];

            System.arraycopy(src, 0, dst, 0, size);
            return dst;
        }


        public static char[] test4() {
            int size = 150;
            char[] src = new char[size];
            src = init(src);
            char[] dst = new char[size];

            System.arraycopy(src, 0, dst, 0, size);
            return dst;
        }

        public static char[] test5() {
            char[] src = new char[150];
            src = init(src);
            char[] dst = new char[150];

            System.arraycopy(src, 0, dst, 0, 150);
            return dst;
        }

        public static char[] test6() {
            char[] src = new char[80];
            src = init(src);
            char[] dst = new char[80];

            System.arraycopy(src, 0, dst, 0, 80);
            return dst;
        }

        public static char[] test7() {
            char[] src = new char[150];
            src = init(src);
            char[] dst = new char[150];

            System.arraycopy(src, 0, dst, 100, 50);
            return dst;
        }
        public static void check7(char[] dst) {
            for (int i = 0; i < 100; i++) {
                assertTrue(dst[i] == 0);
            }
            for (int i = 100; i < 150; i++) {
                assertTrue(dst[i] == (i - 100));
            }
        }

        public static char[] test8() {
            char[] src = new char[150];
            src = init(src);

            System.arraycopy(src, 0, src, 100, 50);
            return src;
        }
        public static void check8(char[] dst) {
            for (int i = 0; i < 100; i++) {
                assertTrue(dst[i] == i);
            }
            for (int i = 100; i < 150; i++) {
                assertTrue(dst[i] == (i - 100));
            }
        }

        public static char[] test9() {
            char[] src = new char[150];
            src = init(src);

            System.arraycopy(src, 100, src, 0, 50);
            return src;
        }
        public static void check9(char[] dst) {
            for (int i = 0; i < 50; i++) {
                assertTrue(dst[i] == (100 + i));
            }
            for (int i = 50; i < 150; i++) {
                assertTrue(dst[i] == i);
            }
        }

        public static void test10(char[] src, char[] dst, int i) {
            System.arraycopy(src, 0, dst, 0, i);
        }

        public static void runTests() {
            System.out.print("[CharArrayTests]: ");

            char[] src15 = createArray(15);
            char[] dst15 = createArray0(15);
            test1(src15, dst15);
            checkIncrementedArray(dst15);

            char[] src150 = createArray(150);
            char[] dst150 = createArray0(150);
            test2(src150, dst150);
            checkIncrementedArray(dst150);

            checkIncrementedArray(test3());
            checkIncrementedArray(test4());
            checkIncrementedArray(test5());
            checkIncrementedArray(test6());

            check7(test7());
            check8(test8());
            check9(test9());

            for (int i = 1; i < 256; i++) {
                char[] src = createArray(i);
                char[] dst = createArray0(i);
                test10(src, dst, i);
                checkIncrementedArray(dst);
            }

            System.out.println("passed");
        }
    }

    private static class IntArrayTests {
        public static int[] init(int[] arr) {
            for (int i = 0; i < arr.length; i++) {
                arr[i] = (int) i;
            }
            return arr;
        }

        public static void assertTrue(boolean condition) {
            if (!condition) {
                throw new Error();
            }
        }

        public static int[] createArray0(int size) {
            int[] arr = new int[size];
            return arr;
        }

        public static int[] createArray(int size) {
            int[] arr = new int[size];
            init(arr);
            return arr;
        }

        public static void checkIncrementedArray(int[] dst) {
            for (int i = 0; i < dst.length; i++) {
                assertTrue(dst[i] == i);
            }
        }

        public static void test1(int[] src, int[] dst) {
            System.arraycopy(src, 0, dst, 0, 15);
        }

        public static void test2(int[] src, int[] dst) {
            System.arraycopy(src, 0, dst, 0, 150);
        }

        public static int[] test3() {
            int size = 15;
            int[] src = new int[size];
            src = init(src);
            int[] dst = new int[size];

            System.arraycopy(src, 0, dst, 0, size);
            return dst;
        }


        public static int[] test4() {
            int size = 150;
            int[] src = new int[size];
            src = init(src);
            int[] dst = new int[size];

            System.arraycopy(src, 0, dst, 0, size);
            return dst;
        }

        public static int[] test5() {
            int[] src = new int[150];
            src = init(src);
            int[] dst = new int[150];

            System.arraycopy(src, 0, dst, 0, 150);
            return dst;
        }

        public static int[] test6() {
            int[] src = new int[80];
            src = init(src);
            int[] dst = new int[80];

            System.arraycopy(src, 0, dst, 0, 80);
            return dst;
        }

        public static int[] test7() {
            int[] src = new int[150];
            src = init(src);
            int[] dst = new int[150];

            System.arraycopy(src, 0, dst, 100, 50);
            return dst;
        }
        public static void check7(int[] dst) {
            for (int i = 0; i < 100; i++) {
                assertTrue(dst[i] == 0);
            }
            for (int i = 100; i < 150; i++) {
                assertTrue(dst[i] == (i - 100));
            }
        }

        public static int[] test8() {
            int[] src = new int[150];
            src = init(src);

            System.arraycopy(src, 0, src, 100, 50);
            return src;
        }
        public static void check8(int[] dst) {
            for (int i = 0; i < 100; i++) {
                assertTrue(dst[i] == i);
            }
            for (int i = 100; i < 150; i++) {
                assertTrue(dst[i] == (i - 100));
            }
        }

        public static int[] test9() {
            int[] src = new int[150];
            src = init(src);

            System.arraycopy(src, 100, src, 0, 50);
            return src;
        }
        public static void check9(int[] dst) {
            for (int i = 0; i < 50; i++) {
                assertTrue(dst[i] == (100 + i));
            }
            for (int i = 50; i < 150; i++) {
                assertTrue(dst[i] == i);
            }
        }

        public static void test10(int[] src, int[] dst, int i) {
            System.arraycopy(src, 0, dst, 0, i);
        }

        public static void runTests() {
            System.out.print("[IntArrayTests]: ");

            int[] src15 = createArray(15);
            int[] dst15 = createArray0(15);
            test1(src15, dst15);
            checkIncrementedArray(dst15);

            int[] src150 = createArray(150);
            int[] dst150 = createArray0(150);
            test2(src150, dst150);
            checkIncrementedArray(dst150);

            checkIncrementedArray(test3());
            checkIncrementedArray(test4());
            checkIncrementedArray(test5());
            checkIncrementedArray(test6());

            check7(test7());
            check8(test8());
            check9(test9());

            for (int i = 1; i < 256; i++) {
                int[] src = createArray(i);
                int[] dst = createArray0(i);
                test10(src, dst, i);
                checkIncrementedArray(dst);
            }

            System.out.println("passed");
        }
    }

    public static void main(String[] args) {
        ByteArrayTests.runTests();
        CharArrayTests.runTests();
        IntArrayTests.runTests();
    }
}
