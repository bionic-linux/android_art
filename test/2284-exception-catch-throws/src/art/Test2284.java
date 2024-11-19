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

package art;

import java.lang.reflect.Executable;
import java.lang.reflect.Method;

public class Test2284 {
  static int numExceptions = 0;
  static int numCaught = 0;

  public static class TestException extends Error {
    public TestException(String s) { super(s); }
    public TestException() { super("test exception"); }
  }

  public static void ExceptionEvent(Thread thr,
                                    Executable throw_method,
                                    long throw_location,
                                    Throwable exception,
                                    Executable catch_method,
                                    long catch_location) {
    System.out.println(thr.getName() + ": " + throw_method + " @ line = " +
        Breakpoint.locationToLine(throw_method, throw_location) + " throws " +
        exception.getClass() + ": " + exception.getMessage());
    String catch_message;
    if (catch_method == null) {
      catch_message = "<UNKNOWN>";
    } else {
      catch_message = catch_method.toString() + " @ line = " +
          Breakpoint.locationToLine(catch_method, catch_location);
    }
    if (numExceptions == 1) {
      System.out.println("Shouldn't receive exceptions thrown by catch handlers but received one");
    }
    numExceptions++;
  }

  public static void notifyMethodExit(Executable m, boolean exception, Object result) {
    if (m.getDeclaringClass() == Test2284.class) {
      if (m.getName() == "run") {
        Trace.disableTracing(Thread.currentThread());
      }
      System.out.println("MethodExit " + exception + " " + m.toString());
    }
  }

  public static void enableMethodExit() throws Exception {
    // Enable method exit
    Exceptions.disableExceptionTracing(Thread.currentThread());
    Trace.enableMethodTracing(Test2284.class, null,
        Test2284.class.getDeclaredMethod(
            "notifyMethodExit", Executable.class, Boolean.TYPE, Object.class), Thread.currentThread());
  }

  public static void testExceptionCatchHandlerThrows() {
    throw new TestException();
  }

  public static void testMethod() throws Exception {
    try {
      try {
        testExceptionCatchHandlerThrows();
      } catch (TestException e) {
        System.out.println("Exception caught");
      }
    } catch (TestException e) {
      System.out.println("Exception caught again");
    }
  }

  public static void run() throws Exception {
    // Make sure classes are loaded first.
    System.out.println(TestException.class.toString());
    Exceptions.setupExceptionTracing(
        Test2284.class,
        TestException.class,
        Test2284.class.getDeclaredMethod(
            "ExceptionEvent",
            Thread.class,
            Executable.class,
            Long.TYPE,
            Throwable.class,
            Executable.class,
            Long.TYPE),
        Test2284.class.getDeclaredMethod(
            "exceptionCatchHandlerNative",
            Thread.class,
            Executable.class,
            Long.TYPE,
            Throwable.class));
    Exceptions.enableExceptionEvent(Thread.currentThread());
    Exceptions.enableExceptionCatchEvent(Thread.currentThread());

    try {
      testMethod();
    } catch (Error e) {
      System.out.println("Caught Error");
    }
  }

  public static native void exceptionCatchHandlerNative(Thread thr, Executable catch_method,
                                         long catch_location,
                                         Throwable exception);

}
