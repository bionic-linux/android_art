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

import dalvik.system.DexFile;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;

public class DexCodeModel extends Model {

  public static class Instance extends ModelInstance {
    public String name;

    public Instance(DexFile dexfile) {
      super(dexfile, dexFileSizeFromMaps(dexfile));
      name = dexfile.getName();
    }

    private static long dexFileSizeFromMaps(DexFile file) {
      try {
        String path = file.getName();
        if (path == null) {
          // TODO: What to do here?
          // TODO: Add a test for this case (InMemoryDexClassLoader?)
          return 0;
        }

        String name = new File(path).getName();
        String vdex = name.substring(0, name.lastIndexOf('.')) + ".vdex";
        BufferedReader reader = new BufferedReader(new FileReader("/proc/self/maps"));
        String line;
        while ((line = reader.readLine()) != null) {
          if (line.endsWith(vdex)) {
            int dash = line.indexOf('-');
            int space = line.indexOf(' ', dash);
            String start = line.substring(0, dash);
            String end = line.substring(dash+1, space);
            return Long.parseUnsignedLong(end, 16) - Long.parseUnsignedLong(start, 16);
          }
        }
        return 0;
      } catch (IOException e) {
        e.printStackTrace();
        return 0;
      }
    }

    private static long dexFileSize(DexFile file) {
      try {
        return file.getStaticSizeOfDexFile();
      } catch (IllegalStateException e) {
        // The dex file must have been closed.
        return 0;
      }
    }

    @Override public String toString() {
      return name;
    }
  }

  public DexCodeModel() {
    super("DexCode");
  }

  @Override public ModelSnapshot sample() {
    Object[][] insts = ReflectiveGetInstances.getInstancesOfClasses(
        new Class[]{ DexFile.class }, true);

    ModelSnapshot snapshot = new ModelSnapshot();
    snapshot.total = 0;
    snapshot.model = this;
    snapshot.instances = new ModelInstance[insts[0].length];
    // TODO: Read the maps file once for all instances, not repeatedly for
    // each one.
    for (int i = 0; i < snapshot.instances.length; ++i) {
      snapshot.instances[i] = new Instance((DexFile)insts[0][i]);
      snapshot.total += snapshot.instances[i].size;
    }
    return snapshot;
  }
}
