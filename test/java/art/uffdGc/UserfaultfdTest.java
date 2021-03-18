/*
 * Copyright (C) 2021 The Android Open Source Project
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
 * limitations under the License
 */

package art.uffdGc;

import static org.junit.Assert.assertEquals;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public final class UserfaultfdTest {

  static {
      System.loadLibrary("userfaultfdtest");
  }

  private native void setUpUserfaultfd();
  private native int performKernelSpaceUffd();
  private native int performUffdWithoutUserModeOnly();
  private native int performMremapDontUnmap();
  private native int performMinorUffd();

  @Before
  public void setUp() {
      setUpUserfaultfd();
  }

  @Test
  public void kernelSpaceUserfault() {
    assertEquals(performKernelSpaceUffd(), 0);
  }

  @Test
  public void nonUserModeOnlyUserfaultfd() {
    assertEquals(performUffdWithoutUserModeOnly(), 0);
  }

  @Test
  public void mremapDontUnmap() {
    assertEquals(performMremapDontUnmap(), 0);
  }

  @Test
  public void minorUserfaultfd() {
    assertEquals(performMinorUffd(), 0);
  }
}
