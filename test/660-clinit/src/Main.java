/*
 * Copyright 2016 The Android Open Source Project
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

import java.util.*;

public class Main {

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);

    if (!checkAppImageLoaded()) {
      System.out.println("AppImage not loaded.");
    }

    if (!checkAppImageContains(ClInit.class)) {
      System.out.println("ClInit class is not in app image!");
    }

    ShouldInit(ClInit.class);
    ShouldInit(A.class);
    ShouldNotInit(D.class);
    ShouldInit(E.class);
    ShouldNotInit(ClinitE.class);
    ShouldInit(ClinitAlloc.class);
    ShouldNotInit(ClinitBulkAlloc.class);
    ShouldInit(Class.forName("FilledNewArray"));

    A x = new A();
    System.out.println("A.a: " + A.a);

    ClInit c = new ClInit();
    int aa = c.a;

    System.out.println("X: " + c.getX());
    System.out.println("Y: " + c.getY());
    System.out.println("str: " + c.str);
    System.out.println("ooo: " + c.ooo);
    System.out.println("Z: " + c.getZ());
    System.out.println("A: " + c.getA());
    System.out.println("AA: " + aa);
    if (c.arr.get(0) + c.arr.get(1) != 1000) {
      System.out.println("arr not right.");
    }

    if (c.a != 101) {
      System.out.println("a != 101");
    }

    try {
      ClinitE e = new ClinitE();
    } catch (Error err) { }

    return;
  }

  static void ShouldInit(Class<?> klass) {
    if (checkInitialized(klass) == false) {
      System.out.println(klass.getName() + " should be initialized!");
    }
  }

  static void ShouldNotInit(Class<?> klass) {
    if (checkInitialized(klass) == true) {
      System.out.println(klass.getName() + " should not be initialized!");
    }
  }

  public static native boolean checkAppImageLoaded();
  public static native boolean checkAppImageContains(Class<?> klass);
  public static native boolean checkInitialized(Class<?> klass);
}

enum Day {
    SUNDAY, MONDAY, TUESDAY, WEDNESDAY,
    THURSDAY, FRIDAY, SATURDAY
}

class ClInit {

  static String ooo = "OoooooO";
  static String str;
  static int z;
  int x, y;
  public static volatile int a = 100;
  static ArrayList<Integer> arr;
  static HashMap<Integer, String> map;

  static {
    StringBuilder sb = new StringBuilder();
    sb.append("Hello ");
    sb.append("World!");
    str = sb.toString();
  }

  static {
    z = 0xFF;
    z += 0xFF00;
    z += 0xAA0000;
  }

  static {
    arr = new ArrayList<Integer>(3);
    arr.add(new Integer(1));
    arr.add(new Integer(999));

    map = new HashMap<>();
    map.put(1, "1.00");
    map.put(2, "2.00");
    map.put(3, "3.00");
  }

  {
    for(int i = 0; i < 100; i++) {
      x += i;
    }
  }

  {
    y = this.x;
    for(int i = 0; i < 40; i++) {
      y += i;
    }
  }

  ClInit() {

  }

  int getX() {
    return x;
  }

  int getZ() {
    return z;
  }

  int getY() {
    return y;
  }

  int getA() {
    return a;
  }
}

class A {
  public static int a = 2;
  static {
    a = 5;
  }
}

class D {
  static int d;
  static {
    d = E.e; // fail here
  }
}

class E {
  public static final int e;
  static {
    e = 100;
  }
}

class ClinitE {
  static {
    if (Math.sin(3) < 0.5) {
      // throw anyway, can't initialized
      throw new ExceptionInInitializerError("Can't initialize this class!");
    }
  }
}

class Pair {
  public int a, b;
}

class ClinitAlloc {
  static byte[] iArr1 = new byte[10];
  static int[] iArr2 = new int[10];
  static double[] iArr3 = new double[10];
  static Pair[] iArr4 = new Pair[10];
  static int[] iArr5 = {1, 2, 3, 4, 5};
  static Pair[] iArr6 = {new Pair(), new Pair(), new Pair()};
}

class ClinitBulkAlloc {
  static Pair[] iArr = new Pair[10000];
}


