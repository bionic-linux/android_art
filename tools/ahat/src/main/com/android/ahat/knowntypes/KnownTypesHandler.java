/*
 * Copyright (C) 2023 The Android Open Source Project
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

package com.android.ahat.knowntypes;

import com.android.ahat.Doc;
import com.android.ahat.DocString;
import com.android.ahat.Query;
import com.android.ahat.heapdump.AhatClassObj;
import com.android.ahat.heapdump.AhatInstance;

import java.util.Set;

public interface KnownTypesHandler {
  Set<String> handlesTypes();

  default String getKnownBaseClass(AhatInstance instance) {
    Set<String> knownTypes = handlesTypes();
    AhatClassObj classObj = instance.getClassObj();
    while (classObj != null) {
      if (knownTypes.contains(classObj.getName())) {
        return classObj.getName();
      }
      classObj = classObj.getSuperClassObj();
    }
    return null;
  }

  DocString summarize(AhatInstance instance);

  void printDetailsSection(Doc doc, Query query, AhatInstance instance);
}
