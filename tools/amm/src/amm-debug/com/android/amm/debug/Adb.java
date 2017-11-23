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

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintStream;
import java.net.Socket;

class Adb {
  private PrintStream os;
  private BufferedReader is;
  private String host = "host";

  private Adb(PrintStream out, BufferedReader in, String serial) {
    os = out;
    is = in;
    if (serial != null) {
      host = "host-serial:" + serial;
    }
  }

  // Establish a connection with the adb server for the device identified by
  // serial. Serial may be null to connect to any device.
  public static Adb connect(String serial) throws AdbException {
    try {
      Socket socket = new Socket("localhost", 5037);
      PrintStream out = new PrintStream(socket.getOutputStream(), true);
      BufferedReader in = new BufferedReader(new InputStreamReader(
            socket.getInputStream()));
      return new Adb(out, in, serial);
    } catch (Exception e) {
      throw new AdbException("Unable to connect to adb", e);
    }
  }

  // Read a string of n characters from the input.
  private String read(int n) throws AdbException {
    String str = "";
    for (int i = 0; i < n; i++) {
      try {
        int c = is.read();
        if (c == -1) {
          throw new AdbException("Incomplete Read");
        }
        str += (char)c;
      } catch (IOException e) {
        throw new AdbException("Exception when reading", e);
      }
    }
    return str;
  }

  // Send the given message over adb.
  // Throws an exception on error.
  private void send(String msg) throws AdbException {
    os.print(String.format("%04x%s", msg.length(), msg));
    String result = read(4);
    if (result.equals("OKAY")) {
      return;
    } else if (result.equals("FAIL")) {
      int len = Integer.parseInt(read(4), 16);
      String err = read(len);
      throw new AdbException(err);
    } else {
      throw new AdbException("Received unexpected result: " + result);
    }
  }

  // Set up forwarding from the local host TCP port <port> to the JDWP thread
  // on VM process <pid> on the device.
  public void forwardJdwp(int port, int pid) throws AdbException {
    send(host + ":forward:tcp:" + port + ";jdwp:" + pid);
  }

  // Remove the port forwarding set up for the given local host TCP port.
  public void killForward(int port) throws AdbException {
    send(host + ":killforward:tcp:" + port);
  }

  public static class AdbException extends Exception {
    public AdbException(String msg) {
      super(msg);
    }

    public AdbException(String msg, Exception cause) {
      super(msg, cause);
    }
  }
}
