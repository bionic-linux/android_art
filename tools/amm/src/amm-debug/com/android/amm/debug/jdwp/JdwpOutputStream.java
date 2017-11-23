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

import java.io.DataOutputStream;
import java.io.IOException;

/**
 * Output stream for writing jdwp data.
 */
class JdwpOutputStream {
  private DataOutputStream os;

  public JdwpOutputStream(DataOutputStream out) {
    os = out;
  }

  public void writeByte(int x) throws IOException {
    os.write(x);
  }

  public void writeBytes(byte[] bytes) throws IOException {
    os.write(bytes, 0, bytes.length);
  }

  public void writeBytes(byte[] bytes, int off, int len) throws IOException {
    os.write(bytes, off, len);
  }

  public void writeInt(int x) throws IOException {
    os.writeInt(x);
  }

  public void writeFieldId(long x) throws IOException {
    // TODO: Don't assume field ids are 8 bytes.
    os.writeLong(x);
  }

  public void writeClassId(long x) throws IOException {
    writeReferenceTypeId(x);
  }

  public void writeThreadId(long x) throws IOException {
    writeObjectId(x);
  }

  public void writeMethodId(long x) throws IOException {
    // TODO: Don't assume method ids are 8 bytes
    os.writeLong(x);
  }

  public void writeReferenceTypeId(long x) throws IOException {
    writeObjectId(x);
  }

  public void writeObjectId(long x) throws IOException {
    // TODO: Don't assume object id takes 8 bytes.
    os.writeLong(x);
  }

  public void writeArrayId(long x) throws IOException {
    writeObjectId(x);
  }

  public void writeArrayTypeId(long x) throws IOException {
    writeReferenceTypeId(x);
  }

  public void writeObjectValue(long objectId) throws IOException {
    writeByte('L');
    writeObjectId(objectId);
  }

  public void writeString(String x) throws IOException {
    // TODO: Len is the length of the string or the length of the bytes?
    os.writeInt(x.length());
    os.writeBytes(x);
  }
}
