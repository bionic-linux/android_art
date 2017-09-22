/*
 * Copyright (C) 2016 The Android Open Source Project
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

package com.android.ahat.heapdump;

import com.android.ahat.dominators.DominatorsComputation;
import com.android.tools.perflib.captures.DataBuffer;
import com.android.tools.perflib.captures.MemoryMappedFileBuffer;
import com.android.tools.perflib.heap.ArrayInstance;
import com.android.tools.perflib.heap.ClassInstance;
import com.android.tools.perflib.heap.ClassObj;
import com.android.tools.perflib.heap.Heap;
import com.android.tools.perflib.heap.Instance;
import com.android.tools.perflib.heap.ProguardMap;
import com.android.tools.perflib.heap.RootObj;
import com.android.tools.perflib.heap.Snapshot;
import com.android.tools.perflib.heap.StackFrame;
import com.android.tools.perflib.heap.StackTrace;
import gnu.trove.TObjectProcedure;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Comparator;
import java.util.List;

public class AhatSnapshot implements Diffable<AhatSnapshot> {
  private final Site mRootSite = new Site("ROOT");

  // Collection of objects whose immediate dominator is the SENTINEL_ROOT.
  private final List<AhatInstance> mRooted;

  // List of all ahat instances stored in increasing order by id.
  private final List<AhatInstance> mInstances = new ArrayList<AhatInstance>();

  private final List<AhatHeap> mHeaps = new ArrayList<AhatHeap>();

  private AhatSnapshot mBaseline = this;

  /**
   * Create an AhatSnapshot from an hprof file.
   */
  public static AhatSnapshot fromHprof(File hprof, ProguardMap map) throws IOException {
    return fromDataBuffer(new MemoryMappedFileBuffer(hprof), map);
  }

  /**
   * Create an AhatSnapshot from an in-memory data buffer.
   */
  public static AhatSnapshot fromDataBuffer(DataBuffer buffer, ProguardMap map) throws IOException {
    AhatSnapshot snapshot = new AhatSnapshot(buffer, map);

    // Request a GC now to clean up memory used by perflib. This helps to
    // avoid a noticable pause when visiting the first interesting page in
    // ahat.
    System.gc();

    return snapshot;
  }

  /**
   * Constructs an AhatSnapshot for the given hprof binary data.
   */
  private AhatSnapshot(DataBuffer buffer, ProguardMap map) throws IOException {
    Snapshot snapshot = Snapshot.createSnapshot(buffer, map);

    // Properly label the class of class objects in the perflib snapshot.
    final ClassObj javaLangClass = snapshot.findClass("java.lang.Class");
    if (javaLangClass != null) {
      for (Heap heap : snapshot.getHeaps()) {
        Collection<ClassObj> classes = heap.getClasses();
        for (ClassObj clsObj : classes) {
          if (clsObj.getClassObj() == null) {
            clsObj.setClassId(javaLangClass.getId());
          }
        }
      }
    }

    // Create mappings from id to ahat instance and heaps.
    Collection<Heap> heaps = snapshot.getHeaps();
    for (Heap heap : heaps) {
      // Note: mHeaps will not be in index order if snapshot.getHeaps does not
      // return heaps in index order. That's fine, because we don't rely on
      // mHeaps being in index order.
      mHeaps.add(new AhatHeap(heap.getName(), snapshot.getHeapIndex(heap)));
      TObjectProcedure<Instance> doCreate = new TObjectProcedure<Instance>() {
        @Override
        public boolean execute(Instance inst) {
          long id = inst.getId();
          if (inst instanceof ClassInstance) {
            mInstances.add(new AhatClassInstance(id));
          } else if (inst instanceof ArrayInstance) {
            mInstances.add(new AhatArrayInstance(id));
          } else if (inst instanceof ClassObj) {
            AhatClassObj classObj = new AhatClassObj(id);
            mInstances.add(classObj);
          }
          return true;
        }
      };
      for (Instance instance : heap.getClasses()) {
        doCreate.execute(instance);
      }
      heap.forEachInstance(doCreate);
    }

    // Sort the instances by id so we can use binary search to lookup
    // instances by id.
    mInstances.sort(new Comparator<AhatInstance>() {
      @Override
      public int compare(AhatInstance a, AhatInstance b) {
        return Long.compare(a.getId(), b.getId());
      }
    });

    // Initialize ahat snapshot and instances based on the perflib snapshot
    // and instances.
    List<AhatInstance> cleaners = new ArrayList<AhatInstance>();
    for (AhatInstance ahat : mInstances) {
      Instance inst = snapshot.findInstance(ahat.getId());

      StackFrame[] frames = null;
      StackTrace stack = inst.getStack();
      if (stack != null) {
        frames = stack.getFrames();
      }
      ahat.initialize(this, inst, mRootSite.getSite(frames));

      ClassObj classObj = inst.getClassObj();
      if (classObj != null && "sun.misc.Cleaner".equals(classObj.getClassName())) {
        cleaners.add(ahat);
      }
    }

    // Record the roots and their types.
    SuperRoot superRoot = new SuperRoot();
    for (RootObj root : snapshot.getGCRoots()) {
      Instance inst = root.getReferredInstance();
      if (inst != null) {
        AhatInstance ahat = findInstance(inst.getId());
        if (!ahat.isRoot()) {
          superRoot.addRoot(ahat);
        }
        ahat.addRootType(root.getRootType().toString());
      }
    }
    snapshot.dispose();

    // Update registered native allocation size.
    for (AhatInstance cleaner : cleaners) {
      AhatInstance.RegisteredNativeAllocation nra = cleaner.asRegisteredNativeAllocation();
      if (nra != null) {
        nra.referent.addRegisteredNativeSize(nra.size);
      }
    }

    AhatInstance.computeReverseReferences(superRoot);
    DominatorsComputation.computeDominators(superRoot);
    AhatInstance.computeRetainedSize(superRoot, mHeaps.size());

    mRooted = superRoot.getDominated();
    for (AhatHeap heap : mHeaps) {
      heap.addToSize(superRoot.getRetainedSize(heap));
    }

    mRootSite.prepareForUse(0, mHeaps.size());
  }

  /**
   * Returns the instance with given id in this snapshot.
   * Returns null if no instance with the given id is found.
   */
  public AhatInstance findInstance(long id) {
    // Binary search over the sorted instances.
    int start = 0;
    int end = mInstances.size();
    while (start < end) {
      int mid = start + ((end - start) / 2);
      AhatInstance midInst = mInstances.get(mid);
      long midId = midInst.getId();
      if (id == midId) {
        return midInst;
      } else if (id < midId) {
        end = mid;
      } else {
        start = mid + 1;
      }
    }
    return null;
  }

  /**
   * Returns the AhatClassObj with given id in this snapshot.
   * Returns null if no class object with the given id is found.
   */
  public AhatClassObj findClassObj(long id) {
    AhatInstance inst = findInstance(id);
    return inst == null ? null : inst.asClassObj();
  }

  /**
   * Returns the heap with the given name, if any.
   * Returns null if no heap with the given name could be found.
   */
  public AhatHeap getHeap(String name) {
    // We expect a small number of heaps (maybe 3 or 4 total), so a linear
    // search should be acceptable here performance wise.
    for (AhatHeap heap : getHeaps()) {
      if (heap.getName().equals(name)) {
        return heap;
      }
    }
    return null;
  }

  /**
   * Returns a list of heaps in the snapshot in canonical order.
   * Modifications to the returned list are visible to this AhatSnapshot,
   * which is used by diff to insert place holder heaps.
   */
  public List<AhatHeap> getHeaps() {
    return mHeaps;
  }

  /**
   * Returns a collection of instances whose immediate dominator is the
   * SENTINEL_ROOT.
   */
  public List<AhatInstance> getRooted() {
    return mRooted;
  }

  /**
   * Returns the root site for this snapshot.
   */
  public Site getRootSite() {
    return mRootSite;
  }

  // Get the site associated with the given id.
  // Returns the root site if no such site found.
  public Site getSite(long id) {
    Site site = mRootSite.findSite(id);
    return site == null ? mRootSite : site;
  }

  // Return the Value for the given perflib value object.
  Value getValue(Object value) {
    if (value instanceof Instance) {
      value = findInstance(((Instance)value).getId());
    }
    return Value.pack(value);
  }

  public void setBaseline(AhatSnapshot baseline) {
    mBaseline = baseline;
  }

  /**
   * Returns true if this snapshot has been diffed against another, different
   * snapshot.
   */
  public boolean isDiffed() {
    return mBaseline != this;
  }

  @Override public AhatSnapshot getBaseline() {
    return mBaseline;
  }

  @Override public boolean isPlaceHolder() {
    return false;
  }
}
