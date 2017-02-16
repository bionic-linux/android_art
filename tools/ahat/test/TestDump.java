/*
 * Copyright (C) 2015 The Android Open Source Project
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

package com.android.ahat;

import com.android.ahat.heapdump.AhatClassObj;
import com.android.ahat.heapdump.AhatInstance;
import com.android.ahat.heapdump.AhatSnapshot;
import com.android.ahat.heapdump.FieldValue;
import com.android.ahat.heapdump.Value;
import com.android.tools.perflib.heap.ProguardMap;
import java.io.File;
import java.io.IOException;
import java.text.ParseException;

/**
 * The TestDump class is used to get an AhatSnapshot for the test-dump
 * program.
 */
public class TestDump {
  // It can take on the order of a second to parse and process the test-dump
  // hprof. To avoid repeating this overhead for each test case, we cache the
  // loaded instance of TestDump and reuse it when possible. In theory the
  // test cases should not be able to modify the cached snapshot in a way that
  // is visible to other test cases.
  private static TestDump mCachedTestDump = null;

  private AhatSnapshot mSnapshot = null;

  /**
   * Load the test-dump.hprof file.
   * The location of the file is read from the system property
   * "ahat.test.dump.hprof", which is expected to be set on the command line.
   * For example:
   *   java -Dahat.test.dump.hprof=test-dump.hprof -jar ahat-tests.jar
   *
   * An IOException is thrown if there is a failure reading the hprof file or
   * the proguard map.
   */
  private TestDump() throws IOException {
      String hprof = System.getProperty("ahat.test.dump.hprof");

      String mapfile = System.getProperty("ahat.test.dump.map");
      ProguardMap map = new ProguardMap();
      try {
        map.readFromFile(new File(mapfile));
      } catch (ParseException e) {
        throw new IOException("Unable to load proguard map", e);
      }

      mSnapshot = AhatSnapshot.fromHprof(new File(hprof), map);
  }

  /**
   * Get the AhatSnapshot for the test dump program.
   */
  public AhatSnapshot getAhatSnapshot() {
    return mSnapshot;
  }

  /**
   * Returns the value of a field in the DumpedStuff instance in the
   * snapshot for the test-dump program.
   */
  public Value getDumpedValue(String name) {
    AhatClassObj main = mSnapshot.findClass("Main");
    AhatInstance stuff = null;
    for (FieldValue fields : main.getStaticFieldValues()) {
      if ("stuff".equals(fields.getName())) {
        stuff = fields.getValue().asAhatInstance();
      }
    }
    return stuff.getField(name);
  }

  /**
   * Returns the value of a non-primitive field in the DumpedStuff instance in
   * the snapshot for the test-dump program.
   */
  public AhatInstance getDumpedAhatInstance(String name) {
    Value value = getDumpedValue(name);
    return value == null ? null : value.asAhatInstance();
  }

  /**
   * Get the test dump.
   * An IOException is thrown if there is an error reading the test dump hprof
   * file.
   * To improve performance, this returns a cached instance of the TestDump
   * when possible.
   */
  public static synchronized TestDump getTestDump() throws IOException {
    if (mCachedTestDump == null) {
      mCachedTestDump = new TestDump();
    }
    return mCachedTestDump;
  }
}
