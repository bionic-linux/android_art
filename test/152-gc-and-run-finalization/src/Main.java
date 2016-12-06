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

import java.util.concurrent.atomic.AtomicInteger;

import dalvik.system.VMRuntime;

public class Main {

    static volatile boolean done = false;

    public static void main(String[] args) throws Exception {
        // Try to call System.runFinalization while GC is running.
        Thread gcThread = new GcThread();
        Thread[] finalizationThreads = new FinalizationThread[100];
        for (int i = 0; i < finalizationThreads.length; ++i) {
            finalizationThreads[i] = new FinalizationThread();
        }
        gcThread.start();
        for (int i = 0; i < finalizationThreads.length; ++i) {
            finalizationThreads[i].start();
        }
        gcThread.join();
        for (int i = 0; i < finalizationThreads.length; ++i) {
            finalizationThreads[i].join();
        }
        System.out.println("PASS");
    }

    static class GcThread extends Thread {
        public void run() {
            for (int i = 0; i < 1000; ++i) {
                Runtime.getRuntime().gc();
            }
            done = true;
        }
    }

    static class FinalizationThread extends Thread {
        public void run() {
            while (!done) {
                new Finalizable();
                VMRuntime.runFinalization(250 * 1000 * 1000);  // 250 ms
            }
        }
    }

    static class Finalizable {
        protected void finalize() {
            finalizable_counter.decrementAndGet();
        }
    }
}
