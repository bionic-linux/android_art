/*
 * Copyright (C) 2019 The Android Open Source Project
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

import dalvik.annotation.optimization.DeadReferenceSafe;
import dalvik.annotation.optimization.ReachabilitySensitive;
import java.util.concurrent.atomic.AtomicInteger;

// A subset of DeadReferenceSafeTest, but without the annotation. It should behave
// differently.

public final class DeadReferenceUnsafeTest implements Runnable {
  AtomicInteger nFinalized = new AtomicInteger(0);
  boolean realTest = false;
  final int WARMUP_ITERS = 2000;
  final int INNER_ITERS = 10;
  static int n;

  class Finalizable {
    final int m = 1;
    protected void finalize() {
      nFinalized.incrementAndGet();
    }
    int doLittle() {
      n++;
      return m;
    }
  }

  private void loop() {
    Finalizable x = new Finalizable();
    for (int i = 0; i < INNER_ITERS; ++i) {
      if (i == 5 && realTest) {
        Runtime.getRuntime().gc();
        System.runFinalization();
        // With dead reference elimination, x should not be traced by the GC here.
        // Thus all 5 prior objects should have been found unreachable and finalized.
        int finalizedCount = nFinalized.get();
        System.out.println("Finalized " + finalizedCount + " objects");
        if (finalizedCount != 4) {
          System.out.println("Dead reference should be retained by default!");
        }
      }
      // Expected to be inlined and not to keep x live. But we shouldn't eliminate dead
      // references anyway.
      x.doLittle();
      x = new Finalizable();
    }
  }

  private void reset(int expected_count) {
    realTest = false;
    Runtime.getRuntime().gc();
    System.runFinalization();
    if (nFinalized.get() != expected_count) {
      System.out.println("DeadReferenceUnsafeTest: Wrong number of finalized objects:"
                         + nFinalized.get());
    }
    nFinalized.set(0);
  }

  public void run() {
    // Strongly encourage loop() etc. to be compiled.
    for (int i = 0; i < WARMUP_ITERS; ++i) {
      loop();
    }
    try {
      Thread.sleep(300);
    } catch(InterruptedException e) {
      throw new AssertionError("Interrupted");
    }
    realTest = true;

    reset(WARMUP_ITERS * (INNER_ITERS + 1));
    loop();

    if (n != (WARMUP_ITERS + 1) * (INNER_ITERS + 1)) {
      System.out.println("DeadReferenceUnsafeTest: Finalizable.n wrong: " + n);
    }
    reset(INNER_ITERS);
  }
}
