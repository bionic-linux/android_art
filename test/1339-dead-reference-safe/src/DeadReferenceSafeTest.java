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
import java.lang.ref.Reference;
import java.util.concurrent.atomic.AtomicInteger;

@DeadReferenceSafe
public final class DeadReferenceSafeTest implements Runnable {
  AtomicInteger nFinalized = new AtomicInteger(0);
  boolean realTest = false;
  final int WARMUP_ITERS = 2000;
  final int INNER_ITERS = 10;
  static int n;

  // ??? FIXME: This shouldn't be needed. We should proagate @DeadReferenceSafe to inner classes.
  @DeadReferenceSafe
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

  @DeadReferenceSafe
  class SensitiveFinalizable {
    @ReachabilitySensitive
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
        System.out.println("Loop finalized " + finalizedCount + " objects");
        if (finalizedCount != 5) {
          System.out.println("Dead reference erroneously retained!");
        }
      }
      // Expected to be inlined and not to keep x live.
      x.doLittle();
      x = new Finalizable();
    }
  }

  private void sensitiveLoop() {
    SensitiveFinalizable x = new SensitiveFinalizable();
    for (int i = 0; i < INNER_ITERS; ++i) {
      if (i == 5 && realTest) {
        Runtime.getRuntime().gc();
        System.runFinalization();
        // With dead reference elimination, x should not be traced by the GC here.
        // Thus all 5 prior objects should have been found unreachable and finalized.
        int finalizedCount = nFinalized.get();
        System.out.println("SensitiveLoop finalized " + finalizedCount + " objects");
        if (finalizedCount != 4) {
          System.out.println("ReachabilitySensitive access ignored!");
        }
      }
      // Expected to be inlined. Should keep x live, since it accesses ReachabilitySensitive field.
      x.doLittle();
      x = new SensitiveFinalizable();
    }
  }

  private void fencedLoop() {
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
          System.out.println("ReachabilityFence ignored!");
        }
      }
      // Expected to be inlined and not to keep x live.
      x.doLittle();
      // But this should.
      Reference.reachabilityFence(x);
      x = new Finalizable();
    }
  }

  private void reset(int expected_count) {
    realTest = false;
    Runtime.getRuntime().gc();
    System.runFinalization();
    if (nFinalized.get() != expected_count) {
      System.out.println("Wrong number of finalized objects:" + nFinalized.get());
    }
    nFinalized.set(0);
  }

  private void mySleep(long n) {
    try {
      Thread.sleep(n);
    } catch(InterruptedException e) {
      throw new AssertionError("Interrupted");
    }
  }

  public void run() {
    // Strongly encourage loop() etc. to be compiled.
    for (int i = 0; i < WARMUP_ITERS; ++i) {
      loop();
    }
    mySleep(50);
    for (int i = 0; i < WARMUP_ITERS; ++i) {
      sensitiveLoop();
    }
    mySleep(50);
    for (int i = 0; i < WARMUP_ITERS; ++i) {
      fencedLoop();
    }
    mySleep(300);
    realTest = true;

    reset(3 * WARMUP_ITERS * (INNER_ITERS + 1));
    loop();

    reset(INNER_ITERS + 1);
    sensitiveLoop();

    reset(INNER_ITERS + 1);
    fencedLoop();

    if (n != 3 * (WARMUP_ITERS + 1) * (INNER_ITERS + 1)) {
      System.out.println("doLittle() execution count is wrong: " + n);
    }
    reset(INNER_ITERS);
  }
}
