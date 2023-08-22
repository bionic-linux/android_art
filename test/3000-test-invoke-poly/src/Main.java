import static java.lang.invoke.MethodType.methodType;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.lang.invoke.WrongMethodTypeException;

public class Main {

    private int field = 0;
    private static int sField;
    private static final MethodHandle PRINT_MH;
    private static final MethodHandle PRINT_OO_MH;
    private static final MethodHandle FIELD_GETTER_MH;
    private static final MethodHandle FIELD_SETTER_MH;
    private static final MethodHandle SFIELD_SETTER_MH;
    private static final MethodHandle SFIELD_GETTER_MH;
    private static final MethodHandle PRINT_STATIC_MH;

    static {
        try {
            PRINT_MH = MethodHandles.lookup()
                .findVirtual(Main.class, "print", methodType(void.class, long.class, int.class));
            PRINT_OO_MH = MethodHandles.lookup()
                .findVirtual(Main.class, "print", methodType(void.class, Object.class, Object.class));
            FIELD_GETTER_MH = MethodHandles.lookup()
                .findGetter(Main.class, "field", int.class);
            FIELD_SETTER_MH = MethodHandles.lookup()
                .findSetter(Main.class, "field", int.class);
            SFIELD_SETTER_MH = MethodHandles.lookup()
                .findStaticSetter(Main.class, "sField", int.class);
            SFIELD_GETTER_MH = MethodHandles.lookup()
                .findStaticGetter(Main.class, "sField", int.class);
            PRINT_STATIC_MH = MethodHandles.lookup()
                .findStatic(Main.class, "print", methodType(void.class, Object.class));
        } catch (Throwable t) {
            throw new RuntimeException(t);
        }
    }

    public void print(long s, int s2) {
        System.out.println("print " + s + s2);
    }

    public void print(Object o1, Object o2) {
        System.out.println(o1 + " " + o2);
    }

    public static void print(Object o) {
        System.out.println(o);
    }

    private static void fail(String msg) {
        throw new AssertionError(msg);
    }

    public static void main(String[] args) throws Throwable {
        PRINT_MH.invokeExact(new Main(), 42L, 43);
        try {
            PRINT_MH.invokeExact(new Main(), 42L, 43L, 44L);
            fail("");
        } catch (WrongMethodTypeException ignored) {}

        /*
        Main m = new Main();

        PRINT_STATIC_MH.invokeExact((Object) "oneone");

        try {
            PRINT_OO_MH.invokeExact(m, new Object(), new Object(), new Object());
        } catch (WrongMethodTypeException ignored) {}

        FIELD_SETTER_MH.invokeExact(m, 402);
        System.out.println((int) FIELD_GETTER_MH.invokeExact(m));
        SFIELD_SETTER_MH.invokeExact(1001);
        System.out.println((int) SFIELD_GETTER_MH.invokeExact()); */
    }
}
