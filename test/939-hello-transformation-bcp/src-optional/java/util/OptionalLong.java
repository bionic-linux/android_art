package java.util;
import java.util.function.LongConsumer;
import java.util.function.LongSupplier;
import java.util.function.Supplier;
import java.util.stream.LongStream;
public final class OptionalLong {
  // Make sure we have a <clinit> function since the real implementation of OptionalLong does.
  static { EMPTY = null; }
  private static final OptionalLong EMPTY;
  private final boolean isPresent;
  private final long value;
  private OptionalLong() { isPresent = false; value = 0; }
  private OptionalLong(long l) { this(); }
  public static OptionalLong empty() { return null; }
  public static OptionalLong of(long value) { return null; }
  public long getAsLong() { return 0; }
  public boolean isPresent() { return false; }
  public boolean isEmpty() { return false; }
  public void ifPresent(LongConsumer c) { }
  public void ifPresentOrElse(LongConsumer action, Runnable emptyAction) { }
  public LongStream stream() { return null; }
  public long orElse(long l) { return 0; }
  public long orElseGet(LongSupplier s) { return 0; }
  public long orElseThrow() { return 0; }
  public<X extends Throwable> long orElseThrow(Supplier<? extends X> s) throws X { return 0; }
  public boolean equals(Object o) { return false; }
  public int hashCode() { return 0; }
  public String toString() { return "Redefined OptionalLong!"; }
}
