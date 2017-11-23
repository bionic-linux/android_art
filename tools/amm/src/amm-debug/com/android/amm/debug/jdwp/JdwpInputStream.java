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
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * Input stream for reading jdwp data.
 */
class JdwpInputStream {
  private DataInputStream is;

  public JdwpInputStream(DataInputStream in) {
    is = in;
  }

  public byte readByte() throws IOException {
    return is.readByte();
  }

  public int readInt() throws IOException {
    return is.readInt();
  }

  public long readThreadId() throws IOException {
    return readObjectId();
  }

  /**
   * Returns just the object id, not including the tag.
   */
  public long readTaggedObjectId() throws IOException {
    readByte();
    return readObjectId();
  }

  /**
   * Returns the value as an id for objects, or an extended primitive type.
   */
  public long readValue() throws IOException {
    char sig = (char)readByte();
    switch (sig) {
      case '[': return readObjectId();
      case 'L': return readObjectId();
      case 'B': return readByte();
      case 'V': return 0;
      case 'S': return readObjectId();
      case 'c': return readObjectId();
      case 'l': return readObjectId();
      case 's': return readObjectId();
      default: throw new AssertionError("TODO: sig=" + sig);
    }
  }

  public long readObjectId() throws IOException {
    // TODO: Don't assume object ids are 8 bytes.
    return is.readLong();
  }

  public long readMethodId() throws IOException {
    // TODO: Don't assume method ids are 8 bytes.
    return is.readLong();
  }

  public long readStringId() throws IOException {
    return readObjectId();
  }

  public long readReferenceTypeId() throws IOException {
    return readObjectId();
  }

  public String readString() throws IOException {
    // TODO: Len is the length of the string or the length of the bytes?
    int len = is.readInt();
    byte[] bytes = new byte[len];
    is.readFully(bytes);
    return new String(bytes, StandardCharsets.UTF_8);
  }
}
