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

package com.android.amm;

import java.lang.ref.FinalizerReference;
import java.lang.ref.Reference;
import java.lang.reflect.Field;
import java.util.ArrayList;
import sun.misc.Cleaner;

/**
 * Provides a method to iterate over heap instances without the VMDebug API.
 * For older versions of the Android platform.
 */
class ReflectiveGetInstances {

  private static void add(ArrayList<ArrayList<Object>> objects,
                          Class[] classes,
                          boolean assignable,
                          Object inst) {
    if (inst == null) {
      return;
    }

    for (int i = 0; i < classes.length; ++i) {
      if (assignable) {
        if (classes[i].isAssignableFrom(inst.getClass())) {
          objects.get(i).add(inst);
        }
      } else {
        if (classes[i].equals(inst.getClass())) {
          objects.get(i).add(inst);
        }
      }
    }
  }

  /**
   * Get instances of classes on the Java heap.
   * As if by VMDebug.getInstancesOfClasses, except with the limitation that
   * only instances with finalizers or Cleaners are considered due to the
   * limitations of what is supported by reflection.
   */
  public static Object[][] getInstancesOfClasses(Class[] classes, boolean assignable) {
    try {
      ArrayList<ArrayList<Object>> objects = new ArrayList<ArrayList<Object>>();
      for (int i = 0; i < classes.length; ++i) {
        objects.add(new ArrayList<Object>());
      }

      Field fReference_referent = Reference.class.getDeclaredField("referent");
      fReference_referent.setAccessible(true);

      // Iterate over finalizable objects.
      Field fFinalizerReference_head = FinalizerReference.class.getDeclaredField("head");
      fFinalizerReference_head.setAccessible(true);

      Field fFinalizerReference_next = FinalizerReference.class.getDeclaredField("next");
      fFinalizerReference_next.setAccessible(true);

      // TODO: synchronize on the LIST_LOCK when doing this traversal.
      Object curr = fFinalizerReference_head.get(null);
      while (curr != null) {
        Object inst = fReference_referent.get(curr);
        add(objects, classes, assignable, inst);
        curr = fFinalizerReference_next.get(curr);
      }

      // Iterate over cleaner objects.
      Field fCleaner_first = Cleaner.class.getDeclaredField("first");
      fCleaner_first.setAccessible(true);

      Field fCleaner_next = Cleaner.class.getDeclaredField("next");
      fCleaner_next.setAccessible(true);

      // TODO: synchronize on the Cleaner class when doing this traversal.
      curr = fCleaner_first.get(null);
      while (curr != null) {
        Object inst = fReference_referent.get(curr);
        add(objects, classes, assignable, inst);
        curr = fCleaner_next.get(curr);
      }

      Object[][] result = new Object[classes.length][];
      for (int i = 0; i < classes.length; ++i) {
        result[i] = objects.get(i).toArray();
      }
      return result;
    } catch (NoSuchFieldException e) {
      throw new AssertionError(e);
    } catch (IllegalAccessException e) {
      throw new AssertionError(e);
    }
  }
}
