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

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.OutputStream;

public class Jdwp {

  private Connection connection;
  private int packetId = 1;

  private Jdwp(Connection connection) throws IOException {
    this.connection = connection;

    IdSizes sizes = idSizes();
    if (sizes.object != 8) {
      throw new JdwpException("This implementation assumes 8 byte object ids");
    }
    if (sizes.method != 8) {
      throw new JdwpException("This implementation assumes 8 byte method ids");
    }
    if (sizes.field != 8) {
      throw new JdwpException("This implementation assumes 8 byte field ids");
    }
  }

  /**
   * Establishes a jdwp connection.
   *
   * @param port The port on localhost to debug
   */
  public static Jdwp connect(int port) throws IOException {
    return new Jdwp(new Connection(port));
  }

  public interface JdwpDataWriter {
    void write(JdwpOutputStream os) throws IOException;
  }

  /**
   * Sends a Jdwp command to the debugger.
   *
   * @return The data contents of the reply packet.
   */
  private JdwpInputStream command(Command cmd, JdwpDataWriter writer) throws IOException {
    ByteArrayOutputStream bos = new ByteArrayOutputStream();
    JdwpOutputStream os = new JdwpOutputStream(new DataOutputStream(bos));
    writer.write(os);

    int id = packetId++;
    connection.command(id, cmd, bos.toByteArray());
    Packet reply = connection.read();
    if (!reply.reply) {
      throw new JdwpException("Expected reply, but got: " + reply);
    }
    if (reply.id != id) {
      throw new JdwpException("Unexpected reply id");
    }
    if (reply.error != 0) {
      throw new JdwpException("Got error: " + reply.error);
    }
    return reply.dataInputStream();
  }

  private IdSizes idSizes() throws IOException {
    JdwpInputStream is = command(Command.VirtualMachine.IdSizes, data -> {});
    IdSizes sizes = new IdSizes();
    sizes.field = is.readInt();
    sizes.method = is.readInt();
    sizes.object = is.readInt();
    sizes.type = is.readInt();
    sizes.frame = is.readInt();
    return sizes;
  }

  /**
   * Suspends all threads in the target VM and returns the id of a thread
   * suitable for executing methods.
   */
  public long suspendForExecution() throws IOException {
    command(Command.VirtualMachine.Suspend, data -> {});

    // Set up a 1-time event request, then find and interrupt the
    // ReferenceQueueDaemon thread to force trigger that event. The
    // ReferenceQueueDaemon thread shouldn't mind being interrupted.
    // Note: It's important that we don't get hold of the ReferenceQueueDaemon
    // when it is holding the ReferenceQueue.class lock, because then we'll
    // deadlock as soon as GC runs. Using a METHOD ENTRY event request appears
    // to avoid this case.
    final int eventType = 40; // method entry event request
    final int suspendAll = 2;
    int responseId = command(Command.EventRequest.Set, data -> {
      data.writeByte(eventType);
      data.writeByte(suspendAll);
      data.writeInt(1);    // Single modifier
      data.writeByte(1);   // Count modifier
      data.writeInt(1);    // With count of 1.
    }).readInt();

    JdwpInputStream is = command(Command.VirtualMachine.AllThreads, data -> {});
    int threads = is.readInt();
    for (int i = 0; i < threads; ++i) {
      long thread = is.readThreadId();
      String name = command(Command.ThreadReference.Name, data -> {
        data.writeThreadId(thread);
      }).readString();
      if (name.equals("ReferenceQueueDaemon")) {
        // Interrupt the thread to wake it up.
        command(Command.ThreadReference.Interrupt, data -> {
          data.writeThreadId(thread);
        });
        break;
      }
    }

    command(Command.VirtualMachine.Resume, data -> {});

    // Wait for the event to trigger.
    Packet event = connection.read();
    if (event.reply) {
      throw new JdwpException("Unexpected reply");
    }

    if (event.cmdset != Command.Event.cmdset) {
      throw new JdwpException("Unexpected cmdset");
    }
    if (event.cmd != Command.Event.Composite.cmd) {
      throw new JdwpException("Unexpected command");
    }
    is = event.dataInputStream();
    byte suspendPolicy = is.readByte();
    if (suspendPolicy != suspendAll) {
      throw new JdwpException("Unexpectd suspend policy");
    }
    int eventCount = is.readInt();
    if (eventCount != 1) {
      throw new JdwpException("Unexpected event count");
    }
    byte kind = is.readByte();
    if (kind != eventType) {
      throw new JdwpException("Unexpected event kind");
    }
    int reqId = is.readInt();
    if (reqId != responseId) {
      throw new JdwpException("Unexpected response id");
    }

    return is.readThreadId();
  }

  /**
   * Returns the reference type id of the class with the given signature.
   * Returns 0 if there is no class with the given signature.
   * If there are multiple classes with the given signature, an arbitrary one
   * of them is returned.
   */
  public long classBySignature(String sig) throws IOException {
    JdwpInputStream is = command(Command.VirtualMachine.ClassesBySignature, data -> {
      data.writeString(sig);
    });

    int count = is.readInt();
    if (count == 0) {
      return 0;
    }
    int kind = is.readByte();
    return is.readReferenceTypeId();
  }

  /**
   * Returns the fieldId of the field with given name and signature from the
   * class with given class id. Inherited fields are not included.
   */
  public long fieldByName(long classId, String name, String sig) throws IOException {
    JdwpInputStream is = command(Command.ReferenceType.Fields, data -> {
      data.writeClassId(classId);
    });

    int count = is.readInt();
    for (int i = 0; i < count; ++i) {
      long fieldId = is.readMethodId();
      String fieldName = is.readString();
      String fieldSig = is.readString();
      int modBits = is.readInt();
      if (fieldName.equals(name) && fieldSig.equals(sig)) {
        return fieldId;
      }
    }
    throw new JdwpException("Failed to find field");
  }

  /**
   * Returns the methodId of the method with given name and signature from the
   * class with given class id. Inherited methods are not included.
   */
  public long methodByName(long classId, String name, String sig) throws IOException {
    JdwpInputStream is = command(Command.ReferenceType.Methods, data -> {
      data.writeClassId(classId);
    });

    int count = is.readInt();
    for (int i = 0; i < count; ++i) {
      long methodId = is.readMethodId();
      String methodName = is.readString();
      String methodSig = is.readString();
      int modBits = is.readInt();
      if (methodName.equals(name) && methodSig.equals(sig)) {
        return methodId;
      }
    }
    throw new JdwpException("Failed to find method");
  }

  /**
   * Creates a string and returns the id of the created string.
   */
  public long createString(String string) throws IOException {
    JdwpInputStream is = command(Command.VirtualMachine.CreateString, data -> {
      data.writeString(string);
    });

    return is.readStringId();
  }

  /**
   * Creates a byte array and returns the created array id.
   */
  public long createByteArray(byte[] bytes) throws IOException {
    long byteArrayType = classBySignature("[B");
    JdwpInputStream is = command(Command.ArrayType.NewInstance, data -> {
      data.writeArrayTypeId(byteArrayType);
      data.writeInt(bytes.length);
    });

    long arrayId = is.readTaggedObjectId();

    // ART's implementation of jdwp doesn't support command packets of size
    // greater than 8K, so split up the byte array into smaller chunks as
    // necessary.
    int sent = 0;
    while (sent < bytes.length) {
      final int off = sent;
      final int len = Math.min(bytes.length - sent, 4096);
      command(Command.ArrayReference.SetValues, data -> {
        data.writeArrayId(arrayId);
        data.writeInt(off);            // first index
        data.writeInt(len);            // number of values to set
        data.writeBytes(bytes, off, len);
      });

      sent += len;
    }

    return arrayId;
  }

  /**
   * Returns the string value for a string object.
   */
  public String stringValue(long stringId) throws IOException {
    JdwpInputStream is = command(Command.StringReference.Value, data -> {
      data.writeObjectId(stringId);
    });

    return is.readString();
  }

  /**
   * Creates a new instance of an object.
   *
   * @param clazz - class id of the object to create.
   * @param thread - thread id of the thread to create it on.
   * @param method - method id of the constructor.
   * @param args - list of ids of objects to pass to the constructor.
   * @return the object id.
   */
  public long newInstance(long clazz, long thread, long method, long... args)
    throws IOException {
    JdwpInputStream is = command(Command.ClassType.NewInstance, data -> {
      data.writeClassId(clazz);
      data.writeThreadId(thread);
      data.writeMethodId(method);
      data.writeInt(args.length);
      for (long arg : args) {
        data.writeObjectValue(arg);
      }
      data.writeInt(0);
    });

    long objectId = is.readTaggedObjectId();
    long exceptionId = is.readTaggedObjectId();
    if (exceptionId != 0) {
      throw new JdwpException("Unexpected exception thrown");
    }
    return objectId;
  }

  /**
   * Invokes an instance method on an object.
   *
   * @param object - object id of the object whose instance method to invoke.
   * @param thread - thread id of the thread to create it on.
   * @param clazz - class id of the object.
   * @param method - method id of the method to invoke.
   * @param args - list of ids of objects to pass to the method.
   * @return the resulting value, as a long - either an id or an extended
   *         primitive value.
   */
  public long invokeMethod(long object, long thread, long clazz, long method, long... args)
    throws IOException {
    JdwpInputStream is = command(Command.ObjectReference.InvokeMethod, data -> {
      data.writeObjectId(object);
      data.writeThreadId(thread);
      data.writeClassId(clazz);
      data.writeMethodId(method);
      data.writeInt(args.length);
      for (long arg : args) {
        data.writeObjectValue(arg);
      }
      data.writeInt(0);
    });

    long value = is.readValue();
    long exceptionId = is.readTaggedObjectId();
    if (exceptionId != 0) {
      throw new JdwpException("Unexpected exception thrown: "
          + printException(thread, exceptionId));
    }
    return value;
  }

  private String printException(long thread, long exception) throws IOException {
    long throwableType = classBySignature("Ljava/lang/Throwable;");
    long printStackTraceMethod = methodByName(throwableType, "printStackTrace", "()V");
    invokeMethod(exception, thread, throwableType, printStackTraceMethod);
    return "See device logcat for exception details";
  }

  /**
   * Invokes a static method of a class.
   *
   * @param clazz - class id of the object.
   * @param thread - thread id of the thread to create it on.
   * @param method - method id of the method to invoke.
   * @param args - list of ids of objects to pass to the method.
   * @return the resulting value, as a long - either an id or an extended
   *         primitive value.
   */
  public long invokeStaticMethod(long clazz, long thread, long method, long... args)
    throws IOException {
    JdwpInputStream is = command(Command.ClassType.InvokeMethod, data -> {
      data.writeClassId(clazz);
      data.writeThreadId(thread);
      data.writeMethodId(method);
      data.writeInt(args.length);
      for (long arg : args) {
        data.writeObjectValue(arg);
      }
      data.writeInt(0);
    });

    long value = is.readValue();
    long exceptionId = is.readTaggedObjectId();
    if (exceptionId != 0) {
      throw new JdwpException("Unexpected exception thrown");
    }
    return value;
  }

  /**
   * Get the value of a static field.
   *
   * @param referenceTypeId id of the class
   * @param fieldId id of the field to get the value of
   * @return the value packed into a long
   */
  public long getValue(long referenceTypeId, long fieldId) throws IOException {
    JdwpInputStream is = command(Command.ReferenceType.GetValues, data -> {
      data.writeReferenceTypeId(referenceTypeId);
      data.writeInt(1);
      data.writeFieldId(fieldId);
    });

    int count = is.readInt();
    if (count != 1) {
      throw new JdwpException("Unexpected count: " + count);
    }
    return is.readValue();
  }

  /*
   * DDMS Command Set (199)
   * DDMS CMD (1)
   *   4 byte type: base 256 ascii encoding of the command.
   *     For example: "HPDS" => 0x48504453
   *   4 byte length: length of the rest of the content.
   *   variable data: 'length' bytes of command specific data
   *  Note: it appears the protocol is to allow multiple type,length,data
   *  chunks in a single jdwp packet, but more than one may not be supported
   *  by the runtime.
   *
   *  See the implementation of android.ddm.* to find details on the specific
   *  DDMS commands and data formats.
   *
   * HPDS: no output data. reply data is the contents of the heap dump.
   */

  /**
   * Triggers a heap dump and writes the resulting heap dump to the given
   * output stream.
   */
  public void dumpHeap(OutputStream os) throws IOException {
    // TODO: Ideally we stream the reply data directly to the output stream
    // rather than copy the entire contents to a byte array first.
    //
    // Note: Ddms doesn't give a 'reply' packet for a chunk like the other
    // jdwp commands. It treats it as a command from the debuggee.
    ByteArrayOutputStream bos = new ByteArrayOutputStream();
    JdwpOutputStream jos = new JdwpOutputStream(new DataOutputStream(bos));
    jos.writeInt(0x48504453); // type: "HPDS"
    jos.writeInt(0);          // length: 0

    int id = packetId++;
    connection.command(id, Command.Ddms.Cmd, bos.toByteArray());
    Packet reply = connection.read();

    os.write(reply.data, 8, reply.data.length - 8);
  }
}
