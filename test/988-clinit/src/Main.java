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

  public static void main(String[] args) {
    System.loadLibrary(args[0]);
    if (!checkAppImageLoaded()) {
      System.out.println("AppImage not loaded.");
    }

    if (!checkAppImageContains(ClInit.class)) {
      System.out.println("ClInit class is not in app image!");
    }

    ShouldInit(ClInit.class);
    ShouldInit(A.class);
    ShouldNotInit(B.class);
    ShouldNotInit(C.class);
    ShouldInit(D.class);
    ShouldNotInit(E.class);
    ShouldInit(F.class);
    ShouldNotInit(F1.class);
    ShouldInit(F2.class);

    A x = new A();
    System.out.println("A.a: " + A.a);

    B y = new B();
    C z = new C();
    System.out.println("A.a: " + A.a);
    System.out.println("B.b: " + B.b);
    System.out.println("C.c: " + C.c);

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

class B {
  public static int b;
  static {
    A.a = 10;
    b = A.a;
  }
}

class C {
  public static int c;
  static {
    c = A.a; // fail here
  }
}

class D {
  public static final int d;
  static {
    d = 100;
  }
}

class E {
  static int e;
  static {
    e = D.d; // fail here
  }
}

class F {
  static final int f = 111;
  static int getFScale(int x) {
    int tmp = f * 100;
    return tmp * x;
  }
  static int getScale(int x, int y) {
    return x * y;
  }
}

class F1 {
  static int f1;
  static {
    f1 = F.getFScale(9);  // fail here
  }
}

class F2 {
  static int f2;
  static {
    f2 = F.getScale(100, 20);
  }
}

