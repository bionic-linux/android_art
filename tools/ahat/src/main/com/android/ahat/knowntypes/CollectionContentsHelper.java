/*
 * Copyright (C) 2023 The Android Open Source Project
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

package com.android.ahat.knowntypes;

import com.android.ahat.DocString;
import com.android.ahat.Summarizer;
import com.android.ahat.heapdump.Diffable;
import com.android.ahat.heapdump.Value;

import java.util.Objects;

public class CollectionContentsHelper {
  private static long sizeLong(Value value) {
    if (value == null) {
      return 0;
    }
    if (!value.isAhatInstance()) {
      return Value.getType(value).size(0); // refSize doesn't matter for non-objects.
    }
    return value.asAhatInstance().getSize().getSize();
  }

  private static long retainedSizeLong(Value value) {
    if (value == null) {
      return 0;
    }
    if (!value.isAhatInstance()) {
      return 0;
    }
    return value.asAhatInstance().getTotalRetainedSize().getSize();
  }

  /** A DocString expressing the direct size of the given value. */
  public static DocString size(Value value) {
    return DocString.size(sizeLong(value), isPlaceholder(value));
  }

  /**
   * A DocString expressing the total retained size of the given value, or zero if it's a primitive
   * value that doesn't retain anything.
   */
  public static DocString retainedSize(Value value) {
    return DocString.size(retainedSizeLong(value), isPlaceholder(value));
  }

  /**
   * A DocString expressing the difference in direct size between the given current and previous
   * values.
   */
  public static DocString sizeDelta(Value now, Value was) {
    return DocString.delta(isPlaceholder(now), isPlaceholder(was), sizeLong(now), sizeLong(was));
  }

  /**
   * A DocString expressing the difference in total retained size between the given current and
   * previous values.
   */
  public static DocString retainedDelta(Value now, Value was) {
    return DocString.delta(
        isPlaceholder(now), isPlaceholder(was), retainedSizeLong(now), retainedSizeLong(was));
  }

  /** Whether the given Value is a placeholder or not. */
  public static boolean isPlaceholder(Value value) {
    if (value == null) {
      return false;
    }
    return isPlaceholder(value.asAhatInstance());
  }

  /** Whether the given Diffable is a placeholder or not. */
  public static boolean isPlaceholder(Diffable<?> instance) {
    if (instance == null) {
      // This diffable represents a field/value/etc. that was actually null in the dump
      return false;
    }
    return instance.isPlaceHolder();
  }
}
