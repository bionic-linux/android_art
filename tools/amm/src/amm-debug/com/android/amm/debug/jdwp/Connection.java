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

package com.android.amm.debug.jdwp;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

/**
 * Jdwp stream for sending and receiving packets.
 */
class Connection {
  private DataOutputStream os;
  private DataInputStream is;

  public Connection(int port) throws IOException {
    try {
      Socket socket = new Socket("localhost", port);
      os = new DataOutputStream(socket.getOutputStream());
      is = new DataInputStream(socket.getInputStream());
    } catch (Exception e) {
      throw new JdwpException("Unable to connect to jdwp", e);
    }

    os.writeBytes("JDWP-Handshake");

    byte[] bytes = new byte[14];
    is.readFully(bytes);
    String response = new String(bytes, StandardCharsets.US_ASCII);
    if (!"JDWP-Handshake".equals(response)) {
      throw new JdwpException("Failed jdwp handshake");
    }
  }

  /**
   * Sends a command packet over jdwp
   */
  public void command(int id, Command cmd, byte[] data) throws IOException {
    os.writeInt(11 + data.length);
    os.writeInt(id);
    os.write(0);
    os.write(cmd.cmdset);
    os.write(cmd.cmd);
    os.write(data, 0, data.length);
  }

  /**
   * Reads the next jdwp packet from the jdwp connection.
   * Blocks until a complete packet is available.
   */
  public Packet read() throws IOException {
    Packet packet = new Packet();
    int length = is.readInt();
    packet.id = is.readInt();

    int flags = is.readUnsignedByte();
    if (flags != 0x00 && flags != 0x80) {
      throw new JdwpException("Invalid packet flags");
    }
    packet.reply = (flags == 0x80);

    if (packet.reply) {
      packet.error = is.readShort();
    } else if (flags == 0x00) {
      packet.cmdset = is.readByte();
      packet.cmd = is.readByte();
    }

    packet.data = new byte[length - 11];
    is.readFully(packet.data);
    return packet;
  }
}
