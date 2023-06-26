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

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;

public class ListHandler implements KnownTypesHandler {
  private static final String LIST_CONTENTS_ID = "listContents";
  /**
   To avoid excessively long summaries, cap the length of summarized list contents.
   Note that this may result in printing 0 elements if the first one is too long.
  */
  private static int kMaxContentsChars = 200;

  /**
   * To avoid blowing up when summarizing deeply recursive structures, we want to short-circuit
   * instead of deeply inspecting the contents of a list we've already seen.
   */
  private final ThreadLocal<Set<AhatInstance>> tRecursiveSummarizing =
      ThreadLocal.withInitial(HashSet::new);

  private static abstract class ClassSpec {
    private final String className;

    ClassSpec(String className) {
      this.className = className;
    }

    abstract List<Value> getContents(AhatInstance instance);

    int getBackingArrayCapacity(AhatInstance instance) {
      return -1;
    }
  }

  /**
   * A ClassSpec for interpreting lists that use a backing array, with or without a corresponding
   * size field.
   */
  private static class ArrayBackedClassSpec extends ClassSpec {
    private final String arrayFieldName;
    private final String sizeFieldName;

    ArrayBackedClassSpec(String className, String arrayFieldName) {
      super(className);
      this.arrayFieldName = arrayFieldName;
      this.sizeFieldName = null;
    }

    ArrayBackedClassSpec(String className, String arrayFieldName, String sizeFieldName) {
      super(className);
      this.arrayFieldName = arrayFieldName;
      this.sizeFieldName = sizeFieldName;
    }

    AhatArrayInstance getBackingArray(AhatInstance instance) {
      return instance.getField(arrayFieldName).asAhatInstance().asArrayInstance();
    }

    int getSize(AhatInstance instance) {
      if (sizeFieldName == null) {
        return getBackingArray(instance).getLength();
      } else {
        return instance.getField(sizeFieldName).asInteger();
      }
    }

    @Override
    List<Value> getContents(AhatInstance instance) {
      int size = getSize(instance);
      AhatArrayInstance arr = getBackingArray(instance);
      return arr.getValues().stream().limit(size).collect(Collectors.toList());
    }

    @Override
    int getBackingArrayCapacity(AhatInstance instance) {
      return getBackingArray(instance).getLength();
    }
  }

  /**
   * A ClassSpec for interpreting lists that use a backing array as a ring buffer, with indexes
   * into the array that specify the current bounds.
   */
  private static class ArrayBackedRingBufferClassSpec extends ArrayBackedClassSpec {
    private String headFieldName;
    private String tailFieldName;

    ArrayBackedRingBufferClassSpec(
        String className, String arrayFieldName, String headFieldName, String tailFieldName) {
      super(className, arrayFieldName);
      this.headFieldName = headFieldName;
      this.tailFieldName = tailFieldName;
    }

    private int getHead(AhatInstance instance) {
      return instance.getField(headFieldName).asInteger();
    }

    private int getTail(AhatInstance instance) {
      return instance.getField(tailFieldName).asInteger();
    }

    @Override
    int getSize(AhatInstance instance) {
      int delta = getTail(instance) - getHead(instance);
      if (delta < 0) {
        delta += getBackingArrayCapacity(instance);
      }
      return delta;
    }

    @Override
    List<Value> getContents(AhatInstance instance) {
      int size = getSize(instance);
      int head = getHead(instance);
      AhatArrayInstance arr = getBackingArray(instance);
      List<Value> result = new ArrayList<>(size);
      for (int i = 0; i < size; i++) {
        Value v = arr.getValue((head + i) % arr.getLength());
        result.add(v);
      }
      return result;
    }
  }

  private static final ClassSpec[] specs = new ClassSpec[] {
      new ArrayBackedClassSpec("java.util.ArrayList", "elementData", "size"),
      new ArrayBackedClassSpec("java.util.Vector", "elementData", "elementCount"),
      new ArrayBackedRingBufferClassSpec("java.util.ArrayDeque", "elements", "head", "tail"),
      new ArrayBackedClassSpec("com.google.common.collect.RegularImmutableList", "array"),
      new ArrayBackedClassSpec("android.util.IntArray", "mValues", "mSize"),
      new ArrayBackedClassSpec("android.util.LongArray", "mValues", "mSize"),
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
    if (tRecursiveSummarizing.get().contains(instance)) {
      return DocString.text("");
    }

    ClassSpec spec = specsByClass.get(getKnownBaseClass(instance));

    tRecursiveSummarizing.get().add(instance);
    try {
      DocString summary = new DocString();
      List<Value> contents = spec.getContents(instance);
      if (spec.getBackingArrayCapacity(instance) > 0) {
        summary.appendFormat(" %d/%d [", contents.size(), spec.getBackingArrayCapacity(instance));
      } else {
        summary.appendFormat(" %d [", contents.size());
      }
      int remaining = kMaxContentsChars;
      for (int i = 0; i < contents.size(); i++) {
        if (i > 0) {
          summary.append(", ");
        }
        DocString elementSummary = Summarizer.summarize(contents.get(i));
        if (elementSummary.visibleSize() > remaining) {
          summary.appendFormat("... +%d", contents.size() - i);
          break;
        }
        summary.append(elementSummary);
        remaining -= elementSummary.visibleSize();
      }
      summary.append("]");
      return summary;
    } catch (Exception e) {
      return DocString.text("Error summarizing (heap corrupted?)");
    } finally {
      tRecursiveSummarizing.get().remove(instance);
    }
  }

  @Override
  public void printDetailsSection(Doc doc, Query query, AhatInstance instance) {
    doc.section("List Contents");

    try {
      AhatInstance base = instance.getBaseline();
      boolean diffing = instance != base && !base.isPlaceHolder();

      if (diffing) {
        printDiffTable(doc, query, getContents(instance), getContents(base));
      } else {
        printTable(doc, query, getContents(instance));
      }
    } catch (Exception e) {
      doc.println(DocString.text("Error evaluating list contents (possible heap corruption?)"));
      doc.println(DocString.internalError(e));
    }
  }

  private List<Value> getContents(AhatInstance instance) {
    ClassSpec spec = specsByClass.get(getKnownBaseClass(instance));
    return spec.getContents(instance);
  }

  private static void printTable(Doc doc, Query query, List<Value> values) {
    SubsetSelector<Value> subset = new SubsetSelector<>(query, LIST_CONTENTS_ID, values);

    doc.table(new Column("Value"), new Column("Size"), new Column("Retained Size"));

    for (Value val : subset.selected()) {
      doc.row(Summarizer.summarize(val), CollectionContentsHelper.size(val),
          CollectionContentsHelper.retainedSize(val));
    }

    doc.end();
    subset.render(doc);
  }

  private static void printDiffTable(
      Doc doc, Query query, List<Value> values, List<Value> oldValues) {
    SubsetSelector<Value> newSubset = new SubsetSelector<>(query, LIST_CONTENTS_ID, values);
    SubsetSelector<Value> oldSubset = new SubsetSelector<>(query, LIST_CONTENTS_ID, oldValues);
    SubsetSelector<Value> largestSelector =
        values.size() > oldValues.size() ? newSubset : oldSubset;

    List<Value> now = newSubset.selected();
    List<Value> was = oldSubset.selected();
    int count = Math.max(now.size(), was.size());

    doc.table(new Column("Value Now"), new Column("Old Value"), new Column("Size"), new Column("Δ"),
        new Column("Retained Size"), new Column("Δ"));

    for (int i = 0; i < count; i++) {
      // TODO: Smart diffing of elements to present insertions, removals etc. in a clever way
      // This might need us to process the whole list and do the subsetting at the end though.
      Value nowValue = i < now.size() ? now.get(i) : null;
      Value wasValue = i < was.size() ? was.get(i) : null;

      doc.row(Summarizer.summarize(nowValue), Summarizer.summarize(wasValue),
          CollectionContentsHelper.size(nowValue),
          CollectionContentsHelper.sizeDelta(nowValue, wasValue),
          CollectionContentsHelper.retainedSize(nowValue),
          CollectionContentsHelper.retainedDelta(nowValue, wasValue));
    }
    doc.end();

    largestSelector.render(doc);
  }
}
