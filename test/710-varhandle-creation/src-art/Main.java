/*
 * Copyright 2017 Google Inc.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Google designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Google in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 */

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.lang.invoke.VarHandle.AccessMode;
import java.lang.reflect.Field;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;

public final class Main {
    // Mutable fields
    boolean z;
    byte b;
    char c;
    short s;
    int i;
    long j;
    float f;
    double d;
    Object o;

    // Final fields
    final boolean fz = true;
    final byte fb = (byte) 2;
    final char fc = 'c';
    final short fs = (short) 3;
    final int fi = 4;
    final long fj = 5;
    final float ff = 6.0f;
    final double fd = 7.0;
    final Object fo = "Hello";
    final String fss = "Boo";

    // Static fields
    static boolean sz;
    static byte sb;
    static char sc;
    static short ss;
    static int si;
    static long sj;
    static float sf;
    static double sd;
    static Object so;

    // VarHandle instances for mutable fields
    static final VarHandle vz;
    static final VarHandle vb;
    static final VarHandle vc;
    static final VarHandle vs;
    static final VarHandle vi;
    static final VarHandle vj;
    static final VarHandle vf;
    static final VarHandle vd;
    static final VarHandle vo;

    // VarHandle instances for final fields
    static final VarHandle vfz;
    static final VarHandle vfb;
    static final VarHandle vfc;
    static final VarHandle vfs;
    static final VarHandle vfi;
    static final VarHandle vfj;
    static final VarHandle vff;
    static final VarHandle vfd;
    static final VarHandle vfo;
    static final VarHandle vfss;

    // VarHandle instances for static fields
    static final VarHandle vsz;
    static final VarHandle vsb;
    static final VarHandle vsc;
    static final VarHandle vss;
    static final VarHandle vsi;
    static final VarHandle vsj;
    static final VarHandle vsf;
    static final VarHandle vsd;
    static final VarHandle vso;

    // VarHandle instances for array elements
    static final VarHandle vaz;
    static final VarHandle vab;
    static final VarHandle vac;
    static final VarHandle vas;
    static final VarHandle vai;
    static final VarHandle vaj;
    static final VarHandle vaf;
    static final VarHandle vad;
    static final VarHandle vao;

    // VarHandle instances for byte array view
    static final VarHandle vbaz;
    static final VarHandle vbab;
    static final VarHandle vbac;
    static final VarHandle vbas;
    static final VarHandle vbai;
    static final VarHandle vbaj;
    static final VarHandle vbaf;
    static final VarHandle vbad;
    static final VarHandle vbao;

    // VarHandle instances for byte buffer view
    static final VarHandle vbbz;
    static final VarHandle vbbb;
    static final VarHandle vbbc;
    static final VarHandle vbbs;
    static final VarHandle vbbi;
    static final VarHandle vbbj;
    static final VarHandle vbbf;
    static final VarHandle vbbd;
    static final VarHandle vbbo;

    static {
        try {
            vz = MethodHandles.lookup().findVarHandle(Main.class, "z", boolean.class);
            vb = MethodHandles.lookup().findVarHandle(Main.class, "b", byte.class);
            vc = MethodHandles.lookup().findVarHandle(Main.class, "c", char.class);
            vs = MethodHandles.lookup().findVarHandle(Main.class, "s", short.class);
            vi = MethodHandles.lookup().findVarHandle(Main.class, "i", int.class);
            vj = MethodHandles.lookup().findVarHandle(Main.class, "j", long.class);
            vf = MethodHandles.lookup().findVarHandle(Main.class, "f", float.class);
            vd = MethodHandles.lookup().findVarHandle(Main.class, "d", double.class);
            vo = MethodHandles.lookup().findVarHandle(Main.class, "o", Object.class);

            vfz = MethodHandles.lookup().findVarHandle(Main.class, "fz", boolean.class);
            vfb = MethodHandles.lookup().findVarHandle(Main.class, "fb", byte.class);
            vfc = MethodHandles.lookup().findVarHandle(Main.class, "fc", char.class);
            vfs = MethodHandles.lookup().findVarHandle(Main.class, "fs", short.class);
            vfi = MethodHandles.lookup().findVarHandle(Main.class, "fi", int.class);
            vfj = MethodHandles.lookup().findVarHandle(Main.class, "fj", long.class);
            vff = MethodHandles.lookup().findVarHandle(Main.class, "ff", float.class);
            vfd = MethodHandles.lookup().findVarHandle(Main.class, "fd", double.class);
            vfo = MethodHandles.lookup().findVarHandle(Main.class, "fo", Object.class);
            vfss = MethodHandles.lookup().findVarHandle(Main.class, "fss", String.class);

            vsz = MethodHandles.lookup().findStaticVarHandle(Main.class, "sz", boolean.class);
            vsb = MethodHandles.lookup().findStaticVarHandle(Main.class, "sb", byte.class);
            vsc = MethodHandles.lookup().findStaticVarHandle(Main.class, "sc", char.class);
            vss = MethodHandles.lookup().findStaticVarHandle(Main.class, "ss", short.class);
            vsi = MethodHandles.lookup().findStaticVarHandle(Main.class, "si", int.class);
            vsj = MethodHandles.lookup().findStaticVarHandle(Main.class, "sj", long.class);
            vsf = MethodHandles.lookup().findStaticVarHandle(Main.class, "sf", float.class);
            vsd = MethodHandles.lookup().findStaticVarHandle(Main.class, "sd", double.class);
            vso = MethodHandles.lookup().findStaticVarHandle(Main.class, "so", Object.class);

            vaz = MethodHandles.arrayElementVarHandle(boolean[].class);
            vab = MethodHandles.arrayElementVarHandle(byte[].class);
            vac = MethodHandles.arrayElementVarHandle(char[].class);
            vas = MethodHandles.arrayElementVarHandle(short[].class);
            vai = MethodHandles.arrayElementVarHandle(int[].class);
            vaj = MethodHandles.arrayElementVarHandle(long[].class);
            vaf = MethodHandles.arrayElementVarHandle(float[].class);
            vad = MethodHandles.arrayElementVarHandle(double[].class);
            vao = MethodHandles.arrayElementVarHandle(Object[].class);

            try {
                MethodHandles.byteArrayViewVarHandle(boolean[].class, ByteOrder.LITTLE_ENDIAN);
                throw new RuntimeException("Unexpected instantiation");
            } catch (UnsupportedOperationException e) {
                vbaz = null;
            }
            try {
                MethodHandles.byteArrayViewVarHandle(byte[].class, ByteOrder.LITTLE_ENDIAN);
                throw new RuntimeException("Unexpected instantiation");
            } catch (UnsupportedOperationException e) {
                vbab = null;
            }
            vbac = MethodHandles.byteArrayViewVarHandle(char[].class, ByteOrder.LITTLE_ENDIAN);
            vbas = MethodHandles.byteArrayViewVarHandle(short[].class, ByteOrder.BIG_ENDIAN);
            vbai = MethodHandles.byteArrayViewVarHandle(int[].class, ByteOrder.LITTLE_ENDIAN);
            vbaj = MethodHandles.byteArrayViewVarHandle(long[].class, ByteOrder.LITTLE_ENDIAN);
            vbaf = MethodHandles.byteArrayViewVarHandle(float[].class, ByteOrder.LITTLE_ENDIAN);
            vbad = MethodHandles.byteArrayViewVarHandle(double[].class, ByteOrder.BIG_ENDIAN);
            try {
                MethodHandles.byteArrayViewVarHandle(Object[].class, ByteOrder.LITTLE_ENDIAN);
                throw new RuntimeException("Unexpected instantiation");
            } catch (UnsupportedOperationException e) {
                vbao = null;
            }

            try {
                MethodHandles.byteBufferViewVarHandle(boolean[].class, ByteOrder.LITTLE_ENDIAN);
                throw new RuntimeException("Unexpected instantiation");
            } catch (UnsupportedOperationException e) {
                vbbz = null;
            }
            try {
                MethodHandles.byteBufferViewVarHandle(byte[].class, ByteOrder.LITTLE_ENDIAN);
                throw new RuntimeException("Unexpected instantiation");
            } catch (UnsupportedOperationException e) {
                vbbb = null;
            }
            vbbc = MethodHandles.byteBufferViewVarHandle(char[].class, ByteOrder.LITTLE_ENDIAN);
            vbbs = MethodHandles.byteBufferViewVarHandle(short[].class, ByteOrder.BIG_ENDIAN);
            vbbi = MethodHandles.byteBufferViewVarHandle(int[].class, ByteOrder.LITTLE_ENDIAN);
            vbbj = MethodHandles.byteBufferViewVarHandle(long[].class, ByteOrder.LITTLE_ENDIAN);
            vbbf = MethodHandles.byteBufferViewVarHandle(float[].class, ByteOrder.LITTLE_ENDIAN);
            vbbd = MethodHandles.byteBufferViewVarHandle(double[].class, ByteOrder.BIG_ENDIAN);
            try {
                MethodHandles.byteBufferViewVarHandle(Object[].class, ByteOrder.LITTLE_ENDIAN);
                throw new RuntimeException("Unexpected instantiation");
            } catch (UnsupportedOperationException e) {
                vbbo = null;
            }
        } catch (RuntimeException e) {
            throw e;
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    public static void fail(String reason) {
        throw new RuntimeException("FAIL: " + reason);
    }

    public static void report(VarHandle v) {
        if (v == null) {
            return;
        }
        System.out.print(String.format("varType %s coordinateTypes %s",
                                       v.varType().toString(),
                                       v.coordinateTypes().toString()));
        System.out.print("accessModeType\n");
        for (VarHandle.AccessMode am : VarHandle.AccessMode.values()) {
            String valid = v.isAccessModeSupported(am) ? "Y" : "N";
            String s = String.format("  %-28s %s %s", am.toString(), valid,
                                     v.accessModeType(am).toString());
            System.out.println(s);
        }
    }

    public static class LookupCheckA {
        public String fieldA = "123";
        public Object fieldB = "123";
        protected int fieldC = 0;
        private int fieldD = 0;

        public static String staticFieldA = "123";
        public static Object staticFieldB = "123";
        protected static int staticFieldC = 0;
        private static int staticFieldD = 0;

        private static final VarHandle vhA;
        private static final VarHandle vhB;
        private static final VarHandle vhC;
        private static final VarHandle vhD;

        private static final VarHandle vhsA;
        private static final VarHandle vhsB;
        private static final VarHandle vhsC;
        private static final VarHandle vhsD;

        static {
            try {
                // Instance fields
                try {
                    // Mis-spelling field name
                    MethodHandles.lookup().findVarHandle(LookupCheckA.class, "feldA", Object.class);
                    fail("Misspelled field name succeeded.");
                } catch (NoSuchFieldException e) {}

                try {
                    // Using wrong field type
                    MethodHandles.lookup().findVarHandle(LookupCheckA.class, "fieldA", Float.class);
                    fail("Misspelled field name succeeded.");
                } catch (NoSuchFieldException e) {}

                try {
                    // Using wrong field type
                    MethodHandles.lookup().findVarHandle(LookupCheckA.class, "fieldB", Float.class);
                    fail("Wrong field type succeeded.");
                } catch (NoSuchFieldException e) {}

                try {
                    // Looking up static field
                    MethodHandles.lookup().findVarHandle(LookupCheckA.class, "staticFieldA", String.class);
                    fail("Static field resolved as instance field.");
                } catch (IllegalAccessException e) {}

                vhA = MethodHandles.lookup().findVarHandle(LookupCheckA.class, "fieldA", String.class);
                vhB = MethodHandles.lookup().findVarHandle(LookupCheckA.class, "fieldB", Object.class);
                vhC = MethodHandles.lookup().findVarHandle(LookupCheckA.class, "fieldC", int.class);
                vhD = MethodHandles.lookup().findVarHandle(LookupCheckA.class, "fieldD", int.class);

                // Static fields
                try {
                    // Mis-spelling field name
                    MethodHandles.lookup().findStaticVarHandle(LookupCheckA.class, "staticFeldA", Object.class);
                    fail("Misspelled field name succeeded.");
                } catch (NoSuchFieldException e) {}

                try {
                    // Using wrong field type
                    MethodHandles.lookup().findStaticVarHandle(LookupCheckA.class, "staticFieldA", Float.class);
                    fail("Misspelled field name succeeded.");
                } catch (NoSuchFieldException e) {}

                try {
                    // Using wrong field type
                    MethodHandles.lookup().findStaticVarHandle(LookupCheckA.class, "staticFieldB", Float.class);
                    fail("Wrong field type succeeded");
                } catch (NoSuchFieldException e) {}

                try {
                    // Looking up instance field
                    MethodHandles.lookup().findStaticVarHandle(LookupCheckA.class, "fieldA", String.class);
                    fail("Instance field resolved as static field");
                } catch (IllegalAccessException e) {}

                vhsA = MethodHandles.lookup().findStaticVarHandle(LookupCheckA.class, "staticFieldA", String.class);
                vhsB = MethodHandles.lookup().findStaticVarHandle(LookupCheckA.class, "staticFieldB", Object.class);
                vhsC = MethodHandles.lookup().findStaticVarHandle(LookupCheckA.class, "staticFieldC", int.class);
                vhsD = MethodHandles.lookup().findStaticVarHandle(LookupCheckA.class, "staticFieldD", int.class);
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        }

        protected static void fail(String reason) {
            Main.fail(reason);
        }

        public static void run() {
            System.out.print("LookupCheckA...");
            if (vhA == null) fail("vhA is null");
            if (vhB == null) fail("vhB is null");
            if (vhC == null) fail("vhC is null");
            if (vhD == null) fail("vhD is null");
            if (vhsA == null) fail("vhsA is null");
            if (vhsB == null) fail("vhsB is null");
            if (vhsC == null) fail("vhsC is null");
            if (vhsD == null) fail("vhsD is null");
            System.out.println("PASS");
        }
    }

    final static class LookupCheckB extends LookupCheckA {
        private static final VarHandle vhA;
        private static final VarHandle vhB;
        private static final VarHandle vhC;

        private static final VarHandle vhsA;
        private static final VarHandle vhsB;
        private static final VarHandle vhsC;

        static {
            try {
                vhA = MethodHandles.lookup().findVarHandle(LookupCheckA.class, "fieldA", String.class);
                MethodHandles.lookup().findVarHandle(LookupCheckB.class, "fieldA", String.class);

                vhB = MethodHandles.lookup().findVarHandle(LookupCheckA.class, "fieldB", Object.class);
                MethodHandles.lookup().findVarHandle(LookupCheckB.class, "fieldB", Object.class);

                vhC = MethodHandles.lookup().findVarHandle(LookupCheckA.class, "fieldC", int.class);
                MethodHandles.lookup().findVarHandle(LookupCheckB.class, "fieldC", int.class);

                try {
                    MethodHandles.lookup().findVarHandle(LookupCheckA.class, "fieldD", int.class);
                    fail("Accessing private field");
                } catch (IllegalAccessException e) {}

                vhsA = MethodHandles.lookup().findStaticVarHandle(LookupCheckA.class, "staticFieldA", String.class);
                MethodHandles.lookup().findStaticVarHandle(LookupCheckB.class, "staticFieldA", String.class);

                vhsB = MethodHandles.lookup().findStaticVarHandle(LookupCheckA.class, "staticFieldB", Object.class);
                MethodHandles.lookup().findStaticVarHandle(LookupCheckB.class, "staticFieldB", Object.class);

                vhsC = MethodHandles.lookup().findStaticVarHandle(LookupCheckA.class, "staticFieldC", int.class);
                MethodHandles.lookup().findStaticVarHandle(LookupCheckB.class, "staticFieldC", int.class);

                try {
                    MethodHandles.lookup().findStaticVarHandle(LookupCheckA.class, "staticFieldD", int.class);
                    fail("Accessing private field");
                } catch (IllegalAccessException e) {}
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        }

        public static void run() {
            // Testing access
            System.out.print("LookupCheckB...");
            if (vhA == null) fail("vhA is null");
            if (vhB == null) fail("vhB is null");
            if (vhC == null) fail("vhC is null");
            if (vhsA == null) fail("vhsA is null");
            if (vhsB == null) fail("vhsB is null");
            if (vhsC == null) fail("vhsC is null");
            System.out.println("PASS");
        }
    }

    public static class LookupCheckC {
        private static final VarHandle vhA;
        private static final VarHandle vhB;
        private static final VarHandle vhC;
        private static final VarHandle vhsA;
        private static final VarHandle vhsB;
        private static final VarHandle vhsC;

        static {
            try {
                vhA = MethodHandles.lookup().findVarHandle(LookupCheckB.class, "fieldA", String.class);
                try {
                    MethodHandles.lookup().findVarHandle(LookupCheckB.class, "fieldA", Float.class);
                } catch (NoSuchFieldException e) {}
                vhB = MethodHandles.lookup().findVarHandle(LookupCheckB.class, "fieldB", Object.class);
                try {
                    MethodHandles.lookup().findVarHandle(LookupCheckB.class, "fieldB", int.class);
                } catch (NoSuchFieldException e) {}
                vhC = MethodHandles.lookup().findVarHandle(LookupCheckB.class, "fieldC", int.class);
                try {
                    MethodHandles.lookup().findVarHandle(LookupCheckB.class, "fieldD", int.class);
                    fail("Accessing private field in unrelated class");
                } catch (IllegalAccessException e) {}

                vhsA = MethodHandles.lookup().findStaticVarHandle(LookupCheckB.class, "staticFieldA", String.class);
                try {
                    MethodHandles.lookup().findStaticVarHandle(LookupCheckB.class, "staticFieldA", Float.class);
                } catch (NoSuchFieldException e) {}
                vhsB = MethodHandles.lookup().findStaticVarHandle(LookupCheckB.class, "staticFieldB", Object.class);
                try {
                    MethodHandles.lookup().findStaticVarHandle(LookupCheckB.class, "staticFieldB", int.class);
                } catch (NoSuchFieldException e) {}
                vhsC = MethodHandles.lookup().findStaticVarHandle(LookupCheckB.class, "staticFieldC", int.class);
                try {
                    MethodHandles.lookup().findStaticVarHandle(LookupCheckB.class, "staticFieldD", int.class);
                    fail("Accessing private field in unrelated class");
                } catch (IllegalAccessException e) {}

                try {
                    MethodHandles.lookup().findStaticVarHandle(LookupCheckB.class, "fieldA", String.class);
                    fail("Found instance field looking for static");
                } catch (IllegalAccessException e) {}
                try {
                    MethodHandles.lookup().findVarHandle(LookupCheckB.class, "staticFieldA", String.class);
                    fail("Found static field looking for instance");
                } catch (IllegalAccessException e) {}
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        }

        public static void run() {
            System.out.print("UnreflectCheck...");
            if (vhA == null) fail("vhA is null");
            if (vhB == null) fail("vhB is null");
            if (vhsA == null) fail("vhsA is null");
            if (vhsB == null) fail("vhsB is null");
            System.out.println("PASS");
        }
    }

    public static final class UnreflectCheck {
        private static final VarHandle vhA;
        private static final VarHandle vhsA;

        static {
            try {
                Field publicField = LookupCheckA.class.getField("fieldA");
                vhA = MethodHandles.lookup().unreflectVarHandle(publicField);
                try {
                    Field protectedField = LookupCheckA.class.getField("fieldC");
                    MethodHandles.lookup().unreflectVarHandle(protectedField);
                    fail("Unreflected protected field");
                } catch (NoSuchFieldException e) {}
                try {
                    Field privateField = LookupCheckA.class.getField("fieldD");
                    MethodHandles.lookup().unreflectVarHandle(privateField);
                    fail("Unreflected private field");
                } catch (NoSuchFieldException e) {}
                try {
                    Field privateField = LookupCheckA.class.getField("fieldD");
                    privateField.setAccessible(true);
                    MethodHandles.lookup().unreflectVarHandle(privateField);
                    fail("Unreflected private field");
                } catch (NoSuchFieldException e) {}

                Field staticPublicField = LookupCheckA.class.getField("staticFieldA");
                vhsA = MethodHandles.lookup().unreflectVarHandle(staticPublicField);
                try {
                    Field protectedField = LookupCheckA.class.getField("staticFieldC");
                    MethodHandles.lookup().unreflectVarHandle(protectedField);
                    fail("Unreflected protected field");
                } catch (NoSuchFieldException e) {}
                try {
                    Field privateField = LookupCheckA.class.getField("staticFieldD");
                    MethodHandles.lookup().unreflectVarHandle(privateField);
                    fail("Unreflected private field");
                } catch (NoSuchFieldException e) {}
                try {
                    Field privateField = LookupCheckA.class.getField("staticFieldD");
                    privateField.setAccessible(true);
                    MethodHandles.lookup().unreflectVarHandle(privateField);
                    fail("Unreflected private field");
                } catch (NoSuchFieldException e) {}
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        }

        public static void run() {
            System.out.print("LookupCheckC...");
            if (vhA == null) fail("vhA is null");
            if (vhsA == null) fail("vhsA is null");
            System.out.println("PASS");
        }
    }

    public static void main(String[] args) {
        Main a = new Main();

        System.out.println("=== Fields ===");
        report(vz);
        report(vb);
        report(vc);
        report(vs);
        report(vi);
        report(vj);
        report(vd);
        report(vo);

        System.out.println("=== Final Fields ===");
        report(vfz);
        report(vfb);
        report(vfc);
        report(vfs);
        report(vfi);
        report(vfj);
        report(vfd);
        report(vfo);
        report(vfss);

        System.out.println("=== Static Fields ===");
        report(vsz);
        report(vsb);
        report(vsc);
        report(vss);
        report(vsi);
        report(vsj);
        report(vsf);
        report(vsd);
        report(vso);

        System.out.println("=== Array Element ===");
        report(vaz);
        report(vab);
        report(vac);
        report(vas);
        report(vai);
        report(vaj);
        report(vaf);
        report(vad);
        report(vao);

        System.out.println("=== Byte Array View ===");
        report(vbaz);
        report(vbab);
        report(vbac);
        report(vbas);
        report(vbai);
        report(vbaj);
        report(vbaf);
        report(vbad);
        report(vbao);

        System.out.println("=== Byte Buffer View ===");
        report(vbbz);
        report(vbbb);
        report(vbbc);
        report(vbbs);
        report(vbbi);
        report(vbbj);
        report(vbbf);
        report(vbbd);
        report(vbbo);

        // Checks on names, types, access
        LookupCheckA.run();
        LookupCheckB.run();
        LookupCheckC.run();
        UnreflectCheck.run();
    }
}
