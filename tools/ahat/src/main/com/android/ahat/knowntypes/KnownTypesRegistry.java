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
