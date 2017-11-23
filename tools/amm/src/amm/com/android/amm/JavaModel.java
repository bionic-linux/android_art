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

public class JavaModel extends Model {

  public static class Instance extends ModelInstance {
    public Instance(long size) {
      // Set object to null to avoid double counting the Java memory in the
      // heap dump.
      super(null, size);
    }

    @Override public String toString() {
      return "Java";
    }
  }

  public JavaModel() {
    super("Java");
  }

  @Override public ModelSnapshot sample() {
    Runtime runtime = Runtime.getRuntime();
    long size = runtime.totalMemory() - runtime.freeMemory();
    ModelSnapshot snapshot = new ModelSnapshot();
    snapshot.total = size;
    snapshot.model = this;
    snapshot.instances = new Instance[] { new Instance(size) };
    return snapshot;
  }
}
