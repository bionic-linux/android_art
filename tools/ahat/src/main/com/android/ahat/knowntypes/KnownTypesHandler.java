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
