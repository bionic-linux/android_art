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

import java.lang.reflect.Method;

public class Main {
    public static void main(String[] args) {
        // Check if we're running dalvik or RI.
        boolean usingRI = false;
        try {
            Class.forName("dalvik.system.PathClassLoader");
        } catch (ClassNotFoundException e) {
            usingRI = true;
        }

        try {
            /*
             * Test1
             * -----
             * Tested functions:
             *     class Test1Base                      {
             *         public void foo() { ... }
             *     }
             *     class Test1Derived extends Test1Base {
             *         private void foo() { ... }
             *         ...
             *     }
             * Tested invokes:
             *     invoke-direct  Test1Derived.foo()V   from Test1Derived in first dex file
             *     invoke-virtual Test1Derived.foo()V   from Test1User    in second dex file
             *     invoke-virtual Test1Derived.foo()V   from Test1User2   in first dex file
             *
             * Previously, the behavior was very odd because the lookups for direct and
             * virtual methods were independent but results were stored in a single slot
             * in the DexCache method array and then retrieved from there without checking
             * the resolution kind. Thus, the first invoke-direct stored the private
             * Test1Derived.foo() in the DexCache and the attempt to use invoke-virtual
             * from the same dex file (by Test1User2) would throw ICCE. However, the same
             * invoke-virtual from a different dex file (by Test1User) would ignore the
             * direct method Test1Derived.foo() and find the Test1Base.foo() and call it.
             *
             * The method lookup has been changed and we now consistently find the private
             * Derived.foo() and throw ICCE for both invoke-virtual calls.
             *
             * Note that for the Java files to compile the test case, we need Test1User
             * and Test1User2 to see the class Test1Derived defining a public foo()V but
             * we need a different Test1Derived with a private foo()V for the final test,
             * so we define Test1Derived in src/ and src2/. The Test1Derived in src2/ with
             * private foo()V () needs to derive from a Test1Base class that defines a
             * public foo()V in the final test but it cannot reduce its visibility and
             * therefore needs to be compiled with a Test1Base that does not define it,
             * so we define Test1Base in src2/ and src3/. Files:
             *   src/Test1User2.java         - calls invoke-virtual Test1Derived.foo().
             *   src-multidex/Test1User.java - calls invoke-virtual Test1Derived.foo().
             *   src/Test1Derived.java       - defines public foo()V, not used in the test.
             *   src2/Test1Derived.java      - defines private foo()V, calls it with invoke-direct.
             *   src2/Test1Base.java         - does not define foo()V, not used in the test.
             *   src3/Test1Base.java         - defines public foo()V.
             */
            invokeUserTest("Test1Derived");
            invokeUserTest("Test1User");
            invokeUserTest("Test1User2");

            /*
             * Test2
             * -----
             * Tested functions:
             *     class Test2Base {
             *         public static void foo() { ... }
             *     }
             *     interface Test2Interface {
             *         default void foo() { ... }  // default: avoid subclassing Test2Derived.
             *     }
             *     class Test2Derived extends Test2Base implements Test2Interface { }
             * Tested invokes:
             *     invoke-virtual Test2Derived.foo()V   from Test2User  in first dex file
             *     invoke-static  Test2Derived.foo()V   from Test2User2 in first dex file
             *
             * Previously, due to different lookup types and multi-threaded verification,
             * it was undeterministic which method ended up in the DexCache, so this test
             * was flaky, sometimes erroneously executing the Test2Interface.foo().
             *
             * The method lookup has been changed and we now consistently find the
             * Test2Base.foo()V over the method from the interface, in line with the RI.
             *
             * Similarly to Test1, we need to use src/, src2/ and src3/. Files:
             *   src/Test2User2.java         - calls invoke-static Test2Derived.foo()
             *   src/Test2Derived.java       - defines public static foo()V, not used in the test.
             *   src2/Test2User.java         - calls invoke-virtual Test2Derived.foo()
             *   src2/Test2Base.java         - does not define foo()V, not used in the test.
             *   src2/Test2Interface.java    - defines default foo()V.
             *   src2/Test2Derived.java      - extends Test2Derived, implements Test2Interface.
             *   src3/Test2Base.java         - defines public static foo()V.
             */
            invokeUserTest("Test2User");
            invokeUserTest("Test2User2");

            /*
             * Test3
             * -----
             * Tested functions:
             *     class Test3Base {
             *         public static void foo() { ... }
             *     }
             *     interface Test3Interface {
             *         default void foo() { ... }  // default: avoid subclassing Test2Derived.
             *     }
             *     class Test3Derived extends Test3Base implements Test3Interface { }
             * Tested invokes:
             *     invoke-virtual Test3Derived.foo()V   from Test2User  in second dex file
             *
             * This is a variation of Test2 where we do not test invoke-static and test the
             * invoke-interface from the second dex file to avoid the effects of the DexCache.
             *
             * Previously the invoke-virtual would resolve to the Test3Interface.foo()V but
             * it now resolves to Test3Base.foo()V and throws ICCE in line with the RI.
             *
             * Files:
             *   src-multidex/Test3User.java - calls invoke-virtual Test3Derived.foo()
             *   src/Test3Base.java          - does not define foo()V, not used in the test.
             *   src/Test3Interface.java     - defines default foo()V.
             *   src/Test3Derived.java       - extends Test2Derived, implements Test2Interface.
             *   src2/Test3Base.java         - defines public static foo()V.
             */
            invokeUserTest("Test3User");

            /*
             * Test4
             * -----
             * Tested functions:
             *     interface Test4Interface {
             *         // Not declaring toString().
             *     }
             * Tested invokes:
             *     invoke-interface Test4Interface.toString()Ljava/lang/String; in first dex file
             *
             * The RI resolves the call to java.lang.Object.toString() and executes it.
             * ART used to resolve it in a secondary resolution attempt only to distinguish
             * between ICCE and NSME and then throw ICCE. We now allow the call to proceed.
             *
             * We need to compile Test4User with Test4Interface declaring toString()
             * but execute the test with Test4Interface that does not declare it. Files:
             *   src/Test4User.java          - calls invoke-interface Test4Interface.toString().
             *   src/Test4Interface.java     - declares toString(), not used in the test.
             *   src/Test4Derived.java       - extends Test4Interface.
             *   src2/Test4Interface.java    - does not declare toString().
             */
            invokeUserTest("Test4User");

            /*
             * Test5
             * -----
             * Tested functions:
             *     interface Test5Interface {
             *         public void foo();
             *     }
             *     abstract class Test5Base implements Test5Interface{
             *         // Not declaring foo().
             *     }
             *     class Test5Derived extends Test5Base {
             *         public void foo() { ... }
             *     }
             * Tested invokes:
             *     invoke-virtual   Test5Derived.foo()V from Test5User  in first dex file
             *     invoke-interface Test5Derived.foo()V from Test5User2 in first dex file
             *
             * We previously didn't check the type of the referencing class when the method
             * was found in the dex cache and the invoke-interface would only check the
             * type of the resolved method which happens to be OK; then we would fail a
             * DCHECK(!method->IsCopied()) in Class::FindVirtualMethodForInterface(). This has
             * been fixed and we consistently check the type of the referencing class as well.
             *
             * Since normal virtual method dispatch in compiled or quickened code does not
             * actually use the DexCache and we want to populate the Test5Base.foo()V entry
             * anyway, we force verification at runtime by adding a call to an arbitrary
             * unresolved method to Test5User.test(), catching and ignoring the ICCE. Files:
             *   src/Test5User2.java         - calls invoke-interface Test5Base.foo()V.
             *   src/Test5Base.java          - interface, declares foo()V, not used in the test.
             *   src/Test5Derived.java       - implements Test5Base, foo()V, not used in the test.
             *   src2/Test5User.java         - calls invoke-virtual Test5Base.foo()V,
             *                               - calls undefined Test5Base.bar()V, supresses ICCE.
             *   src2/Test5Base.java         - class, defines foo()V, bar()V, not used in the test.
             *   src2/Test5Derived.java      - extends Test5Base, not used in the test.
             *   src3/Test5Interface.java    - interface, declares foo()V.
             *   src3/Test5Base.java         - abstract class, implements Test5Interface.
             *   src3/Test5Derived.java      - extends Test5Base, implements foo()V.
             */
            invokeUserTest("Test5User");
            invokeUserTest("Test5User2");

            /*
             * Test6
             * -----
             * Tested functions:
             *     interface Test6Interface {
             *         // Not declaring toString().
             *     }
             * Tested invokes:
             *     invoke-interface Test6Interface.toString() from Test6User in first dex file
             *     invoke-virtual   Test6Interface.toString() from Test6User in first dex file
             *
             * Previously, the invoke-interface would have been rejected, throwing ICCE,
             * and the invoke-virtual would have been accepted, calling Object.toString().
             *
             * The method lookup has been changed and we now accept the invoke-interface,
             * calling Object.toString(), and reject the invoke-virtual, throwing ICCE,
             * in line with the RI. However, if the method is already in the DexCache for
             * the invoke-virtual, we need to check the referenced class in order to throw
             * the ICCE as the resolved method kind actually matches the invoke-virtual.
             * This test ensures that we do.
             *
             * The invoke-virtual needs to be compiled with Test6Interface defined as a class,
             * the invoke-interface needs to be compiled with Test6Interface defined as an
             * interface that declares toString(), and the final test needs to run with the
             * Test6Interface defined as an interface that does not declare toString(). Files:
             *   src/Test6User2.java         - calls invoke-virtual Test6Interface.toString().
             *   src/Test6Interface.java     - class, defines toString(), not used in the test.
             *   src/Test6Derived.java       - extends Test6Interface, not used in the test.
             *   src2/Test6User.java         - calls invoke-interface Test6Interface.toString().
             *   src2/Test6Interface.java    - interface, declares toString(), not used in the test.
             *   src2/Test6Derived.java      - implements Test6Interface.
             *   src3/Test6Interface.java    - interface, does not declare toString().
             */
            invokeUserTest("Test6User");
            invokeUserTest("Test6User2");

            /*
             * Test7
             * -----
             * Tested function:
             *     class Test7Base {
             *         private void foo() { ... }
             *     }
             *     interface Test7Interface {
             *         default void foo() { ... }
             *     }
             *     class Test7Derived extends Test7Base implements Test7Interface {
             *         // Not declaring foo().
             *     }
             * Tested invokes:
             *     invoke-virtual Test7Derived.foo()V   from Test7User    in first dex file
             *
             * This test shows deliberate divergence from JVMS where we ignore private
             * methods in superclasses and find a method inherited from an interface.
             * Results for --jvm are faked.
             *
             * Files:
             *   src/Test7User.java          - calls invoke-virtual Test7Derived.foo()V.
             *   src/Test7Base.java          - defines private foo()V.
             *   src/Test7Interface.java     - defines default foo()V.
             *   src/Test7Derived.java       - extends Test7Base, implements Test7Interface.
             */
            if (usingRI) {
                // For RI, just print the expected output to hide the deliberate divergence.
                System.out.println("Calling Test7User.test():\nTest7Interface.foo()");
            } else {
                invokeUserTest("Test7User");
            }

            // TODO: How to test that interface method resolution returns the unique
            // maximally-specific non-abstract superinterface method if there is one?
            // Maybe reflection? (This is not even implemented yet!)
        } catch (Throwable t) {
            t.printStackTrace(System.out);
        }
    }

    private static void invokeUserTest(String userName) throws Exception {
        System.out.println("Calling " + userName + ".test():");
        try {
            Class<?> user = Class.forName(userName);
            Method utest = user.getDeclaredMethod("test");
            utest.invoke(null);
        } catch (Throwable t) {
            System.out.println("Caught " + t.getClass().getName());
            for (Throwable c = t.getCause(); c != null; c = c.getCause()) {
                System.out.println("  caused by " + c.getClass().getName());
            }
        }
    }
}
