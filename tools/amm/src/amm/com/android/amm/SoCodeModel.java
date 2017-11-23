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

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.util.Map;
import java.util.HashMap;

public class SoCodeModel extends Model {
  public static class Entry {
    public String name;
    public long size;

    public Entry(String name, long size) {
      this.name = name;
      this.size = size;
    }
  }

  public static class Instance extends ModelInstance {
    public final Entry[] entries;

    public Instance(Entry[] entries, long size) {
      super(Object.class.getClassLoader(), size);
      this.entries = entries;
    }

    @Override public String toString() {
      StringBuilder sb = new StringBuilder();
      for (Entry entry : entries) {
        sb.append("  ");
        sb.append(entry.name);
        sb.append(": ");
        sb.append(entry.size);
        sb.append('\n');
      }
      return sb.toString();
    }
  }

  public SoCodeModel() {
    super("SoCode");
  }

  @Override public ModelSnapshot sample() {
    // Add up the virtual size of memory mappings for .so files.
    // TODO: What about .so files embedded in apks?
    try {
      Map<String, Long> sizes = new HashMap<String, Long>();
      BufferedReader reader = new BufferedReader(new FileReader("/proc/self/maps"));
      String line;
      String lastname = null;
      while ((line = reader.readLine()) != null) {
        if (line.endsWith(".so")) {
          int slash = line.indexOf('/');
          String name = line.substring(slash);

          int dash = line.indexOf('-');
          int space = line.indexOf(' ', dash);
          String start = line.substring(0, dash);
          String end = line.substring(dash+1, space);
          long size = Long.parseUnsignedLong(end, 16) - Long.parseUnsignedLong(start, 16);
          Long old = sizes.get(name);
          sizes.put(name, old == null ? size : old + size);

          lastname = name;
        } else if (lastname != null && line.endsWith("[anon:.bss]")) {
          // The .bss section immediately following a .so file should be
          // counted against that .so file.
          int dash = line.indexOf('-');
          int space = line.indexOf(' ', dash);
          String start = line.substring(0, dash);
          String end = line.substring(dash+1, space);
          long size = Long.parseUnsignedLong(end, 16) - Long.parseUnsignedLong(start, 16);
          Long old = sizes.get(lastname);
          sizes.put(lastname, old == null ? size : old + size);
        } else {
          lastname = null;
        }
      }

      long total = 0;
      Entry[] entries = new Entry[sizes.size()];
      int i = 0;
      for (Map.Entry<String, Long> entry : sizes.entrySet()) {
        entries[i++] = new Entry(entry.getKey(), entry.getValue());
        total += entry.getValue();
      }

      ModelSnapshot snapshot = new ModelSnapshot();
      snapshot.model = this;
      snapshot.total = total;
      snapshot.instances = new ModelInstance[] { new Instance(entries, total) };
      return snapshot;
    } catch (IOException e) {
      e.printStackTrace();
      ModelSnapshot snapshot = new ModelSnapshot();
      snapshot.model = this;
      snapshot.total = 0;
      snapshot.instances = new ModelInstance[0];
      return snapshot;
    }
  }
}
