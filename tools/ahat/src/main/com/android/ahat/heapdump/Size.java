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

package com.android.ahat.heapdump;

/**
 * Used to represent how much space an instance takes up.
 * An abstraction is introduced rather than using a long directly in order to
 * more easily keep track of the different components of the size. For
 * example, some instances may have associated native, code, or graphics
 * sizes.
 * <p>
 * Size objects are immutable.
 */
public class Size {
  private final long mJavaSize;
  private final long mModeledExternalSize;

  /**
   * An instance of Size with 0 for all categories.
   */
  public static Size ZERO = new Size(0, 0);

  /**
   * Constructs a new instance of Size.
   *
   * @param javaSize number of bytes in the java category
   * @param modeledExternalSize number of bytes in the modeledExternalSize
   *        category
   */
  public Size(long javaSize, long modeledExternalSize) {
    mJavaSize = javaSize;
    mModeledExternalSize = modeledExternalSize;
  }

  /**
   * Returns the sum of the size of all categories.
   *
   * @return the total size
   */
  public long getSize() {
    return mJavaSize + mModeledExternalSize;
  }

  /**
   * Returns the size of the java category.
   *
   * @return the java category size
   */
  public long getJavaSize() {
    return mJavaSize;
  }

  /**
   * Returns the size of the modeled external category.
   *
   * @return the modeled external category size
   */
  public long getModeledExternalSize() {
    return mModeledExternalSize;
  }

  /**
   * Returns true if all categories of this size are zero.
   *
   * @return true if the size is zero
   */
  public boolean isZero() {
    return mJavaSize == 0 && mModeledExternalSize == 0;
  }

  /**
   * Returns a new Size object that is the sum of this size and the other.
   *
   * @param other the size to sum with this size
   * @return the new size object
   */
  public Size plus(Size other) {
    if (isZero()) {
      return other;
    } else if (other.isZero()) {
      return this;
    } else {
      return new Size(mJavaSize + other.mJavaSize,
          mModeledExternalSize + other.mModeledExternalSize);
    }
  }

  /**
   * Returns a new Size object that has <code>size</code> more modeled
   * external size than this Size object.
   *
   * @param size the size to add to the modeled external category
   * @return the new size object
   */
  public Size plusModeledExternalSize(long size) {
    return new Size(mJavaSize, mModeledExternalSize + size);
  }

  @Override public boolean equals(Object other) {
    if (other instanceof Size) {
      Size s = (Size)other;
      return mJavaSize == s.mJavaSize && mModeledExternalSize == s.mModeledExternalSize;
    }
    return false;
  }
}

