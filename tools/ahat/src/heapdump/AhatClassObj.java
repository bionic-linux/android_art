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

import com.android.tools.perflib.heap.ClassObj;
import com.android.tools.perflib.heap.Instance;
import java.util.AbstractList;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.Map;

public class AhatClassObj extends AhatInstance {
  private String mClassName;
  private AhatClassObj mSuperClassObj;
  private AhatInstance mClassLoader;
  private FieldValue[] mStaticFieldValues;
  private Field[] mInstanceFields;

  public AhatClassObj(long id) {
    super(id);
  }

  @Override void initialize(AhatSnapshot snapshot, Instance inst, Site site) {
    super.initialize(snapshot, inst, site);

    ClassObj classObj = (ClassObj)inst;
    mClassName = classObj.getClassName();

    ClassObj superClassObj = classObj.getSuperClassObj();
    if (superClassObj != null) {
      mSuperClassObj = snapshot.findClassObj(superClassObj.getId());
    }

    Instance loader = classObj.getClassLoader();
    if (loader != null) {
      mClassLoader = snapshot.findInstance(loader.getId());
    }

    Collection<Map.Entry<com.android.tools.perflib.heap.Field, Object>> fieldValues
      = classObj.getStaticFieldValues().entrySet();
    mStaticFieldValues = new FieldValue[fieldValues.size()];
    int index = 0;
    for (Map.Entry<com.android.tools.perflib.heap.Field, Object> field : fieldValues) {
      String name = field.getKey().getName();
      String type = field.getKey().getType().toString();
      Value value = snapshot.getValue(field.getValue());
      mStaticFieldValues[index++] = new FieldValue(name, type, value);
    }

    com.android.tools.perflib.heap.Field[] fields = classObj.getFields();
    mInstanceFields = new Field[fields.length];
    for (int i = 0; i < fields.length; i++) {
      mInstanceFields[i] = new Field(fields[i].getName(), fields[i].getType().toString());
    }
  }

  /**
   * Returns the name of the class this is a class object for.
   */
  public String getName() {
    return mClassName;
  }

  /**
   * Returns the superclass of this class object.
   */
  public AhatClassObj getSuperClassObj() {
    return mSuperClassObj;
  }

  /**
   * Returns the class loader of this class object.
   */
  public AhatInstance getClassLoader() {
    return mClassLoader;
  }

  /**
   * Returns the static field values for this class object.
   */
  public List<FieldValue> getStaticFieldValues() {
    return Arrays.asList(mStaticFieldValues);
  }

  /**
   * Returns the fields of instances of this class.
   */
  public Field[] getInstanceFields() {
    return mInstanceFields;
  }

  @Override
  Iterable<Reference> getReferences() {
    List<Reference> refs = new AbstractList<Reference>() {
      @Override
      public int size() {
        return mStaticFieldValues.length;
      }

      @Override
      public Reference get(int index) {
        FieldValue field = mStaticFieldValues[index];
        Value value = field.value;
        if (value != null && value.isAhatInstance()) {
          return new Reference(AhatClassObj.this, "." + field.name, value.asAhatInstance(), true);
        }
        return null;
      }
    };
    return new SkipNullsIterator(refs);
  }

  @Override public boolean isClassObj() {
    return true;
  }

  @Override public AhatClassObj asClassObj() {
    return this;
  }

  @Override public String toString() {
    return "class " + mClassName;
  }

  @Override AhatInstance newPlaceHolderInstance() {
    return new AhatPlaceHolderClassObj(this);
  }
}
