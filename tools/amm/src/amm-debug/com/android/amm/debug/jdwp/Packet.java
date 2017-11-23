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

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;

/**
 * A jdwp command or reply packet.
 */
class Packet {
  public int id;
  boolean reply;
  byte cmdset;    // only valid if reply is false
  byte cmd;       // only valid if reply is false
  short error;    // only valid if reply is true
  byte[] data;

  public JdwpInputStream dataInputStream() {
    return new JdwpInputStream(new DataInputStream(new ByteArrayInputStream(data)));
  }

  @Override
  public String toString() {
    if (reply) {
      return String.format("REPLY: id=%d err=%d dlen=%d", id, error, data.length);
    } else {
      return String.format("CMD: id=%d cmdset=%d cmd=%d dlen=%d", id, cmdset, cmd, data.length);
    }
  }
}
