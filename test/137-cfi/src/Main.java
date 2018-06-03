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

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.InputStreamReader;
import java.util.Arrays;
import java.util.Comparator;

public class Main extends Base implements Comparator<Main> {
  // Whether to test local unwinding.
  private boolean testLocal;

  // Unwinding another process, modelling debuggerd.
  private boolean testRemote;

  public static void main(String[] args) throws Exception {
      // Call the test via base class to test multidex unwinding.
      new Main().runTest(args);
  }

  public void test(String[] args) {
      System.loadLibrary(args[0]);
      for (int i = 1; i < args.length; i++) {
          if (args[i].equals("--test-local")) {
              testLocal = true;
          } else if (args[i].equals("--test-remote")) {
              testRemote = true;
          } else {
              System.out.println("Unknown argument: " + args[i]);
          }
      }
      if (!testLocal && !testRemote) {
          System.out.println("No test selected.");
      }

      // Call unwind() via Arrays.binarySearch.
      // This tests that we can unwind through framework code.
      Main[] array = { this, this, this };
      Arrays.binarySearch(array, 0, 3, this /* value */, this /* comparator */);
  }

  public int compare(Main lhs, Main rhs) {
      unwind();
      // Returning "equal" ensures that we terminate search
      // after first item and thus call unwind() only once.
      return 0;
  }

  public void unwind() {
      if (testLocal) {
          String result = unwindInProcess() ? "PASSED" : "FAILED";
          System.out.println("Unwind in process: " + result);
      }

      if (testRemote) {
          // Fork so that we have other process to unwind. The child will block in the call.
          int pid = forkSecondary();
          String result = unwindOtherProcess(pid) ? "PASSED" : "FAILED";
          System.out.println("Unwind other process: " + result);
      }
  }

  // Native functions. Note: to avoid deduping, they must all have different signatures.
  public native int forkSecondary();
  public native boolean unwindInProcess();
  public native boolean unwindOtherProcess(int pid);
}
