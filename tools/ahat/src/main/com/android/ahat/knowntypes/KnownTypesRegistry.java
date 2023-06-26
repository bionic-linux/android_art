package com.android.ahat.knowntypes;

import com.android.ahat.heapdump.AhatClassObj;
import com.android.ahat.heapdump.AhatInstance;

import java.util.HashMap;

public class KnownTypesRegistry {
  private static final KnownTypesRegistry instance = new KnownTypesRegistry();
  static {
    instance.registerHandler(new ListHandler());
    instance.registerHandler(new MapHandler());
  }
  public static KnownTypesRegistry getInstance() {
    return instance;
  }

  private final HashMap<String, KnownTypesHandler> handlers = new HashMap<>();

  public void registerHandler(KnownTypesHandler handler) {
    for (String className : handler.handlesTypes()) {
      handlers.put(className, handler);
    }
  }

  public boolean isKnownType(AhatInstance instance) {
    return getHandler(instance) != null;
  }

  public KnownTypesHandler getHandler(AhatInstance instance) {
    AhatClassObj classObj = instance.getClassObj();
    while (classObj != null) {
      KnownTypesHandler handler = handlers.get(classObj.getName());
      if (handler != null) {
        return handler;
      }
      classObj = classObj.getSuperClassObj();
    }
    return null;
  }
}
