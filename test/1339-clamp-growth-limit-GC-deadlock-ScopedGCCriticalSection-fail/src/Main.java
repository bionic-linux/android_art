/*
 * Copyright (C) 2016 The Android Open Source Project
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

    public static void testClamp(){
        Thread mutatorThread = new Thread() {
            public void run() {
                mutatorHeld();
            }
        };
        mutatorThread.start();
        while(!mutatorThread.isAlive()) {
            try {
                Thread.sleep(1000);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        Thread gcThread = new Thread() {
            public void run() {
                gcRunCheckpoint();
            }
        };
        gcThread.start();

        try {
            Thread.sleep(10000);
        } catch (InterruptedException e) {
            e.printStackTrace();
        }

        Thread.State state = mutatorThread.getState();
        if (state != Thread.State.TERMINATED) {
            System.out.println("Test timed out, current state: " + state + "\n");
            System.exit(1);
        }
    }

    public static void main(String[] args) {
        System.loadLibrary(args[0]);
        testClamp();
  }

  public static native void mutatorHeld();
  public static native void gcRunCheckpoint();
}
