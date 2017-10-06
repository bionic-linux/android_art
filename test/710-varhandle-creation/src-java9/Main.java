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
    }
}
