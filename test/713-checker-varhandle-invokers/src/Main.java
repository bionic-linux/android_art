/*
 * Copyright (C) 2018 The Android Open Source Project
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

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.invoke.VarHandle;
import java.lang.invoke.WrongMethodTypeException;

public final class Main {

    static class B {
        public int value;

        public B() {
            this.value = 13;
        }
        public B(int value) {
            this.value = value;
        }
    }

    static class A {
        public static int intx;
        public static long longx;
        public static boolean boolx;
        public static char charx;
        public static B objx;

        int value;

        public A() {
            this.value = 30;
        }
    }

    static class MyTest {
        private static final VarHandle vhint;
        private static final VarHandle vhlong;
        private static final VarHandle vhbool;
        private static final VarHandle vhchar;
        private static final VarHandle vhobj;

        static {
            try {
                vhint = MethodHandles.lookup().in(A.class).findStaticVarHandle(A.class, "intx", int.class);
                vhlong = MethodHandles.lookup().in(A.class).findStaticVarHandle(A.class, "longx", long.class);
                vhbool = MethodHandles.lookup().in(A.class).findStaticVarHandle(A.class, "boolx", boolean.class);
                vhchar = MethodHandles.lookup().in(A.class).findStaticVarHandle(A.class, "charx", char.class);
                vhobj = MethodHandles.lookup().in(A.class).findStaticVarHandle(A.class, "objx", B.class);
			} catch (NoSuchFieldException | IllegalAccessException e) {
                throw new Error();
        	}
        }

        void run() {
            A.intx = 1000;
            A.longx = 23000000000l;
            A.boolx = false;
            A.charx = 'z';
            A.objx = new B(70);

            int resint = (int) vhint.get();
            if (resint == 1000)
                System.out.println("MyTestInt");

            long reslong = (long) vhlong.get();
            if (reslong == 23000000000l)
                System.out.println("MyTestLong");

            boolean resbool = (boolean) vhbool.get();
            if (!resbool)
                System.out.println("MyTestBool");
            else
                System.out.println("result: " + resbool + " expected: " + true);

            // A obj = new A();
            char reschar = (char) vhchar.get();
            if (reschar == 'z')
                System.out.println("MyTestChar");
            else
                System.out.println("result: " + reschar + " expected: z");

            B check = (B) vhobj.get();
            if (check.value == 70) {
                System.out.println("MyTestReference");
            } else {
                System.out.println("Reference test does not work");
            }
            
        }
    }

    public static void main(String[] args) throws Throwable {
        new MyTest().run();
    }
}
