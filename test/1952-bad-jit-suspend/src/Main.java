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

public class Main {

  public static class TargetClass {
    public void foo() {}
  }

  public static void doNothing() {}
  public static void main(String[] args) throws Exception {
    Thread wait_thread = new Thread("Wait thread");
    Thread redefine_thread = new Thread("Redefine Thread");
    StartWaitThread(wait_thread);
    StartRedefineThread(redefine_thread);
    // Force initialize.
    TargetClass.class.toString();
    while (shouldContinue()) {
      doNothing();
    }
    wait_thread.join();
    redefine_thread.join();
  }

  public static native boolean shouldContinue();
  public static native void StartWaitThread(Thread thr);
  public static native void StartRedefineThread(Thread thr);
}
