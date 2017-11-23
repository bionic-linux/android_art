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

package com.android.amm.debug;

import com.android.amm.debug.jdwp.Jdwp;
import java.io.ByteArrayOutputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintStream;

public class Main {
  private static final int HOST_DEBUG_PORT = 21352;

  private static void Help(PrintStream out) {
    out.println("amm [-s] [-v] [--dump FILE] <pid>");
    out.println("");
    out.println("OPTIONS:");
    out.println("   -s");
    out.println("      Only show summary information.");
    out.println("   -v");
    out.println("      Show verbose logging.");
    out.println("   --dump");
    out.println("      Dump a heap to FILE on the host machine.");
    out.println("   <pid>");
    out.println("      The pid of the process to get the metric for.");
  }

  public static void main(String[] args) throws Exception {
    boolean detailed = true;
    int pid = -1; 
    String hprof = null;
    boolean dumpArg = false;
    boolean verbose = false;
    for (String arg : args) {
      if (arg.equals("--help")) {
        Help(System.out);
        return;
      } else if (dumpArg) {
        hprof = arg;
        dumpArg = false;
      } else if (arg.equals("-s")) {
        detailed = false;
      } else if (arg.equals("-v")) {
        verbose = true;
      } else if (arg.equals("--dump")) {
        dumpArg = true;
      } else if (pid == -1) {
        pid = Integer.parseInt(arg);
      } else {
        System.err.println("too many arguments");
        Help(System.err);
        return;
      }
    }
    if (dumpArg) {
      System.err.println("no --dump FILE provided");
      System.err.println("");
      Help(System.err);
      return;
    }

    if (pid == -1) {
      System.err.println("no pid provided");
      System.err.println("");
      Help(System.err);
      return;
    }

    // Set up port forwarding to the specified pid.
    debug(verbose, "establishing adb connection...");
    Adb adb = Adb.connect(System.getenv("ANDROID_SERIAL"));
    adb.forwardJdwp(HOST_DEBUG_PORT, pid);

    debug(verbose, "establishing jdwp connection...");
    Jdwp jdwp = Jdwp.connect(HOST_DEBUG_PORT);

    debug(verbose, "suspending the target app...");
    long thread = jdwp.suspendForExecution();

    long ammType = jdwp.classBySignature("Lcom/android/amm/ActionableMemoryMetric;");
    long snapshotType = jdwp.classBySignature("Lcom/android/amm/Snapshot;");
    if (ammType == 0 || snapshotType == 0) {
      debug(verbose, "loading amm classes...");
      // We need to load the amm classes.
      long classLoaderType = jdwp.classBySignature("Ljava/lang/ClassLoader;");
      long loadClassMethod = jdwp.methodByName(classLoaderType,
                                               "loadClass",
                                               "(Ljava/lang/String;)Ljava/lang/Class;");


      long systemClassLoaderMethod = jdwp.methodByName(classLoaderType,
                                                       "getSystemClassLoader",
                                                       "()Ljava/lang/ClassLoader;");
      long classLoaderId = jdwp.invokeStaticMethod(classLoaderType,
                                                   thread,
                                                   systemClassLoaderMethod);

      long inMemoryLoaderType = jdwp.classBySignature("Ldalvik/system/InMemoryDexClassLoader;");
      if (inMemoryLoaderType == 0) {
        // We need to load the InMemoryDexClassLoader class.
        long inMemoryStringId = jdwp.createString("dalvik.system.InMemoryDexClassLoader");
        jdwp.invokeMethod(classLoaderId,
                          thread,
                          classLoaderType,
                          loadClassMethod,
                          inMemoryStringId);
        inMemoryLoaderType = jdwp.classBySignature("Ldalvik/system/InMemoryDexClassLoader;");
        if (inMemoryLoaderType == 0) {
          System.err.println("ERROR: Failed to load InMemoryDexClassLoader class.");
          return;
        }
      }

      long mkInMemoryLoader = jdwp.methodByName(inMemoryLoaderType,
                                                "<init>",
                                                "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");

      long byteBufferType = jdwp.classBySignature("Ljava/nio/ByteBuffer;");
      long byteBufferWrapMethod = jdwp.methodByName(byteBufferType,
                                                    "wrap",
                                                    "([B)Ljava/nio/ByteBuffer;");

      long ammBytesId = jdwp.createByteArray(getAmmDexBytes());
      long ammByteBufferId = jdwp.invokeStaticMethod(byteBufferType,
                                                     thread,
                                                     byteBufferWrapMethod,
                                                     ammBytesId);

      long inMemoryLoader = jdwp.newInstance(inMemoryLoaderType,
                                             thread,
                                             mkInMemoryLoader,
                                             ammByteBufferId,
                                             classLoaderId);

      long ammClassNameStringId = jdwp.createString("com.android.amm.ActionableMemoryMetric");
      jdwp.invokeMethod(inMemoryLoader, thread, inMemoryLoaderType, loadClassMethod, ammClassNameStringId);

      long ammSnapshotClassNameStringId = jdwp.createString("com.android.amm.Snapshot");
      jdwp.invokeMethod(inMemoryLoader, thread, inMemoryLoaderType, loadClassMethod, ammSnapshotClassNameStringId);

      ammType = jdwp.classBySignature("Lcom/android/amm/ActionableMemoryMetric;");
      snapshotType = jdwp.classBySignature("Lcom/android/amm/Snapshot;");

      if (ammType == 0 || snapshotType == 0) {
        System.err.println("ERROR: Failed to load ActionableMemoryMetric classes.");
        return;
      }
    }
    debug(verbose, "amm classes loaded.");

    long getStandardMetricMethod = jdwp.methodByName(ammType, "getStandardMetric", "()Lcom/android/amm/ActionableMemoryMetric;");
    long sampleMethod = jdwp.methodByName(ammType, "sample", "()Lcom/android/amm/Snapshot;");
    long detailMethod = jdwp.methodByName(snapshotType,
                                          detailed ? "detail" : "summary",
                                          "()Ljava/lang/String;");

    debug(verbose, "sampling amm...");
    long standardMetric = jdwp.invokeStaticMethod(ammType, thread, getStandardMetricMethod);
    long snapshot = jdwp.invokeMethod(standardMetric, thread, ammType, sampleMethod);

    debug(verbose, "extracting snapshot detail...");
    long detail = jdwp.invokeMethod(snapshot, thread, snapshotType, detailMethod);
    System.out.println(jdwp.stringValue(detail));

    if (hprof != null) {
      System.out.println("Dumping heap to " + hprof + "...");
      jdwp.dumpHeap(new FileOutputStream(hprof));
      System.out.println("Heap dump completed.");
    }

    // Clean up the port forwarding.
    adb.killForward(HOST_DEBUG_PORT);
  }

  /**
   * Returns the raw bytes of the amm.dex file resource.
   */
  static private byte[] getAmmDexBytes() throws IOException {
    ClassLoader loader = Main.class.getClassLoader();
    InputStream is = loader.getResourceAsStream("amm.dex");
    ByteArrayOutputStream os = new ByteArrayOutputStream();
    int read;
    byte[] buf = new byte[4096];
    while ((read = is.read(buf)) >= 0) {
      os.write(buf, 0, read);
    }
    is.close();
    return os.toByteArray();
  }

  private static void debug(boolean verbose, String msg) {
    if (verbose) {
      System.err.println(msg);
    }
  }
}
