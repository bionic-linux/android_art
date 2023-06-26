package com.android.ahat.knowntypes;

import com.android.ahat.Column;
import com.android.ahat.Doc;
import com.android.ahat.DocString;
import com.android.ahat.Query;
import com.android.ahat.SubsetSelector;
import com.android.ahat.Summarizer;
import com.android.ahat.heapdump.AhatArrayInstance;
import com.android.ahat.heapdump.AhatInstance;
import com.android.ahat.heapdump.Value;

import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;

public class MapHandler implements KnownTypesHandler {
  private static final String MAP_ENTRIES_ID = "mapEntries";

  private static abstract class ClassSpec {
    private final String className;

    private ClassSpec(String className) {
      this.className = className;
    }

    abstract HashMap<Value, Value> getMapContents(AhatInstance map);
  }

  /**
   *  A ClassSpec for interpreting maps that store their contents in a set of Nodes, each Node
   * containing both a key and the corresponding value. The nodes can optionally form a linked list,
   * e.g. when multiple nodes belong to the same hash bucket.
   */
  private static final class NodeBasedClassSpec extends ClassSpec {
    private final String bucketTableFieldName;
    private final String nodeKeyFieldName;
    private final String nodeValueFieldName;
    private final String nodeLinkFieldName;

    private NodeBasedClassSpec(String className, String bucketTableFieldName,
        String nodeKeyFieldName, String nodeValueFieldName, String nodeLinkFieldName) {
      super(className);
      this.bucketTableFieldName = bucketTableFieldName;
      this.nodeKeyFieldName = nodeKeyFieldName;
      this.nodeValueFieldName = nodeValueFieldName;
      this.nodeLinkFieldName = nodeLinkFieldName;
    }

    @Override
    HashMap<Value, Value> getMapContents(AhatInstance map) {
      // TODO: Support tree'd buckets
      HashMap<Value, Value> mapContents = new HashMap<>();
      AhatArrayInstance table =
          map.getField(bucketTableFieldName).asAhatInstance().asArrayInstance();
      for (Value v : table.getValues()) {
        while (v != null) {
          AhatInstance node = v.asAhatInstance();
          mapContents.put(node.getField(nodeKeyFieldName), node.getField(nodeValueFieldName));
          v = node.getField(nodeLinkFieldName);
        }
      }
      return mapContents;
    }
  }

  /**
   * A ClassSpec for interpreting maps that store their contents as alternating keys and values in
   * a single array.
   */
  private static final class AlternatingKeyValueMapClassSpec extends ClassSpec {
    private final String alternatingKeyValueTableFieldName;

    private AlternatingKeyValueMapClassSpec(
        String className, String alternatingKeyValueTableFieldName) {
      super(className);
      this.alternatingKeyValueTableFieldName = alternatingKeyValueTableFieldName;
    }

    @Override
    HashMap<Value, Value> getMapContents(AhatInstance map) {
      AhatArrayInstance alternatingArray =
          map.getField(alternatingKeyValueTableFieldName).asAhatInstance().asArrayInstance();
      HashMap<Value, Value> result = new HashMap<>();
      for (int i = 0; i < alternatingArray.getLength(); i += 2) {
        Value key = alternatingArray.getValue(i);
        Value value = alternatingArray.getValue(i + 1);
        if (key != null) {
          result.put(key, value);
        }
      }
      return result;
    }
  }

  /** A ClassSpec for interpreting maps that hold a single value directly. */
  private static final class SingletonMapClassSpec extends ClassSpec {
    private final String keyFieldName;
    private final String valueFieldName;

    private SingletonMapClassSpec(String className, String keyFieldName, String valueFieldName) {
      super(className);
      this.keyFieldName = keyFieldName;
      this.valueFieldName = valueFieldName;
    }

    @Override
    HashMap<Value, Value> getMapContents(AhatInstance map) {
      HashMap<Value, Value> result = new HashMap<>();
      result.put(map.getField(keyFieldName), map.getField(valueFieldName));
      return result;
    }
  }

  /**
   *  A ClassSpec for interpreting maps that use two parallel arrays for keys and values.
   * There are two common schemes that are supported:
   * - A null indicates no value, while a special object indicates a stored null
   * - A null indicates that null was stored in the map, while a special object indicates no value
   */
  private static final class ParallelArraysClassSpec extends ClassSpec {
    private final String keyArrayFieldName;
    private final String valueArrayFieldName;
    private final String sentinelFieldName;
    private final boolean sentinelIndicatesNull;

    private ParallelArraysClassSpec(
        String className, String keyArrayFieldName, String valueArrayFieldName) {
      this(className, keyArrayFieldName, valueArrayFieldName, null, false);
    }

    private ParallelArraysClassSpec(String className, String keyArrayFieldName,
        String valueArrayFieldName, String sentinelFieldName, boolean sentinelIndicatesNull) {
      super(className);
      this.keyArrayFieldName = keyArrayFieldName;
      this.valueArrayFieldName = valueArrayFieldName;
      this.sentinelFieldName = sentinelFieldName;
      this.sentinelIndicatesNull = sentinelIndicatesNull;
    }

    @Override
    HashMap<Value, Value> getMapContents(AhatInstance map) {
      AhatArrayInstance keys = map.getField(keyArrayFieldName).asAhatInstance().asArrayInstance();
      AhatArrayInstance values =
          map.getField(valueArrayFieldName).asAhatInstance().asArrayInstance();
      int size = Math.min(keys.getLength(), values.getLength());
      Value sentinel = null;
      if (sentinelFieldName != null) {
        sentinel = map.getField(sentinelFieldName);
      }
      HashMap<Value, Value> result = new HashMap<>();
      for (int i = 0; i < size; i++) {
        Value k = keys.getValue(i);
        Value v = values.getValue(i);

        if (sentinelFieldName == null || isRealValue(v, sentinel)) {
          result.put(k, getRealValue(v, sentinel));
        }
      }
      return result;
    }

    boolean isRealValue(Value value, Value sentinel) {
      if (sentinelIndicatesNull) {
        return value != null;
      } else {
        return value != sentinel;
      }
    }

    Value getRealValue(Value value, Value sentinel) {
      if (sentinelIndicatesNull) {
        return value == sentinel ? null : value;
      } else {
        return value;
      }
    }
  }

  /** A ClassSpec for interpreting maps that merely wrap some other Map object internally. */
  private static final class WrapperMapClassSpec extends ClassSpec {
    private final String wrappedFieldName;

    private WrapperMapClassSpec(String className, String wrappedFieldName) {
      super(className);
      this.wrappedFieldName = wrappedFieldName;
    }

    @Override
    HashMap<Value, Value> getMapContents(AhatInstance map) {
      return new MapHandler().getMapContents(map.getField(wrappedFieldName).asAhatInstance());
    }
  }

  private static final ClassSpec[] specs = new ClassSpec[] {
      new NodeBasedClassSpec("java.util.HashMap", "table", "key", "value", "next"),
      new NodeBasedClassSpec("java.util.Hashtable", "table", "key", "value", "next"),
      new ParallelArraysClassSpec("java.util.EnumMap", "keyUniverse", "vals", "NULL", true),
      new SingletonMapClassSpec("java.util.Collections.SingletonMap", "k", "v"),
      new WrapperMapClassSpec("java.util.collections.UnmodifiableMap", "m"),
      new AlternatingKeyValueMapClassSpec(
          "com.google.common.collect.RegularImmutableMap", "alternatingKeysAndValues"),
      new AlternatingKeyValueMapClassSpec("android.util.ArrayMap", "mArray"),
      new ParallelArraysClassSpec("android.util.SparseArray", "mKeys", "mValues", "DELETED", false),
      new ParallelArraysClassSpec("android.util.SparseBooleanArray", "mKeys", "mValues"),
      new WrapperMapClassSpec("android.util.SparseDoubleArray", "mValues"),
      new ParallelArraysClassSpec("android.util.SparseIntArray", "mKeys", "mValues"),
      new ParallelArraysClassSpec("android.util.SparseLongArray", "mKeys", "mValues"),
      new ParallelArraysClassSpec("android.util.LongSparseArray", "mKeys", "mValues"),
      new ParallelArraysClassSpec("android.util.LongSparseLongArray", "mKeys", "mValues"),
  };

  private static final HashMap<String, ClassSpec> specsByClass = new HashMap<>();

  static {
    for (ClassSpec spec : specs) {
      specsByClass.put(spec.className, spec);
    }
  }

  @Override
  public Set<String> handlesTypes() {
    return specsByClass.keySet();
  }

  @Override
  public DocString summarize(AhatInstance instance) {
    return DocString.text("");
  }

  @Override
  public void printDetailsSection(Doc doc, Query query, AhatInstance instance) {
    doc.section("Map Contents");

    try {
      AhatInstance base = instance.getBaseline();
      boolean diffing = instance != base && !base.isPlaceHolder();

      HashMap<Value, Value> mapContents = getMapContents(instance);

      if (diffing) {
        HashMap<Value, Value> baseContents = getMapContents(base);
        // Union the keys from current and baseline maps.
        Set<Value> keys = new HashSet<>(mapContents.keySet());
        baseContents.keySet().stream().map(Value::getBaseline).forEachOrdered(keys::add);

        printDiffTable(doc, query,
            keys.stream().sorted(new KeySorter()).collect(Collectors.toList()), mapContents,
            baseContents);
      } else {
        printTable(doc, query,
            mapContents.keySet().stream().sorted(new KeySorter()).collect(Collectors.toList()),
            mapContents);
      }

    } catch (Exception e) {
      doc.println(DocString.text("Error evaluating map contents (possible heap corruption?)"));
      doc.println(DocString.internalError(e));
    }
  }

  private HashMap<Value, Value> getMapContents(AhatInstance map) {
    ClassSpec spec = specsByClass.get(getKnownBaseClass(map));
    return spec.getMapContents(map);
  }

  static class KeySorter implements Comparator<Value> {
    @Override
    public int compare(Value a, Value b) {
      int typeDiff = Value.getType(a).compareTo(Value.getType(b));
      if (typeDiff != 0) {
        return typeDiff;
      }

      return switch (Value.getType(a)) {
        case OBJECT
            -> {
          // TODO: Identify strings and boxed primitives and sort them by value

          // Sort all other instances by their ID within the dump
          long aId = a == null ? 0:
          a.asAhatInstance().getId();
          long bId = b == null ? 0 : b.asAhatInstance().getId();
          yield Long.compare(aId, bId);
      }
        case CHAR -> a.asChar().compareTo(b.asChar());
        case BYTE -> a.asByte().compareTo(b.asByte());
        case INT -> a.asInteger().compareTo(b.asInteger());
        case LONG -> a.asLong().compareTo(b.asLong());
        // TODO: Add a way to sort by the underlying value here
        case BOOLEAN, FLOAT, DOUBLE, SHORT ->
            a.toString().compareTo(b.toString());
      };
    }
  }


  public static void printTable(Doc doc, Query query, List<Value> orderedKeys, Map<Value, Value> map) {
    SubsetSelector<Value> subset = new SubsetSelector<>(query, MAP_ENTRIES_ID, orderedKeys);

    doc.table(
        new Column("Key"),
        new Column("Size"),
        new Column("Retained"),
        new Column("Value"),
        new Column("Size"),
        new Column("Retained"));

    for (Value key : subset.selected()) {
            Value value = map.get(key);
            doc.row(Summarizer.summarize(key), CollectionContentsHelper.size(key),
                CollectionContentsHelper.retainedSize(key), Summarizer.summarize(value),
                CollectionContentsHelper.size(value), CollectionContentsHelper.retainedSize(value));
          }

          doc.end();
          subset.render(doc);
    }

    public static void printDiffTable(Doc doc, Query query, List<Value> orderedKeys,
        Map<Value, Value> map, Map<Value, Value> oldMap) {
        SubsetSelector<Value> subset = new SubsetSelector<>(query, MAP_ENTRIES_ID, orderedKeys);

        doc.table(new Column("Key"), new Column("Size"), new Column("Δ"), new Column("Retained"),
            new Column("Δ"), new Column("Value Now"), new Column("Old Value"), new Column("Size"),
            new Column("Δ"), new Column("Retained"), new Column("Δ"));

        for (Value key : subset.selected()) {
          Value baselineKey = Value.getBaseline(key);
          Value nowValue = map.get(key);
          Value wasValue = oldMap.get(baselineKey);

          doc.row(Summarizer.summarize(key), CollectionContentsHelper.size(key),
              CollectionContentsHelper.sizeDelta(key, baselineKey),
              CollectionContentsHelper.retainedSize(key),
              CollectionContentsHelper.retainedDelta(key, baselineKey),
              Summarizer.summarize(nowValue), Summarizer.summarize(wasValue),
              CollectionContentsHelper.size(nowValue),
              CollectionContentsHelper.sizeDelta(nowValue, wasValue),
              CollectionContentsHelper.retainedSize(nowValue),
              CollectionContentsHelper.retainedDelta(nowValue, wasValue));
        }
    }
  }
