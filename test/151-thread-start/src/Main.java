
import java.util.concurrent.locks.ReentrantLock;
import java.util.logging.Level;
import java.util.logging.Logger;

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

/**
 * Test thread start.
 */
public class Main {
    public static void main(String[] args) throws Exception {
        testThreadStart();
        testThreadDoubleStart();
    }

    private static void testThreadStart() {
        Thread thread = new Thread() {
            public void run() {
                System.out.println("Thread started");
            }
        };
        thread.start();
        try {
            thread.join();
        } catch (InterruptedException e) {
            System.out.println("Error: " + e.getMessage());
        }
    }

    private static void testThreadDoubleStart() {
        final ReentrantLock lock = new ReentrantLock();
        Thread thread = new Thread() {
            public void run() {
                lock.lock();
            }
        };
        lock.lock();
        try {
            thread.start();
            try {
                thread.start();
                System.out.println("Thread started again");
            } catch (IllegalThreadStateException e) {
                System.out.println("Second start failed");
            }
        } finally {
            lock.unlock();
        }
        try {
            thread.join();
        } catch (InterruptedException e) {
            System.out.println("Error: " + e.getMessage());
        }
    }
}
