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

/**
 * The constants for various JDWP commands.
 * Only the JDWP commands currently needed are defined.
 */
class Command {
  public final int cmdset;
  public final int cmd;

  public Command(int cmdset, int cmd) {
    this.cmdset = cmdset;
    this.cmd = cmd;
  }

  public static class VirtualMachine {
    public static final int cmdset = 1;
    public static final Command ClassesBySignature = new Command(cmdset, 2);
    public static final Command AllThreads = new Command(cmdset, 4);
    public static final Command IdSizes = new Command(cmdset, 7);
    public static final Command Suspend = new Command(cmdset, 8);
    public static final Command Resume = new Command(cmdset, 9);
    public static final Command CreateString = new Command(cmdset, 11);
  }

  public static class ReferenceType {
    public static final int cmdset = 2;
    public static final Command Fields = new Command(cmdset, 4);
    public static final Command Methods = new Command(cmdset, 5);
    public static final Command GetValues = new Command(cmdset, 6);
  }

  public static class ClassType {
    public static final int cmdset = 3;
    public static final Command InvokeMethod = new Command(cmdset, 3);
    public static final Command NewInstance = new Command(cmdset, 4);
  }

  public static class ArrayType {
    public static final int cmdset = 4;
    public static final Command NewInstance = new Command(cmdset, 1);
  }

  public static class ObjectReference {
    public static final int cmdset = 9;
    public static final Command InvokeMethod = new Command(cmdset, 6);
  }

  public static class StringReference {
    public static final int cmdset = 10;
    public static final Command Value = new Command(cmdset, 1);
  }

  public static class ThreadReference {
    public static final int cmdset = 11;
    public static final Command Name = new Command(cmdset, 1);
    public static final Command Status = new Command(cmdset, 4);
    public static final Command Interrupt = new Command(cmdset, 11);
  }

  public static class ArrayReference {
    public static final int cmdset = 13;
    public static final Command SetValues = new Command(cmdset, 3);
  }

  public static class EventRequest {
    public static final int cmdset = 15;
    public static final Command Set = new Command(cmdset, 1);
  }

  public static class Event {
    public static final int cmdset = 64;
    public static final Command Composite = new Command(cmdset, 100);
  }

  public static class Ddms {
    public static final int cmdset = 199;
    public static final Command Cmd = new Command(cmdset, 1);
  }

  public static class Dummy {
    public static final int cmdset = 200;
    public static final Command Cmd = new Command(cmdset, 1);
  }
}
